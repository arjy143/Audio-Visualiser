#pragma once
#include <GL/glew.h>

namespace render
{
//load vertex shader and fragment shader onto gpu and link
class ShaderProgram
{
    //0 means invalid/not created
    GLuint program_{0};

public:
    //load files, compile, link
    ShaderProgram(const char* vert_path, const char* frag_path);

    ~ShaderProgram();

    //non copyable
    ShaderProgram(const ShaderProgram&)=delete;
    ShaderProgram& operator=(const ShaderProgram&)=delete;

    //use these for subsequent draw calls
    void bind() const noexcept;
    void unbind() const noexcept;

    //get uniform location by name, for setting shader params
    [[nodiscard]] GLint uniform(const char* name) const noexcept;

    //ID needed when passing directly to opengl
    [[nodiscard]] GLuint id() const noexcept;
};

}