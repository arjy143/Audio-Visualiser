#include "render/renderer.hpp"

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

        //construct shaderprogram
        shader_ = std::make_unique<ShaderProgram>("shaders/spectrum.vert", "shaders/spectrum.frag");

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

        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shader_->bind();

        //set uniforms
        glUniform1i(shader_->uniform("uBinCount"), static_cast<int>(dsp::k_spectrum_bins));
        glUniform1f(shader_->uniform("uMinDB"), -90.0f);
        glUniform1f(shader_->uniform("uMaxDB"), 0.0f);
        glUniform1f(shader_->uniform("uMinFreq"), 20.0f);
        glUniform1f(shader_->uniform("uMaxFreq"), 24000.0f);


        //adding some symmetry
        constexpr int k_symmetry = 6;
        constexpr float k_two_pi = 2.0f * 3.14159265f;

        glBindVertexArray(vao_);
        for (int i = 0; i < k_symmetry; ++i)
        {
            const float rotation = k_two_pi * static_cast<float>(i) / static_cast<float>(k_symmetry);
            glUniform1f(shader_->uniform("uRotation"), rotation);

            glUniform1f(shader_->uniform("uFlip"), 0.0f);
            glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(dsp::k_spectrum_bins));

            glUniform1f(shader_->uniform("uFlip"), 1.0f);
            glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(dsp::k_spectrum_bins));
        }

        glfwSwapBuffers(window_);
    }
}