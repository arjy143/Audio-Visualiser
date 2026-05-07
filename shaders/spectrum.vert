#version 330 core

layout(location = 0) in float magnitude;

uniform int   uBinCount;
uniform float uMinDB;
uniform float uMaxDB;

out float vMagnitude;

void main()
{
    float x = (float(gl_VertexID) / float(uBinCount - 1)) * 2.0 - 1.0;

    float normalised = clamp((magnitude - uMinDB) / (uMaxDB - uMinDB), 0.0, 1.0);
    float y = normalised * 2.0 - 1.0;

    gl_Position = vec4(x, y, 0.0, 1.0);
    vMagnitude = normalised;
}
