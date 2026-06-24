#include "starfield.hpp"

#include "mxvk/mxvk.hpp"

namespace space {

    void StarField::init(int star_count, float min_radius, float max_radius) {
        if (initialized) {
            return;
        }

        stars.resize(static_cast<size_t>(star_count));
        this->min_radius = min_radius;
        this->max_radius = max_radius;
        for (auto &star : stars) {
            respawn(star, glm::vec3(0.0f));
        }
        initialized = true;
    }

    void StarField::setSprite(mxvk::VK_Sprite3D *sprite_batch) {
        sprite = sprite_batch;
    }

    void StarField::resize(mxvk::VK_Window *window) {
        if (sprite != nullptr) {
            sprite->resize(window);
        }
    }

    void StarField::update(float delta_time, const glm::vec3 &camera_position, float elapsed_time) {
        for (auto &star : stars) {
            star.position += star.velocity * delta_time;
            const float distance = glm::length(star.position - camera_position);
            if (distance > max_radius * 1.2f || distance < min_radius * 0.35f) {
                respawn(star, camera_position);
            }

            const float twinkle = 0.65f + 0.35f * std::sin(elapsed_time * star.twinkle_speed + star.twinkle_phase);
            const float fade = std::clamp(1.0f - ((distance - min_radius) / (max_radius - min_radius)), 0.2f, 0.7f);
            const float brightness = star.brightness * twinkle * fade;
            star.color = glm::vec4(
                std::clamp(star.base_color.r * brightness, 0.0f, 1.0f),
                std::clamp(star.base_color.g * brightness, 0.0f, 1.0f),
                std::clamp(star.base_color.b * brightness, 0.0f, 1.0f),
                std::clamp(0.2f + brightness, 0.0f, 1.0f));
        }
    }

    void StarField::draw() {
        if (sprite == nullptr) {
            return;
        }
        for (const auto &star : stars) {
            sprite->drawSprite(star.position, glm::vec2(star.size), star.color);
        }
    }

    void StarField::respawn(Star &star, const glm::vec3 &center) {
        const float theta = random_float(0.0f, 2.0f * PI);
        const float phi = std::acos(random_float(-1.0f, 1.0f));
        const float radius = random_float(min_radius, max_radius);

        star.position.x = center.x + radius * std::sin(phi) * std::cos(theta);
        star.position.y = center.y + radius * std::sin(phi) * std::sin(theta);
        star.position.z = center.z + radius * std::cos(phi);
        star.velocity = glm::vec3(
            random_float(-0.06f, 0.06f),
            random_float(-0.06f, 0.06f),
            random_float(-0.06f, 0.06f));

        const float roll = random_float(0.0f, 1.0f);
        if (roll < 0.5f) {
            star.base_color = {0.90f, 0.90f, 1.00f, 1.0f};
            star.size = random_float(0.026f, 0.056f);
        } else if (roll < 0.65f) {
            star.base_color = {0.60f, 0.78f, 1.00f, 1.0f};
            star.size = random_float(0.034f, 0.073f);
        } else if (roll < 0.75f) {
            star.base_color = {1.00f, 0.95f, 0.65f, 1.0f};
            star.size = random_float(0.035f, 0.078f);
        } else if (roll < 0.85f) {
            star.base_color = {1.00f, 0.66f, 0.32f, 1.0f};
            star.size = random_float(0.042f, 0.090f);
        } else if (roll < 0.92f) {
            star.base_color = {1.00f, 0.40f, 0.40f, 1.0f};
            star.size = random_float(0.045f, 0.095f);
        } else {
            star.base_color = {1.00f, 1.00f, 1.00f, 1.0f};
            star.size = random_float(0.070f, 0.133f);
        }

        star.brightness = random_float(0.25f, 1.0f);
        star.color = star.base_color;
        star.twinkle_phase = random_float(0.0f, 2.0f * PI);
        star.twinkle_speed = random_float(0.4f, 3.2f);
        star.layer = roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2);
    }

} // namespace space
