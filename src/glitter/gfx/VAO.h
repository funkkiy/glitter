#pragma once

#include <glad/glad.h>

#include <string_view>
#include <initializer_list>

namespace Glitter::Gfx {

struct VAOAttribute {
    GLint m_size;
    GLenum m_type;
    GLuint m_offset;
    GLuint m_binding {0};
    GLboolean m_normalized {false};
};

[[nodiscard]] GLuint CreateVAO(std::string_view name, std::initializer_list<VAOAttribute> attributes);

} // namespace Glitter::Gfx
