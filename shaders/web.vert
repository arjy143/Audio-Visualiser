#version 330 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_col;

out vec4 vCol;

void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    vCol        = a_col;
}
