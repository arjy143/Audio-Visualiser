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
    GLuint vao_{0};
    GLuint vbo_{0};

    std::unique_ptr<ShaderProgram> shader_;
    dsp::Analyser& analyser_;

    struct Uniforms
    {
        GLint bin_count, min_db, max_db, min_freq, max_freq;
        GLint time, rotation, flip, fan_mode, scale, alpha, bass_energy, beat_kick;
    };
    Uniforms uniforms_{};

    float bass_avg_{0.0f};
    float beat_kick_{0.0f};

    float onset_avg_{0.0f};   // running mean of broadband percussive energy
    float onset_kick_{0.0f};  // fast-decaying impulse for snare/hi-hat/clap

    // Post-processing
    GLuint scene_fbo_{0}, ping_fbo_{0}, pong_fbo_{0};
    GLuint scene_tex_{0}, ping_tex_{0}, pong_tex_{0};
    GLuint quad_vao_{0};

    std::unique_ptr<ShaderProgram> blur_shader_;
    std::unique_ptr<ShaderProgram> composite_shader_;
    std::unique_ptr<ShaderProgram> fade_shader_;

    struct BlurUniforms      { GLint image, horizontal;             } blur_uniforms_{};
    struct CompositeUniforms { GLint scene, bloom;                  } composite_uniforms_{};
    struct FadeUniforms      { GLint bass_energy, beat_kick, time;  } fade_uniforms_{};

    int current_symmetry_{6};

    // Bars mode VAO/VBO — magnitudes stored doubled: [m0,m0, m1,m1, ...]
    // so that each GL_LINES pair shares the same magnitude (inner + outer endpoint).
    GLuint bars_vao_{0};
    GLuint bars_vbo_{0};
    std::array<float, dsp::k_spectrum_bins * 2> bars_data_{};

    // Bass pulse mode — frequency corona + staggered membrane + ripple rings
    static constexpr int k_pulse_segs = 128;
    static constexpr int k_pulse_max  = 12;   // larger pool for fast music + double-spawns
    static constexpr int k_corona_n   = 64;   // radial frequency spokes around the membrane

    struct PulseRing { float radius = 0.0f; float alpha = 0.0f; };
    std::array<PulseRing, k_pulse_max> pulse_rings_{};

    GLuint pulse_vao_{0};
    GLuint pulse_vbo_{0};

    // Corona uses the same (x,y,r,g,b,a) per-vertex format as the web shader
    GLuint corona_vao_{0};
    GLuint corona_vbo_{0};
    std::array<float, k_corona_n * 12> corona_data_{};  // 2 verts × 6 floats per spoke

    std::unique_ptr<ShaderProgram> pulse_shader_;
    struct PulseUniforms { GLint radius, colour; } pulse_uniforms_{};

    // Resonance web mode — N-choose-2 mesh of frequency-band vertices
    // 12 vertices → 66 lines; each line endpoint carries (x,y,r,g,b,a).
    static constexpr int k_web_n     = 12;
    static constexpr int k_web_lines = k_web_n * (k_web_n - 1) / 2;  // 66

    GLuint web_vao_{0};
    GLuint web_vbo_{0};
    std::array<float, k_web_lines * 2 * 6> web_data_{};

    std::unique_ptr<ShaderProgram> web_shader_;

    // Visual mode: 0=Aura, 1=Tunnel, 2=Bars, 3=Burst, 4=Circle, 5=BassPulse, 6=Web
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