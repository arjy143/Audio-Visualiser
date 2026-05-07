#include "dsp/fft.hpp"
#include <cmath>
#include <numbers>

namespace dsp
{

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
    //copy input floats into complex number buffer 
    for (size_t i = 0; i < in.size(); i++)
    {
        //set imaginary part to 0
        buf_[i] = std::complex<float>(in[i], 0.0f);
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

    //convert to dB magnitude
    constexpr float k_floor = -90.0f;
    for (size_t k = 0; k < out.size(); k++)
    {
        const float mag = std::abs(buf_[k]) / static_cast<float>(n_);
        out[k] = (mag > 0.0f) ? 20.0f * std::log10(mag) : k_floor;
    }
}

}