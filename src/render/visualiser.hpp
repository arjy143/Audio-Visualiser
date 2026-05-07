#pragma once

namespace render
{

//simple class that wraps opengl
class Visualiser
{
public:
    Visualiser() = default;

    //called each frame after vbo updated
    void draw(int bin_count) const noexcept;
};

}