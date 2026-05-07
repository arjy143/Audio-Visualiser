#version 330 core

in  float vMagnitude;
out vec4  fragColour;

void main()
{
    vec3 colour;
    if (vMagnitude < 0.5)
        colour = mix(vec3(0.0, 0.2, 1.0), vec3(0.0, 1.0, 0.5), vMagnitude * 2.0);
    else
        colour = mix(vec3(0.0, 1.0, 0.5), vec3(1.0, 0.2, 0.0), (vMagnitude - 0.5) * 2.0);

    fragColour = vec4(colour, 1.0);
}
