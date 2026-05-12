#version 330 core

layout(location = 0) in vec2 a_pos;   // unit-circle vertex (cos θ, sin θ)

uniform float uRadius;                 // scale the unit circle to the desired radius

void main()
{
    gl_Position = vec4(a_pos * uRadius, 0.0, 1.0);
}
