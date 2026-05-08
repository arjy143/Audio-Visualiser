#version 330 core

layout(location = 0) in float sample_val;  // raw PCM, range -1..+1

uniform int   uSampleCount;
uniform float uTime;
uniform float uBassEnergy;
uniform float uBeatKick;

out float vMagnitude;
out float vFrequency;

void main()
{
    // Map sample index linearly around the full circle
    float t     = float(gl_VertexID) / float(uSampleCount);
    float angle = t * 2.0 * 3.14159265 + uTime * 0.04;

    // Base radius grows slightly with bass; amplitude scale flares on beat
    float base_r = 0.45 + uBassEnergy * 0.10;
    float r      = base_r + sample_val * (0.35 + uBeatKick * 0.10);

    gl_Position = vec4(r * cos(angle), r * sin(angle), 0.0, 1.0);

    // vFrequency drives hue (cycles around the circle as colour).
    // vMagnitude drives brightness.
    vFrequency = t;
    vMagnitude = clamp(abs(sample_val) * 1.5, 0.0, 1.0);
}
