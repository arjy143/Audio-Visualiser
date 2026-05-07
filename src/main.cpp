#include "render/renderer.hpp"
#include "audio/ring_buffer.hpp"
#include "dsp/analyser.hpp"
#include "audio/capture.hpp"
#include <stdexcept>
#include <cstdio>

int main()
{  
    try
    {
        ring_buffer::RingBuffer<float, 65536> buffer;
        capture::Capture{buffer};
        dsp::Analyser analyser{buffer};
        render::Renderer renderer{analyser, "Audio visualiser", 800, 600};

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
