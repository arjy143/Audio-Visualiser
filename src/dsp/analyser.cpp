#include "dsp/analyser.hpp"

namespace dsp
{
    //initialise ring buffer and fft, zero everything else
    Analyser::Analyser(ring_buffer::RingBuffer<float, 65536>& rb) : ring_buffer_(rb), fft_(k_FFT_size), samples_{}, spectrum_{}, smoothed_{} 
    {
        smoothed_.fill(-90.0f);
    }

    //call this once per frame
    void Analyser::update() noexcept
    {
        //no partial fft if the buffer isn't full
        if (ring_buffer_.available() < k_FFT_size)
        {
            return;
        }    

        //read from ring buffer into samples
        ring_buffer_.pop(samples_.data(), k_FFT_size);

        //apply hann window
        apply_window(k_hann_window.data(), samples_.data(), k_FFT_size);

        //FFT
        fft_.compute(std::span<const float>(samples_), std::span<float>(spectrum_));

        //apply exponential moving average for smoothness
        constexpr float k_attack = 0.8f;
        constexpr float k_decay = 0.15f;

        for (size_t k = 0; k < k_spectrum_bins; k++)
        {
            const float alpha = spectrum_[k] > smoothed_[k] ? k_attack : k_decay;
            smoothed_[k] = alpha * spectrum_[k] + (1.0f - alpha) * smoothed_[k];
        }


    }

    //renderer will use this to get the latest smoothed spectrum
    //use span for no copy, only read access
    std::span<const float> Analyser::spectrum() const noexcept
    {
        return std::span<const float>(smoothed_);
    }
}