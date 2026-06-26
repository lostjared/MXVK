#ifndef DEFENDER_TYPES_HPP
#define DEFENDER_TYPES_HPP

#include <SDL3/SDL.h>

#include <cmath>

#include <glm/glm.hpp>

namespace defender {

    constexpr int STAR_COUNT = 90000;
    constexpr int MAX_PROJECTILES = 64;
    constexpr int MAX_UFOS = 6;
    constexpr int MAX_ASTEROIDS = 5;
    constexpr int UFO_ANIMATION_FRAMES = 8;
    constexpr int UFO_SPRITE_SET_COUNT = 3;
    constexpr float SHIP_MODEL_SCALE = 1.65f;
    constexpr float PROJECTILE_SPEED = 118.0f;
    constexpr float PROJECTILE_LIFETIME = 0.72f;
    constexpr int FIRE_COOLDOWN = 4;
    constexpr glm::vec4 PROJECTILE_COLOR{1.0f, 0.96f, 0.60f, 1.0f};
    constexpr float WORLD_TOP = 8.5f;
    constexpr float WORLD_BOTTOM = -8.5f;
    constexpr float WORLD_WIDTH = 160.0f;
    constexpr float WORLD_HALF_WIDTH = WORLD_WIDTH * 0.5f;
    constexpr int RADAR_TOP = 42;
    constexpr int RADAR_HEIGHT = 104;
    constexpr int GAME_VIEWPORT_TOP = RADAR_TOP + RADAR_HEIGHT + 8;
    constexpr float CAMERA_DISTANCE = 26.0f;
    constexpr float CAMERA_HEIGHT = 1.6f;
    constexpr float CAMERA_SCROLL_EDGE_FRACTION = 0.68f;
    constexpr Sint16 CONTROLLER_DEAD_ZONE = 8000;
    constexpr float CONTROLLER_AXIS_MAX = 32767.0f;

    [[nodiscard]] inline float wrap_world_x(float x) {
        x = std::fmod(x + WORLD_HALF_WIDTH, WORLD_WIDTH);
        if (x < 0.0f) {
            x += WORLD_WIDTH;
        }
        return x - WORLD_HALF_WIDTH;
    }

    [[nodiscard]] inline float wrapped_delta_x(float target, float origin) {
        return wrap_world_x(target - origin);
    }

    [[nodiscard]] inline float nearest_world_x(float target, float origin) {
        return origin + wrapped_delta_x(target, origin);
    }

    [[nodiscard]] inline glm::vec3 nearest_world_position(glm::vec3 position, float origin_x) {
        position.x = nearest_world_x(position.x, origin_x);
        return position;
    }

} // namespace defender

#endif
