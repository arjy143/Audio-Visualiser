#version 330 core

in  vec2 vTexCoord;
out vec4 fragColour;

uniform sampler2D uScene;
uniform sampler2D uBloom;

void main()
{
    // Chromatic aberration: offset R and B channels in opposite directions
    // away from screen centre.  Scaling the offset by 'dir' (not a fixed
    // pixel amount) means the fringing is zero at the centre and grows
    // toward the edges — exactly what a real lens does.
    vec2  centre = vec2(0.5);
    vec2  dir    = vTexCoord - centre;
    const float aberr = 0.005;

    float sceneR = texture(uScene, vTexCoord + dir * aberr).r;
    float sceneG = texture(uScene, vTexCoord).g;
    float sceneB = texture(uScene, vTexCoord - dir * aberr).b;

    float bloomR = texture(uBloom, vTexCoord + dir * aberr).r;
    float bloomG = texture(uBloom, vTexCoord).g;
    float bloomB = texture(uBloom, vTexCoord - dir * aberr).b;

    vec3 colour = vec3(sceneR, sceneG, sceneB)
                + vec3(bloomR, bloomG, bloomB) * 0.8;

    // Vignette: smoothstep from bright at centre to dark at edges.
    // smoothstep(0.85, 0.25, d) reaches zero slightly beyond the screen
    // corner (d ≈ 0.707), so the corners go fully black.
    float vignette = smoothstep(0.85, 0.25, length(dir));
    colour *= vignette;

    fragColour = vec4(colour, 1.0);
}
