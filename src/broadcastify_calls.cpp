/*
 * broadcastify_calls.cpp
 * Broadcastify Calls API upload support
 *
 * Copyright (c) 2026 RTLSDR-Airband contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <queue>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <lame/lame.h>
#include <curl/curl.h>

#include "rtl_airband.h"

#define BCFY_CALLS_URL "https://api.broadcastify.com/call-upload"
#define BCFY_CALLS_DEV_URL "https://api.broadcastify.com/call-upload-dev"

#define BCFY_DEFAULT_MAX_QUEUE_DEPTH 25
#define BCFY_MAX_DRAIN_ON_SHUTDOWN 50
#define BCFY_DEFAULT_MAX_CALL_DURATION 120  // seconds
#define BCFY_RETRY_COUNT 3
#define BCFY_POST_TIMEOUT 30L
#define BCFY_PUT_TIMEOUT 60L

enum upload_result {
    UPLOAD_SUCCESS,
    UPLOAD_FAIL_PERMANENT,
    UPLOAD_FAIL_TRANSIENT,
};

#define BCFY_MP3_OUT_SAMPLERATE 8000
#define BCFY_MP3_BITRATE 16
// Generous buffer: 1.25 * bitrate * duration + padding
// For encoding, we allocate based on sample count
#define BCFY_MP3_BUF_SIZE(nsamples) ((size_t)(1.25 * BCFY_MP3_BITRATE * 1000 / 8 * ((double)(nsamples) / WAVE_RATE) + 7200 + 1024))

static std::queue<bcfy_call_record*> upload_queue;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_t upload_thread_id;
static bool upload_thread_running = false;

struct curl_write_ctx {
    char* buf;
    size_t len;
    size_t capacity;
};

static size_t bcfy_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct curl_write_ctx* ctx = (struct curl_write_ctx*)userdata;
    size_t bytes = size * nmemb;
    if (ctx->len + bytes >= ctx->capacity) {
        size_t new_cap = ctx->capacity * 2 + bytes;
        char* new_buf = (char*)realloc(ctx->buf, new_cap);
        if (!new_buf) return 0;
        ctx->buf = new_buf;
        ctx->capacity = new_cap;
    }
    memcpy(ctx->buf + ctx->len, ptr, bytes);
    ctx->len += bytes;
    ctx->buf[ctx->len] = '\0';
    return bytes;
}

struct curl_read_ctx {
    const unsigned char* data;
    size_t len;
    size_t offset;
};

static size_t bcfy_read_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    struct curl_read_ctx* ctx = (struct curl_read_ctx*)userdata;
    size_t remaining = ctx->len - ctx->offset;
    size_t to_copy = size * nmemb;
    if (to_copy > remaining) to_copy = remaining;
    memcpy(ptr, ctx->data + ctx->offset, to_copy);
    ctx->offset += to_copy;
    return to_copy;
}

static unsigned char* encode_mp3(bcfy_call_record* rec, size_t* out_len) {
    lame_t lame = lame_init();
    if (!lame) {
        log(LOG_ERR, "Broadcastify Calls: lame_init failed\n");
        return NULL;
    }

    lame_set_in_samplerate(lame, rec->sample_rate);
    lame_set_out_samplerate(lame, BCFY_MP3_OUT_SAMPLERATE);
    lame_set_VBR(lame, vbr_off);
    lame_set_brate(lame, BCFY_MP3_BITRATE);
    lame_set_num_channels(lame, 1);
    lame_set_mode(lame, MONO);
    lame_set_quality(lame, 2);
    lame_set_highpassfreq(lame, 100);
    lame_set_lowpassfreq(lame, 2500);

    if (lame_init_params(lame) < 0) {
        log(LOG_ERR, "Broadcastify Calls: lame_init_params failed\n");
        lame_close(lame);
        return NULL;
    }

    size_t mp3_buf_size = BCFY_MP3_BUF_SIZE(rec->sample_count);
    unsigned char* mp3_buf = (unsigned char*)malloc(mp3_buf_size);
    if (!mp3_buf) {
        log(LOG_ERR, "Broadcastify Calls: failed to allocate MP3 buffer (%zu bytes)\n", mp3_buf_size);
        lame_close(lame);
        return NULL;
    }

    int mp3_bytes = lame_encode_buffer_ieee_float(
        lame, rec->samples, NULL, (int)rec->sample_count,
        mp3_buf, (int)mp3_buf_size
    );

    if (mp3_bytes < 0) {
        log(LOG_ERR, "Broadcastify Calls: lame_encode_buffer_ieee_float returned %d\n", mp3_bytes);
        free(mp3_buf);
        lame_close(lame);
        return NULL;
    }

    int flush_bytes = lame_encode_flush(lame, mp3_buf + mp3_bytes, (int)(mp3_buf_size - mp3_bytes));
    if (flush_bytes < 0) {
        log(LOG_ERR, "Broadcastify Calls: lame_encode_flush returned %d\n", flush_bytes);
        free(mp3_buf);
        lame_close(lame);
        return NULL;
    }

    *out_len = (size_t)(mp3_bytes + flush_bytes);
    lame_close(lame);
    return mp3_buf;
}

static enum upload_result do_upload(bcfy_call_record* rec, unsigned char* mp3_data, size_t mp3_len) {
    const char* api_url = rec->use_dev_api ? BCFY_CALLS_DEV_URL : BCFY_CALLS_URL;

    // Step 1: POST metadata to register the call
    CURL* curl = curl_easy_init();
    if (!curl) {
        log(LOG_ERR, "Broadcastify Calls: curl_easy_init failed\n");
        return UPLOAD_FAIL_TRANSIENT;
    }

    // Build multipart form data (API uses aws-lambda-multipart-parser)
    curl_mime* mime = curl_mime_init(curl);

    char buf_system_id[16], buf_duration[16], buf_ts[32], buf_tg[16], buf_freq[32];
    snprintf(buf_system_id, sizeof(buf_system_id), "%d", rec->system_id);
    snprintf(buf_duration, sizeof(buf_duration), "%.1f", rec->duration);
    snprintf(buf_ts, sizeof(buf_ts), "%ld", (long)rec->ts);
    snprintf(buf_tg, sizeof(buf_tg), "%d", rec->tg);
    snprintf(buf_freq, sizeof(buf_freq), "%d", rec->freq);

    curl_mimepart* part;

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "apiKey");
    curl_mime_data(part, rec->api_key, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "systemId");
    curl_mime_data(part, buf_system_id, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "callDuration");
    curl_mime_data(part, buf_duration, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "ts");
    curl_mime_data(part, buf_ts, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "tg");
    curl_mime_data(part, buf_tg, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "freq");
    curl_mime_data(part, buf_freq, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "src");
    curl_mime_data(part, "0", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "enc");
    curl_mime_data(part, "mp3", CURL_ZERO_TERMINATED);

    if (rec->test_mode) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "test");
        curl_mime_data(part, "1", CURL_ZERO_TERMINATED);
    }

    struct curl_write_ctx response = { NULL, 0, 0 };
    response.buf = (char*)malloc(512);
    response.capacity = 512;
    response.buf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, bcfy_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, BCFY_POST_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    log(LOG_INFO, "Broadcastify Calls: POST tg=%d freq=%d ts=%ld duration=%.1f -> HTTP %ld: %s\n",
        rec->tg, rec->freq, (long)rec->ts, rec->duration, http_code,
        (res == CURLE_OK && response.buf) ? response.buf : curl_easy_strerror(res));

    if (res != CURLE_OK) {
        log(LOG_WARNING, "Broadcastify Calls: POST failed: %s\n", curl_easy_strerror(res));
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    if (http_code >= 400 && http_code < 500) {
        log(LOG_WARNING, "Broadcastify Calls: POST returned HTTP %ld: %s\n", http_code, response.buf);
        free(response.buf);
        return UPLOAD_FAIL_PERMANENT;
    }

    if (http_code >= 500) {
        log(LOG_WARNING, "Broadcastify Calls: POST returned HTTP %ld (server error)\n", http_code);
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    // Parse response: "0 <upload-url>" or "1 SKIPPED..." or "<error-code> <message>"
    if (response.len < 2) {
        log(LOG_WARNING, "Broadcastify Calls: empty response from API\n");
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    if (response.buf[0] == '1' && response.buf[1] == ' ') {
        const char* msg = response.buf + 2;
        if (strstr(msg, "SKIPPED---ALREADY-RECEIVED-THIS-CALL") != NULL) {
            log(LOG_INFO, "Broadcastify Calls: call skipped (duplicate): tg=%d freq=%d ts=%ld\n",
                rec->tg, rec->freq, (long)rec->ts);
            free(response.buf);
            return UPLOAD_SUCCESS;  // not an error, another source already uploaded this call
        }
        // All other "1 ..." responses are permanent API errors, don't retry
        log(LOG_WARNING, "Broadcastify Calls: API error: %s\n", response.buf);
        free(response.buf);
        return UPLOAD_FAIL_PERMANENT;
    }

    if (response.buf[0] != '0' || response.buf[1] != ' ') {
        log(LOG_WARNING, "Broadcastify Calls: unexpected API response: %s\n", response.buf);
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    // Extract upload URL (everything after "0 ")
    char* upload_url = response.buf + 2;
    // Trim trailing whitespace
    size_t url_len = strlen(upload_url);
    while (url_len > 0 && (upload_url[url_len - 1] == '\n' || upload_url[url_len - 1] == '\r' || upload_url[url_len - 1] == ' ')) {
        upload_url[--url_len] = '\0';
    }

    if (url_len == 0) {
        log(LOG_WARNING, "Broadcastify Calls: empty upload URL in response\n");
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    // Step 2: PUT audio to the one-time upload URL
    curl = curl_easy_init();
    if (!curl) {
        log(LOG_ERR, "Broadcastify Calls: curl_easy_init failed for PUT\n");
        free(response.buf);
        return UPLOAD_FAIL_TRANSIENT;
    }

    struct curl_read_ctx read_ctx = { mp3_data, mp3_len, 0 };

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: audio/mpeg");

    curl_easy_setopt(curl, CURLOPT_URL, upload_url);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, bcfy_read_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA, &read_ctx);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)mp3_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, BCFY_PUT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response.buf);

    if (res != CURLE_OK) {
        log(LOG_WARNING, "Broadcastify Calls: PUT failed: %s\n", curl_easy_strerror(res));
        return UPLOAD_FAIL_TRANSIENT;
    }

    if (http_code >= 400) {
        log(LOG_WARNING, "Broadcastify Calls: PUT returned HTTP %ld\n", http_code);
        return UPLOAD_FAIL_TRANSIENT;
    }

    log(LOG_INFO, "Broadcastify Calls: uploaded call tg=%d freq=%d duration=%.1fs\n",
        rec->tg, rec->freq, rec->duration);
    return UPLOAD_SUCCESS;
}

static void encode_and_upload(bcfy_call_record* rec) {
    size_t mp3_len = 0;
    unsigned char* mp3_data = encode_mp3(rec, &mp3_len);
    if (!mp3_data) {
        return;
    }

    enum upload_result result = UPLOAD_FAIL_TRANSIENT;
    for (int attempt = 0; attempt < BCFY_RETRY_COUNT && result == UPLOAD_FAIL_TRANSIENT; attempt++) {
        if (attempt > 0) {
            int delay = 1 << attempt;  // 2s, 4s
            log(LOG_INFO, "Broadcastify Calls: retry %d/%d in %ds\n", attempt + 1, BCFY_RETRY_COUNT, delay);
            sleep(delay);
        }
        result = do_upload(rec, mp3_data, mp3_len);
    }

    if (result == UPLOAD_FAIL_PERMANENT) {
        log(LOG_WARNING, "Broadcastify Calls: permanent failure uploading call tg=%d freq=%d, not retrying\n",
            rec->tg, rec->freq);
    } else if (result == UPLOAD_FAIL_TRANSIENT) {
        log(LOG_WARNING, "Broadcastify Calls: failed to upload call tg=%d freq=%d after %d attempts\n",
            rec->tg, rec->freq, BCFY_RETRY_COUNT);
    }

    free(mp3_data);
}

static void free_record(bcfy_call_record* rec) {
    if (rec) {
        free(rec->samples);
        free(rec);
    }
}

static void* upload_thread_func(void*) {
    while (true) {
        bcfy_call_record* rec = NULL;

        pthread_mutex_lock(&queue_mutex);
        while (upload_queue.empty() && upload_thread_running) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }

        if (!upload_thread_running && upload_queue.empty()) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        rec = upload_queue.front();
        upload_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        if (rec) {
            encode_and_upload(rec);
            free_record(rec);
        }
    }

    // Drain remaining items on shutdown
    int drained = 0;
    pthread_mutex_lock(&queue_mutex);
    if (!upload_queue.empty()) {
        int remaining = (int)upload_queue.size();
        log(LOG_INFO, "Broadcastify Calls: draining up to %d queued calls before shutdown\n",
            remaining < BCFY_MAX_DRAIN_ON_SHUTDOWN ? remaining : BCFY_MAX_DRAIN_ON_SHUTDOWN);
    }
    while (!upload_queue.empty() && drained < BCFY_MAX_DRAIN_ON_SHUTDOWN) {
        bcfy_call_record* rec = upload_queue.front();
        upload_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        encode_and_upload(rec);
        free_record(rec);
        drained++;

        pthread_mutex_lock(&queue_mutex);
    }
    // Discard any remaining if we hit the drain limit
    while (!upload_queue.empty()) {
        bcfy_call_record* rec = upload_queue.front();
        upload_queue.pop();
        free_record(rec);
    }
    pthread_mutex_unlock(&queue_mutex);

    if (drained > 0) {
        log(LOG_INFO, "Broadcastify Calls: drained %d calls on shutdown\n", drained);
    }

    return NULL;
}

static void enqueue_call(bcfy_call_record* rec, int max_queue_depth) {
    pthread_mutex_lock(&queue_mutex);

    // Drop oldest if queue is full
    if ((int)upload_queue.size() >= max_queue_depth) {
        bcfy_call_record* old = upload_queue.front();
        upload_queue.pop();
        log(LOG_WARNING, "Broadcastify Calls: upload queue full (%d), dropping oldest call (tg=%d)\n", max_queue_depth, old->tg);
        free_record(old);
    }

    upload_queue.push(rec);
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
}

static void finalize_call(bcfy_calls_data* bdata, output_t* output) {
    struct timeval now;
    gettimeofday(&now, NULL);

    double duration = (double)(now.tv_sec - bdata->call_start.tv_sec) +
                      (double)(now.tv_usec - bdata->call_start.tv_usec) / 1000000.0;

    if (duration < (double)bdata->min_call_duration) {
        // Too short, discard
        bdata->sample_buf_len = 0;
        output->active = false;
        return;
    }

    bcfy_call_record* rec = (bcfy_call_record*)calloc(1, sizeof(bcfy_call_record));
    if (!rec) {
        log(LOG_ERR, "Broadcastify Calls: failed to allocate call record\n");
        bdata->sample_buf_len = 0;
        output->active = false;
        return;
    }

    // Transfer ownership of sample buffer to the record
    rec->samples = bdata->sample_buf;
    rec->sample_count = bdata->sample_buf_len;
    rec->sample_rate = WAVE_RATE;
    rec->duration = duration;
    rec->ts = bdata->call_start.tv_sec;
    rec->tg = bdata->tg;
    rec->freq = bdata->call_freq;
    rec->api_key = bdata->api_key;
    rec->system_id = bdata->system_id;
    rec->use_dev_api = bdata->use_dev_api;
    rec->test_mode = bdata->test_mode;

    // Allocate a fresh buffer for the next call
    bdata->sample_buf = (float*)calloc(bdata->sample_buf_capacity, sizeof(float));
    bdata->sample_buf_len = 0;

    output->active = false;

    log(LOG_INFO, "Broadcastify Calls: call complete tg=%d freq=%d duration=%.1fs samples=%zu\n",
        rec->tg, rec->freq, rec->duration, rec->sample_count);

    enqueue_call(rec, bdata->max_queue_depth);
}

void bcfy_calls_init(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    upload_thread_running = true;
    pthread_create(&upload_thread_id, NULL, upload_thread_func, NULL);

    log(LOG_INFO, "Broadcastify Calls: upload thread started\n");
}

void bcfy_calls_shutdown(void) {
    log(LOG_INFO, "Broadcastify Calls: shutting down upload thread\n");

    pthread_mutex_lock(&queue_mutex);
    upload_thread_running = false;
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);

    pthread_join(upload_thread_id, NULL);

    curl_global_cleanup();
    log(LOG_INFO, "Broadcastify Calls: shutdown complete\n");
}

void bcfy_calls_process(channel_t* channel, output_t* output) {
    bcfy_calls_data* bdata = (bcfy_calls_data*)output->data;

    if (channel->axcindicate != NO_SIGNAL) {
        // Signal present — accumulate samples
        if (!output->active) {
            // Transmission just started
            gettimeofday(&bdata->call_start, NULL);
            bdata->call_freq = channel->freqlist[channel->freq_idx].frequency;
            bdata->sample_buf_len = 0;
            output->active = true;
        }

        // Grow buffer if needed
        size_t needed = bdata->sample_buf_len + WAVE_BATCH;
        if (needed > bdata->sample_buf_capacity) {
            size_t new_cap = bdata->sample_buf_capacity * 2;
            if (new_cap < needed) new_cap = needed;
            float* new_buf = (float*)realloc(bdata->sample_buf, new_cap * sizeof(float));
            if (!new_buf) {
                log(LOG_ERR, "Broadcastify Calls: failed to grow sample buffer\n");
                return;
            }
            bdata->sample_buf = new_buf;
            bdata->sample_buf_capacity = new_cap;
        }

        memcpy(bdata->sample_buf + bdata->sample_buf_len, channel->waveout, WAVE_BATCH * sizeof(float));
        bdata->sample_buf_len += WAVE_BATCH;

        // Auto-split at max call duration
        size_t max_samples = (size_t)WAVE_RATE * (size_t)bdata->max_call_duration;
        if (bdata->max_call_duration > 0 && bdata->sample_buf_len >= max_samples) {
            log(LOG_INFO, "Broadcastify Calls: max call duration reached, auto-splitting\n");
            finalize_call(bdata, output);
            // Start new call immediately since signal is still present
            gettimeofday(&bdata->call_start, NULL);
            bdata->call_freq = channel->freqlist[channel->freq_idx].frequency;
            output->active = true;
        }
    } else if (output->active) {
        // Signal just ended — finalize the call
        finalize_call(bdata, output);
    }
}

void bcfy_calls_disable(bcfy_calls_data* bdata) {
    if (bdata) {
        free(bdata->sample_buf);
        bdata->sample_buf = NULL;
        bdata->sample_buf_len = 0;
        bdata->sample_buf_capacity = 0;
    }
}
