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

    int current_symmetry_{6};

    // Bars mode VAO/VBO — magnitudes stored doubled: [m0,m0, m1,m1, ...]
    // so that each GL_LINES pair shares the same magnitude (inner + outer endpoint).
    GLuint bars_vao_{0};
    GLuint bars_vbo_{0};
    std::array<float, dsp::k_spectrum_bins * 2> bars_data_{};


    // Visual mode: 0=Aura, 1=Tunnel, 2=Bars, 3=Burst, 4=Circle
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