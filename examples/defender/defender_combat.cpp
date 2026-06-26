#include "defender_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

namespace defender {

    void DefenderWindow::fire_projectile() {
        const glm::vec3 forward{ship_forward_direction, 0.0f, 0.0f};
        glm::vec3 muzzle = ship.position + forward * 1.35f + glm::vec3(0.0f, -0.22f, 0.0f);
        muzzle.x = wrap_world_x(muzzle.x);

        for (auto &projectile : projectiles) {
            if (projectile.active) {
                continue;
            }
            projectile.position = muzzle;
            projectile.prev_position = muzzle;
            projectile.velocity = forward * PROJECTILE_SPEED + glm::vec3(ship.velocity.x * 0.25f, 0.0f, 0.0f);
            projectile.color = PROJECTILE_COLOR;
            projectile.lifetime = 0.0f;
            projectile.active = true;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(shoot_sound);
#endif
            log_game(std::format("Laser fired from {}.", format_vec3(muzzle)));
            return;
        }
    }

    void DefenderWindow::update_projectiles(float dt) {
        for (auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }
            projectile.prev_position = projectile.position;
            projectile.position += projectile.velocity * dt;
            projectile.position.x = wrap_world_x(projectile.position.x);
            projectile.lifetime += dt;
            if (projectile.lifetime >= PROJECTILE_LIFETIME) {
                projectile.active = false;
            }
        }
    }

    void DefenderWindow::check_projectile_ufo_hits() {
        constexpr float beam_collision_length = 7.4f;

        for (auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }

            const float direction = projectile.velocity.x >= 0.0f ? 1.0f : -1.0f;
            const float x0 = projectile.prev_position.x;
            const float x1 = x0 + wrapped_delta_x(projectile.position.x, x0) + direction * beam_collision_length;
            const float min_x = std::min(x0, x1);
            const float max_x = std::max(x0, x1);

            for (auto &ufo : ufos) {
                if (!ufo.active) {
                    continue;
                }
                const int frame = current_ufo_frame(ufo);
                const SpriteAlphaBounds &bounds = ufo_sprite_bounds[static_cast<std::size_t>(ufo.sprite_set)][static_cast<std::size_t>(frame)];
                const glm::vec2 draw_size = ufo_draw_size(ufo, current_ufo_pulse(ufo));
                const glm::vec2 body_center{
                    nearest_world_x(ufo.position.x, (x0 + x1) * 0.5f) + ((bounds.min_x + bounds.max_x) * 0.5f) * draw_size.x,
                    ufo.position.y + ((bounds.min_y + bounds.max_y) * 0.5f) * draw_size.y,
                };
                const glm::vec2 body_radius{
                    std::max(0.001f, (bounds.max_x - bounds.min_x) * 0.5f * draw_size.x),
                    std::max(0.001f, (bounds.max_y - bounds.min_y) * 0.5f * draw_size.y),
                };
                const bool overlaps_x = (body_center.x + body_radius.x) >= min_x && (body_center.x - body_radius.x) <= max_x;
                const bool overlaps_y = std::abs(body_center.y - projectile.position.y) <= body_radius.y;
                if (!overlaps_x || !overlaps_y) {
                    continue;
                }

                if (ufo.sprite_set == UfoSpriteSet::Alien) {
                    spawn_alien_explosion(ufo.position);
                } else {
                    spawn_ufo_explosion(ufo.position);
                }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
                play_sound(ufo_explosion_sound);
#endif
                ufo.active = false;
                ufo.respawn_timer = space::random_float(1.2f, 2.8f);
                projectile.active = false;
                score += 5;
                log_game(std::format("{} destroyed. Score={}.", ufo.sprite_set == UfoSpriteSet::Alien ? "Alien" : "UFO", score));
                break;
            }
        }
    }

    void DefenderWindow::check_projectile_asteroid_hits() {
        constexpr float beam_collision_length = 7.4f;

        for (auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }

            const float direction = projectile.velocity.x >= 0.0f ? 1.0f : -1.0f;
            const float x0 = projectile.prev_position.x;
            const float x1 = x0 + wrapped_delta_x(projectile.position.x, x0) + direction * beam_collision_length;
            const float min_x = std::min(x0, x1);
            const float max_x = std::max(x0, x1);

            for (auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }

                const float asteroid_x = nearest_world_x(asteroid.position.x, (x0 + x1) * 0.5f);
                const float closest_x = std::clamp(asteroid_x, min_x, max_x);
                const glm::vec2 delta{asteroid_x - closest_x, asteroid.position.y - projectile.position.y};
                if (glm::dot(delta, delta) > asteroid.collision_radius * asteroid.collision_radius) {
                    continue;
                }

                spawn_ufo_explosion(asteroid.position, asteroid.scale);
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
                play_sound(asteroid_explosion_sound);
#endif
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(1.0f, 2.5f);
                projectile.active = false;
                score += 10;
                log_game(std::format("Asteroid destroyed. Score={}.", score));
                break;
            }
        }
    }

    void DefenderWindow::check_ship_ufo_collisions() {
        constexpr float ship_half_width = 1.75f;
        constexpr float ship_half_height = 0.85f;

        for (auto &ufo : ufos) {
            if (!ufo.active) {
                continue;
            }

            const int frame = current_ufo_frame(ufo);
            const SpriteAlphaBounds &bounds = ufo_sprite_bounds[static_cast<std::size_t>(ufo.sprite_set)][static_cast<std::size_t>(frame)];
            const float pulse = current_ufo_pulse(ufo);
            const glm::vec2 draw_size = ufo_draw_size(ufo, pulse);
            const glm::vec2 body_center{
                nearest_world_x(ufo.position.x, ship.position.x) + ((bounds.min_x + bounds.max_x) * 0.5f) * draw_size.x,
                ufo.position.y + ((bounds.min_y + bounds.max_y) * 0.5f) * draw_size.y,
            };
            const glm::vec2 body_radius{
                std::max(0.001f, (bounds.max_x - bounds.min_x) * 0.5f * draw_size.x),
                std::max(0.001f, (bounds.max_y - bounds.min_y) * 0.5f * draw_size.y),
            };

            const float closest_x = std::clamp(body_center.x, ship.position.x - ship_half_width, ship.position.x + ship_half_width);
            const float closest_y = std::clamp(body_center.y, ship.position.y - ship_half_height, ship.position.y + ship_half_height);
            const glm::vec2 normalized_delta{
                (closest_x - body_center.x) / body_radius.x,
                (closest_y - body_center.y) / body_radius.y,
            };
            if (glm::dot(normalized_delta, normalized_delta) > 1.0f) {
                continue;
            }

            if (ufo.sprite_set == UfoSpriteSet::Alien) {
                spawn_alien_explosion(ufo.position);
            } else {
                spawn_ufo_explosion(ufo.position);
            }
            spawn_ship_explosion(ship.position);
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(ufo_explosion_sound);
#endif
            ufo.active = false;
            ufo.respawn_timer = space::random_float(1.5f, 3.0f);
            log_game("Ship collided with UFO.", SDL_Color{255, 150, 90, 255});
            lose_life();
            break;
        }
    }

    void DefenderWindow::check_ship_asteroid_collisions() {
        constexpr float ship_half_width = 1.75f;
        constexpr float ship_half_height = 0.85f;

        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                continue;
            }

            const float asteroid_x = nearest_world_x(asteroid.position.x, ship.position.x);
            const float closest_x = std::clamp(asteroid_x, ship.position.x - ship_half_width, ship.position.x + ship_half_width);
            const float closest_y = std::clamp(asteroid.position.y, ship.position.y - ship_half_height, ship.position.y + ship_half_height);
            const glm::vec2 delta{asteroid_x - closest_x, asteroid.position.y - closest_y};
            if (glm::dot(delta, delta) > asteroid.collision_radius * asteroid.collision_radius) {
                continue;
            }

            spawn_ufo_explosion(asteroid.position, asteroid.scale);
            spawn_ship_explosion(ship.position);
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(asteroid_explosion_sound);
#endif
            asteroid.active = false;
            asteroid.respawn_timer = space::random_float(1.0f, 2.5f);
            log_game("Ship collided with asteroid.", SDL_Color{255, 150, 90, 255});
            lose_life();
            break;
        }
    }

    void DefenderWindow::lose_life() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        play_sound(crash_sound);
#endif
        --lives;
        clear_projectiles();
        ship.visible = false;
        log_game(std::format("Ship destroyed. Lives remaining: {}.", std::max(0, lives)), SDL_Color{255, 120, 80, 255});
        if (lives <= 0) {
            lives = 0;
            game_over = true;
            ship.velocity = glm::vec3(0.0f);
            ship.current_speed = 0.0f;
            log_game(std::format("Game over. Final score: {}.", score), SDL_Color{255, 90, 90, 255});
            return;
        }

        ship_respawning = true;
        respawn_timer = 2.2f;
        ship.velocity = glm::vec3(0.0f);
        ship.current_speed = 0.0f;
    }

    void DefenderWindow::update_respawn(float dt) {
        respawn_timer -= dt;
        if (respawn_timer > 0.0f) {
            return;
        }

        reset_ship_to_origin();
        ship_respawning = false;
        start_launch_countdown();
        log_game("Ship respawned at origin.");
    }

    void DefenderWindow::reset_ship_to_origin() {
        camera_center_x = 0.0f;
        camera_position = glm::vec3(0.0f, CAMERA_HEIGHT, CAMERA_DISTANCE);
        reset_ufos_for_origin_start();
        ship.position = glm::vec3(0.0f, 0.0f, 0.0f);
        ship.prev_position = ship.position;
        ship.velocity = glm::vec3(0.0f);
        ship.current_speed = 0.0f;
        ship.visible = true;
        ship_forward_direction = 1.0f;
        barrel_roll_direction = 1.0f;
        barrel_roll_progress = 0.0f;
        barrel_roll_angle = 0.0f;
        ship.rotation = glm::vec3(0.0f, -90.0f, 0.0f);
    }

    void DefenderWindow::reset_ufos_for_origin_start() {
        level_active = false;
        for (auto &ufo : ufos) {
            ufo.active = false;
            ufo.respawn_timer = space::random_float(0.4f, 4.0f);
        }
        for (auto &asteroid : asteroids) {
            asteroid.active = false;
            asteroid.respawn_timer = space::random_float(0.7f, 4.8f);
        }
    }

    void DefenderWindow::restart_game() {
        score = 0;
        lives = 5;
        level = 1;
        game_over = false;
        ship_respawning = false;
        respawn_timer = 0.0f;
        reverse_pressed = false;
        propulsion_pressed = false;
        up_pressed = false;
        down_pressed = false;
        roll_left_pressed = false;
        roll_right_pressed = false;
        fire_pressed = false;
        clear_projectiles();
        clear_particles();
        reset_ship_to_origin();
        start_launch_countdown();
        log_game("Game state reset: score=0 lives=5.");
    }

    void DefenderWindow::clear_projectiles() {
        for (auto &projectile : projectiles) {
            projectile.active = false;
        }
        ship.fire_cooldown = 0;
    }

    void DefenderWindow::draw_projectiles() {
        for (const auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }
            const float life_factor = std::clamp(1.0f - (projectile.lifetime / PROJECTILE_LIFETIME), 0.0f, 1.0f);
            const float flash = (std::sin(elapsed_seconds * 95.0f) > 0.0f) ? 1.0f : 0.48f;
            const float beam_length = 6.4f + 1.2f * life_factor;
            const float core_thickness = 0.085f + 0.025f * flash;
            const float glow_thickness = 0.30f + 0.10f * flash;
            const float direction = projectile.velocity.x >= 0.0f ? 1.0f : -1.0f;
            const glm::vec3 beam_center_offset{direction * beam_length * 0.38f, 0.0f, 0.0f};
            const glm::vec4 glow_color{
                1.0f,
                0.36f + 0.28f * flash,
                0.06f,
                std::clamp(0.20f + 0.32f * flash * life_factor, 0.0f, 1.0f),
            };
            const glm::vec4 core_color{
                1.0f,
                std::clamp(projectile.color.g * (0.82f + 0.18f * flash), 0.0f, 1.0f),
                std::clamp(projectile.color.b * (0.65f + 0.35f * flash), 0.0f, 1.0f),
                std::clamp(0.45f + 0.55f * flash * life_factor, 0.0f, 1.0f),
            };

            const glm::vec3 draw_position = nearest_world_position(projectile.position, camera_position.x) + beam_center_offset;
            projectile_sprite->drawSprite(draw_position, glm::vec2(beam_length, glow_thickness), glow_color);
            projectile_sprite->drawSprite(draw_position, glm::vec2(beam_length, core_thickness), core_color);
        }
    }

    void DefenderWindow::spawn_ufo_explosion(const glm::vec3 &position, float explosion_scale) {
        constexpr std::array<glm::vec3, 4> colors = {
            glm::vec3{1.0f, 1.0f, 1.0f},
            glm::vec3{1.0f, 0.86f, 0.34f},
            glm::vec3{1.0f, 0.42f, 0.08f},
            glm::vec3{0.75f, 0.90f, 1.0f},
        };
        spawn_enemy_explosion(position, explosion_scale, colors);
    }

    void DefenderWindow::spawn_alien_explosion(const glm::vec3 &position, float explosion_scale) {
        constexpr std::array<glm::vec3, 4> colors = {
            glm::vec3{0.82f, 1.0f, 0.52f},
            glm::vec3{0.35f, 1.0f, 0.18f},
            glm::vec3{0.08f, 0.72f, 0.18f},
            glm::vec3{0.70f, 1.0f, 0.82f},
        };
        spawn_enemy_explosion(position, explosion_scale, colors);
    }

    void DefenderWindow::spawn_ship_explosion(const glm::vec3 &position, float explosion_scale) {
        constexpr std::array<glm::vec3, 4> colors = {
            glm::vec3{1.0f, 0.72f, 0.62f},
            glm::vec3{1.0f, 0.20f, 0.12f},
            glm::vec3{0.76f, 0.02f, 0.02f},
            glm::vec3{1.0f, 0.36f, 0.22f},
        };
        spawn_enemy_explosion(position, explosion_scale, colors);
    }

    void DefenderWindow::spawn_enemy_explosion(const glm::vec3 &position, float explosion_scale, const std::array<glm::vec3, 4> &wave_colors) {
        struct ExplosionWave {
            float min_speed;
            float max_speed;
            float min_size;
            float max_size;
            float min_lifetime;
            float max_lifetime;
        };

        constexpr std::array<ExplosionWave, 4> waves = {
            ExplosionWave{24.0f, 42.0f, 0.42f, 0.78f, 0.75f, 1.35f},
            ExplosionWave{16.0f, 32.0f, 0.32f, 0.58f, 0.90f, 1.60f},
            ExplosionWave{10.0f, 24.0f, 0.20f, 0.42f, 1.05f, 1.85f},
            ExplosionWave{5.0f, 16.0f, 0.12f, 0.26f, 1.20f, 2.10f},
        };

        const float visual_scale = std::clamp(explosion_scale, 0.75f, 2.35f);
        const float particle_count_scale = std::clamp(visual_scale, 0.75f, 1.80f);
        const int particles_per_wave = std::max(1, static_cast<int>(std::round(60.0f * particle_count_scale)));
        const float speed_scale = std::lerp(0.85f, 1.35f, std::clamp((visual_scale - 0.75f) / 1.60f, 0.0f, 1.0f));
        const float lifetime_scale = std::lerp(0.90f, 1.20f, std::clamp((visual_scale - 0.75f) / 1.60f, 0.0f, 1.0f));

        for (std::size_t wave_index = 0; wave_index < waves.size(); ++wave_index) {
            const ExplosionWave &wave = waves[wave_index];
            const glm::vec3 &wave_color = wave_colors[wave_index];
            for (int i = 0; i < particles_per_wave; ++i) {
                space::Particle *particle = find_free_particle();
                if (particle == nullptr) {
                    return;
                }
                const glm::vec3 dir = space::normalize_or_zero(glm::vec3(
                    space::random_float(-1.0f, 1.0f),
                    space::random_float(-0.75f, 0.75f),
                    space::random_float(-0.35f, 0.35f)));
                particle->position = position + dir * space::random_float(0.1f, 0.8f) * visual_scale;
                particle->velocity = dir * space::random_float(wave.min_speed, wave.max_speed) * speed_scale;
                particle->color = glm::vec4(
                    wave_color.r * space::random_float(0.9f, 1.1f),
                    wave_color.g * space::random_float(0.9f, 1.1f),
                    wave_color.b * space::random_float(0.9f, 1.1f),
                    0.1f);
                particle->size = space::random_float(wave.min_size, wave.max_size) * visual_scale;
                particle->lifetime = 0.0f;
                particle->max_lifetime = space::random_float(wave.min_lifetime, wave.max_lifetime) * lifetime_scale;
                particle->active = true;
            }
        }
    }

    space::Particle *DefenderWindow::find_free_particle() {
        for (auto &particle : particles) {
            if (!particle.active) {
                return &particle;
            }
        }
        return nullptr;
    }

    void DefenderWindow::update_particles(float dt) {
        for (auto &particle : particles) {
            if (!particle.active) {
                continue;
            }
            particle.position += particle.velocity * dt;
            particle.velocity *= 0.975f;
            particle.velocity.y -= 0.35f * dt;
            particle.lifetime += dt;

            const float life_ratio = particle.lifetime / particle.max_lifetime;
            if (life_ratio >= 1.0f) {
                particle.active = false;
                continue;
            }
            if (life_ratio < 0.2f) {
                particle.color.a = life_ratio / 0.2f;
            } else if (life_ratio > 0.75f) {
                particle.color.a = (1.0f - life_ratio) / 0.25f;
            } else {
                particle.color.a = 1.0f;
            }
            particle.size *= life_ratio < 0.30f ? 1.012f : 0.988f;
            if (particle.color.a < 0.01f) {
                particle.active = false;
            }
        }
    }

    void DefenderWindow::draw_particles() {
        for (const auto &particle : particles) {
            if (!particle.active) {
                continue;
            }
            effect_sprite->drawSprite(nearest_world_position(particle.position, camera_position.x), glm::vec2(particle.size), particle.color);
        }
    }

    void DefenderWindow::clear_particles() {
        for (auto &particle : particles) {
            particle.active = false;
        }
    }

} // namespace defender
