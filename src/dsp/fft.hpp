#pragma once

#include <vector>
#include <complex>
#include <span>
#include "dsp/window.hpp"

//cooley tukey radix 2 dit fft
namespace dsp
{

class FFTPlan
{
    size_t n_;

    std::vector<std::complex<float>> buf_; 

    //precomputed twiddle factors
    std::vector<std::complex<float>> twiddle_;

    //precomputed bit reversal table
    std::vector<size_t> bit_rev_;


public:
    explicit FFTPlan(size_t n);

    void compute(std::span<const float> in, std::span<float> out) noexcept;
};

}