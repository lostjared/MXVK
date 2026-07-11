#ifndef ASTEROIDS3D_SHIP_HPP
#define ASTEROIDS3D_SHIP_HPP

#include "asteroids3d_types.hpp"

namespace space {

    class Ship {
      public:
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 prev_position{0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 rotation{0.0f};
        float current_speed = 1.0f;
        float min_speed = 0.1f;
        float max_speed = 25.0f;
        float turn_speed = 140.0f;
        float pitch_speed_multiplier = 0.55f;
        float camera_distance = 6.0f;
        float camera_height = 1.6f;
        bool visible = true;
        bool exploding = false;
        int explosion_timer = 0;
        int lives = 5;
        int score = 0;
        int fire_cooldown = 0;
        int burst_count = 0;
        int continuous_fire_timer = 0;
        bool overheated = false;
        int overheat_cooldown = 0;

        glm::vec3 forward() const;
    };

} // namespace space

#endif
