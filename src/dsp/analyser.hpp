#pragma once

#include "audio/ring_buffer.hpp"
#include "dsp/window.hpp"
#include "dsp/fft.hpp"
#include <span>
#include <array>

namespace dsp
{

//analyser will tie everything together on the dsp side, as well as smooth out and reduce flicker after the fft step.

class Analyser
{
    ring_buffer::RingBuffer<float, 65536>& ring_buffer_;

    FFTPlan fft_;

    std::array<float, k_FFT_size>      samples_;
    std::array<float, k_spectrum_bins> spectrum_;
    std::array<float, k_spectrum_bins> smoothed_;


public:
    explicit Analyser(ring_buffer::RingBuffer<float, 65536>& rb);

    //call this once per frame
    void update() noexcept;

    //renderer will use this to get the latest smoothed spectrum
    //use span for no copy, only read access
    [[nodiscard]] std::span<const float> spectrum() const noexcept;
};

}