#include "Framebuffer.h"

namespace Glitter::Gfx {

void CreateFramebuffer(Framebuffer& fb, GLsizei width, GLsizei height)
{
    // Create new FBO.
    if (!fb.m_fbo) {
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);
        fb.m_fbo = fbo;
    }

    // Create the color texture used with the FBO.
    GLuint fboColor = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &fboColor);
    glTextureStorage2D(fboColor, 1, GL_RGBA8, width, height);
    glObjectLabel(GL_TEXTURE, fboColor, -1, std::format("{} Texture", fb.m_name).c_str());

    // Create the depth renderbuffer (note: can't be sampled) used with the FBO.
    GLuint fboDepth = 0;
    glCreateRenderbuffers(1, &fboDepth);
    glNamedRenderbufferStorage(fboDepth, GL_DEPTH_COMPONENT24, width, height);
    glObjectLabel(GL_RENDERBUFFER, fboDepth, -1, std::format("{} Depth Renderbuffer", fb.m_name).c_str());

    // Attach the textures to the FBO.
    glNamedFramebufferTexture(*fb.m_fbo, GL_COLOR_ATTACHMENT0, fboColor, 0);
    glNamedFramebufferRenderbuffer(*fb.m_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

    fb.m_color = fboColor;
    fb.m_depth = fboDepth;
}

void UpdateFramebuffer(Framebuffer& fb, GLsizei width, GLsizei height)
{
    GLuint oldColor = fb.m_color;
    GLuint oldDepth = fb.m_depth;

    CreateFramebuffer(fb, width, height);

    glDeleteTextures(1, &oldColor);
    glDeleteRenderbuffers(1, &oldDepth);
}

} // namespace Glitter::Gfx