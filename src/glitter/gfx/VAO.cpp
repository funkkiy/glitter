#include "VAO.h"

namespace Glitter::Gfx {

GLuint CreateVAO(const char *name, std::initializer_list<VAOAttribute> attributes)
{
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    glObjectLabel(GL_VERTEX_ARRAY, vao, -1, name);

    for (GLuint i = 0; i < attributes.size(); i++) {
        const VAOAttribute& attrib = *(attributes.begin() + i);
        glEnableVertexArrayAttrib(vao, i);
        glVertexArrayAttribFormat(vao, i, attrib.m_size, attrib.m_type, attrib.m_normalized, attrib.m_offset);
        glVertexArrayAttribBinding(vao, i, attrib.m_binding);
    }

    return vao;
}

} // namespace Glitter::Gfx
