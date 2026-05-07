#include "render/renderer.hpp"
#include <algorithm>

namespace render
{

    Renderer::Renderer(dsp::Analyser& analyser, const char* title, int width, int height) : analyser_(analyser)
    {
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_)
        {
            throw std::runtime_error("Failed to create GLFW window");
        }
        glfwMakeContextCurrent(window_);

        glewInit();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader_ = std::make_unique<ShaderProgram>("shaders/spectrum.vert", "shaders/spectrum.frag");

        uniforms_ = {
            shader_->uniform("uBinCount"),
            shader_->uniform("uMinDB"),
            shader_->uniform("uMaxDB"),
            shader_->uniform("uMinFreq"),
            shader_->uniform("uMaxFreq"),
            shader_->uniform("uTime"),
            shader_->uniform("uRotation"),
            shader_->uniform("uFlip"),
            shader_->uniform("uFanMode"),
            shader_->uniform("uScale"),
            shader_->uniform("uAlpha"),
            shader_->uniform("uBassEnergy"),
        };

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        //allocate gpu buffer
        glBufferData(GL_ARRAY_BUFFER, dsp::k_spectrum_bins * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

        //1 float per vertex
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        glBindVertexArray(0);
    } 

    Renderer::~Renderer()
    {
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
        if (window_)
        {
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

    [[nodiscard]] bool Renderer::running() const noexcept
    {
        return !glfwWindowShouldClose(window_);
    }

    void Renderer::render() noexcept
    {
        glfwPollEvents();

        //run dsp pipeline
        analyser_.update();
        
        //upload spectrum to gpu
        const auto spectrum = analyser_.spectrum();
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        //overwrite the existing gpu buffer in place
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(spectrum.size_bytes()), spectrum.data());

        glClearColor(0.0f, 0.0f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shader_->bind();

        constexpr float k_min_db = -90.0f;
        constexpr float k_max_db = -25.0f;  // typical music peaks here

        glUniform1i(uniforms_.bin_count, static_cast<int>(dsp::k_spectrum_bins));
        glUniform1f(uniforms_.min_db,    k_min_db);
        glUniform1f(uniforms_.max_db,    k_max_db);
        glUniform1f(uniforms_.min_freq,  20.0f);
        glUniform1f(uniforms_.max_freq,  24000.0f);
        const float time = static_cast<float>(glfwGetTime());
        glUniform1f(uniforms_.time, time);

        // Average the lowest ~17 bins (20–200 Hz) for bass energy
        constexpr int k_bass_bins = 17;
        float bass_sum = 0.0f;
        for (int i = 0; i < k_bass_bins; ++i)
            bass_sum += spectrum[static_cast<size_t>(i)];
        const float bass_avg  = bass_sum / static_cast<float>(k_bass_bins);
        const float bass_norm = std::clamp((bass_avg - k_min_db) / (k_max_db - k_min_db), 0.0f, 1.0f);
        glUniform1f(uniforms_.bass_energy, bass_norm);

        constexpr int   k_symmetry = 6;
        constexpr float k_two_pi   = 2.0f * 3.14159265f;
        const GLsizei   k_bins     = static_cast<GLsizei>(dsp::k_spectrum_bins);

        glUniform1f(uniforms_.scale, 1.0f);

        glBindVertexArray(vao_);
        for (int i = 0; i < k_symmetry; ++i)
        {
            const float rotation = k_two_pi * static_cast<float>(i) / static_cast<float>(k_symmetry);
            glUniform1f(uniforms_.rotation, rotation);

            for (int flip = 0; flip < 2; ++flip)
            {
                glUniform1f(uniforms_.flip, static_cast<float>(flip));

                glUniform1f(uniforms_.fan_mode, 1.0f);
                glUniform1f(uniforms_.alpha,    0.25f);
                glDrawArrays(GL_TRIANGLE_FAN, 0, k_bins);

                glUniform1f(uniforms_.fan_mode, 0.0f);
                glUniform1f(uniforms_.alpha,    1.0f);
                glDrawArrays(GL_LINE_LOOP, 0, k_bins);
            }
        }

        glfwSwapBuffers(window_);
    }
}