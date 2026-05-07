#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "render/shader.hpp"
#include "render/visualiser.hpp"
#include "dsp/analyser.hpp"
#include <memory>

namespace render
{

class Renderer
{
    GLFWwindow* window_{nullptr};
    GLuint vao_{0};
    GLuint vbo_{0};

    std::unique_ptr<ShaderProgram> shader_;
    Visualiser visualiser_;
    dsp::Analyser& analyser_;

    // Uniform locations cached at startup — glGetUniformLocation is slow
    struct Uniforms
    {
        GLint bin_count, min_db, max_db, min_freq, max_freq;
        GLint time, rotation, flip, fan_mode, scale, alpha, bass_energy, beat_kick;
    };
    Uniforms uniforms_{};

    float bass_avg_{0.0f};   // slow EMA of bass level — the "background"
    float beat_kick_{0.0f};  // 1.0 on a beat, decays to 0 each frame

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