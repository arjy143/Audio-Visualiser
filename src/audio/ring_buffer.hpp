#pragma once

#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cstring>


namespace ring_buffer
{


template<typename T, size_t Capacity>
class RingBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "RingBuffer requires trivially copyable T in order for memcpy to be valid");
    static_assert(Capacity && ((Capacity & (Capacity - 1)) == 0), "Capacity is not a power of 2");

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    T buffer_[Capacity];

public:

    size_t push(const T* data, size_t count) noexcept
    {
        //we own head_
        const size_t head = head_.load(std::memory_order_relaxed);
        //consumer wrote tail_
        const size_t tail = tail_.load(std::memory_order_acquire);

        const size_t free_space = Capacity - (head - tail);

        count = std::min(count, free_space);
        if (count == 0) return 0;

        const size_t start = head & (Capacity - 1);

        //in case of wrap around
        const size_t first_part  = std::min(count, Capacity - start);
        const size_t second_part = count - first_part;

        std::memcpy(buffer_ + start, data, first_part * sizeof(T));
        if (second_part > 0)
            std::memcpy(buffer_, data + first_part, second_part * sizeof(T));

        head_.store(head + count, std::memory_order_release);
        return count;
    }

    size_t pop(T* dest, size_t count) noexcept
    {
        //producer wrote head_
        const size_t head = head_.load(std::memory_order_acquire);
        //we own tail_
        const size_t tail = tail_.load(std::memory_order_relaxed);

        const size_t avail = head - tail;
        count = std::min(count, avail);
        if (count == 0) return 0;

        const size_t start = tail & (Capacity - 1);

        //in case of wrap around
        const size_t first_part  = std::min(count, Capacity - start);
        const size_t second_part = count - first_part;

        std::memcpy(dest, buffer_ + start, first_part * sizeof(T));
        if (second_part > 0)
            std::memcpy(dest + first_part, buffer_, second_part * sizeof(T));

        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    size_t discard(size_t count) noexcept
    {
        const size_t tail  = tail_.load(std::memory_order_relaxed);
        const size_t avail = head_.load(std::memory_order_acquire) - tail;
        count = std::min(count, avail);
        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    size_t available() const noexcept
    {
        //producer wrote head_
        const size_t head = head_.load(std::memory_order_acquire);
        //we own tail_
        const size_t tail = tail_.load(std::memory_order_relaxed);

        return head - tail;
    }

};

}
