#version 330 core

in  vec2 vTexCoord;
out vec4 fragColour;

uniform sampler2D uPrev;   // previous frame captured to the milk texture
uniform float     uBass;   // bass energy [0, 1]
uniform float     uBeat;   // beat_kick_ value [0, ~1.8]
uniform float     uTime;   // elapsed seconds

void main()
{
    vec2 p = vTexCoord - 0.5;           // re-centre to [-0.5, 0.5]

    // ── Zoom ─────────────────────────────────────────────────────────────────
    // Divide p by zoom > 1 to magnify the previous frame — the old image grows
    // outward every frame so new injected content always fills the centre.
    // Bass and beats briefly deepen the zoom for a "pulse inward" feel.
    float zoom = 1.018 + uBass * 0.014 + uBeat * 0.024;
    p /= zoom;

    // ── Rotation ─────────────────────────────────────────────────────────────
    // Constant slow spin (~4.6 °/s).  Because the rotation angle is derived
    // from absolute uTime the speed is frame-rate independent.
    float a  = uTime * 0.08;
    float cs = cos(a), sn = sin(a);
    p = vec2(cs * p.x - sn * p.y,
             sn * p.x + cs * p.y);

    // ── Sinusoidal warp field ─────────────────────────────────────────────────
    // Two perpendicular standing waves with distinct frequencies and drifting
    // phases create the characteristic milk-drop ripple.  Bass and beat drive
    // the amplitude so the warp goes wild during loud passages.
    float w = 0.006 + uBass * 0.018 + uBeat * 0.012;
    p.x += sin(p.y * 8.0 + uTime * 1.2) * w;
    p.y += cos(p.x * 6.0 + uTime * 0.9) * w;

    p += 0.5;                           // back to [0, 1] UV

    // ── Sample and decay ─────────────────────────────────────────────────────
    // Clamping avoids black-border artefacts at the edges.
    // The decay multiplier prevents the image saturating to pure white;
    // louder bass lets the history persist slightly longer.
    vec4 prev = texture(uPrev, clamp(p, 0.001, 0.999));
    fragColour = prev * (0.86 + uBass * 0.08);
}
