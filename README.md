# RTLSDR-Airband

![main](https://github.com/rtl-airband/RTLSDR-Airband/actions/workflows/ci_build.yml/badge.svg?branch=main)
![main](https://github.com/rtl-airband/RTLSDR-Airband/actions/workflows/platform_build.yml/badge.svg?branch=main)
![main](https://github.com/rtl-airband/RTLSDR-Airband/actions/workflows/build_docker_containers.yml/badge.svg?branch=main)
![main](https://github.com/rtl-airband/RTLSDR-Airband/actions/workflows/code_formatting.yml/badge.svg?branch=main)

## Fork notes (blantonl/RTLSDR-Airband)

This fork tracks [rtl-airband/RTLSDR-Airband](https://github.com/rtl-airband/RTLSDR-Airband) and adds the following capabilities on top of upstream `v5.1.6`:

- **Broadcastify Calls output** — a new `broadcastify_calls` output type that buffers each transmission, encodes it to MP3, and uploads it to the [Broadcastify Calls](https://www.broadcastify.com/calls/) API. Build with `-DBROADCASTIFY_CALLS=ON`. See [Broadcastify-Calls-Output.md](Broadcastify-Calls-Output.md).
- **Configurable lowpass filter order** — per-channel `filter_order` setting (`2`, `4`, or `6`) controls Bessel IIR steepness for adjacent-channel rejection. Default `2` matches upstream behaviour; `6` gives 36 dB/octave rolloff for 8.33 kHz spacing.
- **Multi-bin FFT extraction** — per-channel `extract_bins` setting (odd, ≥1) sums adjacent FFT bins in the complex domain before computing magnitude, so AM sidebands aren't truncated when running with a larger `fft_size`.
- **SoapySDR sample format selection** — Airspy devices now auto-prefer `CF32`/`CS16` instead of the 8-bit native format (preserving the 12-bit ADC dynamic range), and any SoapySDR device may be pinned with an explicit `sample_format` config key (`"CU8"`, `"CS8"`, `"CS16"`, `"CF32"`).

See [`config/channel_isolation.conf`](config/channel_isolation.conf) for a worked example combining `fft_size`, `extract_bins`, `bandwidth`, and `filter_order` for tight channel spacing.

---

Changes as of v5.1.0:
 - License is now GPLv2 [#503](https://github.com/rtl-airband/RTLSDR-Airband/discussions/503)

NOTE: Repo URL has moved to https://github.com/rtl-airband/RTLSDR-Airband see [#502](https://github.com/rtl-airband/RTLSDR-Airband/discussions/502) for info

Changes as of v5.0.0:
 - PRs will be opened directly against `main` and the `unstable` branch will no longer be used
 - Version tags will be automatically created on each merge to `main`
 - A release will be created on each `major` or `minor` version tag but not `minor` tags
 - Checking out `main` is recommended over using a release artifact to stay on the latest version
 - This repo has significantly diverged from the original project [microtony/RTLSDR-Airband](https://github.com/microtony/RTLSDR-Airband) so it has been been detached (ie no longer a fork).
 - Specific build support for `rpiv1`, `armv7-generic`, and `armv8-generic` have been deprecated for the new default `native`, see [#447](https://github.com/rtl-airband/RTLSDR-Airband/discussions/447)


## Overview

RTLSDR-Airband receives analog radio voice channels and produces
audio streams which can be routed to various outputs, such as online
streaming services like LiveATC.net. Originally the only SDR type
supported by the program was Realtek DVB-T dongle (hence the project's
name). However, thanks to SoapySDR vendor-neutral SDR library, other
radios are now supported as well.

## Documentation

User's manual is now on the [wiki](https://github.com/rtl-airband/RTLSDR-Airband/wiki).

## Credits and thanks

I hereby express my gratitude to everybody who helped with the development and testing
of RTLSDR-Airband. Special thanks go to:

* Dave Pascoe
* SDR Guru
* Marcus Ströbel
* strix-technica
* charlie-foxtrot

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
