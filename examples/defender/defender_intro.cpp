#include "defender_window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace defender {

    void DefenderWindow::update_ship(float dt) {
        ship.prev_position = ship.position;

        if (reverse_pressed) {
            reverse_pressed = false;
            ship_forward_direction *= -1.0f;
            ship.velocity.x *= 0.55f;
        }

        constexpr float horizontal_acceleration = 58.0f;
        constexpr float horizontal_drag = 2.4f;
        const bool propulsion_active = mode == GameMode::Playing && propulsion_pressed;
        if (propulsion_active) {
            ship.velocity.x += ship_forward_direction * horizontal_acceleration * dt;
        } else {
            ship.velocity.x = std::lerp(ship.velocity.x, 0.0f, 1.0f - std::exp(-dt * horizontal_drag));
        }
        ship.velocity.x = std::clamp(ship.velocity.x, -ship.max_speed, ship.max_speed);
        ship.current_speed = std::abs(ship.velocity.x);

        float vertical_input = 0.0f;
        if (up_pressed && !down_pressed) {
            vertical_input = 1.0f;
        } else if (down_pressed && !up_pressed) {
            vertical_input = -1.0f;
        }

        constexpr float vertical_speed = 19.0f;
        constexpr float vertical_response = 12.0f;
        const float target_vertical_velocity = vertical_input * vertical_speed;
        ship.velocity.y = std::lerp(ship.velocity.y, target_vertical_velocity, 1.0f - std::exp(-dt * vertical_response));
        ship.velocity.z = 0.0f;
        ship.position += ship.velocity * dt;
        ship.position.y = std::clamp(ship.position.y, WORLD_BOTTOM, WORLD_TOP);
        ship.position.z = 0.0f;

        const float bank = std::clamp(-ship.velocity.y * 2.0f, -22.0f, 22.0f);
        const float pitch = std::clamp(ship.velocity.y * 0.9f, -12.0f, 12.0f);
        ship.rotation.x = pitch;
        ship.rotation.y = ship_forward_direction > 0.0f ? -90.0f : 90.0f;
        const float thrust_bank = propulsion_active ? ship_forward_direction * -7.0f : 0.0f;
        ship.rotation.z = bank + thrust_bank;

        if (ship.fire_cooldown > 0) {
            --ship.fire_cooldown;
        }
        if (fire_pressed) {
            fire_pressed = false;
            if (ship.fire_cooldown <= 0) {
                fire_projectile();
                ship.fire_cooldown = FIRE_COOLDOWN;
            }
        }
    }

    void DefenderWindow::update_barrel_roll(float dt) {
        const bool ship_visible_mode = mode == GameMode::IntroFadeIn || mode == GameMode::Countdown || mode == GameMode::Playing;
        if (!ship_visible_mode || game_over || ship_respawning) {
            barrel_roll_progress = 0.0f;
            barrel_roll_angle = 0.0f;
            return;
        }

        float roll_input = 0.0f;
        if (roll_left_pressed && !roll_right_pressed) {
            roll_input = -1.0f;
        } else if (roll_right_pressed && !roll_left_pressed) {
            roll_input = 1.0f;
        }
        if (roll_input != 0.0f) {
            barrel_roll_direction = roll_input;
        }

        if (roll_input == 0.0f && barrel_roll_progress <= 0.0f) {
            barrel_roll_angle = 0.0f;
            return;
        }

        const float roll_speed = 360.0f / BARREL_ROLL_DURATION;
        barrel_roll_progress += roll_speed * dt;
        if (barrel_roll_progress >= 360.0f) {
            barrel_roll_progress = (roll_input != 0.0f) ? std::fmod(barrel_roll_progress, 360.0f) : 0.0f;
        }
        barrel_roll_angle = barrel_roll_direction * barrel_roll_progress;
    }

    glm::mat4 DefenderWindow::build_ship_model_matrix() {
        const glm::quat yaw = glm::angleAxis(glm::radians(ship.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat pitch = glm::angleAxis(glm::radians(ship.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat bank = glm::angleAxis(glm::radians(ship.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::quat barrel_roll = glm::angleAxis(glm::radians(barrel_roll_angle), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::quat orientation = yaw * pitch * bank * barrel_roll;

        glm::mat4 model(1.0f);
        model = glm::translate(model, ship.position);
        model *= glm::mat4_cast(orientation);
        model = glm::scale(model, glm::vec3(SHIP_MODEL_SCALE * ship_model.modelRenderScale()));
        model = glm::translate(model, ship_model.modelCenterOffset());
        return model;
    }

    glm::mat4 DefenderWindow::build_asteroid_model_matrix(const Asteroid &asteroid, const mxvk::VKAbstractModel &model_resource) const {
        const glm::quat yaw = glm::angleAxis(glm::radians(asteroid.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat pitch = glm::angleAxis(glm::radians(asteroid.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::quat roll = glm::angleAxis(glm::radians(asteroid.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::mat4 model(1.0f);
        model = glm::translate(model, asteroid.position);
        model *= glm::mat4_cast(yaw * pitch * roll);
        model = glm::scale(model, glm::vec3(asteroid.scale * model_resource.modelRenderScale()));
        model = glm::translate(model, model_resource.modelCenterOffset());
        return model;
    }

    void DefenderWindow::reset_intro_screen() {
        mode = GameMode::Intro;
        intro_fade = 1.0f;
        intro_last_update_ms = SDL_GetTicks();
        intro_fade_in_start_ms = 0;
        if (intro_rain != nullptr) {
            intro_rain->set_opacity(1.0f);
            intro_rain->reset();
        }
    }

    void DefenderWindow::start_launch_countdown() {
        mode = GameMode::Countdown;
        countdown_timer = 0;
        countdown_duration = 1000;
        countdown_start_ms = SDL_GetTicks();
        countdown_number = 3;
        launch_timer = 0;
        launch_duration = 1000;
        launch_start_ms = 0;
    }

    void DefenderWindow::start_intro_fade_in() {
        start_launch_countdown();
        mode = GameMode::IntroFadeIn;
        intro_fade_in_start_ms = SDL_GetTicks();
        log_game("Intro faded out. Launch countdown started.");
    }

    void DefenderWindow::draw_intro(const VkExtent2D &extent) {
        if (intro_sprite == nullptr) {
            start_launch_countdown();
            return;
        }

        const Uint32 current_ms = SDL_GetTicks();
        if ((current_ms - intro_last_update_ms) > 35U) {
            intro_last_update_ms = current_ms;
            intro_fade -= 0.01f;
        }

        if (intro_fade <= 0.0f) {
            intro_fade = 0.0f;
            if (intro_rain != nullptr) {
                intro_rain->set_opacity(0.0f);
            }
            start_intro_fade_in();
            return;
        }

        intro_sprite->setShaderParams(static_cast<float>(current_ms) / 1000.0f, 0.0f, 0.0f, intro_fade);
        intro_sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
        if (intro_rain != nullptr) {
            intro_rain->set_opacity(intro_fade);
            intro_rain->update_and_render(*this, static_cast<int>(extent.width), static_cast<int>(extent.height));
        }
        printText("Press Enter", 24, static_cast<int>(extent.height) - 52, {255, 230, 80, 255});
    }

    void DefenderWindow::draw_intro_fade_in(const VkExtent2D &extent) {
        const Uint32 elapsed_ms = SDL_GetTicks() - intro_fade_in_start_ms;
        const float progress = std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(INTRO_FADE_IN_DURATION_MS), 0.0f, 1.0f);
        const float alpha = 1.0f - progress;
        if (alpha <= 0.0f) {
            mode = GameMode::Countdown;
            return;
        }

        draw_fade_overlay(extent, alpha);
    }

    void DefenderWindow::draw_fade_overlay(const VkExtent2D &extent, float alpha) {
        if (fade_overlay_sprite == nullptr) {
            return;
        }

        alpha = std::clamp(alpha, 0.0f, 1.0f);
        if (alpha <= 0.0f) {
            return;
        }

        fade_overlay_sprite->setShaderParams(0.0f, 0.0f, 0.0f, alpha);
        fade_overlay_sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
    }

    void DefenderWindow::update_camera_scroll(float aspect) {
        const float visible_half_height = std::tan(glm::radians(25.0f)) * CAMERA_DISTANCE;
        const float visible_half_width = visible_half_height * std::max(0.1f, aspect);
        const float scroll_limit = visible_half_width * CAMERA_SCROLL_EDGE_FRACTION;
        const bool propulsion_active = mode == GameMode::Playing && propulsion_pressed;
        const float lead_offset = propulsion_active ? ship_forward_direction * visible_half_width * 0.18f : 0.0f;
        const float desired_camera_x = ship.position.x + lead_offset;
        const float relative_x = ship.position.x - camera_center_x;

        if (relative_x > scroll_limit) {
            camera_center_x = ship.position.x - scroll_limit;
        } else if (relative_x < -scroll_limit) {
            camera_center_x = ship.position.x + scroll_limit;
        }

        if (propulsion_active) {
            camera_center_x = std::lerp(camera_center_x, desired_camera_x, 0.04f);
        }
    }

    void DefenderWindow::update_countdown() {
        const Uint32 now = SDL_GetTicks();
        const Uint32 elapsed = now - countdown_start_ms;
        countdown_timer = elapsed % countdown_duration;

        if (elapsed < (countdown_duration * 3U)) {
            countdown_number = 3 - static_cast<int>(elapsed / countdown_duration);
            launch_start_ms = 0;
            launch_timer = 0;
            return;
        }

        countdown_number = 0;
        if (launch_start_ms == 0U) {
            launch_start_ms = now;
        }
        launch_timer = std::min<Uint32>(now - launch_start_ms, launch_duration);
        if (launch_timer >= launch_duration) {
            mode = GameMode::Playing;
            ufos_enabled = false;
            reverse_pressed = false;
            propulsion_pressed = false;
            fire_pressed = false;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(takeoff_sound);
#endif
            log_game("Mission started.");
        }
    }

    void DefenderWindow::draw_countdown(VkCommandBuffer cmd, uint32_t image_index, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection) {
        update_countdown();

        star_field.update(0.0f, camera_position, elapsed_seconds);
        star_sprite->updateCamera(image_index, view, projection);
        star_field.draw();
        star_sprite->render(cmd, image_index);
        star_sprite->clearQueue();

        if (ship.visible && !ship_respawning) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = build_ship_model_matrix();
            ubo.view = view;
            ubo.proj = projection;
            ship_model.updateUBO(image_index, ubo);
            ship_model.render(cmd, image_index, false);
        }

        if (countdown_number > 0) {
            const std::string number = std::format("{}", countdown_number);
            int text_w = 0;
            int text_h = 0;
            if (!getTextDimensions(number, text_w, text_h, countdown_font)) {
                text_w = 64;
                text_h = 64;
            }
            const bool show_number = ((countdown_timer / 167U) % 2U) == 0U;
            if (show_number) {
                printText(number,
                          static_cast<int>(extent.width) / 2 - text_w / 2,
                          static_cast<int>(extent.height) / 2 - text_h / 2,
                          {255, 255, 255, 255},
                          countdown_font);
            }
        } else {
            const std::string launch = "MISSION START!";
            int text_w = 0;
            int text_h = 0;
            if (!getTextDimensions(launch, text_w, text_h, countdown_font)) {
                text_w = 440;
                text_h = 48;
            }
            printText(launch,
                      static_cast<int>(extent.width) / 2 - text_w / 2,
                      static_cast<int>(extent.height) / 2 - text_h / 2,
                      {0, 255, 255, 255},
                      countdown_font);
        }
    }

    void DefenderWindow::update_fps_counter(float delta_seconds) {
        fps_accumulator += delta_seconds;
        ++fps_frame_count;
        if (fps_accumulator < 0.25f) {
            return;
        }

        const float fps = static_cast<float>(fps_frame_count) / fps_accumulator;
        fps_text = std::format("FPS: {:.1f}", fps);
        fps_accumulator = 0.0f;
        fps_frame_count = 0;
    }

    void DefenderWindow::draw_hud(const VkExtent2D &extent) {
        const SDL_Color white{255, 255, 255, 255};
        const SDL_Color yellow{255, 230, 80, 255};
        const SDL_Color red{255, 70, 60, 255};
        printText("Score: " + std::to_string(score), 24, 22, white);
        printText("Lives: " + std::to_string(lives), 24, 52, lives <= 1 ? red : white);
        if (show_fps_counter) {
            int fps_w = 0;
            int fps_h = 0;
            if (!getTextDimensions(fps_text, fps_w, fps_h)) {
                fps_w = 120;
            }
            const int fps_x = std::max(24, static_cast<int>(extent.width) - fps_w - 24);
            printText(fps_text, fps_x, 22, white);
        }

        if (!game_over) {
            return;
        }

        const std::string title = "GAME OVER";
        const std::string final_score = "Final Score: " + std::to_string(score);
        const std::string prompt = "Press Enter to start a new game";
        int title_w = 0;
        int title_h = 0;
        int score_w = 0;
        int score_h = 0;
        int prompt_w = 0;
        int prompt_h = 0;
        if (!getTextDimensions(title, title_w, title_h)) {
            title_w = 180;
        }
        if (!getTextDimensions(final_score, score_w, score_h)) {
            score_w = 220;
        }
        if (!getTextDimensions(prompt, prompt_w, prompt_h)) {
            prompt_w = 360;
        }
        const int center_x = static_cast<int>(extent.width) / 2;
        const int center_y = static_cast<int>(extent.height) / 2;
        printText(title, center_x - title_w / 2, center_y - 52, red);
        printText(final_score, center_x - score_w / 2, center_y - 12, yellow);
        printText(prompt, center_x - prompt_w / 2, center_y + 28, white);
    }

} // namespace defender
