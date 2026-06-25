#include "defender_types.hpp"

#include "asteroids3d_types.hpp"
#include "rain.hpp"
#include "ship.hpp"
#include "starfield.hpp"

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace defender {

    namespace {
        enum class GameMode {
            Intro,
            IntroFadeIn,
            Countdown,
            Playing
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
    } // namespace

    class DefenderWindow : public mxvk::VK_Window {
      public:
        DefenderWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("Defender Starfield Demo", width, height, fullscreen, MXVK_VALIDATION),
              asset_root((path.empty() || path == ".") ? std::string(DEFENDER_ASSET_DIR) : path) {
            setClearColor(0.0f, 0.0f, 0.01f, 1.0f);
            setFont(asset_root + "/data/font.ttf", HUD_FONT_SIZE);
            countdown_font.reset(asset_root + "/data/font.ttf", COUNTDOWN_FONT_SIZE);

            const std::string model_vert = std::string(DEFENDER_SHADER_DIR) + "/model.vert.spv";
            const std::string model_frag = std::string(DEFENDER_SHADER_DIR) + "/model.frag.spv";
            ship_model.load(this, asset_root + "/data/starship.obj", asset_root + "/data/starship.mtl", asset_root + "/data", 1.0f);
            ship_model.setShaders(this, model_vert, model_frag);
            for (auto &asteroid_model : asteroid_models) {
                asteroid_model.load(this, asset_root + "/data/asteroid.obj", asset_root + "/data/asteroid.mtl", asset_root + "/data", 1.0f);
                asteroid_model.setShaders(this, model_vert, model_frag);
            }

            intro_sprite = createSprite(
                asset_root + "/data/intro.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(DEFENDER_SHADER_DIR) + "/intro.frag.spv");
            fade_overlay_sprite = createSprite(
                1,
                1,
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(DEFENDER_SHADER_DIR) + "/fade_overlay.frag.spv");
            const uint32_t black_pixel = 0xFF000000u;
            fade_overlay_sprite->updateTexture(&black_pixel, 1, 1);
            matrix::RainConfig intro_rain_config = matrix::make_matrix_rain_config(asset_root, false);
            intro_rain_config.font_size += 8;
            intro_rain_config.color = "#ff0000";
            intro_rain = std::make_unique<matrix::Rain>(*this, std::move(intro_rain_config));
            reset_intro_screen();

            star_sprite = createSprite3D(asset_root + "/data/star.png");
            star_sprite->setDepthTestEnabled(false);
            star_sprite->setDepthWriteEnabled(false);
            star_sprite->setAlphaDiscardThreshold(0.02f);
            star_field.init(STAR_COUNT, 16.0f, 112.0f);
            star_field.setSprite(star_sprite);

            projectile_sprite = createSprite3D(asset_root + "/data/particle_projectile.png");
            projectile_sprite->setDepthTestEnabled(true);
            projectile_sprite->setDepthWriteEnabled(false);
            projectile_sprite->setAlphaDiscardThreshold(0.05f);

            for (int i = 0; i < UFO_ANIMATION_FRAMES; ++i) {
                const std::string frame_path = asset_root + std::format("/data/ufo_lights_{:02d}.png", i);
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> saucer_surface(space::load_color_keyed_png(frame_path, 8, 24), SDL_DestroySurface);
                ufo_sprites[static_cast<std::size_t>(i)] = createSprite3D(saucer_surface.get());
                ufo_sprites[static_cast<std::size_t>(i)]->setDepthTestEnabled(true);
                ufo_sprites[static_cast<std::size_t>(i)]->setDepthWriteEnabled(false);
                ufo_sprites[static_cast<std::size_t>(i)]->setAlphaDiscardThreshold(0.02f);
            }

            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> explosion_surface(space::load_color_keyed_png(asset_root + "/data/particle_explosion.png", 12), SDL_DestroySurface);
            effect_sprite = createSprite3D(explosion_surface.get());
            effect_sprite->setDepthTestEnabled(true);
            effect_sprite->setDepthWriteEnabled(false);
            effect_sprite->setAlphaDiscardThreshold(0.05f);

            init_ufos();
            init_asteroids();
            create_flame_resources();

            ship.position = glm::vec3(0.0f, 0.0f, 0.0f);
            ship.velocity = glm::vec3(0.0f, 0.0f, 0.0f);
            ship.current_speed = 0.0f;
            ship.min_speed = 0.0f;
            ship.max_speed = 28.0f;
            ship.rotation = glm::vec3(0.0f, -90.0f, 0.0f);
        }

        ~DefenderWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            cleanup_flame_resources();
            ship_model.cleanup(this);
            for (auto &asteroid_model : asteroid_models) {
                asteroid_model.cleanup(this);
            }
            if (star_sprite != nullptr) {
                star_sprite->cleanup();
            }
            if (projectile_sprite != nullptr) {
                projectile_sprite->cleanup();
            }
            for (mxvk::VK_Sprite3D *sprite : ufo_sprites) {
                if (sprite != nullptr) {
                    sprite->cleanup();
                }
            }
            if (effect_sprite != nullptr) {
                effect_sprite->cleanup();
            }
            intro_rain.reset();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }
            if (mode == GameMode::Intro && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                intro_fade = 0.01f;
                return;
            }
            if (game_over && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                restart_game();
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                const bool pressed = e.type == SDL_EVENT_KEY_DOWN;
                if (e.key.key == SDLK_A) {
                    roll_left_pressed = pressed;
                } else if (e.key.key == SDLK_S) {
                    roll_right_pressed = pressed;
                } else if (e.key.key == SDLK_LEFT) {
                    left_pressed = pressed;
                } else if (e.key.key == SDLK_RIGHT || e.key.key == SDLK_D) {
                    right_pressed = pressed;
                } else if (e.key.key == SDLK_UP || e.key.key == SDLK_W) {
                    up_pressed = pressed;
                } else if (e.key.key == SDLK_DOWN) {
                    down_pressed = pressed;
                } else if (e.key.key == SDLK_SPACE && pressed && !e.key.repeat) {
                    fire_pressed = true;
                }
            }
        }

        void onSwapchainRecreated() override {
            if (intro_sprite != nullptr) {
                intro_sprite->rebuildPipeline();
            }
            if (fade_overlay_sprite != nullptr) {
                fade_overlay_sprite->rebuildPipeline();
            }
            if (intro_rain != nullptr) {
                intro_rain->resize(*this);
            }
            ship_model.resize(this);
            for (auto &asteroid_model : asteroid_models) {
                asteroid_model.resize(this);
            }
            star_field.resize(this);
            if (projectile_sprite != nullptr) {
                projectile_sprite->resize(this);
            }
            for (mxvk::VK_Sprite3D *sprite : ufo_sprites) {
                if (sprite != nullptr) {
                    sprite->resize(this);
                }
            }
            if (effect_sprite != nullptr) {
                effect_sprite->resize(this);
            }
            cleanup_flame_swapchain_resources();
            create_flame_swapchain_resources();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
            last_frame_time = now;
            const float dt = std::min(delta_seconds, 0.1f);
            elapsed_seconds += dt;

            update_barrel_roll(dt);
            if (ship_respawning) {
                update_respawn(dt);
            } else if (mode == GameMode::Playing && !game_over) {
                update_ship(dt);
                update_ufos(dt);
                update_asteroids(dt);
                update_projectiles(dt);
                check_projectile_ufo_hits();
                check_ship_ufo_collisions();
                check_ship_asteroid_collisions();
            }
            update_particles(dt);

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

            if (mode == GameMode::Intro) {
                draw_intro(extent);
                return;
            }

            if (mode == GameMode::IntroFadeIn || mode == GameMode::Countdown) {
                update_camera_scroll(aspect);
                const glm::vec3 camera_target{camera_center_x, CAMERA_HEIGHT, 0.0f};
                const glm::vec3 ideal_camera{camera_center_x, CAMERA_HEIGHT, CAMERA_DISTANCE};
                camera_position = glm::mix(camera_position, ideal_camera, 1.0f - std::exp(-dt * 10.0f));

                const glm::mat4 view = glm::lookAt(camera_position, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 400.0f);
                projection[1][1] *= -1.0f;

                draw_countdown(cmd, image_index, extent, view, projection);
                if (mode == GameMode::IntroFadeIn) {
                    draw_intro_fade_in(extent);
                }
                return;
            }

            update_camera_scroll(aspect);

            const glm::vec3 camera_target{camera_center_x, CAMERA_HEIGHT, 0.0f};
            const glm::vec3 ideal_camera{camera_center_x, CAMERA_HEIGHT, CAMERA_DISTANCE};
            camera_position = glm::mix(camera_position, ideal_camera, 1.0f - std::exp(-dt * 10.0f));

            const glm::mat4 view = glm::lookAt(camera_position, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 400.0f);
            projection[1][1] *= -1.0f;

            star_field.update(dt, camera_position, elapsed_seconds);
            star_sprite->updateCamera(image_index, view, projection);
            star_field.draw();
            star_sprite->render(cmd, image_index);
            star_sprite->clearQueue();

            for (mxvk::VK_Sprite3D *sprite : ufo_sprites) {
                sprite->updateCamera(image_index, view, projection);
            }
            draw_ufos();
            for (mxvk::VK_Sprite3D *sprite : ufo_sprites) {
                sprite->render(cmd, image_index);
                sprite->clearQueue();
            }

            draw_asteroids(cmd, image_index, view, projection);

            projectile_sprite->updateCamera(image_index, view, projection);
            draw_projectiles();
            projectile_sprite->render(cmd, image_index);
            projectile_sprite->clearQueue();

            mxvk::UniformBufferObject ubo{};
            ubo.model = build_ship_model_matrix();
            last_ship_model_matrix = ubo.model;
            ubo.view = view;
            ubo.proj = projection;
            if (ship.visible && !ship_respawning) {
                ship_model.updateUBO(image_index, ubo);
                ship_model.render(cmd, image_index, false);
                draw_engine_flame(cmd, extent, view, projection);
            }

            effect_sprite->updateCamera(image_index, view, projection);
            draw_particles();
            effect_sprite->render(cmd, image_index);
            effect_sprite->clearQueue();

            draw_hud(extent);
        }

      private:
        std::string asset_root;
        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        float elapsed_seconds = 0.0f;
        GameMode mode = GameMode::Intro;
        float intro_fade = 1.0f;
        Uint32 intro_last_update_ms = 0;
        Uint32 intro_fade_in_start_ms = 0;
        static constexpr Uint32 INTRO_FADE_IN_DURATION_MS = 500;
        Uint32 countdown_timer = 0;
        Uint32 countdown_duration = 1000;
        Uint32 countdown_start_ms = 0;
        int countdown_number = 3;
        Uint32 launch_timer = 0;
        Uint32 launch_duration = 1000;
        Uint32 launch_start_ms = 0;
        float ship_forward_direction = 1.0f;
        float camera_center_x = 0.0f;
        float respawn_timer = 0.0f;
        int score = 0;
        int lives = 5;
        bool game_over = false;
        bool ship_respawning = false;
        bool ufos_enabled = false;
        bool asteroids_enabled = false;
        bool left_pressed = false;
        bool right_pressed = false;
        bool up_pressed = false;
        bool down_pressed = false;
        bool fire_pressed = false;
        bool roll_left_pressed = false;
        bool roll_right_pressed = false;
        float barrel_roll_direction = 1.0f;
        float barrel_roll_progress = 0.0f;
        float barrel_roll_angle = 0.0f;
        static constexpr float BARREL_ROLL_DURATION = 0.65f;
        static constexpr int HUD_FONT_SIZE = 24;
        static constexpr int COUNTDOWN_FONT_SIZE = HUD_FONT_SIZE * 2;
        mxvk::Font countdown_font{};

        space::Ship ship{};
        std::array<space::Projectile, MAX_PROJECTILES> projectiles{};
        std::array<Ufo, MAX_UFOS> ufos{};
        std::array<Asteroid, MAX_ASTEROIDS> asteroids{};
        std::array<space::Particle, space::MAX_PARTICLES> particles{};
        space::StarField star_field{};
        mxvk::VKAbstractModel ship_model{};
        std::array<mxvk::VKAbstractModel, MAX_ASTEROIDS> asteroid_models{};
        mxvk::VK_Sprite3D *star_sprite = nullptr;
        mxvk::VK_Sprite3D *projectile_sprite = nullptr;
        std::array<mxvk::VK_Sprite3D *, UFO_ANIMATION_FRAMES> ufo_sprites{};
        mxvk::VK_Sprite3D *effect_sprite = nullptr;
        mxvk::VK_Sprite *intro_sprite = nullptr;
        mxvk::VK_Sprite *fade_overlay_sprite = nullptr;
        std::unique_ptr<matrix::Rain> intro_rain{};
        glm::vec3 camera_position{0.0f, CAMERA_HEIGHT, CAMERA_DISTANCE};
        glm::mat4 last_ship_model_matrix{1.0f};
        VkBuffer flame_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory flame_vertex_buffer_memory = VK_NULL_HANDLE;
        VkPipeline flame_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout flame_pipeline_layout = VK_NULL_HANDLE;
        uint32_t flame_vertex_count = 0;

        void update_ship(float dt) {
            ship.prev_position = ship.position;

            float horizontal_input = 0.0f;
            if (left_pressed && !right_pressed) {
                horizontal_input = -1.0f;
            } else if (right_pressed && !left_pressed) {
                horizontal_input = 1.0f;
            }
            if (horizontal_input != 0.0f) {
                ship_forward_direction = horizontal_input;
            }

            const float target_horizontal_velocity = horizontal_input * 18.0f;
            ship.velocity.x = std::lerp(ship.velocity.x, target_horizontal_velocity, 1.0f - std::exp(-dt * 7.0f));
            ship.current_speed = std::abs(ship.velocity.x);

            float vertical_input = 0.0f;
            if (up_pressed && !down_pressed) {
                vertical_input = 1.0f;
            } else if (down_pressed && !up_pressed) {
                vertical_input = -1.0f;
            }

            const float target_vertical_velocity = vertical_input * 17.0f;
            ship.velocity.y = std::lerp(ship.velocity.y, target_vertical_velocity, 1.0f - std::exp(-dt * 9.0f));
            ship.velocity.z = 0.0f;
            ship.position += ship.velocity * dt;
            ship.position.y = std::clamp(ship.position.y, WORLD_BOTTOM, WORLD_TOP);
            ship.position.z = 0.0f;

            const float bank = std::clamp(-ship.velocity.y * 2.0f, -22.0f, 22.0f);
            const float pitch = std::clamp(ship.velocity.y * 0.9f, -12.0f, 12.0f);
            ship.rotation.x = pitch;
            ship.rotation.y = ship_forward_direction > 0.0f ? -90.0f : 90.0f;
            ship.rotation.z = bank;

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

        void update_barrel_roll(float dt) {
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

        glm::mat4 build_ship_model_matrix() {
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

        glm::mat4 build_asteroid_model_matrix(const Asteroid &asteroid, const mxvk::VKAbstractModel &model_resource) const {
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

        void reset_intro_screen() {
            mode = GameMode::Intro;
            intro_fade = 1.0f;
            intro_last_update_ms = SDL_GetTicks();
            intro_fade_in_start_ms = 0;
            if (intro_rain != nullptr) {
                intro_rain->set_opacity(1.0f);
                intro_rain->reset();
            }
        }

        void start_launch_countdown() {
            mode = GameMode::Countdown;
            countdown_timer = 0;
            countdown_duration = 1000;
            countdown_start_ms = SDL_GetTicks();
            countdown_number = 3;
            launch_timer = 0;
            launch_duration = 1000;
            launch_start_ms = 0;
        }

        void start_intro_fade_in() {
            start_launch_countdown();
            mode = GameMode::IntroFadeIn;
            intro_fade_in_start_ms = SDL_GetTicks();
        }

        void draw_intro(const VkExtent2D &extent) {
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
                intro_rain->update_and_render(*this);
            }
            printText("Press Enter", 24, static_cast<int>(extent.height) - 52, {255, 230, 80, 255});
        }

        void draw_intro_fade_in(const VkExtent2D &extent) {
            const Uint32 elapsed_ms = SDL_GetTicks() - intro_fade_in_start_ms;
            const float progress = std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(INTRO_FADE_IN_DURATION_MS), 0.0f, 1.0f);
            const float alpha = 1.0f - progress;
            if (alpha <= 0.0f) {
                mode = GameMode::Countdown;
                return;
            }

            draw_fade_overlay(extent, alpha);
        }

        void draw_fade_overlay(const VkExtent2D &extent, float alpha) {
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

        void update_camera_scroll(float aspect) {
            const float visible_half_height = std::tan(glm::radians(25.0f)) * CAMERA_DISTANCE;
            const float visible_half_width = visible_half_height * std::max(0.1f, aspect);
            const float scroll_limit = visible_half_width * CAMERA_SCROLL_EDGE_FRACTION;
            const float relative_x = ship.position.x - camera_center_x;

            if (relative_x > scroll_limit) {
                camera_center_x = ship.position.x - scroll_limit;
            } else if (relative_x < -scroll_limit) {
                camera_center_x = ship.position.x + scroll_limit;
            }
        }

        void update_countdown() {
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
                fire_pressed = false;
            }
        }

        void draw_countdown(VkCommandBuffer cmd, uint32_t image_index, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection) {
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

        void init_ufos() {
            for (auto &ufo : ufos) {
                ufo.active = false;
                ufo.respawn_timer = space::random_float(0.2f, 2.0f);
            }
        }

        void init_asteroids() {
            for (auto &asteroid : asteroids) {
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(0.6f, 4.5f);
            }
        }

        void respawn_ufo(Ufo &ufo, bool initial_spawn = false) {
            const float side = space::random_float(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
            const float distance = initial_spawn ? space::random_float(-52.0f, 52.0f) : side * space::random_float(34.0f, 56.0f);
            ufo.position = glm::vec3(camera_center_x + distance, space::random_float(WORLD_BOTTOM + 1.2f, WORLD_TOP - 1.0f), space::random_float(-1.2f, 1.2f));
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
            ufo.respawn_timer = 0.0f;
            ufo.active = true;
        }

        void update_ufos(float dt) {
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

        void respawn_asteroid(Asteroid &asteroid, bool initial_spawn = false) {
            const float side = space::random_float(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
            const float distance = initial_spawn ? space::random_float(-58.0f, 58.0f) : side * space::random_float(36.0f, 68.0f);
            asteroid.scale = space::random_float(0.72f, 2.15f);
            asteroid.collision_radius = asteroid.scale * 1.25f;
            asteroid.position = glm::vec3(camera_center_x + distance, space::random_float(WORLD_BOTTOM + 1.0f, WORLD_TOP - 1.0f), space::random_float(-1.7f, 1.3f));
            asteroid.velocity = glm::vec3(-side * space::random_float(1.8f, 5.6f), space::random_float(-0.55f, 0.55f), 0.0f);
            asteroid.rotation = glm::vec3(
                space::random_float(0.0f, 360.0f),
                space::random_float(0.0f, 360.0f),
                space::random_float(0.0f, 360.0f));
            asteroid.angular_velocity = glm::vec3(
                space::random_float(-74.0f, 74.0f),
                space::random_float(-105.0f, 105.0f),
                space::random_float(-62.0f, 62.0f));
            asteroid.respawn_timer = 0.0f;
            asteroid.active = true;
        }

        void update_asteroids(float dt) {
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

        void draw_ufos() {
            for (const auto &ufo : ufos) {
                if (!ufo.active) {
                    continue;
                }
                const float pulse = 1.0f + std::sin(elapsed_seconds * 2.2f + ufo.phase) * 0.04f;
                const int frame = static_cast<int>(std::floor(elapsed_seconds * 12.0f + ufo.phase * 1.7f)) % UFO_ANIMATION_FRAMES;
                ufo_sprites[static_cast<std::size_t>(frame)]->drawSprite(ufo.position, glm::vec2(ufo.base_size * pulse, ufo.base_size * 0.58f * pulse), ufo.tint, 0.0f);
            }
        }

        void draw_asteroids(VkCommandBuffer cmd, uint32_t image_index, const glm::mat4 &view, const glm::mat4 &projection) {
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

        void create_flame_resources() {
            create_flame_mesh();
            create_flame_swapchain_resources();
        }

        void cleanup_flame_resources() {
            cleanup_flame_swapchain_resources();
            if (flame_vertex_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, flame_vertex_buffer, nullptr);
                flame_vertex_buffer = VK_NULL_HANDLE;
            }
            if (flame_vertex_buffer_memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, flame_vertex_buffer_memory, nullptr);
                flame_vertex_buffer_memory = VK_NULL_HANDLE;
            }
            flame_vertex_count = 0;
        }

        void cleanup_flame_swapchain_resources() {
            if (flame_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, flame_pipeline, nullptr);
                flame_pipeline = VK_NULL_HANDLE;
            }
            if (flame_pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, flame_pipeline_layout, nullptr);
                flame_pipeline_layout = VK_NULL_HANDLE;
            }
        }

        void create_flame_swapchain_resources() {
            if (flame_vertex_count == 0 || device == VK_NULL_HANDLE) {
                return;
            }
            create_flame_pipeline();
        }

        void create_flame_mesh() {
            constexpr int segments = 40;
            constexpr float base_z = 0.555f;
            constexpr float tip_z = 1.18f;
            constexpr float base_y = 0.040f;
            constexpr float outer_radius = 0.072f;
            constexpr float inner_radius = 0.034f;

            std::vector<space::FlameVertex> vertices{};
            vertices.reserve(static_cast<std::size_t>(segments) * 6U);

            const glm::vec4 outer_base_color{1.0f, 0.42f, 0.08f, 0.62f};
            const glm::vec4 outer_tip_color{0.7f, 0.08f, 0.0f, 0.0f};
            const glm::vec4 inner_base_color{1.0f, 0.94f, 0.52f, 0.86f};
            const glm::vec4 inner_tip_color{1.0f, 0.32f, 0.04f, 0.0f};

            auto add_cone = [&](float radius, const glm::vec4 &base_color, const glm::vec4 &tip_color) {
                const glm::vec3 tip{0.0f, base_y, tip_z};
                for (int i = 0; i < segments; ++i) {
                    const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * space::PI;
                    const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * space::PI;
                    const glm::vec3 p0{std::cos(a0) * radius, base_y + std::sin(a0) * radius, base_z};
                    const glm::vec3 p1{std::cos(a1) * radius, base_y + std::sin(a1) * radius, base_z};
                    vertices.push_back({p0, base_color});
                    vertices.push_back({p1, base_color});
                    vertices.push_back({tip, tip_color});
                }
            };

            add_cone(outer_radius, outer_base_color, outer_tip_color);
            add_cone(inner_radius, inner_base_color, inner_tip_color);

            flame_vertex_count = static_cast<uint32_t>(vertices.size());
            const VkDeviceSize buffer_size = sizeof(space::FlameVertex) * static_cast<VkDeviceSize>(vertices.size());
            create_buffer(buffer_size,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          flame_vertex_buffer,
                          flame_vertex_buffer_memory);

            void *data = nullptr;
            if (vkMapMemory(device, flame_vertex_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS || data == nullptr) {
                throw mxvk::Exception("Failed to map defender flame vertex buffer");
            }
            std::memcpy(data, vertices.data(), static_cast<std::size_t>(buffer_size));
            vkUnmapMemory(device, flame_vertex_buffer_memory);
        }

        void create_flame_pipeline() {
            cleanup_flame_swapchain_resources();

            const std::vector<char> vert_shader_code = loadSpv(std::string(DEFENDER_SHADER_DIR) + "/flame.vert.spv");
            const std::vector<char> frag_shader_code = loadSpv(std::string(DEFENDER_SHADER_DIR) + "/flame.frag.spv");

            VkShaderModule vert_shader_module = createShaderModule(device, vert_shader_code);
            VkShaderModule frag_shader_module = VK_NULL_HANDLE;

            try {
                frag_shader_module = createShaderModule(device, frag_shader_code);

                VkPipelineShaderStageCreateInfo vert_stage{};
                vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vert_stage.module = vert_shader_module;
                vert_stage.pName = "main";

                VkPipelineShaderStageCreateInfo frag_stage{};
                frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                frag_stage.module = frag_shader_module;
                frag_stage.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

                VkVertexInputBindingDescription binding_description{};
                binding_description.binding = 0;
                binding_description.stride = sizeof(space::FlameVertex);
                binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(space::FlameVertex, pos);
                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[1].offset = offsetof(space::FlameVertex, color);

                VkPipelineVertexInputStateCreateInfo vertex_input{};
                vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input.vertexBindingDescriptionCount = 1;
                vertex_input.pVertexBindingDescriptions = &binding_description;
                vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertex_input.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo input_assembly{};
                input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                input_assembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewport_state{};
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;

                const std::array<VkDynamicState, 2> dynamic_states = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamic_info{};
                dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
                dynamic_info.pDynamicStates = dynamic_states.data();

                VkPipelineRasterizationStateCreateInfo rasterizer{};
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE;
                rasterizer.rasterizerDiscardEnable = VK_FALSE;
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_NONE;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;

                VkPipelineMultisampleStateCreateInfo multisampling{};
                multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampling.sampleShadingEnable = VK_FALSE;
                multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineDepthStencilStateCreateInfo depth_stencil{};
                depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil.depthTestEnable = VK_TRUE;
                depth_stencil.depthWriteEnable = VK_FALSE;
                depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depth_stencil.depthBoundsTestEnable = VK_FALSE;
                depth_stencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment.blendEnable = VK_TRUE;
                color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
                color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo color_blending{};
                color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                color_blending.logicOpEnable = VK_FALSE;
                color_blending.attachmentCount = 1;
                color_blending.pAttachments = &color_blend_attachment;

                VkPushConstantRange push_range{};
                push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                push_range.offset = 0;
                push_range.size = sizeof(space::FlamePushConstants);

                VkPipelineLayoutCreateInfo layout_info{};
                layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layout_info.pushConstantRangeCount = 1;
                layout_info.pPushConstantRanges = &push_range;

                if (vkCreatePipelineLayout(device, &layout_info, nullptr, &flame_pipeline_layout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create defender flame pipeline layout");
                }

                const VkFormat color_format = getSwapchainFormat();
                const VkFormat depth_format = getDepthFormat();

                VkPipelineRenderingCreateInfo rendering_info{};
                rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                rendering_info.colorAttachmentCount = 1;
                rendering_info.pColorAttachmentFormats = &color_format;
                if (depth_format != VK_FORMAT_UNDEFINED) {
                    rendering_info.depthAttachmentFormat = depth_format;
                }

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.pNext = &rendering_info;
                pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
                pipeline_info.pStages = shader_stages.data();
                pipeline_info.pVertexInputState = &vertex_input;
                pipeline_info.pInputAssemblyState = &input_assembly;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterizer;
                pipeline_info.pMultisampleState = &multisampling;
                pipeline_info.pDepthStencilState = &depth_stencil;
                pipeline_info.pColorBlendState = &color_blending;
                pipeline_info.pDynamicState = &dynamic_info;
                pipeline_info.layout = flame_pipeline_layout;
                pipeline_info.renderPass = VK_NULL_HANDLE;
                pipeline_info.subpass = 0;

                if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &flame_pipeline) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create defender flame pipeline");
                }
            } catch (...) {
                if (frag_shader_module != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, frag_shader_module, nullptr);
                }
                vkDestroyShaderModule(device, vert_shader_module, nullptr);
                cleanup_flame_swapchain_resources();
                throw;
            }

            vkDestroyShaderModule(device, frag_shader_module, nullptr);
            vkDestroyShaderModule(device, vert_shader_module, nullptr);
        }

        void draw_engine_flame(VkCommandBuffer cmd, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection) {
            if (ship.current_speed <= 1.0f || flame_pipeline == VK_NULL_HANDLE || flame_vertex_buffer == VK_NULL_HANDLE || flame_vertex_count == 0) {
                return;
            }

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            space::FlamePushConstants pc{};
            pc.mvp = projection * view * last_ship_model_matrix;
            pc.params = glm::vec4(elapsed_seconds, std::clamp(ship.current_speed / ship.max_speed, 0.0f, 1.0f), 0.0f, 0.0f);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flame_pipeline);
            vkCmdPushConstants(cmd,
                               flame_pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(pc),
                               &pc);

            VkBuffer vertex_buffers[] = {flame_vertex_buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
            vkCmdDraw(cmd, flame_vertex_count, 1, 0, 0);
        }

        void create_buffer(VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties,
                           VkBuffer &buffer,
                           VkDeviceMemory &buffer_memory) const {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = size;
            buffer_info.usage = usage;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create defender buffer");
            }

            VkMemoryRequirements mem_requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_requirements.size;
            alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

            if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to allocate defender buffer memory");
            }
            if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS) {
                vkFreeMemory(device, buffer_memory, nullptr);
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
                buffer_memory = VK_NULL_HANDLE;
                throw mxvk::Exception("Failed to bind defender buffer memory");
            }
        }

        [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties mem_properties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

            for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
                if ((type_filter & (1U << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw mxvk::Exception("Failed to find defender memory type");
        }

        void fire_projectile() {
            const glm::vec3 forward{ship_forward_direction, 0.0f, 0.0f};
            const glm::vec3 muzzle = ship.position + forward * 1.35f + glm::vec3(0.0f, -0.22f, 0.0f);

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
                return;
            }
        }

        void update_projectiles(float dt) {
            for (auto &projectile : projectiles) {
                if (!projectile.active) {
                    continue;
                }
                projectile.prev_position = projectile.position;
                projectile.position += projectile.velocity * dt;
                projectile.lifetime += dt;
                if (projectile.lifetime >= PROJECTILE_LIFETIME) {
                    projectile.active = false;
                }
            }
        }

        void check_projectile_ufo_hits() {
            constexpr float beam_collision_length = 7.4f;
            constexpr float ufo_hit_half_height = 0.62f;

            for (auto &projectile : projectiles) {
                if (!projectile.active) {
                    continue;
                }

                const float direction = projectile.velocity.x >= 0.0f ? 1.0f : -1.0f;
                const float x0 = projectile.prev_position.x;
                const float x1 = projectile.position.x + direction * beam_collision_length;
                const float min_x = std::min(x0, x1);
                const float max_x = std::max(x0, x1);

                for (auto &ufo : ufos) {
                    if (!ufo.active) {
                        continue;
                    }
                    const float half_width = ufo.base_size * 0.58f;
                    const bool overlaps_x = (ufo.position.x + half_width) >= min_x && (ufo.position.x - half_width) <= max_x;
                    const bool overlaps_y = std::abs(ufo.position.y - projectile.position.y) <= ufo_hit_half_height;
                    if (!overlaps_x || !overlaps_y) {
                        continue;
                    }

                    spawn_ufo_explosion(ufo.position);
                    ufo.active = false;
                    ufo.respawn_timer = space::random_float(1.2f, 2.8f);
                    projectile.active = false;
                    score += 5;
                    break;
                }
            }
        }

        void check_ship_ufo_collisions() {
            constexpr float ship_half_width = 1.75f;
            constexpr float ship_half_height = 0.85f;

            for (auto &ufo : ufos) {
                if (!ufo.active) {
                    continue;
                }

                const float ufo_half_width = ufo.base_size * 0.58f;
                const float ufo_half_height = ufo.base_size * 0.34f;
                const bool overlaps_x = std::abs(ufo.position.x - ship.position.x) <= ship_half_width + ufo_half_width;
                const bool overlaps_y = std::abs(ufo.position.y - ship.position.y) <= ship_half_height + ufo_half_height;
                if (!overlaps_x || !overlaps_y) {
                    continue;
                }

                spawn_ufo_explosion(ufo.position);
                spawn_ufo_explosion(ship.position);
                ufo.active = false;
                ufo.respawn_timer = space::random_float(1.5f, 3.0f);
                lose_life();
                break;
            }
        }

        void check_ship_asteroid_collisions() {
            constexpr float ship_half_width = 1.75f;
            constexpr float ship_half_height = 0.85f;

            for (auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }

                const float closest_x = std::clamp(asteroid.position.x, ship.position.x - ship_half_width, ship.position.x + ship_half_width);
                const float closest_y = std::clamp(asteroid.position.y, ship.position.y - ship_half_height, ship.position.y + ship_half_height);
                const glm::vec2 delta{asteroid.position.x - closest_x, asteroid.position.y - closest_y};
                if (glm::dot(delta, delta) > asteroid.collision_radius * asteroid.collision_radius) {
                    continue;
                }

                spawn_ufo_explosion(asteroid.position);
                spawn_ufo_explosion(ship.position);
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(1.0f, 2.5f);
                lose_life();
                break;
            }
        }

        void lose_life() {
            --lives;
            clear_projectiles();
            ship.visible = false;
            if (lives <= 0) {
                lives = 0;
                game_over = true;
                ship.velocity = glm::vec3(0.0f);
                ship.current_speed = 0.0f;
                return;
            }

            ship_respawning = true;
            respawn_timer = 2.2f;
            ship.velocity = glm::vec3(0.0f);
            ship.current_speed = 0.0f;
        }

        void update_respawn(float dt) {
            respawn_timer -= dt;
            if (respawn_timer > 0.0f) {
                return;
            }

            reset_ship_to_origin();
            ship_respawning = false;
            start_launch_countdown();
        }

        void reset_ship_to_origin() {
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

        void reset_ufos_for_origin_start() {
            ufos_enabled = false;
            for (auto &ufo : ufos) {
                ufo.active = false;
                ufo.respawn_timer = space::random_float(0.4f, 4.0f);
            }
            asteroids_enabled = false;
            for (auto &asteroid : asteroids) {
                asteroid.active = false;
                asteroid.respawn_timer = space::random_float(0.7f, 4.8f);
            }
        }

        void restart_game() {
            score = 0;
            lives = 5;
            game_over = false;
            ship_respawning = false;
            respawn_timer = 0.0f;
            left_pressed = false;
            right_pressed = false;
            up_pressed = false;
            down_pressed = false;
            roll_left_pressed = false;
            roll_right_pressed = false;
            fire_pressed = false;
            clear_projectiles();
            clear_particles();
            reset_ship_to_origin();
            start_launch_countdown();
        }

        void clear_projectiles() {
            for (auto &projectile : projectiles) {
                projectile.active = false;
            }
            ship.fire_cooldown = 0;
        }

        void draw_projectiles() {
            for (const auto &projectile : projectiles) {
                if (!projectile.active) {
                    continue;
                }
                const float life_factor = std::clamp(1.0f - (projectile.lifetime / PROJECTILE_LIFETIME), 0.0f, 1.0f);
                const float flash = (std::sin(elapsed_seconds * 95.0f) > 0.0f) ? 1.0f : 0.48f;
                const float beam_length = 6.4f + 1.2f * life_factor;
                const float core_thickness = 0.085f + 0.025f * flash;
                const float glow_thickness = 0.30f + 0.10f * flash;
                const glm::vec3 beam_center_offset{ship_forward_direction * beam_length * 0.38f, 0.0f, 0.0f};
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

                projectile_sprite->drawSprite(projectile.position + beam_center_offset, glm::vec2(beam_length, glow_thickness), glow_color);
                projectile_sprite->drawSprite(projectile.position + beam_center_offset, glm::vec2(beam_length, core_thickness), core_color);
            }
        }

        void spawn_ufo_explosion(const glm::vec3 &position) {
            struct ExplosionWave {
                float min_speed;
                float max_speed;
                float min_size;
                float max_size;
                float min_lifetime;
                float max_lifetime;
                glm::vec3 color;
            };

            constexpr std::array<ExplosionWave, 4> waves = {
                ExplosionWave{24.0f, 42.0f, 0.42f, 0.78f, 0.75f, 1.35f, {1.0f, 1.0f, 1.0f}},
                ExplosionWave{16.0f, 32.0f, 0.32f, 0.58f, 0.90f, 1.60f, {1.0f, 0.86f, 0.34f}},
                ExplosionWave{10.0f, 24.0f, 0.20f, 0.42f, 1.05f, 1.85f, {1.0f, 0.42f, 0.08f}},
                ExplosionWave{5.0f, 16.0f, 0.12f, 0.26f, 1.20f, 2.10f, {0.75f, 0.90f, 1.0f}},
            };

            for (const ExplosionWave &wave : waves) {
                for (int i = 0; i < 60; ++i) {
                    space::Particle *particle = find_free_particle();
                    if (particle == nullptr) {
                        return;
                    }
                    const glm::vec3 dir = space::normalize_or_zero(glm::vec3(
                        space::random_float(-1.0f, 1.0f),
                        space::random_float(-0.75f, 0.75f),
                        space::random_float(-0.35f, 0.35f)));
                    particle->position = position + dir * space::random_float(0.1f, 0.8f);
                    particle->velocity = dir * space::random_float(wave.min_speed, wave.max_speed);
                    particle->color = glm::vec4(
                        wave.color.r * space::random_float(0.9f, 1.1f),
                        wave.color.g * space::random_float(0.9f, 1.1f),
                        wave.color.b * space::random_float(0.9f, 1.1f),
                        0.1f);
                    particle->size = space::random_float(wave.min_size, wave.max_size);
                    particle->lifetime = 0.0f;
                    particle->max_lifetime = space::random_float(wave.min_lifetime, wave.max_lifetime);
                    particle->active = true;
                }
            }
        }

        space::Particle *find_free_particle() {
            for (auto &particle : particles) {
                if (!particle.active) {
                    return &particle;
                }
            }
            return nullptr;
        }

        void update_particles(float dt) {
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

        void draw_particles() {
            for (const auto &particle : particles) {
                if (!particle.active) {
                    continue;
                }
                effect_sprite->drawSprite(particle.position, glm::vec2(particle.size), particle.color);
            }
        }

        void clear_particles() {
            for (auto &particle : particles) {
                particle.active = false;
            }
        }

        void draw_hud(const VkExtent2D &extent) {
            const SDL_Color white{255, 255, 255, 255};
            const SDL_Color yellow{255, 230, 80, 255};
            const SDL_Color red{255, 70, 60, 255};
            printText("Score: " + std::to_string(score), 24, 22, white);
            printText("Lives: " + std::to_string(lives), 24, 52, lives <= 1 ? red : white);

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
    };

} // namespace defender

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        defender::DefenderWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();

    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
