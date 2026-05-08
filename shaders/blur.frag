#version 330 core

in  vec2 vTexCoord;
out vec4 fragColour;

uniform sampler2D uImage;
uniform int       uHorizontal;

void main()
{
    // 9-tap separable Gaussian — weights sum to 1.0
    const float weight[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec2 dir   = uHorizontal > 0 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);

    vec4 result = texture(uImage, vTexCoord) * weight[0];
    for (int i = 1; i < 5; ++i)
    {
        result += texture(uImage, vTexCoord + dir * float(i)) * weight[i];
        result += texture(uImage, vTexCoord - dir * float(i)) * weight[i];
    }
    fragColour = result;
}
