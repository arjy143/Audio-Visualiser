#version 330 core

in  float vMagnitude;
out vec4  fragColour;

uniform float uTime;
uniform float uAlpha;

void main()
{
    // Shift the colour lookup by time so hues cycle continuously
    float h = fract(vMagnitude + uTime * 0.08);

    vec3 colour;
    if (h < 0.33)
        colour = mix(vec3(0.0, 0.2, 1.0), vec3(0.0, 1.0, 0.3), h * 3.0);
    else if (h < 0.67)
        colour = mix(vec3(0.0, 1.0, 0.3), vec3(1.0, 0.1, 0.0), (h - 0.33) * 3.0);
    else
        colour = mix(vec3(1.0, 0.1, 0.0), vec3(0.0, 0.2, 1.0), (h - 0.67) * 3.0);

    fragColour = vec4(colour, uAlpha);
}
