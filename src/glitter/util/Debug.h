#pragma once

#include "util/Common.h"

#include <glad/glad.h>

namespace Glitter::Util::Debug {

#define GL_DEBUG_SCOPE(name) Glitter::Util::Debug::GlScope GLITTER_CONCAT(__debug_group_, __LINE__)(name)

class GlScope {
public:
    GLITTER_FORCE_INLINE GlScope(const char* name) { glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name); }
    GLITTER_FORCE_INLINE ~GlScope() { glPopDebugGroup(); }
};

} // namespace Glitter::Util
