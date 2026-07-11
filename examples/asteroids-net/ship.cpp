#include "ship.hpp"

namespace space {

    glm::vec3 Ship::forward() const {
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        return normalize_or_zero(glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    }

} // namespace space
