#version 330 core

layout(location = 0) in vec4 aParticle;  // xy = position, z = life, w = hue

out float vLife;
out float vHue;

void main()
{
    gl_Position  = vec4(aParticle.xy, 0.0, 1.0);
    gl_PointSize = aParticle.z * 5.0;  // shrinks as particle fades
    vLife        = aParticle.z;
    vHue         = aParticle.w;
}
