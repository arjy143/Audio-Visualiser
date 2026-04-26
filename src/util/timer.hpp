#pragma once

#include <ctime>
#include "util/logger.hpp"

namespace timer
{
    //keep track of time since last frame
    class FrameTimer
    {
        struct timespec ts_;
        static constexpr float budget_ms = 16.67f;
    public:
        FrameTimer()
        {
            clock_gettime(CLOCK_MONOTONIC, &ts_);
        }

        ~FrameTimer(){}
        
        //make sure timing information is not lost
        [[nodiscard]] float mark() noexcept
        {
            struct timespec new_ts;
            clock_gettime(CLOCK_MONOTONIC, &new_ts);

            double ms = (new_ts.tv_sec - ts_.tv_sec) * 1000.0 + (new_ts.tv_nsec - ts_.tv_nsec) / 1e6;

            ts_ = new_ts;

            if (ms > budget_ms)
            {
                logger::LOG_WARN("Frame budget exceeded: %f", ms);
            }

            return static_cast<float>(ms);
        }
    };

    //self destructing timer
    class [[nodiscard]] ScopedTimer
    {
        const char* label_;
        struct timespec ts_;

    public:
        ScopedTimer(const char* label) noexcept : label_(label) 
        {
            clock_gettime(CLOCK_MONOTONIC, &ts_);
        } 
        ~ScopedTimer() noexcept
        {
            timespec new_ts;
            clock_gettime(CLOCK_MONOTONIC, &new_ts);
            
            double ms = (new_ts.tv_sec - ts_.tv_sec) * 1000.0 + (new_ts.tv_nsec - ts_.tv_nsec) / 1e6;

            logger::LOG_DEBUG("Scoped time elapsed: %f", ms);

        }
    };


}
