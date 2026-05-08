#version 330 core

in  vec2 vTexCoord;
out vec4 fragColour;

uniform sampler2D uScene;
uniform sampler2D uBloom;

void main()
{
    vec4 scene = texture(uScene, vTexCoord);
    vec4 bloom = texture(uBloom, vTexCoord);
    // Additive blend: bright areas bleed into surrounding dark pixels
    fragColour = scene + bloom * 0.8;
}
