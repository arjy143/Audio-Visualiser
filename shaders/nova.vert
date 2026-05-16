#version 330 core

layout(location = 0) in vec2 a_pos;  // spike vertex relative to origin
layout(location = 1) in vec4 a_col;

uniform vec2  uCenter;  // orb centre in NDC
uniform float uScale;   // uniform scale applied before translation

out vec4 vCol;

void main()
{
    gl_Position = vec4(a_pos * uScale + uCenter, 0.0, 1.0);
    vCol = a_col;
}
