#version 330 core

in  float vMagnitude;
in  float vFrequency;
out vec4  fragColour;

uniform float uTime;
uniform float uAlpha;

void main()
{
    // Frequency sets hue, time slowly cycles the whole palette
    float h = fract(vFrequency * 0.75 + uTime * 0.05);

    // Convert hue to RGB by walking the 6-segment colour wheel
    vec3 colour = clamp(abs(fract(h + vec3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);

    // Magnitude controls brightness — quiet bins are dark, loud ones glow
    float brightness = 0.1 + vMagnitude * 0.9;
    colour *= brightness;

    fragColour = vec4(colour, uAlpha);
}
