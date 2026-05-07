#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <cstdint>

//windowing to prevent spectral leakage in fast fourier transform
namespace dsp
{

constexpr size_t k_FFT_size = 4096;
constexpr size_t k_spectrum_bins = k_FFT_size / 2 + 1;
constexpr uint32_t k_sample_rate = 48000;



template<size_t N>
constexpr std::array<float, N> make_hann_window()
{
    static_assert(N > 1, "Window size must be greater than 1");
    std::array<float, N> w{};

    for (size_t n = 0; n < N; n++)
    {
        w[n] = 0.5f * (1.0f - std::cos((2.0f * std::numbers::pi_v<float> * static_cast<float>(n)) / static_cast<float>(N - 1)));
    }

    return w;
}

template<size_t N>
constexpr std::array<float, N> make_blackman_window()
{
    static_assert(N > 1, "Window size must be greater than 1");
    std::array<float, N> w{};

    for (size_t n = 0; n < N; n++)
    {
        const float t = static_cast<float>(n) / static_cast<float>(N - 1);
        w[n] = 0.42f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * t)
                     + 0.08f * std::cos(4.0f * std::numbers::pi_v<float> * t);
    }

    return w;
}

constexpr auto k_hann_window = make_hann_window<k_FFT_size>();

constexpr auto k_blackman_window = make_blackman_window<k_FFT_size>();

//multiply buffer of samples in place using chosen window function
inline void apply_window(const float* __restrict__ window, float* __restrict__ samples, size_t n) noexcept
{
    //__restrict__ lets the compiler know they don't overlap, so it can autovec
    for (size_t i = 0; i < n; i++)
    {
        samples[i] *= window[i];
    }
}

}