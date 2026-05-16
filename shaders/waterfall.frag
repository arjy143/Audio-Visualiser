#version 330 core

uniform sampler2D uTex;
uniform float     uOffset;   // = (wf_cursor+1) / k_wf_w — shifts oldest data to left edge

in  vec2 vTexCoord;
out vec4 fragColour;

// Inferno-style colormap: black → dark purple → red → orange → yellow → white
vec3 inferno(float t)
{
    t = clamp(t, 0.0, 1.0);
    const vec3 c0 = vec3(0.000, 0.000, 0.020);
    const vec3 c1 = vec3(0.280, 0.000, 0.480);
    const vec3 c2 = vec3(0.720, 0.080, 0.130);
    const vec3 c3 = vec3(0.980, 0.510, 0.030);
    const vec3 c4 = vec3(0.998, 0.940, 0.000);
    const vec3 c5 = vec3(1.000, 1.000, 1.000);
    if (t < 0.2) return mix(c0, c1, t * 5.0);
    if (t < 0.4) return mix(c1, c2, (t - 0.2) * 5.0);
    if (t < 0.6) return mix(c2, c3, (t - 0.4) * 5.0);
    if (t < 0.8) return mix(c3, c4, (t - 0.6) * 5.0);
    return             mix(c4, c5, (t - 0.8) * 5.0);
}

void main()
{
    // Remap x so that column (cursor+1) appears at x=0 and column (cursor) at x≈1.
    // GL_REPEAT on the texture makes this wrap correctly with no branch.
    float x   = fract(vTexCoord.x + uOffset);
    float mag = texture(uTex, vec2(x, vTexCoord.y)).r;
    fragColour = vec4(inferno(mag), 1.0);
}
