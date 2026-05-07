#version 330 core

layout(location = 0) in float magnitude;

uniform int   uBinCount;
uniform float uMinDB;
uniform float uMaxDB;
uniform float uMinFreq;
uniform float uMaxFreq;

out float vMagnitude;

void main()
{
    float freq = float(gl_VertexID) * uMaxFreq / float(uBinCount - 1);
    float t = log(max(freq, uMinFreq) / uMinFreq) / log(uMaxFreq / uMinFreq);
    float x = t * 2.0 - 1.0;

    float normalised = clamp((magnitude - uMinDB) / (uMaxDB - uMinDB), 0.0, 1.0);
    float y = normalised * 2.0 - 1.0;

    gl_Position = vec4(x, y, 0.0, 1.0);
    vMagnitude = normalised;
}
