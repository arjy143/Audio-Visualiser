#include "render/shader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace render
{
    //helper functions
    //read file to string, used for loading GLSL source from disk
    [[nodiscard]] static std::string read_file(const char* path)
    {
        std::ifstream file{path};
        
        if (!file.is_open())
        {
            throw std::runtime_error("Could not read shader file");
        }

        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()); 
    }

    //compile a single shader and return gpu handle
    [[nodiscard]] static GLuint compile_shader(GLenum type, const std::string& source)
    {
        GLuint shader = glCreateShader(type);

        const char* src = source.c_str();
        const GLint len = static_cast<GLint>(source.size());
        glShaderSource(shader, 1, &src, &len);
        glCompileShader(shader);

        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(std::string("Shader compile error: ") + log);
        }
        
        return shader;
    }


    ShaderProgram::ShaderProgram(const char* vert_path, const char* frag_path)
    {
        std::string vert_contents = read_file(vert_path);
        std::string frag_contents = read_file(frag_path);

        GLuint vert_shader = compile_shader(GL_VERTEX_SHADER, vert_contents);
        GLuint frag_shader = compile_shader(GL_FRAGMENT_SHADER, frag_contents);
        
        program_ = glCreateProgram();
        glAttachShader(program_, vert_shader);
        glAttachShader(program_, frag_shader);
        glLinkProgram(program_);

        GLint ok;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
            glDeleteShader(vert_shader);
            glDeleteShader(frag_shader);
            glDeleteProgram(program_);
            program_ = 0;
            throw std::runtime_error(std::string("Shader link error: ") + log);
        }

        glDeleteShader(vert_shader);
        glDeleteShader(frag_shader);
        


    }

    ShaderProgram::~ShaderProgram()
    {
        if (program_ != 0)
        {
            glDeleteProgram(program_);
        }
    }

    //use these for subsequent draw calls
    void ShaderProgram::bind() const noexcept
    {
        glUseProgram(program_);
    }

    void ShaderProgram::unbind() const noexcept
    {
        glUseProgram(0);
    }

    //get uniform location by name, for setting shader params
    [[nodiscard]] GLint ShaderProgram::uniform(const char* name) const noexcept
    {
        return glGetUniformLocation(program_, name);
    }

    //ID needed when passing directly to opengl
    [[nodiscard]] GLuint ShaderProgram::id() const noexcept
    {
        return program_;
    }
}