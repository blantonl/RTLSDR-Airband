/*
 * broadcastify_calls.h
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

#ifndef _BROADCASTIFY_CALLS_H
#define _BROADCASTIFY_CALLS_H

#include <sys/time.h>
#include <cstddef>

struct channel_t;
struct output_t;

struct bcfy_calls_data {
    const char* api_key;
    int system_id;
    int tg;
    float min_call_duration;
    int max_call_duration;    // seconds, 0 = no limit
    int max_queue_depth;
    bool use_dev_api;
    bool test_mode;

    // sample buffer for current call (owned, output thread only)
    float* sample_buf;
    size_t sample_buf_len;
    size_t sample_buf_capacity;

    // current call state
    struct timeval call_start;
    int call_freq;
};

struct bcfy_call_record {
    float* samples;       // owned sample buffer
    size_t sample_count;
    int sample_rate;
    double duration;
    time_t ts;            // call start unix epoch
    int tg;
    int freq;             // frequency in Hz
    const char* api_key;  // not owned, points to config
    int system_id;
    bool use_dev_api;
    bool test_mode;
};

void bcfy_calls_init(void);
void bcfy_calls_shutdown(void);
void bcfy_calls_process(channel_t* channel, output_t* output);
void bcfy_calls_disable(bcfy_calls_data* bdata);

#endif /* _BROADCASTIFY_CALLS_H */
