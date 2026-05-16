#include "dsp/fft.hpp"
#include <cmath>
#include <cstring>
#include <numbers>

namespace dsp
{

namespace
{
// ⚡ Performance note: fast_log2 replaces std::abs (sqrt) + std::log10 for each bin.
// IEEE 754 floats store: sign(1) | exponent(8, biased by 127) | mantissa(23).
// Reading the exponent directly gives floor(log2(x)); treating the mantissa as a
// float in [1.0, 2.0) and using log2(m) ≈ m − 1 gives the fractional part.
// Max error: ~0.086 bits ≈ 0.26 dB — imperceptible in a visualiser.
// memcpy is the standard-blessed way to type-pun between float and uint32_t.
[[nodiscard]] inline float fast_log2(float x) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    const int exp = static_cast<int>((bits >> 23u) & 0xFFu) - 127;
    // Force exponent to 127 (= 2⁰) so the reinterpreted value is in [1.0, 2.0)
    bits = (bits & 0x007FFFFFu) | 0x3F800000u;
    float frac;
    std::memcpy(&frac, &bits, sizeof(frac));
    return static_cast<float>(exp) + frac - 1.0f;
}
} // anonymous namespace

//reverse a bitstring given an amount of bits
constexpr size_t bit_reverse(size_t input, int bits) noexcept
{
    size_t result = 0;

    for (int i = 0; i < bits; i++)
    {
        result = (result << 1) | (input & 1);
        input >>= 1;
    }

    return result;
}

FFTPlan::FFTPlan(size_t n) : n_(n)
{
    buf_.resize(n);
    bit_rev_.resize(n);
    twiddle_.resize(n / 2);
    n_log2_x2_ = 2.0f * std::log2(static_cast<float>(n));

    //precompute twiddle and bit reversal table
    for (size_t i = 0; i < n; i++)
    {
        bit_rev_[i] = bit_reverse(i, static_cast<int>(std::log2(n_))); 
    }

    for (size_t k = 0; k < n/2; k++)
    {
        const float angle = -2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(n_);

        //eulers formula
        twiddle_[k] = std::complex<float>(std::cos(angle), std::sin(angle));
    }
}

void FFTPlan::compute(std::span<const float> in, std::span<float> out) noexcept
{
    // ⚡ Performance note: writing to the raw float pairs lets the auto-vectoriser
    // use wider stores (SSE/AVX) rather than constructing std::complex temporaries.
    // std::complex<float> layout is guaranteed to be two consecutive floats (real, imag).
    float* raw = reinterpret_cast<float*>(buf_.data());
    for (size_t i = 0; i < n_; ++i)
    {
        raw[i * 2]     = in[i];
        raw[i * 2 + 1] = 0.0f;
    }

    for (size_t i = 0; i < n_; i++)
    {
        //if swapping hasn't occured already between this pair
        if (i < bit_rev_[i])
        {
            std::swap(buf_[i], buf_[bit_rev_[i]]);
        }
    }

    //butterfly passes
    for (size_t len = 2; len <= n_; len *=2)
    {
        const size_t half = len / 2;

        for (size_t i = 0; i < n_; i += len)
        {
            for (size_t k = 0; k < half; k++)
            {
                //apply twiddle
                const std::complex<float> t = twiddle_[k * (n_ / len)] * buf_[i + k + half];

                buf_[i + k + half] = buf_[i + k] - t;
                buf_[i + k] = buf_[i + k] + t;
            }
        }
    }

    // 🧠 Concept: 20·log10(|x|/n) = (10/log₂10) · (log₂(r²+i²) − 2·log₂n)
    // This avoids one sqrt (inside std::abs) and one log10 per bin.
    // fast_log2 cuts the remaining log cost to ~5 integer ops via the exponent field.
    constexpr float k_floor = -90.0f;
    constexpr float k_coeff = 10.0f / 3.32192809f;  // 10 / log₂(10)
    for (size_t k = 0; k < out.size(); ++k)
    {
        const float r  = buf_[k].real();
        const float im = buf_[k].imag();
        const float sq = r * r + im * im;
        out[k] = (sq > 0.0f)
               ? std::max(k_coeff * (fast_log2(sq) - n_log2_x2_), k_floor)
               : k_floor;
    }
}

}