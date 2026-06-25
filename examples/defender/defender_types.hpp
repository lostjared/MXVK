#ifndef DEFENDER_TYPES_HPP
#define DEFENDER_TYPES_HPP

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

namespace defender {

    constexpr int STAR_COUNT = 90000;
    constexpr int MAX_PROJECTILES = 64;
    constexpr int MAX_UFOS = 6;
    constexpr int MAX_ASTEROIDS = 5;
    constexpr int UFO_ANIMATION_FRAMES = 8;
    constexpr float SHIP_MODEL_SCALE = 1.65f;
    constexpr float PROJECTILE_SPEED = 118.0f;
    constexpr float PROJECTILE_LIFETIME = 0.72f;
    constexpr int FIRE_COOLDOWN = 4;
    constexpr glm::vec4 PROJECTILE_COLOR{1.0f, 0.96f, 0.60f, 1.0f};
    constexpr float WORLD_TOP = 12.0f;
    constexpr float WORLD_BOTTOM = -8.5f;
    constexpr float CAMERA_DISTANCE = 26.0f;
    constexpr float CAMERA_HEIGHT = 1.6f;
    constexpr float CAMERA_SCROLL_EDGE_FRACTION = 0.68f;
    constexpr Sint16 CONTROLLER_DEAD_ZONE = 8000;
    constexpr float CONTROLLER_AXIS_MAX = 32767.0f;

} // namespace defender

#endif
