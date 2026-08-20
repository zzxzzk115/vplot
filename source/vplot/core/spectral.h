/*
 * The spectral family (psd, csd, cohere, specgram and the three single spectra)
 * shares one implementation, as in matplotlib's mlab._spectral_helper.
 *
 * Notes:
 *   - A one-sided spectrum doubles every bin except DC, and except Nyquist when
 *     NFFT is even; those two bins have no mirror image to fold in.
 *   - scale_by_freq divides by Fs and by sum(window^2) (windowing-loss
 *     correction, Bendat & Piersol 11.5.2); otherwise the divisor is
 *     sum(window)^2.
 *   - The single spectra (magnitude/angle/phase) use the whole signal as one
 *     segment with scale_by_freq=False.
 */

#ifndef VPL_CORE_SPECTRAL_H
#define VPL_CORE_SPECTRAL_H

#include <complex>
#include <cstddef>
#include <vector>

namespace vpl
{

/*
 * In-place discrete Fourier transform of any length: radix-2 Cooley-Tukey for
 * power-of-two lengths, Bluestein's chirp-z otherwise.
 */
void fft_inplace(std::vector<std::complex<double>> &a);

enum class SpectralSides
{
    /* Real input; negative frequencies are redundant. The default. */
    OneSided,
    TwoSided
};

enum class SpectralMode
{
    Psd,
    Magnitude,
    Angle,
    Phase,
    Complex
};

/* Spectrogram value scaling (matplotlib's specgram scale). Default is dB for
   psd and magnitude modes, linear for angle and phase. */
enum class SpecgramScale
{
    Default,
    Linear,
    Db
};

/* Window applied to each segment. Hanning is matplotlib's default. */
enum class SpectralWindow
{
    Hanning,
    Hamming,
    Blackman,
    Bartlett,
    Boxcar
};

/* What is removed from each segment before the transform (matplotlib's
   detrend). None is the default. */
enum class SpectralDetrend
{
    None,
    Mean,
    Linear
};

/* Shared options (matplotlib's window, detrend, sides and pad_to). Defaults
   match matplotlib. */
struct SpectralOptions
{
    SpectralWindow window = SpectralWindow::Hanning;
    SpectralDetrend detrend = SpectralDetrend::None;
    SpectralSides sides = SpectralSides::OneSided;
    std::size_t pad_to = 0; /* 0 means NFFT */
};

struct SpectralResult
{
    /* numFreqs rows by nsegs columns, column-major by segment: entry (f, s) is
       at values[s * numFreqs + f]. */
    std::vector<double> values;
    /*
     * Imaginary half, filled only for the cross-spectrum and empty otherwise.
     * csd averages the complex cross-spectrum across segments before taking the
     * modulus, so the imaginary part must be retained.
     */
    std::vector<double> values_imag;
    std::size_t numFreqs = 0;
    std::size_t nsegs = 0;

    std::vector<double> freqs;
    /* Segment centre times, for specgram. */
    std::vector<double> times;
};

/*
 * matplotlib's mlab._spectral_helper.
 *
 * `y` may be null (auto-spectrum); when given, mode must be Psd and the
 * cross-spectrum is computed instead.
 */
SpectralResult spectral_helper(const double *x, const double *y, std::size_t n,
                               std::size_t NFFT, double Fs, std::size_t noverlap,
                               std::size_t pad_to, SpectralSides sides, SpectralMode mode,
                               bool scale_by_freq,
                               SpectralWindow window_type = SpectralWindow::Hanning,
                               SpectralDetrend detrend = SpectralDetrend::None);

} // namespace vpl

#endif /* VPL_CORE_SPECTRAL_H */
