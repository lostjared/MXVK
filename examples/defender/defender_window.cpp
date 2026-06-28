#include "defender_window.hpp"

#include "mxvk/mxvk_exception.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace defender {

    DefenderWindow::DefenderWindow(const std::string &path, int width, int height, bool fullscreen)
        : mxvk::VK_Window("Defender Starfield Demo", width, height, fullscreen, MXVK_VALIDATION),
          asset_root((path.empty() || path == ".") ? std::string(DEFENDER_ASSET_DIR) : path) {
        setClearColor(0.0f, 0.0f, 0.01f, 1.0f);
        setFont(asset_root + "/data/font.ttf", DEFAULT_FONT_SIZE);
        hud_font.reset(asset_root + "/data/font.ttf", HUD_FONT_SIZE);
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
        attachPostProcessingShader(std::string(DEFENDER_SHADER_DIR) + "/crt.frag.spv", 0.0f, 3.0f, 0.5f, 0.002f);
        setPostProcessingShaderTimeEnabled(true);
        matrix::RainConfig intro_rain_config = matrix::make_matrix_rain_config(asset_root, false);
        intro_rain_config.color = "#ff0000";
        intro_rain_config.surface_width = INTRO_RAIN_TEXTURE_WIDTH;
        intro_rain_config.surface_height = INTRO_RAIN_TEXTURE_HEIGHT;
        intro_rain = std::make_unique<matrix::Rain>(*this, std::move(intro_rain_config));
        reset_intro_screen();

        star_sprite = createSprite3D(asset_root + "/data/star.png");
        star_sprite->setDepthTestEnabled(false);
        star_sprite->setDepthWriteEnabled(false);
        star_sprite->setAlphaDiscardThreshold(0.02f);
        star_field.init(STAR_COUNT, 16.0f, 112.0f);
        star_field.setSprite(star_sprite);

        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> terrain_surface(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (terrain_surface == nullptr) {
            throw mxvk::Exception("Failed to create defender terrain surface");
        }
        const SDL_PixelFormatDetails *terrain_format = SDL_GetPixelFormatDetails(terrain_surface->format);
        if (terrain_format == nullptr) {
            throw mxvk::Exception("Failed to query defender terrain surface format");
        }
        SDL_FillSurfaceRect(terrain_surface.get(), nullptr, SDL_MapRGBA(terrain_format, nullptr, 255, 255, 255, 255));
        terrain_sprite = createSprite3D(terrain_surface.get());
        terrain_sprite->setDepthTestEnabled(false);
        terrain_sprite->setDepthWriteEnabled(false);
        terrain_sprite->setAlphaDiscardThreshold(0.01f);

        projectile_sprite = createSprite3D(asset_root + "/data/particle_projectile.png");
        projectile_sprite->setDepthTestEnabled(true);
        projectile_sprite->setDepthWriteEnabled(false);
        projectile_sprite->setAlphaDiscardThreshold(0.05f);

        for (int i = 0; i < UFO_ANIMATION_FRAMES; ++i) {
            const std::string frame_path = asset_root + std::format("/data/ufo_lights_{:02d}.png", i);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> saucer_surface(space::load_color_keyed_png(frame_path, 8, 24), SDL_DestroySurface);
            ufo_sprite_bounds[static_cast<std::size_t>(UfoSpriteSet::Classic)][static_cast<std::size_t>(i)] = calculate_alpha_bounds(saucer_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Classic)][static_cast<std::size_t>(i)] = createSprite3D(saucer_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Classic)][static_cast<std::size_t>(i)]->setDepthTestEnabled(true);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Classic)][static_cast<std::size_t>(i)]->setDepthWriteEnabled(false);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Classic)][static_cast<std::size_t>(i)]->setAlphaDiscardThreshold(0.02f);

            const std::string ufox_frame_path = asset_root + std::format("/data/ufox{}.png", i + 1);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> ufox_surface(load_light_background_png(ufox_frame_path), SDL_DestroySurface);
            ufo_sprite_bounds[static_cast<std::size_t>(UfoSpriteSet::Ufox)][static_cast<std::size_t>(i)] = calculate_alpha_bounds(ufox_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Ufox)][static_cast<std::size_t>(i)] = createSprite3D(ufox_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Ufox)][static_cast<std::size_t>(i)]->setDepthTestEnabled(true);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Ufox)][static_cast<std::size_t>(i)]->setDepthWriteEnabled(false);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Ufox)][static_cast<std::size_t>(i)]->setAlphaDiscardThreshold(0.02f);

            const std::string alien_frame_path = asset_root + std::format("/data/alien{}.png", i + 1);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> alien_surface(load_light_background_png(alien_frame_path), SDL_DestroySurface);
            ufo_sprite_bounds[static_cast<std::size_t>(UfoSpriteSet::Alien)][static_cast<std::size_t>(i)] = calculate_alpha_bounds(alien_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Alien)][static_cast<std::size_t>(i)] = createSprite3D(alien_surface.get());
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Alien)][static_cast<std::size_t>(i)]->setDepthTestEnabled(true);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Alien)][static_cast<std::size_t>(i)]->setDepthWriteEnabled(false);
            ufo_sprite_sets[static_cast<std::size_t>(UfoSpriteSet::Alien)][static_cast<std::size_t>(i)]->setAlphaDiscardThreshold(0.02f);
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

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        background_music = std::make_unique<mxvk::VK_Mixer>();
        background_music_track = background_music->loadMusic(asset_root + "/data/background.ogg");
        crash_sound = background_music->loadWav(asset_root + "/data/crash.wav");
        shoot_sound = background_music->loadWav(asset_root + "/data/shoot.wav");
        takeoff_sound = background_music->loadWav(asset_root + "/data/takeoff.wav");
        asteroid_explosion_sound = background_music->loadWav(asset_root + "/data/asteroid.wav");
        ufo_explosion_sound = background_music->loadWav(asset_root + "/data/ufo.wav");
        ensure_background_music_playing();
#endif
        configure_console();
        open_controller();
    }

    DefenderWindow::~DefenderWindow() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        if (background_music) {
            background_music->stopMusic();
        }
#endif
        cleanup_flame_resources();
        ship_model.cleanup(this);
        for (auto &asteroid_model : asteroid_models) {
            asteroid_model.cleanup(this);
        }
        if (star_sprite != nullptr) {
            star_sprite->cleanup();
        }
        if (terrain_sprite != nullptr) {
            terrain_sprite->cleanup();
        }
        if (projectile_sprite != nullptr) {
            projectile_sprite->cleanup();
        }
        for (auto &sprite_set : ufo_sprite_sets) {
            for (mxvk::VK_Sprite3D *sprite : sprite_set) {
                if (sprite != nullptr) {
                    sprite->cleanup();
                }
            }
        }
        if (effect_sprite != nullptr) {
            effect_sprite->cleanup();
        }
        intro_rain.reset();
    }

    void DefenderWindow::event(SDL_Event &e) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (!controller.active()) {
                controller.connectEvent(e);
            }
            sync_controller_connection();
            return;
        }
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            controller.connectEvent(e);
            sync_controller_connection();
            return;
        }

        const bool was_console_visible = console.isVisible();
        const bool is_console_toggle = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3;
        console.handleEvent(e);
        if (is_console_toggle) {
            clear_input_state();
            log_game(console.isVisible() ? "Console opened." : "Console closed.");
            return;
        }
        if (was_console_visible) {
            clear_input_state();
            return;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            log_game("Exit requested.");
            exit();
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F4 && !e.key.repeat) {
            show_fps_counter = !show_fps_counter;
            log_game(std::string("FPS counter ") + (show_fps_counter ? "enabled." : "disabled."));
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F8 && !e.key.repeat) {
            crt_enabled = !crt_enabled;
            setPostProcessingEnabled(crt_enabled);
            log_game(std::string("CRT effect ") + (crt_enabled ? "enabled." : "disabled."));
            return;
        }
        if (mode == GameMode::Intro && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
            intro_fade = 0.01f;
            log_game("Intro skipped from keyboard.");
            return;
        }
        if (game_over && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
            restart_game();
            log_game("Game restarted from keyboard.");
            return;
        }

        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                log_game("Exit requested from controller.");
                exit();
                return;
            }
            if (mode == GameMode::Intro && (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START)) {
                intro_fade = 0.01f;
                log_game("Intro skipped from controller.");
                return;
            }
            if (game_over && (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START)) {
                restart_game();
                log_game("Game restarted from controller.");
                return;
            }
            if (mode == GameMode::Playing && !game_over && !ship_respawning) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    fire_pressed = true;
                } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    reverse_pressed = true;
                }
            }
        }

        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            const bool pressed = e.type == SDL_EVENT_KEY_DOWN;
            const bool accepts_ship_thrust = mode == GameMode::Playing && !game_over && !ship_respawning;
            if (e.key.key == SDLK_A) {
                roll_left_pressed = pressed;
            } else if (e.key.key == SDLK_S) {
                roll_right_pressed = pressed;
            } else if (e.key.key == SDLK_X) {
                if (accepts_ship_thrust && pressed && !e.key.repeat) {
                    reverse_pressed = true;
                } else if (!accepts_ship_thrust) {
                    reverse_pressed = false;
                }
            } else if (e.key.key == SDLK_Z) {
                propulsion_pressed = accepts_ship_thrust && pressed;
            } else if (e.key.key == SDLK_D) {
                boost_pressed = accepts_ship_thrust && pressed;
            } else if (e.key.key == SDLK_UP || e.key.key == SDLK_W) {
                up_pressed = pressed;
            } else if (e.key.key == SDLK_DOWN) {
                down_pressed = pressed;
            } else if (e.key.key == SDLK_SPACE && pressed && !e.key.repeat) {
                fire_pressed = true;
            }
        }
    }

    void DefenderWindow::onSwapchainRecreated() {
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
        if (terrain_sprite != nullptr) {
            terrain_sprite->resize(this);
        }
        if (projectile_sprite != nullptr) {
            projectile_sprite->resize(this);
        }
        for (auto &sprite_set : ufo_sprite_sets) {
            for (mxvk::VK_Sprite3D *sprite : sprite_set) {
                if (sprite != nullptr) {
                    sprite->resize(this);
                }
            }
        }
        if (effect_sprite != nullptr) {
            effect_sprite->resize(this);
        }
        cleanup_flame_swapchain_resources();
        create_flame_swapchain_resources();
    }

    void DefenderWindow::onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        ensure_background_music_playing();
#endif
        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
        last_frame_time = now;
        const float dt = std::min(delta_seconds, 0.1f);
        elapsed_seconds += dt;
        update_fps_counter(delta_seconds);

        const bool console_visible = console.isVisible();
        if (!console_visible) {
            sync_controller_connection();
            update_controller_input();
            update_barrel_roll(dt);
            if (ship_respawning) {
                update_respawn(dt);
            } else if (mode == GameMode::Playing && !game_over) {
                update_ship(dt);
                update_ufos(dt);
                update_asteroids(dt);
                resolve_enemy_overlaps();
                update_projectiles(dt);
                check_projectile_ufo_hits();
                check_projectile_asteroid_hits();
                check_ship_ufo_collisions();
                check_ship_asteroid_collisions();
                update_level_progress();
            }
        }
        update_particles(dt);

        const VkExtent2D extent = getSwapchainExtent();
        update_playable_world_top(extent);
        const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

        if (mode == GameMode::Intro) {
            draw_intro(extent);
            console.draw();
            return;
        }

        if (mode == GameMode::IntroFadeIn || mode == GameMode::Countdown) {
            update_camera_scroll(aspect);
            update_camera_position(dt);
            const glm::vec3 camera_target{nearest_world_x(camera_center_x, camera_position.x), CAMERA_HEIGHT, 0.0f};

            const glm::mat4 view = glm::lookAt(camera_position, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 400.0f);
            projection[1][1] *= -1.0f;

            draw_countdown(cmd, image_index, extent, view, projection);
            if (mode == GameMode::IntroFadeIn) {
                draw_intro_fade_in(extent);
            }
            console.draw();
            return;
        }

        update_camera_scroll(aspect);

        update_camera_position(dt);
        const glm::vec3 camera_target{nearest_world_x(camera_center_x, camera_position.x), CAMERA_HEIGHT, 0.0f};

        const glm::mat4 view = glm::lookAt(camera_position, camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 400.0f);
        projection[1][1] *= -1.0f;

        VkRect2D gameplay_scissor{};
        gameplay_scissor.offset = {0, std::min(GAME_VIEWPORT_TOP, static_cast<int>(extent.height))};
        gameplay_scissor.extent = {extent.width, extent.height - static_cast<uint32_t>(gameplay_scissor.offset.y)};
        vkCmdSetScissor(cmd, 0, 1, &gameplay_scissor);

        star_field.update(dt, camera_position, elapsed_seconds);
        star_sprite->updateCamera(image_index, view, projection);
        star_field.draw();
        star_sprite->render(cmd, image_index);
        star_sprite->clearQueue();

        terrain_sprite->updateCamera(image_index, view, projection);
        draw_terrain();
        terrain_sprite->render(cmd, image_index);
        terrain_sprite->clearQueue();

        for (auto &sprite_set : ufo_sprite_sets) {
            for (mxvk::VK_Sprite3D *sprite : sprite_set) {
                sprite->updateCamera(image_index, view, projection);
            }
        }
        draw_ufos();
        for (auto &sprite_set : ufo_sprite_sets) {
            for (mxvk::VK_Sprite3D *sprite : sprite_set) {
                sprite->render(cmd, image_index);
                sprite->clearQueue();
            }
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

        VkRect2D full_scissor{};
        full_scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &full_scissor);

        if (!console_visible) {
            draw_hud(extent);
        }
        console.draw();
    }

    void DefenderWindow::ensure_background_music_playing() {
        if (!background_music || background_music_track < 0) {
            return;
        }
        if (!background_music->isMusicPlaying(background_music_track)) {
            if (background_music->playMusic(background_music_track, -1) != 0) {
                throw mxvk::Exception("Could not start Defender background music");
            }
        }
    }

    void DefenderWindow::play_sound(int sound_id) {
        if (!background_music || sound_id < 0) {
            return;
        }
        background_music->playWav(sound_id);
    }

} // namespace defender
