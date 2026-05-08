#include "render/renderer.hpp"
#include "audio/ring_buffer.hpp"
#include "dsp/analyser.hpp"
#include "audio/capture.hpp"
#include <stdexcept>
#include <cstdio>
#include <filesystem>
#include <unistd.h>

int main()
{
    try
    {
        // Set CWD to the directory containing the binary so that relative
        // paths like "shaders/spectrum.vert" work regardless of how the app
        // is launched (terminal, systemd service, desktop autostart, etc.).
        {
            char buf[4096] = {};
            if (::readlink("/proc/self/exe", buf, sizeof(buf) - 1) > 0)
                std::filesystem::current_path(
                    std::filesystem::path(buf).parent_path());
        }
        ring_buffer::RingBuffer<float, 65536> buffer;
        capture::Capture capture{buffer};
        dsp::Analyser analyser{buffer};
        render::Renderer renderer{analyser, "Audio visualiser", 800, 800};

        while (renderer.running())
        {
            renderer.render();
        }

        return 0;
    }
    catch(const std::exception& e)
    {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());

        return 1;
    }
     
}
