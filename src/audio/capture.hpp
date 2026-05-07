#pragma once

#include <atomic>
#include <cstdint>
#include <pipewire/pipewire.h>
#include "audio/ring_buffer.hpp"

namespace capture
{

    //open pipewire audio stream, and write buffer of samples into ring buffer
class Capture
{
    //pointer to opaque struct
    pw_thread_loop* loop_;
    pw_stream* stream_;

    ring_buffer::RingBuffer<float, 65536>& ring_buffer_;

    std::atomic<uint32_t> x_run_count_;

    //pipewire calls this on real time thread
    static void on_process(void* userdata) noexcept;

public:
    explicit Capture(ring_buffer::RingBuffer<float, 65536>& ring_buffer);

    ~Capture();

    //non copyable nor movable
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;

    [[nodiscard]] uint32_t x_run_count() const noexcept;
};

}