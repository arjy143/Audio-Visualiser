#version 330 core

layout(location = 0) in vec2 a_pos;   // unit-circle vertex (cos θ, sin θ)

uniform float uRadius;
uniform vec2  uCenter;   // orb position in NDC; set to (0,0) for centred use

void main()
{
    gl_Position = vec4(a_pos * uRadius + uCenter, 0.0, 1.0);
}
