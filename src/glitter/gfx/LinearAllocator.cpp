#include "LinearAllocator.h"

namespace Glitter::Gfx {

void LinearAllocator::InitializeAlignment() {
    if (!m_alignment) {
        GLint alignment {0};
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        m_alignment = alignment;
    }
}

} // namespace Glitter::Gfx
