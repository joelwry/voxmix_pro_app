#pragma once
// Small iterative radix-2 complex FFT (offline analysis and denoising only; not used on the
// audio thread). Sizes must be powers of two.

#include <cmath>
#include <complex>
#include <vector>

namespace vox
{
class Fft
{
public:
    explicit Fft (int order) : n (1 << order), twiddle ((size_t) n / 2), rev ((size_t) n)
    {
        for (int i = 0; i < n / 2; ++i)
            twiddle[(size_t) i] = std::polar (1.0f, (float) (-2.0 * 3.14159265358979323846 * i / n));
        for (int i = 0, j = 0; i < n; ++i)
        {
            rev[(size_t) i] = j;
            int bit = n >> 1;
            while (j & bit) { j ^= bit; bit >>= 1; }
            j |= bit;
        }
    }

    int size() const noexcept { return n; }

    /** In-place forward (inverse = false) or inverse (unnormalised) transform. */
    void transform (std::vector<std::complex<float>>& x, bool inverse = false) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (x[(size_t) i], x[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    auto w = twiddle[(size_t) (k * step)];
                    if (inverse) w = std::conj (w);
                    const auto a = x[(size_t) (i + k)];
                    const auto b = x[(size_t) (i + k + len / 2)] * w;
                    x[(size_t) (i + k)] = a + b;
                    x[(size_t) (i + k + len / 2)] = a - b;
                }
        }
    }

    /** Magnitudes of bins 0..n/2 of a real frame. */
    void magnitudes (const float* frame, std::vector<float>& mag, std::vector<std::complex<float>>& work) const
    {
        work.assign ((size_t) n, {});
        for (int i = 0; i < n; ++i) work[(size_t) i] = { frame[i], 0.0f };
        transform (work);
        mag.resize ((size_t) n / 2 + 1);
        for (int i = 0; i <= n / 2; ++i) mag[(size_t) i] = std::abs (work[(size_t) i]);
    }

private:
    int n;
    std::vector<std::complex<float>> twiddle;
    std::vector<int> rev;
};

} // namespace vox
