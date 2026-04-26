/*
 * filters.cpp
 *
 * Copyright (C) 2022-2023 charlie-foxtrot
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

#include "logging.h"  // debug_print()

#include "filters.h"

using namespace std;

// Default constructor is no filter
NotchFilter::NotchFilter(void) : enabled_(false) {}

// Notch Filter based on https://www.dsprelated.com/showcode/173.php
NotchFilter::NotchFilter(float notch_freq, float sample_freq, float q) : enabled_(true), x{0.0}, y{0.0} {
    if (notch_freq <= 0.0) {
        debug_print("Invalid frequency %f Hz, disabling notch filter\n", notch_freq);
        enabled_ = false;
        return;
    }

    debug_print("Adding notch filter for %f Hz with parameters {%f, %f}\n", notch_freq, sample_freq, q);

    float wo = 2 * M_PI * (notch_freq / sample_freq);

    e = 1 / (1 + tan(wo / (q * 2)));
    p = cos(wo);
    d[0] = e;
    d[1] = 2 * e * p;
    d[2] = (2 * e - 1);

    debug_print("wo:%f e:%f p:%f d:{%f,%f,%f}\n", wo, e, p, d[0], d[1], d[2]);
}

void NotchFilter::apply(float& value) {
    if (!enabled_) {
        return;
    }

    x[0] = x[1];
    x[1] = x[2];
    x[2] = value;

    y[0] = y[1];
    y[1] = y[2];
    y[2] = d[0] * x[2] - d[1] * x[1] + d[0] * x[0] + d[1] * y[1] - d[2] * y[0];

    value = y[2];
}

// Default constructor is no filter
LowpassFilter::LowpassFilter(void) : enabled_(false), num_sections_(0) {}

// Normalized Bessel poles (s-domain) for orders 2, 4, 6.
// Only one pole per conjugate pair is listed; the conjugate is derived automatically.
// Source: https://www-users.cs.york.ac.uk/~fisher/mkfilter/
//
// Order 2: 1 conjugate pair
static const complex<double> bessel_poles_2[] = {
    complex<double>(-1.10160133059e+00, 6.36009824757e-01),
};

// Order 4: 2 conjugate pairs
static const complex<double> bessel_poles_4[] = {
    complex<double>(-1.37006783055e+00, 4.10249717494e-01),
    complex<double>(-9.95208764350e-01, 1.25710573945e+00),
};

// Order 6: 3 conjugate pairs
static const complex<double> bessel_poles_6[] = {
    complex<double>(-1.57149040362e+00, 3.20896374221e-01),
    complex<double>(-1.38185809760e+00, 9.71471890712e-01),
    complex<double>(-9.30656522947e-01, 1.66186326894e+00),
};

// Initialize one biquad section from a single conjugate pole pair
void LowpassFilter::init_section(int section_idx, complex<double> pole, float freq, float sample_freq) {
    double raw_alpha = (double)freq / sample_freq;
    double warped_alpha = tan(M_PI * raw_alpha) / M_PI;

    complex<double> zeros[2] = {-1.0, -1.0};
    complex<double> poles[2];
    poles[0] = blt(M_PI * 2 * warped_alpha * pole);
    poles[1] = blt(M_PI * 2 * warped_alpha * conj(pole));

    complex<double> topcoeffs[3];
    complex<double> botcoeffs[3];
    expand(zeros, 2, topcoeffs);
    expand(poles, 2, botcoeffs);
    complex<double> gain_complex = evaluate(topcoeffs, 2, botcoeffs, 2, 1.0);

    BiquadSection& sec = sections_[section_idx];
    sec.gain = (float)hypot(gain_complex.imag(), gain_complex.real());

    for (int i = 0; i <= 2; i++) {
        sec.ycoeffs[i] = (float)(-(botcoeffs[i].real() / botcoeffs[2].real()));
    }

    sec.xv[0] = sec.xv[1] = sec.xv[2] = complex<float>(0.0f, 0.0f);
    sec.yv[0] = sec.yv[1] = sec.yv[2] = complex<float>(0.0f, 0.0f);

    debug_print("  section %d: gain=%f, ycoeffs={%f, %f}\n", section_idx, sec.gain, sec.ycoeffs[0], sec.ycoeffs[1]);
}

// Lowpass Bessel filter, order 2/4/6, implemented as cascaded biquad sections.
// Based on https://www-users.cs.york.ac.uk/~fisher/mkfilter/
LowpassFilter::LowpassFilter(float freq, float sample_freq, int order) : enabled_(true), num_sections_(0) {
    if (freq <= 0.0) {
        debug_print("Invalid frequency %f Hz, disabling lowpass filter\n", freq);
        enabled_ = false;
        return;
    }

    const complex<double>* poles;
    switch (order) {
        case 2:
            poles = bessel_poles_2;
            num_sections_ = 1;
            break;
        case 4:
            poles = bessel_poles_4;
            num_sections_ = 2;
            break;
        case 6:
            poles = bessel_poles_6;
            num_sections_ = 3;
            break;
        default:
            debug_print("Unsupported filter order %d, using order 2\n", order);
            poles = bessel_poles_2;
            num_sections_ = 1;
            break;
    }

    debug_print("Adding lowpass filter at %f Hz, sample rate %f, order %d (%d sections)\n", freq, sample_freq, order, num_sections_);

    for (int i = 0; i < num_sections_; i++) {
        init_section(i, poles[i], freq, sample_freq);
    }
}

complex<double> LowpassFilter::blt(complex<double> pz) {
    return (2.0 + pz) / (2.0 - pz);
}

/* evaluate response, substituting for z */
complex<double> LowpassFilter::evaluate(complex<double> topco[], int nz, complex<double> botco[], int np, complex<double> z) {
    return eval(topco, nz, z) / eval(botco, np, z);
}

/* evaluate polynomial in z, substituting for z */
complex<double> LowpassFilter::eval(complex<double> coeffs[], int npz, complex<double> z) {
    complex<double> sum(0.0);
    for (int i = npz; i >= 0; i--) {
        sum = (sum * z) + coeffs[i];
    }
    return sum;
}

/* compute product of poles or zeros as a polynomial of z */
void LowpassFilter::expand(complex<double> pz[], int npz, complex<double> coeffs[]) {
    coeffs[0] = 1.0;
    for (int i = 0; i < npz; i++) {
        coeffs[i + 1] = 0.0;
    }
    for (int i = 0; i < npz; i++) {
        multin(pz[i], npz, coeffs);
    }
    /* check computed coeffs of z^k are all real */
    for (int i = 0; i < npz + 1; i++) {
        if (fabs(coeffs[i].imag()) > 1e-10) {
            log(LOG_ERR, "coeff of z^%d is not real; poles/zeros are not complex conjugates\n", i);
            error();
        }
    }
}

void LowpassFilter::multin(complex<double> w, int npz, complex<double> coeffs[]) {
    /* multiply factor (z-w) into coeffs */
    complex<double> nw = -w;
    for (int i = npz; i >= 1; i--) {
        coeffs[i] = (nw * coeffs[i]) + coeffs[i - 1];
    }
    coeffs[0] = nw * coeffs[0];
}

void LowpassFilter::apply(float& r, float& j) {
    if (!enabled_) {
        return;
    }

    complex<float> signal(r, j);

    // Cascade through each biquad section
    for (int s = 0; s < num_sections_; s++) {
        BiquadSection& sec = sections_[s];

        sec.xv[0] = sec.xv[1];
        sec.xv[1] = sec.xv[2];
        sec.xv[2] = signal / sec.gain;

        sec.yv[0] = sec.yv[1];
        sec.yv[1] = sec.yv[2];
        sec.yv[2] = (sec.xv[0] + sec.xv[2]) + (2.0f * sec.xv[1]) + (sec.ycoeffs[0] * sec.yv[0]) + (sec.ycoeffs[1] * sec.yv[1]);

        // Output of this section feeds into the next
        signal = sec.yv[2];
    }

    r = signal.real();
    j = signal.imag();
}
