#ifndef DEFENDER_ENTITIES_HPP
#define DEFENDER_ENTITIES_HPP

#include "defender_types.hpp"

#include <cstddef>

#include <glm/glm.hpp>

namespace defender {

    enum class GameMode {
        Intro,
        IntroFadeIn,
        Countdown,
        Playing
    };

    enum class UfoSpriteSet : std::size_t {
        Classic = 0,
        Ufox = 1
    };

    struct Ufo {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 tint{1.0f};
        float base_size = 1.3f;
        float phase = 0.0f;
        float bob_speed = 1.0f;
        float spin_speed = 1.0f;
        float respawn_timer = 0.0f;
        UfoSpriteSet sprite_set = UfoSpriteSet::Classic;
        bool active = true;
    };

    struct Asteroid {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 angular_velocity{0.0f};
        float scale = 1.0f;
        float collision_radius = 1.2f;
        float respawn_timer = 0.0f;
        bool active = false;
    };

    struct SpriteAlphaBounds {
        float min_x = -0.5f;
        float max_x = 0.5f;
        float min_y = -0.5f;
        float max_y = 0.5f;
    };

} // namespace defender

#endif
