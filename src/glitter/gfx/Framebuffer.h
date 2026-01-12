#pragma once

#include <glad/glad.h>

namespace Glitter::Gfx {

enum class FramebufferType : std::uint8_t {
    Main,
    Ppfx,
    Debug,
    Count,
};

constexpr std::size_t operator+(FramebufferType type) { return static_cast<std::size_t>(type); }

struct Framebuffer {
    const char* m_name;
    std::optional<GLuint> m_fbo {};
    GLuint m_color {0};
    GLuint m_depth {0};

    Framebuffer()
        : m_name("Unnamed FBO")
    {
    }

    Framebuffer(const char* name)
        : m_name(name)
    {
    }
};

void CreateFramebuffer(Framebuffer& fb, GLsizei width, GLsizei height);
void UpdateFramebuffer(Framebuffer& fb, GLsizei width, GLsizei height);

} // namespace Glitter::Gfx
