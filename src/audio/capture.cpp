#include "audio/capture.hpp"
#include <spa/param/audio/format-utils.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace capture
{
    //pass in function pointer to allow C library to interact with C++ code.
    //store the state of the class to be used when the callback triggers.
    static const pw_stream_events k_stream_events =
    {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = &Capture::on_process,
    };

    Capture::Capture(ring_buffer::RingBuffer<float, 65536>& ring_buffer) : ring_buffer_(ring_buffer)
    {
        pw_init(nullptr, nullptr);
        //create thread loop for pipewire
        loop_ = pw_thread_loop_new("audiovis-capture", nullptr);

        if (loop_ == nullptr)
        {
            pw_thread_loop_destroy(loop_);
            throw std::runtime_error("Thread loop is null");
        }
        
        //create stream for the audio capture
        stream_ = pw_stream_new_simple(
            pw_thread_loop_get_loop(loop_),
            "audiovis",
            pw_properties_new(
                PW_KEY_MEDIA_TYPE, "Audio",
                PW_KEY_MEDIA_CATEGORY, "Capture",
                PW_KEY_MEDIA_ROLE, "DSP",
                PW_KEY_STREAM_CAPTURE_SINK, "true",
                nullptr
            ),
            &k_stream_events,
            this
        );

        if (stream_ == nullptr)
        {
            pw_stream_destroy(stream_);
            throw std::runtime_error("Stream is null");
        }

        //specify sample format
        uint8_t buffer[1024];

        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

        const spa_audio_info_raw info = 
        {
            .format = SPA_AUDIO_FORMAT_F32, //float32 to match ring buffer
            .rate = 48000,
            .channels = 2,
        };

        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

        //connect stream and start thread loop
        pw_stream_connect(
            stream_,
            PW_DIRECTION_INPUT, //input goes to here
            PW_ID_ANY, //connect to any source
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
            params,
            1
        );

        pw_thread_loop_start(loop_);
    }

    Capture::~Capture()
    {
        //stop loop and destroy everything in reverse order
        pw_thread_loop_stop(loop_);

        pw_stream_destroy(stream_);
        pw_thread_loop_destroy(loop_);

        pw_deinit();

    }

    void Capture::on_process(void* userdata) noexcept
    {
        //pipewire gives interleaved stereo, so we need mono

        //cast userdata back to Capture*
        auto* self = static_cast<Capture*>(userdata);

        //dequeue pipewire buffer
        pw_buffer* buf = pw_stream_dequeue_buffer(self->stream_);

        if (!buf)
        {
            return;
        }

        //downmix
        spa_buffer* spa_buf = buf->buffer;

        auto* samples = static_cast<float*>(spa_buf->datas[0].data);

        if (samples)
        {
            const uint32_t frames = spa_buf->datas[0].chunk->size / sizeof(float) / 2;
            const uint32_t count = std::min(frames, uint32_t{4096});

            float mono[4096];
            for (uint32_t i = 0; i < count; i++)
            {
                mono[i] = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;
            }

            self->ring_buffer_.push(mono, count);
        }

        pw_stream_queue_buffer(self->stream_, buf);
    }

    uint32_t Capture::x_run_count() const noexcept
    {
        return x_run_count_.load(std::memory_order_relaxed); 
    }
}