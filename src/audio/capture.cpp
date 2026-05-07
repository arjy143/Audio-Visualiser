#include "audio/capture.hpp"

namespace capture
{
    explicit Capture::Capture(ring_buffer::RingBuffer<float, 65536>& ring_buffer) : ring_buffer_(ring_buffer);

    ~Capture();

    static void on_process(void* userdata) noexcept;
    [[nodiscard]] uint32_t x_run_count() const noexcept;
}