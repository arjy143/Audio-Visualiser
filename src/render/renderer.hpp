#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "render/shader.hpp"
#include "dsp/analyser.hpp"
#include <memory>
#include <array>

namespace render
{

class Renderer
{
    GLFWwindow* window_{nullptr};
    int width_{0}, height_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    std::unique_ptr<ShaderProgram> shader_;
    dsp::Analyser& analyser_;

    //cache the GLint values instead of calling inside the driver every time
    struct Uniforms
    {
        GLint bin_count, min_db, max_db, min_freq, max_freq;
        GLint time, rotation, flip, fan_mode, scale, alpha, bass_energy, beat_kick;
    };
    Uniforms uniforms_{};

    //beat detection
    float bass_avg_{0.0f};
    float beat_kick_{0.0f};

    //post-processing
    GLuint scene_fbo_{0}, ping_fbo_{0}, pong_fbo_{0};
    GLuint scene_tex_{0}, ping_tex_{0}, pong_tex_{0};
    GLuint quad_vao_{0};

    std::unique_ptr<ShaderProgram> blur_shader_;
    std::unique_ptr<ShaderProgram> composite_shader_;
    std::unique_ptr<ShaderProgram> fade_shader_;

    struct BlurUniforms      { GLint image, horizontal;             } blur_uniforms_{};
    struct CompositeUniforms { GLint scene, bloom;                  } composite_uniforms_{};
    struct FadeUniforms      { GLint bass_energy, beat_kick, time;  } fade_uniforms_{};

    // Milk-drop feedback — persistent texture that the warp shader reads each frame
    GLuint milk_fbo_{0};
    GLuint milk_tex_{0};
    std::unique_ptr<ShaderProgram> milk_shader_;
    struct MilkUniforms { GLint prev, bass, beat, time; } milk_uniforms_{};

    int current_symmetry_{6};

    // Bars mode VAO/VBO — magnitudes stored doubled: [m0,m0, m1,m1, ...]
    // so that each GL_LINES pair shares the same magnitude (inner + outer endpoint).
    GLuint bars_vao_{0};
    GLuint bars_vbo_{0};
    std::array<float, dsp::k_spectrum_bins * 2> bars_data_{};

    // Mode 4 Iris — inner anchor ring (unit-circle geometry, scaled by shader)
    static constexpr int k_pulse_segs = 128;
    GLuint pulse_vao_{0};
    GLuint pulse_vbo_{0};

    // Mode 5 Prism — kaleidoscope: fundamental sector + 2N draw calls
    // Sector spans [0, π/N]; mirror copy covers [-π/N, 0]; N pairs tile 2π.
    static constexpr int k_kal_segs = 128;
    GLuint kal_vao_{0};
    GLuint kal_vbo_{0};
    std::array<float, (k_kal_segs + 1) * 6> kal_data_{};  // 1 centre + k_kal_segs arc verts

    std::unique_ptr<ShaderProgram> pulse_shader_;
    struct PulseUniforms { GLint radius, colour, center; } pulse_uniforms_{};

    std::unique_ptr<ShaderProgram> kal_shader_;
    struct KalUniforms { GLint angle, mirror; } kal_uniforms_{};

    // Mode 4: Iris — filled annular spectrum ring (GL_TRIANGLE_STRIP).
    // Interleaved layout: inner[0], outer[0], inner[1], outer[1], ..., closing pair.
    // Each vertex is (x, y, r, g, b, a) = 6 floats.
    static constexpr int k_orb_n = 1024;  // log-spaced samples around ring (20 Hz – 20 kHz)
    GLuint orb_vao_{0};
    GLuint orb_vbo_{0};
    std::array<float, (k_orb_n + 1) * 12> orb_data_{};

    // Resonance web mode — N-choose-2 mesh of frequency-band vertices
    // 12 vertices → 66 lines; each line endpoint carries (x,y,r,g,b,a).
    static constexpr int k_web_n     = 12;
    static constexpr int k_web_lines = k_web_n * (k_web_n - 1) / 2;  // 66

    GLuint web_vao_{0};
    GLuint web_vbo_{0};
    std::array<float, k_web_lines * 2 * 6> web_data_{};

    std::unique_ptr<ShaderProgram> web_shader_;

    // Mode 8: Nova — radial spike lattice + shockwave rings driven by bass + transients
    // Each ring expands outward from the centre and fades, spawned on every beat hit.
    // Spikes show the spectrum continuously; onset_kick_ fires them all simultaneously.
    static constexpr int k_nova_rings  = 8;
    static constexpr int k_nova_spikes = 80;

    struct NovaRing { float radius = 0.0f; float alpha = 0.0f; };
    std::array<NovaRing, k_nova_rings> nova_rings_{};

    GLuint spike_vao_{0};
    GLuint spike_vbo_{0};
    std::array<float, k_nova_spikes * 2 * 6> spike_data_{};  // GL_LINES: 2 verts × 6 floats

    std::unique_ptr<ShaderProgram> nova_shader_;
    struct NovaUniforms { GLint center, scale; } nova_uniforms_{};

    // Precomputed lookup tables — built once in constructor to avoid per-frame
    // pow() / cos() / sin() inside the vertex-building loops.
    // Declared after all k_* constants so array sizes are resolved.
    std::array<int,   k_orb_n>          orb_bins_{};
    std::array<float, k_orb_n>          orb_cos_{};
    std::array<float, k_orb_n>          orb_sin_{};

    std::array<int,   k_nova_spikes>    nova_bins_{};
    std::array<float, k_nova_spikes>    nova_cos_{};
    std::array<float, k_nova_spikes>    nova_sin_{};

    std::array<int,   k_web_n>          web_bins_{};
    std::array<float, k_web_n>          web_cos_{};
    std::array<float, k_web_n>          web_sin_{};

    std::array<int,   k_kal_segs>       kal_bins_{};

    // 3-bin-max normalised amplitude, rebuilt once per frame and shared by all modes.
    std::array<float, dsp::k_spectrum_bins> smooth_spec_{};

    // Visual mode: 0=Aura, 1=Tunnel, 2=Bars, 3=Burst, 4=Iris, 5=Prism, 6=Web, 7=Milk, 8=Nova
    int  mode_{0};
    int  mode_frames_{0};
    bool key_m_prev_{false};

public:
    Renderer(dsp::Analyser& analyser, const char* title, int width, int height);
    ~Renderer();

    //non copyable
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;

    [[nodiscard]] bool running() const noexcept;

    //call once per frame
    void render() noexcept;
};

}