# Broadcastify Calls Output

## Overview

The Broadcastify Calls output type uploads individual radio transmissions ("calls") to the [Broadcastify Calls](https://www.broadcastify.com/calls/) platform. Unlike the Icecast output which streams audio continuously, this output buffers audio during each transmission and uploads it as a discrete MP3 file after the transmission ends.

This is designed for conventional radio systems where each channel monitors a single frequency.

## Requirements

### Build Dependencies

- **libcurl** - used for HTTP communication with the Broadcastify Calls API

On Debian/Ubuntu:
```bash
sudo apt install libcurl4-openssl-dev
```

### Build

Enable the feature at cmake configure time:
```bash
cmake -DBROADCASTIFY_CALLS=ON ..
make
```

The cmake summary will confirm:
```
- Broadcastify Calls:   requested: ON, enabled: TRUE
```

### Broadcastify Calls Account

You will need the following from your Broadcastify Calls system registration:

| Item | Description |
|------|-------------|
| **API Key** | UUID issued per system (e.g. `6a1f044b-88c3-11fa-bd8b-12348ab9ccea`) |
| **System ID** | Integer identifier for your system |
| **Frequency Slot IDs (tg)** | One integer per monitored frequency, assigned by the Broadcastify administrator |

## Configuration

The `broadcastify_calls` output is added to a channel's `outputs` list, just like `icecast` or `file` outputs. It can be used alongside other output types on the same channel.

### Required Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | string | Must be `"broadcastify_calls"` |
| `api_key` | string | Your Broadcastify Calls API key (UUID) |
| `system_id` | integer | Your Broadcastify Calls system ID |
| `tg` | integer | Frequency slot ID for this channel, assigned by Broadcastify |

### Optional Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min_call_duration` | float | `1.0` | Minimum transmission duration in seconds. Transmissions shorter than this are discarded. Useful for filtering squelch noise bursts. |
| `max_call_duration` | integer | `120` | Maximum call duration in seconds before auto-splitting into a new call. Set to `0` to disable. |
| `max_queue_depth` | integer | `25` | Maximum number of calls queued for upload. When exceeded, the oldest queued call is dropped. |
| `use_dev_api` | boolean | `false` | Use the Broadcastify Calls development API endpoint instead of production. |
| `test_mode` | boolean | `false` | Send `test=1` to the API for credential validation without uploading real calls. |

### Limitations

- **Multichannel mode only** - not supported on scan mode channels (which have multiple frequencies per channel).
- **Not supported on mixers** - only on device channel outputs.

### Example

Combined with an Icecast stream on the same channel:

```
devices:
({
  type = "rtlsdr";
  serial = "777755221";
  gain = 25;
  centerfreq = 123.5;
  mode = "multichannel";
  channels:
  (
    {
      freq = 123.7;
      outputs: (
        {
          type = "icecast";
          server = "audio1.broadcastify.com";
          port = 8080;
          mountpoint = "feed1";
          username = "source";
          password = "mypassword";
        },
        {
          type = "broadcastify_calls";
          api_key = "6a1f044b-88c3-11fa-bd8b-12348ab9ccea";
          system_id = 12345;
          tg = 1001;
        }
      );
    }
  );
});
```

Standalone (Broadcastify Calls only, no streaming):

```
{
  freq = 124.1;
  outputs: (
    {
      type = "broadcastify_calls";
      api_key = "6a1f044b-88c3-11fa-bd8b-12348ab9ccea";
      system_id = 12345;
      tg = 1002;
      min_call_duration = 0.5;
      max_call_duration = 60;
    }
  );
}
```

## How It Works

### Call Detection

The output uses RTLSDR-Airband's existing squelch system to detect transmission boundaries:

1. When the squelch opens (signal detected), audio samples are buffered in memory.
2. When the squelch closes (signal lost), the buffered audio is finalized as a complete call.
3. If the call duration is shorter than `min_call_duration`, it is discarded.
4. If a transmission exceeds `max_call_duration`, it is automatically split into separate calls.

### Upload Process

Completed calls are placed on an internal upload queue and processed by a dedicated upload thread, so the main audio processing is never blocked by network I/O.

For each call, the upload thread:

1. Encodes the buffered audio to MP3 (mono, 8000 Hz, 16 kbps CBR).
2. Sends a multipart POST request to the Broadcastify Calls API with call metadata (API key, system ID, talkgroup, frequency, timestamp, duration).
3. If the API returns a one-time upload URL, PUTs the MP3 audio to that URL.
4. On transient failure (HTTP 5xx or network error), retries up to 3 times with exponential backoff.

### Memory Usage

Audio is buffered as raw float samples at 8000 Hz mono (~32 KB per second of audio). A typical 10-second transmission uses ~320 KB of memory. The upload queue depth limit (`max_queue_depth`) bounds total memory usage.

No temporary files are written to disk.

## Logging

All Broadcastify Calls activity is logged through RTLSDR-Airband's standard logging (syslog with `-l`, stderr with `-e`).

| Log Level | Message | Meaning |
|-----------|---------|---------|
| INFO | `call complete tg=... duration=...` | Transmission detected and queued for upload |
| INFO | `uploaded call tg=...` | Call successfully uploaded |
| INFO | `call skipped (duplicate)` | API reports another source already uploaded this call |
| INFO | `POST tg=... -> HTTP ...: ...` | Raw API response for every upload attempt |
| WARNING | `POST failed: ...` | Network/transport error |
| WARNING | `API error: 1 ...` | Broadcastify API rejected the call (check credentials/config) |
| WARNING | `failed to upload call ... after 3 attempts` | All retries exhausted |
| WARNING | `upload queue full` | Queue depth exceeded, oldest call dropped |
| ERROR | `lame_init failed` | MP3 encoder initialization failure |

## Troubleshooting

### `1 NO-SYSTEM-ID-SPECIFIED` or `1 INVALID-SYSTEM-ID`
Check that `system_id` in your config matches the system ID assigned by Broadcastify.

### `1 Invalid-API-Key` or `1 API-Key-Access-Denied`
Verify your `api_key` is correct and that the API key is associated with the specified `system_id`.

### `1 No-Freq-Table-Entry-For-Upload`
The `tg` (frequency slot ID) is not configured in the Broadcastify system. Contact the Broadcastify administrator to add the frequency slot.

### `1 Uploads-not-enabled-for-this-system`
The Broadcastify Calls system is not enabled for uploads. Contact the Broadcastify administrator.

### Calls uploading but audio sounds tinny
Ensure your channel's `highpass` and `lowpass` settings are appropriate for the modulation type. The defaults (100 Hz highpass, 2500 Hz lowpass) are suitable for AM aviation voice.

### Too many short calls being uploaded
Increase `min_call_duration` to filter out squelch noise bursts. The default of 1.0 second works well for most scenarios.

### Testing credentials
Set `test_mode = true` and `use_dev_api = true` to validate your API credentials without uploading real calls. Check the [Broadcastify Calls dev status page](https://www.broadcastify.com/calls-dev/status/) for results.
