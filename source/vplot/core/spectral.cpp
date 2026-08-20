#include "spectral.h"

#include <algorithm>
#include <cmath>

namespace vpl
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

bool is_power_of_two(std::size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

/* Iterative radix-2 Cooley-Tukey, in place, for power-of-two lengths. */
void fft_radix2(std::vector<std::complex<double>> &a)
{
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }

    /* Bit-reversal permutation. */
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

void fft_inplace(std::vector<std::complex<double>> &a)
{
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }
    if (is_power_of_two(n)) {
        fft_radix2(a);
        return;
    }

    /*
     * Bluestein's algorithm: rewrite the DFT exponent nk as
     * (n^2 + k^2 - (n-k)^2) / 2, turning the transform into a convolution of two
     * chirp sequences that a power-of-two transform can compute for any length.
     */
    std::size_t m = 1;
    while (m < 2 * n - 1) {
        m <<= 1;
    }

    std::vector<std::complex<double>> chirp(n);
    for (std::size_t i = 0; i < n; ++i) {
        /* Reduce i*i mod 2n first to keep the angle small and precise for long
           inputs. */
        const double ang = kPi * static_cast<double>((i * i) % (2 * n)) / static_cast<double>(n);
        chirp[i] = std::complex<double>(std::cos(ang), -std::sin(ang));
    }

    std::vector<std::complex<double>> av(m, std::complex<double>(0.0, 0.0));
    std::vector<std::complex<double>> bv(m, std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        av[i] = a[i] * chirp[i];
        bv[i] = std::conj(chirp[i]);
        if (i > 0) {
            bv[m - i] = std::conj(chirp[i]);
        }
    }

    fft_radix2(av);
    fft_radix2(bv);
    for (std::size_t i = 0; i < m; ++i) {
        av[i] *= bv[i];
    }
    /* Inverse transform by conjugation. */
    for (std::complex<double> &v : av) {
        v = std::conj(v);
    }
    fft_radix2(av);
    for (std::complex<double> &v : av) {
        v = std::conj(v) / static_cast<double>(m);
    }

    for (std::size_t i = 0; i < n; ++i) {
        a[i] = av[i] * chirp[i];
    }
}

SpectralResult spectral_helper(const double *x, const double *y, std::size_t n,
                               std::size_t NFFT, double Fs, std::size_t noverlap,
                               std::size_t pad_to, SpectralSides sides, SpectralMode mode,
                               bool scale_by_freq, SpectralWindow window_type,
                               SpectralDetrend detrend)
{
    SpectralResult out;
    if (n < NFFT || NFFT == 0 || noverlap >= NFFT) {
        return out;
    }
    if (pad_to == 0) {
        pad_to = NFFT;
    }

    const bool same_data = (y == nullptr);

    /*
     * A one-sided spectrum keeps pad_to/2 + 1 bins for even pad_to (DC through
     * Nyquist) and (pad_to+1)/2 for odd (no Nyquist bin). The factor of two
     * folds the discarded negative frequencies back in.
     */
    std::size_t numFreqs = 0;
    double scaling_factor = 1.0;
    std::size_t freqcenter = 0;
    if (sides == SpectralSides::TwoSided) {
        numFreqs = pad_to;
        freqcenter = (pad_to % 2) ? ((pad_to - 1) / 2 + 1) : (pad_to / 2);
        scaling_factor = 1.0;
    } else {
        numFreqs = (pad_to % 2) ? ((pad_to + 1) / 2) : (pad_to / 2 + 1);
        scaling_factor = 2.0;
    }

    /* Per-segment window, using numpy's forms (matplotlib's default is
       hanning). */
    std::vector<double> window(NFFT);
    if (NFFT == 1) {
        window[0] = 1.0;
    } else {
        const double M = static_cast<double>(NFFT);
        for (std::size_t i = 0; i < NFFT; ++i) {
            const double t = 2.0 * kPi * static_cast<double>(i) / (M - 1.0);
            switch (window_type) {
                case SpectralWindow::Hanning:
                    window[i] = 0.5 - 0.5 * std::cos(t);
                    break;
                case SpectralWindow::Hamming:
                    window[i] = 0.54 - 0.46 * std::cos(t);
                    break;
                case SpectralWindow::Blackman:
                    window[i] = 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2.0 * t);
                    break;
                case SpectralWindow::Bartlett: {
                    const double h = (M - 1.0) / 2.0;
                    window[i] = (2.0 / (M - 1.0)) * (h - std::fabs(static_cast<double>(i) - h));
                    break;
                }
                case SpectralWindow::Boxcar:
                    window[i] = 1.0;
                    break;
            }
        }
    }
    double window_sum = 0.0;
    double window_sq_sum = 0.0;
    for (double w : window) {
        window_sum += w;
        window_sq_sum += w * w;
    }

    const std::size_t step = NFFT - noverlap;
    const std::size_t nsegs = (n - NFFT) / step + 1;

    out.numFreqs = numFreqs;
    out.nsegs = nsegs;
    out.values.assign(numFreqs * nsegs, 0.0);
    if (!same_data) {
        out.values_imag.assign(numFreqs * nsegs, 0.0);
    }

    /* One segment's transform: detrended, windowed and zero-padded to pad_to. */
    auto transform = [&](const double *src, std::size_t offset) {
        std::vector<double> seg(NFFT);
        for (std::size_t i = 0; i < NFFT; ++i) {
            seg[i] = src[offset + i];
        }
        if (detrend == SpectralDetrend::Mean) {
            double mean = 0.0;
            for (double v : seg) {
                mean += v;
            }
            mean /= static_cast<double>(NFFT);
            for (double &v : seg) {
                v -= mean;
            }
        } else if (detrend == SpectralDetrend::Linear) {
            /* Subtract the least-squares line over i = 0..NFFT-1. */
            const double N = static_cast<double>(NFFT);
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (std::size_t i = 0; i < NFFT; ++i) {
                const double xi = static_cast<double>(i);
                sx += xi;
                sy += seg[i];
                sxx += xi * xi;
                sxy += xi * seg[i];
            }
            const double denom = N * sxx - sx * sx;
            const double b = denom != 0.0 ? (N * sxy - sx * sy) / denom : 0.0;
            const double a = (sy - b * sx) / N;
            for (std::size_t i = 0; i < NFFT; ++i) {
                seg[i] -= a + b * static_cast<double>(i);
            }
        }

        std::vector<std::complex<double>> buf(pad_to, std::complex<double>(0.0, 0.0));
        for (std::size_t i = 0; i < NFFT; ++i) {
            buf[i] = std::complex<double>(seg[i] * window[i], 0.0);
        }
        fft_inplace(buf);
        return buf;
    };

    for (std::size_t s = 0; s < nsegs; ++s) {
        const std::size_t offset = s * step;
        const std::vector<std::complex<double>> fx = transform(x, offset);

        if (!same_data) {
            /* The cross-spectrum stays COMPLEX -- see SpectralResult. */
            const std::vector<std::complex<double>> fy = transform(y, offset);
            for (std::size_t f = 0; f < numFreqs; ++f) {
                const std::complex<double> c = std::conj(fx[f]) * fy[f];
                out.values[s * numFreqs + f] = c.real();
                out.values_imag[s * numFreqs + f] = c.imag();
            }
            continue;
        }

        for (std::size_t f = 0; f < numFreqs; ++f) {
            double v = 0.0;
            switch (mode) {
                case SpectralMode::Psd:
                    v = std::norm(fx[f]); /* conj(z) * z is |z|^2, real */
                    break;
                case SpectralMode::Magnitude:
                    v = std::abs(fx[f]) / window_sum;
                    break;
                case SpectralMode::Angle:
                case SpectralMode::Phase:
                    v = std::arg(fx[f]);
                    break;
                case SpectralMode::Complex:
                    v = fx[f].real() / window_sum;
                    break;
            }
            out.values[s * numFreqs + f] = v;
        }
    }

    if (mode == SpectralMode::Psd) {
        const double divisor =
            scale_by_freq ? (Fs * window_sq_sum) : (window_sum * window_sum);
        for (std::size_t s = 0; s < nsegs; ++s) {
            /* Everything but DC is doubled; and everything but Nyquist too,
               when there IS a Nyquist bin (pad_to even). */
            const std::size_t last = (NFFT % 2 == 0) ? (numFreqs - 1) : numFreqs;
            for (std::size_t f = 1; f < last; ++f) {
                out.values[s * numFreqs + f] *= scaling_factor;
                if (!out.values_imag.empty()) {
                    out.values_imag[s * numFreqs + f] *= scaling_factor;
                }
            }
            for (std::size_t f = 0; f < numFreqs; ++f) {
                out.values[s * numFreqs + f] /= divisor;
                if (!out.values_imag.empty()) {
                    out.values_imag[s * numFreqs + f] /= divisor;
                }
            }
        }
    }

    /* np.fft.fftfreq(pad_to, 1/Fs): 0..(pad_to-1)/2 then the negatives. */
    out.freqs.resize(numFreqs);
    for (std::size_t f = 0; f < numFreqs; ++f) {
        const std::size_t half = (pad_to - 1) / 2 + 1;
        const double k = f < half ? static_cast<double>(f)
                                  : static_cast<double>(f) - static_cast<double>(pad_to);
        out.freqs[f] = k * Fs / static_cast<double>(pad_to);
    }

    out.times.resize(nsegs);
    for (std::size_t s = 0; s < nsegs; ++s) {
        out.times[s] = (static_cast<double>(NFFT) / 2.0 + static_cast<double>(s * step)) / Fs;
    }

    if (sides == SpectralSides::TwoSided) {
        /* Roll the negative half to the front so the axis runs -Fs/2..Fs/2. */
        std::vector<double> rf(numFreqs);
        std::vector<double> rv(out.values.size());
        std::vector<double> ri(out.values_imag.size());
        for (std::size_t f = 0; f < numFreqs; ++f) {
            const std::size_t src = (f + freqcenter) % numFreqs;
            rf[f] = out.freqs[src];
            for (std::size_t s = 0; s < nsegs; ++s) {
                rv[s * numFreqs + f] = out.values[s * numFreqs + src];
                if (!ri.empty()) {
                    ri[s * numFreqs + f] = out.values_imag[s * numFreqs + src];
                }
            }
        }
        out.freqs.swap(rf);
        out.values.swap(rv);
        out.values_imag.swap(ri);
    } else if (pad_to % 2 == 0) {
        /* The Nyquist bin comes out of fftfreq negative; one-sided, it is the
           top of the range and must read positive. */
        out.freqs[numFreqs - 1] *= -1.0;
    }

    if (mode == SpectralMode::Phase) {
        /* np.unwrap down each segment: add multiples of 2*pi so no step
           between neighbouring bins exceeds pi. This is the ONLY difference
           between the angle and phase spectra. */
        for (std::size_t s = 0; s < nsegs; ++s) {
            double correction = 0.0;
            for (std::size_t f = 1; f < numFreqs; ++f) {
                const double prev = out.values[s * numFreqs + f - 1];
                double d = out.values[s * numFreqs + f] + correction - prev;
                const double wrapped = std::remainder(d, 2.0 * kPi);
                correction += wrapped - d;
                out.values[s * numFreqs + f] += correction;
            }
        }
    }

    return out;
}

} // namespace vpl
