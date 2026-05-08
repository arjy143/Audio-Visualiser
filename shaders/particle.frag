#version 330 core

in  float vLife;
in  float vHue;
out vec4  fragColour;

void main()
{
    vec3 colour = clamp(abs(fract(vHue + vec3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    float luma  = dot(colour, vec3(0.299, 0.587, 0.114));
    colour      = mix(vec3(luma), colour, 0.6);
    fragColour  = vec4(colour, vLife * vLife);  // alpha fades quadratically
}
