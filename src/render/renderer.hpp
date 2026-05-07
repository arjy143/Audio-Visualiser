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

    //keep a null pointer initially and then construct in the body
    std::unique_ptr<ShaderProgram> shader_;
    Visualiser visualiser_;
    dsp::Analyser& analyser_;

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