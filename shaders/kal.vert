#version 330 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_col;

uniform float uAngle;   // rotation for this sector copy
uniform float uMirror;  // 1.0 = flip y before rotating (mirror image)

out vec4 vCol;

void main()
{
    vec2 p = a_pos;
    if (uMirror > 0.5) p.y = -p.y;
    float cs = cos(uAngle), sn = sin(uAngle);
    p = vec2(cs * p.x - sn * p.y,
             sn * p.x + cs * p.y);
    gl_Position = vec4(p, 0.0, 1.0);
    vCol = a_col;
}
