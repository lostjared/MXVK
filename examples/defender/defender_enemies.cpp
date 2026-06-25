#include "defender_window.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/quaternion.hpp>

namespace defender {

    void DefenderWindow::init_ufos() {
        for (auto &ufo : ufos) {
            ufo.active = false;
            ufo.respawn_timer = space::random_float(0.2f, 2.0f);
        }
    }

    void DefenderWindow::init_asteroids() {
        for (auto &asteroid : asteroids) {
            asteroid.active = false;
            asteroid.respawn_timer = space::random_float(0.6f, 4.5f);
        }
    }

    void DefenderWindow::respawn_ufo(Ufo &ufo, bool initial_spawn) {
        const float side = space::random_float(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
        ufo.velocity = glm::vec3(-side * space::random_float(3.5f, 8.5f), space::random_float(-0.8f, 0.8f), 0.0f);
        ufo.tint = glm::vec4(
            space::random_float(0.82f, 1.0f),
            space::random_float(0.88f, 1.0f),
            space::random_float(0.90f, 1.0f),
            1.0f);
        ufo.base_size = space::random_float(3.6f, 4.4f);
        ufo.phase = space::random_float(0.0f, 2.0f * space::PI);
        ufo.bob_speed = space::random_float(1.1f, 2.8f);
        ufo.spin_speed = space::random_float(2.8f, 6.2f) * (space::random_float(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f);
        ufo.sprite_set = space::random_float(0.0f, 1.0f) < 0.5f ? UfoSpriteSet::Classic : UfoSpriteSet::Ufox;
        const float collision_radius = ufo_collision_radius(ufo);
        for (int attempt = 0; attempt < ENEMY_SPAWN_ATTEMPTS; ++attempt) {
            const float distance = initial_spawn ? space::random_float(-52.0f, 52.0f) : side * space::random_float(34.0f, 56.0f);
            const glm::vec3 candidate{camera_center_x + distance, space::random_float(WORLD_BOTTOM + collision_radius, WORLD_TOP - collision_radius), space::random_float(-1.2f, 1.2f)};
            if (enemy_spawn_is_clear(candidate, collision_radius, &ufo, nullptr)) {
                ufo.position = candidate;
                break;
            }
            if (attempt == ENEMY_SPAWN_ATTEMPTS - 1) {
                ufo.position = candidate;
            }
        }
        ufo.respawn_timer = 0.0f;
        ufo.active = true;
    }

    void DefenderWindow::update_ufos(float dt) {
        if (!ufos_enabled) {
            if (std::abs(camera_center_x) < 18.0f && std::abs(ship.position.x) < 18.0f) {
                return;
            }
            ufos_enabled = true;
        }

        for (auto &ufo : ufos) {
            if (!ufo.active) {
                ufo.respawn_timer -= dt;
                if (ufo.respawn_timer <= 0.0f) {
                    respawn_ufo(ufo);
                }
                continue;
            }

            ufo.phase += dt * ufo.bob_speed;
            ufo.position += ufo.velocity * dt;
            ufo.position.y += std::sin(ufo.phase) * dt * 1.35f;
            if (ufo.position.y < WORLD_BOTTOM + 0.8f || ufo.position.y > WORLD_TOP - 0.8f) {
                ufo.velocity.y = -ufo.velocity.y;
                ufo.position.y = std::clamp(ufo.position.y, WORLD_BOTTOM + 0.8f, WORLD_TOP - 0.8f);
            }

            const float max_distance = 68.0f;
            if (std::abs(ufo.position.x - camera_center_x) > max_distance) {
                respawn_ufo(ufo);
            }
        }
    }

    void DefenderWindow::respawn_asteroid(Asteroid &asteroid, bool initial_spawn) {
        const float side = space::random_float(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
        asteroid.scale = space::random_float(0.72f, 2.15f);
        asteroid.collision_radius = asteroid.scale * 1.25f;
        asteroid.velocity = glm::vec3(-side * space::random_float(1.8f, 5.6f), space::random_float(-0.55f, 0.55f), 0.0f);
        asteroid.rotation = glm::vec3(
            space::random_float(0.0f, 360.0f),
            space::random_float(0.0f, 360.0f),
            space::random_float(0.0f, 360.0f));
        asteroid.angular_velocity = glm::vec3(
            space::random_float(-74.0f, 74.0f),
            space::random_float(-105.0f, 105.0f),
            space::random_float(-62.0f, 62.0f));
        for (int attempt = 0; attempt < ENEMY_SPAWN_ATTEMPTS; ++attempt) {
            const float distance = initial_spawn ? space::random_float(-58.0f, 58.0f) : side * space::random_float(36.0f, 68.0f);
            const glm::vec3 candidate{camera_center_x + distance, space::random_float(WORLD_BOTTOM + asteroid.collision_radius, WORLD_TOP - asteroid.collision_radius), space::random_float(-1.7f, 1.3f)};
            if (enemy_spawn_is_clear(candidate, asteroid.collision_radius, nullptr, &asteroid)) {
                asteroid.position = candidate;
                break;
            }
            if (attempt == ENEMY_SPAWN_ATTEMPTS - 1) {
                asteroid.position = candidate;
            }
        }
        asteroid.respawn_timer = 0.0f;
        asteroid.active = true;
    }

    void DefenderWindow::update_asteroids(float dt) {
        if (!asteroids_enabled) {
            if (std::abs(camera_center_x) < 12.0f && std::abs(ship.position.x) < 12.0f) {
                return;
            }
            asteroids_enabled = true;
        }

        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                asteroid.respawn_timer -= dt;
                if (asteroid.respawn_timer <= 0.0f) {
                    respawn_asteroid(asteroid);
                }
                continue;
            }

            asteroid.position += asteroid.velocity * dt;
            asteroid.rotation += asteroid.angular_velocity * dt;
            asteroid.rotation.x = std::fmod(asteroid.rotation.x, 360.0f);
            asteroid.rotation.y = std::fmod(asteroid.rotation.y, 360.0f);
            asteroid.rotation.z = std::fmod(asteroid.rotation.z, 360.0f);

            if (asteroid.position.y < WORLD_BOTTOM + asteroid.collision_radius || asteroid.position.y > WORLD_TOP - asteroid.collision_radius) {
                asteroid.velocity.y = -asteroid.velocity.y;
                asteroid.position.y = std::clamp(asteroid.position.y, WORLD_BOTTOM + asteroid.collision_radius, WORLD_TOP - asteroid.collision_radius);
            }

            if (std::abs(asteroid.position.x - camera_center_x) > 78.0f) {
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(0.4f, 2.6f);
            }
        }
    }

    [[nodiscard]] float DefenderWindow::ufo_collision_radius(const Ufo &ufo) const {
        return ufo.base_size * 0.58f + ENEMY_SEPARATION_PADDING;
    }

    [[nodiscard]] bool DefenderWindow::enemy_spawn_is_clear(const glm::vec3 &position, float radius, const Ufo *ignored_ufo, const Asteroid *ignored_asteroid) const {
        for (const Ufo &ufo : ufos) {
            if (!ufo.active || &ufo == ignored_ufo) {
                continue;
            }
            const glm::vec2 delta{ufo.position.x - position.x, ufo.position.y - position.y};
            const float min_distance = ufo_collision_radius(ufo) + radius;
            if (glm::dot(delta, delta) < min_distance * min_distance) {
                return false;
            }
        }

        for (const Asteroid &asteroid : asteroids) {
            if (!asteroid.active || &asteroid == ignored_asteroid) {
                continue;
            }
            const glm::vec2 delta{asteroid.position.x - position.x, asteroid.position.y - position.y};
            const float min_distance = asteroid.collision_radius + radius;
            if (glm::dot(delta, delta) < min_distance * min_distance) {
                return false;
            }
        }

        return true;
    }

    void DefenderWindow::clamp_enemy_to_world_y(glm::vec3 &position, glm::vec3 &velocity, float radius) {
        if (position.y < WORLD_BOTTOM + radius || position.y > WORLD_TOP - radius) {
            velocity.y = -velocity.y;
            position.y = std::clamp(position.y, WORLD_BOTTOM + radius, WORLD_TOP - radius);
        }
    }

    void DefenderWindow::separate_enemies(glm::vec3 &first_position,
                                          glm::vec3 &first_velocity,
                                          float first_radius,
                                          glm::vec3 &second_position,
                                          glm::vec3 &second_velocity,
                                          float second_radius,
                                          float restitution) {
        const glm::vec2 delta{second_position.x - first_position.x, second_position.y - first_position.y};
        const float min_distance = first_radius + second_radius;
        const float distance_squared = glm::dot(delta, delta);
        if (distance_squared >= min_distance * min_distance) {
            return;
        }

        const float distance = std::sqrt(std::max(distance_squared, 0.0001f));
        const glm::vec2 normal = distance > 0.0001f ? delta / distance : glm::vec2{1.0f, 0.0f};
        const float first_mass = first_radius * first_radius;
        const float second_mass = second_radius * second_radius;
        const float first_inverse_mass = 1.0f / first_mass;
        const float second_inverse_mass = 1.0f / second_mass;
        const float inverse_mass_sum = first_inverse_mass + second_inverse_mass;
        const float overlap = min_distance - distance;

        const glm::vec2 separation = normal * (overlap / inverse_mass_sum);
        first_position.x -= separation.x * first_inverse_mass;
        first_position.y -= separation.y * first_inverse_mass;
        second_position.x += separation.x * second_inverse_mass;
        second_position.y += separation.y * second_inverse_mass;

        const glm::vec2 relative_velocity{second_velocity.x - first_velocity.x, second_velocity.y - first_velocity.y};
        const float velocity_along_normal = glm::dot(relative_velocity, normal);
        if (velocity_along_normal >= 0.0f) {
            return;
        }

        const float impulse_magnitude = -(1.0f + restitution) * velocity_along_normal / inverse_mass_sum;
        const glm::vec2 impulse = impulse_magnitude * normal;
        first_velocity.x -= impulse.x * first_inverse_mass;
        first_velocity.y -= impulse.y * first_inverse_mass;
        second_velocity.x += impulse.x * second_inverse_mass;
        second_velocity.y += impulse.y * second_inverse_mass;
    }

    void DefenderWindow::resolve_enemy_overlaps() {
        constexpr float asteroid_restitution = 0.92f;
        constexpr float mixed_restitution = 0.72f;
        constexpr int separation_iterations = 4;

        for (int iteration = 0; iteration < separation_iterations; ++iteration) {
            for (std::size_t i = 0; i < ufos.size(); ++i) {
                Ufo &first = ufos[i];
                if (!first.active) {
                    continue;
                }

                for (std::size_t j = i + 1; j < ufos.size(); ++j) {
                    Ufo &second = ufos[j];
                    if (!second.active) {
                        continue;
                    }
                    separate_enemies(first.position, first.velocity, ufo_collision_radius(first), second.position, second.velocity, ufo_collision_radius(second), mixed_restitution);
                }
            }

            for (std::size_t i = 0; i < asteroids.size(); ++i) {
                Asteroid &first = asteroids[i];
                if (!first.active) {
                    continue;
                }

                for (std::size_t j = i + 1; j < asteroids.size(); ++j) {
                    Asteroid &second = asteroids[j];
                    if (!second.active) {
                        continue;
                    }

                    separate_enemies(first.position, first.velocity, first.collision_radius, second.position, second.velocity, second.collision_radius, asteroid_restitution);
                }
            }

            for (Ufo &ufo : ufos) {
                if (!ufo.active) {
                    continue;
                }
                for (Asteroid &asteroid : asteroids) {
                    if (!asteroid.active) {
                        continue;
                    }
                    separate_enemies(ufo.position, ufo.velocity, ufo_collision_radius(ufo), asteroid.position, asteroid.velocity, asteroid.collision_radius, mixed_restitution);
                }
            }

            for (Ufo &ufo : ufos) {
                if (ufo.active) {
                    clamp_enemy_to_world_y(ufo.position, ufo.velocity, ufo_collision_radius(ufo));
                }
            }
            for (Asteroid &asteroid : asteroids) {
                if (asteroid.active) {
                    clamp_enemy_to_world_y(asteroid.position, asteroid.velocity, asteroid.collision_radius);
                }
            }
        }
    }

    [[nodiscard]] int DefenderWindow::current_ufo_frame(const Ufo &ufo) const {
        return static_cast<int>(std::floor(elapsed_seconds * 12.0f + ufo.phase * 1.7f)) % UFO_ANIMATION_FRAMES;
    }

    [[nodiscard]] float DefenderWindow::current_ufo_pulse(const Ufo &ufo) const {
        return 1.0f + std::sin(elapsed_seconds * 2.2f + ufo.phase) * 0.04f;
    }

    void DefenderWindow::draw_ufos() {
        for (const auto &ufo : ufos) {
            if (!ufo.active) {
                continue;
            }
            const float pulse = current_ufo_pulse(ufo);
            const int frame = current_ufo_frame(ufo);
            ufo_sprite_sets[static_cast<std::size_t>(ufo.sprite_set)][static_cast<std::size_t>(frame)]->drawSprite(ufo.position, glm::vec2(ufo.base_size * pulse, ufo.base_size * 0.58f * pulse), ufo.tint, 0.0f);
        }
    }

    void DefenderWindow::draw_asteroids(VkCommandBuffer cmd, uint32_t image_index, const glm::mat4 &view, const glm::mat4 &projection) {
        for (std::size_t i = 0; i < asteroids.size(); ++i) {
            const Asteroid &asteroid = asteroids[i];
            if (!asteroid.active) {
                continue;
            }

            mxvk::UniformBufferObject ubo{};
            ubo.model = build_asteroid_model_matrix(asteroid, asteroid_models[i]);
            ubo.view = view;
            ubo.proj = projection;
            asteroid_models[i].updateUBO(image_index, ubo);
            asteroid_models[i].render(cmd, image_index, false);
        }
    }

} // namespace defender
