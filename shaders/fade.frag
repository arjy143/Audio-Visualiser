#version 330 core
out vec4 fragColour;

uniform float uBassEnergy;
uniform float uBeatKick;
uniform float uTime;

void main()
{
    // Match the hue cycling used in the spectrum shader
    float h = fract(uTime * 0.05);
    vec3 hue = clamp(abs(fract(h + vec3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);

    // Ambient glow from bass, sharp flash on beat
    float intensity = uBassEnergy * 0.025 + uBeatKick * 0.06;
    vec3 bg = hue * intensity;

    // Alpha controls trail length: lower = longer trails
    fragColour = vec4(bg, 0.15);
}
