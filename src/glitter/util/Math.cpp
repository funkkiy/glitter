#include "Math.h"

namespace Glitter::Math {

glm::vec3 Glitter::Math::SafeUpVector(glm::vec3 direction)
{
    if (std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.999f) {
        return glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

} // namespace Glitter::Math
