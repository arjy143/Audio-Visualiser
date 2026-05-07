#include "render/visualiser.hpp"
#include <GL/glew.h>

namespace render
{

void Visualiser::draw(int bin_count) const noexcept
{
    glDrawArrays(GL_LINE_STRIP, 0, bin_count);
}

}
