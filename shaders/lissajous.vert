#version 330 core

layout(location = 0) in vec2 sample_lr;  // x = averaged mono amplitude, y = unused

uniform float uTime;
uniform float uBassEnergy;
uniform float uBeatKick;
uniform int   uSampleCount;

out float vMagnitude;
out float vFrequency;

void main()
{
    float t     = float(gl_VertexID) / float(uSampleCount);
    float angle = t * 2.0 * 3.14159265 + uTime * 0.025;

    // base_r breathes with bass; beat kick flares the ring outward.
    float base_r = 0.45 + uBassEnergy * 0.12 + uBeatKick * 0.05;

    // Averaged audio is much quieter than raw PCM, so scale up aggressively.
    float r = base_r + sample_lr.x * (6.0 + uBeatKick * 2.0);

    gl_Position = vec4(r * cos(angle), r * sin(angle), 0.0, 1.0);

    vFrequency = t;
    // Boost magnitude so even quiet passages produce visible brightness.
    vMagnitude = clamp(abs(sample_lr.x) * 12.0, 0.05, 1.0);
}
