#version 330 core

layout(location = 0) in float magnitude;

uniform int   uBinCount;
uniform float uMinDB;
uniform float uMaxDB;
uniform float uMinFreq;
uniform float uMaxFreq;
uniform float uRotation;
uniform float uFlip;
uniform float uFanMode;   // 0/1 = Aura/Tunnel (lines), 2 = Bars, 3 = Burst (points)
uniform float uScale;
uniform float uTime;
uniform float uBassEnergy;
uniform float uBeatKick;

out float vMagnitude;
out float vFrequency;

void main()
{
    // Bars mode: the VBO stores each magnitude twice so vertices come in
    // (inner, outer) pairs.  Use integer division to get the real bin index,
    // and the LSB to decide which endpoint this vertex represents.
    bool bars   = (uFanMode > 1.5 && uFanMode < 2.5);
    int  bin_id = bars ? gl_VertexID / 2 : gl_VertexID;

    float freq       = float(bin_id) * uMaxFreq / float(uBinCount - 1);
    float t          = log(max(freq, uMinFreq) / uMinFreq) / log(uMaxFreq / uMinFreq);
    float angle      = t * 2.0 * 3.14159265;

    float normalised = clamp((magnitude - uMinDB) / (uMaxDB - uMinDB), 0.0, 1.0);
    float inner      = 0.05 + uBassEnergy * 0.15;
    float r;

    if (bars && (gl_VertexID % 2 == 0))
        r = inner * uScale;                                         // base of bar
    else
        r = (inner + normalised * 0.82) * uScale * (1.0 + uBeatKick * 0.2);  // tip

    float a = (uFlip > 0.5 ? -angle : angle) + uRotation + uTime * 0.08;
    gl_Position = vec4(r * cos(a), r * sin(a), 0.0, 1.0);
    vMagnitude  = normalised;
    vFrequency  = t;

    // Burst mode: point size grows with magnitude and flares on beat
    if (uFanMode > 2.5)
        gl_PointSize = 2.0 + normalised * 7.0 + uBeatKick * 2.0;
}
