# RTLSDR-Airband (blantonl fork)

A fork of [rtl-airband/RTLSDR-Airband](https://github.com/rtl-airband/RTLSDR-Airband) that adds a native **Broadcastify Calls** output type and a handful of demodulation improvements aimed at feeders running tightly-spaced air band channels and higher-precision SDR hardware (Airspy in particular).

**Latest release:** [`bcfy-v5.2.0`](https://github.com/blantonl/RTLSDR-Airband/releases/tag/bcfy-v5.2.0) (based on upstream `v5.1.6`)

## Why this fork exists

Upstream RTLSDR-Airband targets continuous Icecast streaming. Broadcastify Calls is a different shape — discrete per-transmission MP3 uploads to an HTTP API — and there was no first-class output type for it. This fork adds one, plus the signal-processing knobs needed to squeeze acceptable audio out of conventional channels with neighbours close in frequency.

## What this fork adds on top of upstream

- **`broadcastify_calls` output type.** Buffers each transmission while squelch is open, encodes to MP3 with LAME, and uploads to the [Broadcastify Calls](https://www.broadcastify.com/calls/) API using the standard two-step flow (POST metadata → presigned PUT). Retries with exponential backoff and a bounded queue with oldest-drop. Build with `-DBROADCASTIFY_CALLS=ON` (requires `libcurl`). See [Broadcastify-Calls-Output.md](Broadcastify-Calls-Output.md).
- **Configurable lowpass filter order** — per-channel `filter_order` (`2`, `4`, or `6`) controls Bessel IIR steepness on the post-FFT channel filter. Default `2` matches upstream (12 dB/oct). `6` gives 36 dB/octave rolloff for 8.33 kHz European spacing or hot adjacent transmitters.
- **Multi-bin FFT extraction** — per-channel `extract_bins` (odd, ≥1) sums adjacent FFT bins in the complex domain before computing magnitude, so AM sidebands aren't truncated when running with a larger `fft_size`. Default `1` matches upstream.
- **SoapySDR sample format selection** — Airspy devices auto-prefer `CF32` then `CS16` instead of the 8-bit native format (preserving the 12-bit ADC dynamic range), and any SoapySDR device can be pinned with an explicit `sample_format` config key (`"CU8"`, `"CS8"`, `"CS16"`, `"CF32"`).

See [`config/channel_isolation.conf`](config/channel_isolation.conf) for a worked example combining `fft_size`, `extract_bins`, `bandwidth`, and `filter_order` for tight spacing, and [NEWS.md](NEWS.md) for the per-release fork changelog.

All four features are off-by-default or auto-detected — drop in an existing upstream `rtl_airband.conf` and behaviour is identical.

## Quick start

```bash
sudo apt install libcurl4-openssl-dev   # for broadcastify_calls
git clone https://github.com/blantonl/RTLSDR-Airband.git
cd RTLSDR-Airband
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBROADCASTIFY_CALLS=ON
cmake --build build -j$(nproc)
```

To pin to a specific release rather than tracking `main`, check out the tag after cloning, e.g. `git checkout bcfy-v5.2.0`.

Other useful CMake options carry over from upstream: `-DNFM=ON`, `-DPLATFORM=generic|native|rpiv2`, `-DBUILD_UNITTESTS=TRUE`.

## Overview

RTLSDR-Airband receives analog radio voice channels and produces audio streams which can be routed to various outputs — Icecast, file output, raw output, and (in this fork) Broadcastify Calls. Originally the only SDR type supported was the Realtek DVB-T dongle (hence the name); thanks to the SoapySDR vendor-neutral SDR library, other radios are supported as well.

## Documentation

The user's manual lives on the upstream [wiki](https://github.com/rtl-airband/RTLSDR-Airband/wiki) and applies in full to this fork. Fork-specific docs:

- [Broadcastify-Calls-Output.md](Broadcastify-Calls-Output.md) — `broadcastify_calls` output type configuration
- [`config/channel_isolation.conf`](config/channel_isolation.conf) — worked example of channel isolation tuning
- [NEWS.md](NEWS.md) — fork release history

## Upstream lineage

This fork is based on [rtl-airband/RTLSDR-Airband](https://github.com/rtl-airband/RTLSDR-Airband). Notable upstream context retained for reference:

- **As of upstream v5.1.0:** License is now GPLv2 ([#503](https://github.com/rtl-airband/RTLSDR-Airband/discussions/503)). Upstream repo URL moved to https://github.com/rtl-airband/RTLSDR-Airband ([#502](https://github.com/rtl-airband/RTLSDR-Airband/discussions/502)).
- **As of upstream v5.0.0:** Version tags auto-generated on merge to `main`; releases cut on major/minor tags. Specific build support for `rpiv1`, `armv7-generic`, and `armv8-generic` was deprecated in favour of `native` ([#447](https://github.com/rtl-airband/RTLSDR-Airband/discussions/447)). Upstream itself was detached from the original [microtony/RTLSDR-Airband](https://github.com/microtony/RTLSDR-Airband) project at this point.

## Credits and thanks

The bulk of the work is upstream's. Thanks especially to [charlie-foxtrot](https://github.com/charlie-foxtrot) for maintaining the rtl-airband fork, and to everyone listed in upstream's credits:

* Dave Pascoe
* SDR Guru
* Marcus Ströbel
* strix-technica

This fork's additions are maintained by [@blantonl](https://github.com/blantonl).

## License

Copyright (C) 2022-2025 charlie-foxtrot

Copyright (C) 2015-2022 Tomasz Lemiech <szpajder@gmail.com>

Based on original work by Wong Man Hang <microtony@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses/>.

## Open Source Licenses of bundled code

### gpu_fft

BCM2835 "GPU_FFT" release 2.0
Copyright (c) 2014, Andrew Holme.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### rtl-sdr

* Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
* Copyright (C) 2015 by Kyle Keen <keenerd@gmail.com>
* GNU General Public License Version 2
