#ifndef ASTEROIDS3D_SHIP_HPP
#define ASTEROIDS3D_SHIP_HPP

/**
 * @file ship.hpp
 * @brief Player ship state for the Asteroids simulation.
 */

#include "asteroids3d_types.hpp"

namespace space {

    /**
     * @class Ship
     * @brief Mutable gameplay and movement state for a player ship.
     */
    class Ship {
      public:
        /** @brief Current world-space position. */
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        /** @brief Position from the previous simulation step. */
        glm::vec3 prev_position{0.0f, 0.0f, 0.0f};
        /** @brief Current world-space velocity. */
        glm::vec3 velocity{0.0f};
        /** @brief Euler rotation in degrees. */
        glm::vec3 rotation{0.0f};
        /** @brief Current forward speed. */
        float current_speed = 1.0f;
        /** @brief Minimum controllable speed. */
        float min_speed = 0.1f;
        /** @brief Maximum controllable speed. */
        float max_speed = 25.0f;
        /** @brief Yaw rate in degrees per second. */
        float turn_speed = 140.0f;
        /** @brief Pitch rate relative to the yaw rate. */
        float pitch_speed_multiplier = 0.55f;
        /** @brief Third-person camera distance. */
        float camera_distance = 6.0f;
        /** @brief Third-person camera height. */
        float camera_height = 1.6f;
        /** @brief Whether the ship is rendered. */
        bool visible = true;
        /** @brief Whether the ship is currently exploding. */
        bool exploding = false;
        /** @brief Remaining explosion animation frames. */
        int explosion_timer = 0;
        /** @brief Remaining lives. */
        int lives = 5;
        /** @brief Current score. */
        int score = 0;
        /** @brief Frames remaining before the next shot is allowed. */
        int fire_cooldown = 0;
        /** @brief Shots already fired in the current burst. */
        int burst_count = 0;
        /** @brief Timer controlling continuous fire. */
        int continuous_fire_timer = 0;
        /** @brief Whether firing is disabled by overheating. */
        bool overheated = false;
        /** @brief Frames remaining in the overheat cooldown. */
        int overheat_cooldown = 0;

        /** @brief Returns the normalized world-space direction in which the ship points. */
        glm::vec3 forward() const;
    };

} // namespace space

#endif
