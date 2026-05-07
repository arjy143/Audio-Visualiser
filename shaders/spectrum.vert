#version 330 core

layout(location = 0) in float magnitude;

uniform int   uBinCount;
uniform float uMinDB;
uniform float uMaxDB;
uniform float uMinFreq;
uniform float uMaxFreq;
uniform float uRotation;
uniform float uFlip;
uniform float uFanMode;
uniform float uScale;
uniform float uTime;
uniform float uBassEnergy;

out float vMagnitude;
out float vFrequency;

void main()
{
    // Vertex 0 in fan mode is the hub at the origin
    if (uFanMode > 0.5 && gl_VertexID == 0)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        vMagnitude = 0.0;
        vFrequency = 0.0;
        return;
    }

    float freq = float(gl_VertexID) * uMaxFreq / float(uBinCount - 1);
    float t = log(max(freq, uMinFreq) / uMinFreq) / log(uMaxFreq / uMinFreq);
    float angle = t * 2.0 * 3.14159265;

    float normalised = clamp((magnitude - uMinDB) / (uMaxDB - uMinDB), 0.0, 1.0);
    float inner = 0.05 + uBassEnergy * 0.15;
    float r = (inner + normalised * 0.82) * uScale;

    float a = (uFlip > 0.5 ? -angle : angle) + uRotation + uTime * 0.08;
    gl_Position = vec4(r * cos(a), r * sin(a), 0.0, 1.0);
    vMagnitude = normalised;
    vFrequency = t;
}
