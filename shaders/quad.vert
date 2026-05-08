#version 330 core

out vec2 vTexCoord;

void main()
{
    // Fullscreen quad with no VBO — positions come from gl_VertexID
    const vec2 positions[6] = vec2[6](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
        vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );
    const vec2 texcoords[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );

    vTexCoord   = texcoords[gl_VertexID];
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
