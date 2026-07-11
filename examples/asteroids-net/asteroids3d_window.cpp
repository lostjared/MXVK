#include "asteroids3d_window.hpp"
#include "asteroids3d_types.hpp"
#include "multiplayer.hpp"
#include "rain.hpp"
#include "ship.hpp"
#include "starfield.hpp"

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_console.hpp"
#include "mxvk/mxvk_controller.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace space {

    class Asteroids3DWindow : public mxvk::VK_Window {
      public:
        Asteroids3DWindow(const std::string &path, int width, int height, bool fullscreen, bool enable_vsync, bool enable_crt)
            : mxvk::VK_Window("3D Asteroids", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              asset_root((path.empty() || path == ".") ? std::string(ASTEROIDS3D_ASSET_DIR) : path),
              shader_root(asset_root + "/data"),
              crt_enabled(enable_crt) {
            if (asset_root == ".") {
                asset_root = ASTEROIDS3D_ASSET_DIR;
            }

            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            attachPostProcessingShader(shader_root + "/crt.frag.spv", 0.0f, 3.0f, 0.5f, 0.002f);
            setPostProcessingShaderTimeEnabled(true);
            setPostProcessingEnabled(crt_enabled);
            load_loading_screen_resources();
            configure_console();
            open_controller();
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            load_sound_effects();
            ensure_background_music_playing();
#endif
        }

        ~Asteroids3DWindow() override {
            if (mouse_capture_active) {
                SDL_SetWindowRelativeMouseMode(getSDLWindow(), false);
                mouse_capture_active = false;
            }
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            if (loading_thread.joinable()) {
                loading_thread.join();
            }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (sound_effects) {
                sound_effects->stopMusic();
            }
#endif
            cleanup_flame_resources();
            ship_model.cleanup(this);
            for (auto &model : asteroid_models) {
                model.cleanup(this);
            }
            remote_ship_model.cleanup(this);
            if (star_sprite != nullptr) {
                star_sprite->cleanup();
            }
            if (projectile_sprite != nullptr) {
                projectile_sprite->cleanup();
            }
            if (effect_sprite != nullptr) {
                effect_sprite->cleanup();
            }
            intro_rain.reset();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED ||
                e.type == SDL_EVENT_GAMEPAD_REMOVED ||
                e.type == SDL_EVENT_JOYSTICK_ADDED ||
                e.type == SDL_EVENT_JOYSTICK_REMOVED) {
                if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    controller.connectEvent(e);
                }
                sync_controller_connection();
                return;
            }

            const bool was_console_visible = console.isVisible();
            const bool is_console_toggle = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3;
            console.handleEvent(e);
            if (is_console_toggle) {
                log_game(console.isVisible() ? "Console opened." : "Console closed.");
                sync_mouse_capture();
                return;
            }
            if (was_console_visible) {
                return;
            }

            if (mode == GameMode::Lobby) {
                handle_lobby_event(e);
                return;
            }
            if (mode == GameMode::MatchOver && e.type == SDL_EVENT_KEY_DOWN &&
                (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_ESCAPE)) {
                multiplayer.stop();
                multiplayer_match = false;
                mode = GameMode::Lobby;
                lobby_page = LobbyPage::Main;
                lobby_selection = 0;
                lobby_status = "Choose how you want to play.";
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                if (mode == GameMode::Playing) {
                    log_game("Exit requested while playing.");
                    exit();
                } else if (mode == GameMode::GameOver || mode == GameMode::GameComplete) {
                    exit();
                } else {
                    log_game("Exit requested from intro screen.");
                    exit();
                }
                return;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F8 && !e.key.repeat) {
                crt_enabled = !crt_enabled;
                setPostProcessingEnabled(crt_enabled);
                log_game(std::string("CRT effect ") + (crt_enabled ? "enabled." : "disabled."));
                return;
            }
            if (mode == GameMode::Intro &&
                e.type == SDL_EVENT_KEY_DOWN &&
                (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN)) {
                intro_fade = 0.01f;
                log_game("Intro skipped. Starting game.");
                return;
            }
            if (mode == GameMode::Intro &&
                e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                intro_fade = 0.01f;
                log_game("Intro skipped from controller. Starting game.");
                return;
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    log_game("Exit requested from controller.");
                    exit();
                    return;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    debug_menu = !debug_menu;
                    log_game(std::string("Debug HUD ") + (debug_menu ? "enabled from controller." : "disabled from controller."));
                    return;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    inverted_controls = !inverted_controls;
                    log_game(std::string("Controls set to ") + (inverted_controls ? "inverted from controller." : "arcade from controller."));
                    return;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST && mode == GameMode::Playing) {
                    restart_game();
                    log_game("Game restarted from controller.");
                    return;
                }
                if ((mode == GameMode::GameOver || mode == GameMode::GameComplete) &&
                    (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || e.gbutton.button == SDL_GAMEPAD_BUTTON_START)) {
                    prepare_restart_from_game_over();
                    log_game("End screen acknowledged from controller. Returning to intro.");
                    return;
                }
            }
            if ((mode == GameMode::GameOver || mode == GameMode::GameComplete) && e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    prepare_restart_from_game_over();
                    log_game("End screen acknowledged from keyboard. Returning to intro.");
                    return;
                }
            }
            if (mode == GameMode::Playing && e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_F1) {
                    debug_menu = !debug_menu;
                    log_game(std::string("Debug HUD ") + (debug_menu ? "enabled." : "disabled."));
                    return;
                }
                if (e.key.key == SDLK_F2) {
                    inverted_controls = !inverted_controls;
                    log_game(std::string("Controls set to ") + (inverted_controls ? "inverted." : "arcade."));
                    return;
                }
                if (e.key.key == SDLK_F5 && !e.key.repeat) {
                    set_mouse_look_controls(!mouse_look_controls);
                    log_game(std::string("Control scheme set to ") + (mouse_look_controls ? "keyboard/mouse." : "classic keyboard."));
                    return;
                }
                if (e.key.key == SDLK_F7 && !e.key.repeat) {
                    begin_camera_transition(!first_person_camera);
                    log_game(std::string("Camera set to ") + (first_person_camera ? "first person." : "chase view."));
                    return;
                }
            }
            if (mode == GameMode::Playing && mouse_look_controls && e.type == SDL_EVENT_MOUSE_MOTION) {
                if (ignore_next_mouse_motion) {
                    ignore_next_mouse_motion = false;
                    return;
                }
                apply_mouse_look(e.motion.xrel, e.motion.yrel);
                return;
            }
            if (mode == GameMode::Playing && mouse_look_controls && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (can_fire()) {
                    fire_projectile();
                }
                return;
            }
        }

        void onSwapchainRecreated() override {
            if (intro_sprite != nullptr) {
                intro_sprite->rebuildPipeline();
            }
            if (intro_rain != nullptr) {
                intro_rain->resize(*this);
            }
            if (!game_resources_loaded.load(std::memory_order_relaxed)) {
                cleanup_flame_swapchain_resources();
                return;
            }
            ship_model.resize(this);
            remote_ship_model.resize(this);
            for (auto &model : asteroid_models) {
                model.resize(this);
            }
            if (star_sprite != nullptr) {
                star_sprite->resize(this);
            }
            if (projectile_sprite != nullptr) {
                projectile_sprite->resize(this);
            }
            if (effect_sprite != nullptr) {
                effect_sprite->resize(this);
            }
            cleanup_flame_swapchain_resources();
            create_flame_swapchain_resources();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            current_command_buffer = cmd;
            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
            last_frame_time = now;
            const float dt = std::min(delta_seconds, 0.1f);
            last_delta_time = dt;
            elapsed_seconds += dt;

            sync_controller_connection();
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            ensure_background_music_playing();
#endif

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

            if (mode == GameMode::Intro) {
                draw_intro(extent);
                console.draw();
                return;
            }

            if (mode == GameMode::Loading) {
                draw_loading(extent);
                console.draw();
                return;
            }

            if (mode == GameMode::Lobby) {
                draw_lobby(cmd, image_index, extent, aspect);
                console.draw();
                return;
            }

            if (mode == GameMode::MatchOver) {
                draw_multiplayer_end(extent);
                console.draw();
                return;
            }

            if (mode == GameMode::GameOver) {
                draw_end_screen(image_index, aspect, "Game over", SDL_Color{235, 60, 60, 255});
                console.draw();
                return;
            }

            if (mode == GameMode::GameComplete) {
                draw_end_screen(image_index, aspect, "Mission complete", SDL_Color{255, 220, 120, 255});
                console.draw();
                return;
            }

            const bool console_visible = console.isVisible();
            sync_mouse_capture();
            if (!multiplayer_match) {
                update_round_timer(dt);
            }
            if (mode == GameMode::GameOver) {
                draw_game_over(image_index, aspect);
                console.draw();
                return;
            }
            if (!console_visible) {
                handle_input(dt);
            }
            update_ship(console_visible ? 0.0f : dt);
            update_projectiles(dt);
            if (multiplayer_match) {
                if (multiplayer.is_host()) {
                    update_asteroids(dt);
                }
                update_multiplayer(dt);
            } else {
                update_asteroids(dt);
            }
            update_particles(dt);

            if (ship.lives <= 0 && !ship.exploding) {
                mode = GameMode::GameOver;
                ship.visible = false;
                if (star_sprite != nullptr) {
                    star_sprite->clearQueue();
                }
                if (projectile_sprite != nullptr) {
                    projectile_sprite->clearQueue();
                }
                if (effect_sprite != nullptr) {
                    effect_sprite->clearQueue();
                }
                draw_game_over(image_index, aspect);
                console.draw();
                return;
            }

            if (!multiplayer_match && mode == GameMode::Playing && active_asteroids() == 0) {
                mode = GameMode::GameComplete;
                ship.visible = false;
                log_game("All asteroids cleared. Mission complete.", SDL_Color{120, 255, 160, 255});
            }

            if (mode == GameMode::GameComplete) {
                if (star_sprite != nullptr) {
                    star_sprite->clearQueue();
                }
                if (projectile_sprite != nullptr) {
                    projectile_sprite->clearQueue();
                }
                if (effect_sprite != nullptr) {
                    effect_sprite->clearQueue();
                }
                draw_end_screen(image_index, aspect, "Mission complete", SDL_Color{255, 220, 120, 255});
                console.draw();
                return;
            }

            update_camera(dt);
            projection_matrix = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 500.0f);
            projection_matrix[1][1] *= -1.0f;

            star_field.update(dt, camera_position, elapsed_seconds);
            star_field.setSprite(star_sprite);

            star_sprite->updateCamera(image_index, view_matrix, projection_matrix);
            projectile_sprite->updateCamera(image_index, view_matrix, projection_matrix);
            effect_sprite->updateCamera(image_index, view_matrix, projection_matrix);

            star_field.draw();
            star_sprite->render(cmd, image_index);
            star_sprite->clearQueue();

            draw_asteroids(image_index);
            if (!first_person_camera && !camera_transition_active) {
                draw_ship(image_index);
                draw_engine_flame(cmd, extent);
            }
            if (multiplayer_match) {
                draw_remote_ship(image_index);
            }
            draw_projectiles();
            if (multiplayer_match) {
                draw_remote_projectiles();
            }
            draw_particles();
            projectile_sprite->render(cmd, image_index);
            projectile_sprite->clearQueue();
            effect_sprite->render(cmd, image_index);
            effect_sprite->clearQueue();

            if (!console.isVisible()) {
                draw_hud(aspect);
            }
            console.draw();
        }

      private:
        std::string asset_root;
        std::string shader_root;
        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        float elapsed_seconds = 0.0f;
        GameMode mode = GameMode::Intro;
        float intro_fade = 1.0f;
        Uint32 intro_last_update_ms = 0;
        float loading_rain_opacity = 1.0f;
        static constexpr int INTRO_RAIN_TEXTURE_WIDTH = 1280;
        static constexpr int INTRO_RAIN_TEXTURE_HEIGHT = 720;
        bool loading_black_frame_pending = false;
        bool loading_black_frame_shown = false;
        bool restart_after_intro = false;
        enum class LobbyPage {
            Main,
            Host,
            Join
        };
        LobbyPage lobby_page = LobbyPage::Main;
        int lobby_selection = 0;
        int lobby_edit_field = -1;
        std::string lobby_player_name = "Pilot 1";
        std::string lobby_host_address = "127.0.0.1";
        std::string lobby_port = "48120";
        std::string lobby_status = "Choose how you want to play.";
        float lobby_camera_distance = 0.0f;
        bool debug_menu = false;
        bool inverted_controls = false;
        bool mouse_look_controls = false;
        bool mouse_capture_active = false;
        bool ignore_next_mouse_motion = false;
        bool first_person_camera = false;
        bool camera_transition_active = false;
        bool crt_enabled = false;
        bool ship_returning_to_field = false;
        float keyboard_yaw = 0.0f;
        float keyboard_pitch = 0.0f;
        float keyboard_roll = 0.0f;
        float smooth_yaw = 0.0f;
        float smooth_pitch = 0.0f;
        float smooth_roll = 0.0f;
        float return_message_cooldown = 0.0f;
        float camera_transition_elapsed = 0.0f;
        static constexpr float CAMERA_TRANSITION_SECONDS = 0.75f;
        static constexpr float MOUSE_LOOK_SENSITIVITY = 0.04f;

        Ship ship{};
        Ship remote_ship{};
        std::array<Projectile, MAX_PROJECTILES> projectiles{};
        std::array<Asteroid, MAX_ASTEROIDS> asteroids{};
        std::array<Particle, MAX_PARTICLES> particles{};
        StarField star_field{};
        glm::vec3 camera_position{0.0f, 1.6f, 6.0f};
        glm::vec3 camera_target_position{0.0f, 1.6f, 0.0f};
        glm::vec3 camera_up_vector{0.0f, 1.0f, 0.0f};
        glm::vec3 camera_transition_start_position{0.0f, 1.6f, 6.0f};
        glm::vec3 camera_transition_start_target{0.0f, 1.6f, 0.0f};
        glm::vec3 camera_transition_start_up{0.0f, 1.0f, 0.0f};
        glm::mat4 view_matrix{1.0f};
        glm::mat4 projection_matrix{1.0f};

        mxvk::VKAbstractModel ship_model{};
        mxvk::VKAbstractModel remote_ship_model{};
        std::array<mxvk::VKAbstractModel, MAX_ASTEROIDS> asteroid_models{};
        mxvk::VK_Sprite3D *star_sprite = nullptr;
        mxvk::VK_Sprite3D *projectile_sprite = nullptr;
        mxvk::VK_Sprite3D *effect_sprite = nullptr;
        mxvk::VK_Sprite *intro_sprite = nullptr;
        mxvk::VK_Sprite *ui_pixel = nullptr;
        std::unique_ptr<matrix::Rain> intro_rain{};
        mxvk::VK_Console console;
        mxvk::VK_Controller controller;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> sound_effects{};
        int background_music_track = -1;
        int crash_sound = -1;
        int cannon_sound = -1;
        int asteroid_explosion_sound = -1;
#endif
        bool console_ready = false;
        std::atomic<bool> game_resources_loaded{false};
        std::atomic<bool> loading_failed{false};
        std::atomic<bool> model_preload_done{false};
        std::atomic<bool> model_preload_failed{false};
        int last_font_size = 0;
        float round_time_remaining = ROUND_TIME_LIMIT_SECONDS;
        std::atomic<int> loading_step_index{0};
        static constexpr int loading_step_count = MAX_ASTEROIDS + 8;
        glm::mat4 last_ship_model_matrix{1.0f};
        VkBuffer flame_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory flame_vertex_buffer_memory = VK_NULL_HANDLE;
        VkPipeline flame_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout flame_pipeline_layout = VK_NULL_HANDLE;
        std::thread loading_thread{};
        std::string loading_error{};
        std::mutex prepared_model_mutex{};
        std::optional<mxvk::MXModel> prepared_ship_model{};
        std::array<std::optional<mxvk::MXModel>, MAX_ASTEROIDS> prepared_asteroid_models{};
        std::array<std::string, MAX_ASTEROIDS> prepared_asteroid_texture_paths{};
        uint32_t flame_vertex_count = 0;
        MultiplayerSession multiplayer{};
        bool multiplayer_match = false;
        std::uint32_t host_kills = 0;
        std::uint32_t client_kills = 0;
        std::uint32_t host_death_serial = 0;
        std::uint32_t client_death_serial = 0;
        std::uint32_t received_host_death_serial = 0;
        std::uint32_t received_client_death_serial = 0;
        std::uint8_t multiplayer_winner = 0;
        bool remote_ship_exploding = false;
        float remote_explosion_timer = 0.0f;
        std::array<NetworkProjectile, NETWORK_PROJECTILE_COUNT> remote_projectiles{};
        std::array<std::uint32_t, MAX_PROJECTILES> projectile_ids{};
        std::uint32_t next_projectile_id = 1;
        std::unordered_set<std::uint32_t> consumed_remote_projectiles{};
        std::uint32_t consumed_client_projectile_id = 0;

        void log_game(const std::string &message, SDL_Color color = SDL_Color{180, 220, 255, 255}) {
            if (!console_ready) {
                return;
            }
            console.printLine("[game] " + message, color);
        }

        void configure_console() {
            console.attach(*this, asset_root + "/data/font.ttf", 20);
            console.setSpriteYOriginTopLeft(true);
            console.setPrompt("asteroids> ");
            console_ready = true;
            console.printLine("Press F3 to open/close the console.");
            console.printLine("Type 'help' for asteroids3d commands.");
            log_game("Console attached.");
            log_game("asteroids3d initialized.");
            console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
                if (args.empty()) {
                    return true;
                }

                const std::string &cmd = args.front();
                if (cmd == "help") {
                    out << "asteroids3d commands:\n"
                        << "  clear              Clear console output\n"
                        << "  echo <text>        Print text to the console\n"
                        << "  status             Print score, lives, mode, and asteroid count\n"
                        << "  restart            Restart the game\n"
                        << "  intro              Return to the intro screen\n"
                        << "  play               Start or resume play\n"
                        << "  debug              Toggle debug HUD\n"
                        << "  controls           Toggle arcade/inverted pitch controls\n"
                        << "  input              Toggle classic keyboard versus keyboard/mouse controls\n"
                        << "  about              Print program banner\n"
                        << "  quit / exit        Close the window\n";
                    return true;
                }

                if (cmd == "echo") {
                    for (std::size_t i = 1; i < args.size(); ++i) {
                        if (i > 1) {
                            out << ' ';
                        }
                        out << args[i];
                    }
                    return true;
                }

                if (cmd == "status") {
                    const char *mode_name = (mode == GameMode::Intro) ? "intro" : (mode == GameMode::Loading)    ? "loading"
                                                                              : (mode == GameMode::Lobby)        ? "lobby"
                                                                              : (mode == GameMode::Playing)      ? "playing"
                                                                              : (mode == GameMode::GameComplete) ? "complete"
                                                                                                                 : "gameover";
                    out << "Mode: " << mode_name << '\n'
                        << "Score: " << ship.score << '\n'
                        << "Lives: " << std::max(0, ship.lives) << '\n'
                        << "Asteroids: " << active_asteroids() << '\n'
                        << "Time left: " << format_round_time() << '\n'
                        << "Speed: " << ship.current_speed << " / " << ship.max_speed << '\n'
                        << "Control scheme: " << (mouse_look_controls ? "keyboard/mouse" : "classic keyboard") << '\n'
                        << "Camera: " << (first_person_camera ? "first person" : "chase") << '\n'
                        << "Controls: " << (inverted_controls ? "inverted" : "arcade") << '\n'
                        << "Controller: " << controller_status() << '\n'
                        << "Debug HUD: " << (debug_menu ? "on" : "off") << '\n';
                    return true;
                }

                if (cmd == "restart") {
                    restart_game();
                    mode = GameMode::Playing;
                    log_game("Game restarted from console.");
                    out << "Game restarted.";
                    return true;
                }

                if (cmd == "intro") {
                    reset_intro_screen();
                    log_game("Returned to intro screen from console.");
                    out << "Intro screen active.";
                    return true;
                }

                if (cmd == "play") {
                    mode = GameMode::Playing;
                    log_game("Play mode activated from console.");
                    out << "Playing.";
                    return true;
                }

                if (cmd == "debug") {
                    debug_menu = !debug_menu;
                    log_game(std::string("Debug HUD ") + (debug_menu ? "enabled from console." : "disabled from console."));
                    out << "Debug HUD " << (debug_menu ? "enabled." : "disabled.");
                    return true;
                }

                if (cmd == "controls") {
                    inverted_controls = !inverted_controls;
                    log_game(std::string("Controls set to ") + (inverted_controls ? "inverted from console." : "arcade from console."));
                    out << "Controls set to " << (inverted_controls ? "inverted." : "arcade.");
                    return true;
                }

                if (cmd == "input") {
                    set_mouse_look_controls(!mouse_look_controls);
                    log_game(std::string("Control scheme set to ") + (mouse_look_controls ? "keyboard/mouse from console." : "classic keyboard from console."));
                    out << "Control scheme set to " << (mouse_look_controls ? "keyboard/mouse." : "classic keyboard.");
                    return true;
                }

                if (cmd == "about") {
                    out << "asteroids3d: MXVK port of gl_asteroids.\n";
                    return true;
                }

                if (cmd == "quit" || cmd == "exit") {
                    log_game("Exit requested from console.");
                    out << "Closing window...";
                    exit();
                    return true;
                }

                return false;
            });
        }

        bool open_controller() {
            for (int i = 0; i < mxvk::VK_Controller::joysticks(); ++i) {
                if (controller.open(i)) {
                    log_game("Controller connected: " + controller.name());
                    return true;
                }
            }
            return false;
        }

        void sync_controller_connection() {
            if (!controller.active()) {
                open_controller();
            }
        }

        std::string controller_status() const {
            return controller.active() ? ("Connected: " + controller.name()) : "Disconnected";
        }

        float controller_axis(SDL_GamepadAxis axis) const {
            if (!controller.active()) {
                return 0.0f;
            }

            const Sint16 raw_value = controller.getAxis(axis);
            const float magnitude = static_cast<float>(std::abs(static_cast<int>(raw_value)));
            if (magnitude <= static_cast<float>(CONTROLLER_DEAD_ZONE)) {
                return 0.0f;
            }

            const float normalized = std::clamp((magnitude - static_cast<float>(CONTROLLER_DEAD_ZONE)) /
                                                    (CONTROLLER_AXIS_MAX - static_cast<float>(CONTROLLER_DEAD_ZONE)),
                                                0.0f,
                                                1.0f);
            const float curved = normalized * normalized;
            return raw_value < 0 ? -curved : curved;
        }

        void set_mouse_look_controls(bool enabled) {
            mouse_look_controls = enabled;
            keyboard_yaw = 0.0f;
            keyboard_pitch = 0.0f;
            keyboard_roll = 0.0f;
            smooth_yaw = 0.0f;
            smooth_pitch = 0.0f;
            smooth_roll = 0.0f;
            sync_mouse_capture();
        }

        void sync_mouse_capture() {
            const bool should_capture = mouse_look_controls && mode == GameMode::Playing && !console.isVisible();
            if (should_capture == mouse_capture_active) {
                return;
            }

            SDL_SetWindowRelativeMouseMode(getSDLWindow(), should_capture);
            mouse_capture_active = should_capture;
            ignore_next_mouse_motion = should_capture;
        }

        void apply_mouse_look(float delta_x, float delta_y) {
            ship.rotation.y -= delta_x * MOUSE_LOOK_SENSITIVITY;
            const float pitch_delta = inverted_controls ? delta_y * MOUSE_LOOK_SENSITIVITY : -delta_y * MOUSE_LOOK_SENSITIVITY;
            ship.rotation.x = std::clamp(ship.rotation.x + pitch_delta, -75.0f, 75.0f);
        }

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::string asteroids3d_asset_path(const std::string &filename) const {
            const std::string local_path = asset_root + "/data/" + filename;
            if (std::filesystem::exists(local_path)) {
                return local_path;
            }
            return std::string(ASTEROIDS3D_SOURCE_DATA_DIR) + "/" + filename;
        }

        std::string sound_effect_path(const std::string &filename) const {
            const std::string local_path = asset_root + "/data/" + filename;
            if (std::filesystem::exists(local_path)) {
                return local_path;
            }
            return std::string(ASTEROIDS3D_DEFENDER_SOUND_DIR) + "/" + filename;
        }

        void load_sound_effects() {
            sound_effects = std::make_unique<mxvk::VK_Mixer>();
            background_music_track = sound_effects->loadMusic(asteroids3d_asset_path("music.ogg"));
            crash_sound = sound_effects->loadWav(sound_effect_path("crash.wav"));
            cannon_sound = sound_effects->loadWav(asteroids3d_asset_path("cannon.wav"));
            asteroid_explosion_sound = sound_effects->loadWav(sound_effect_path("asteroid.wav"));
        }

        void ensure_background_music_playing() {
            if (!sound_effects || background_music_track < 0) {
                return;
            }
            if (!sound_effects->isMusicPlaying(background_music_track)) {
                if (sound_effects->playMusic(background_music_track, -1) != 0) {
                    throw mxvk::Exception("Could not start asteroids3d background music");
                }
            }
        }

        void play_sound(int sound_id) {
            if (!sound_effects || sound_id < 0) {
                return;
            }
            sound_effects->playWav(sound_id);
        }
#endif

        void load_loading_screen_resources() {
            set_ui_font_size(18);

            intro_sprite = createSprite(
                asset_root + "/data/intro.png",
                asset_root + "/data/sprite.vert.spv",
                shader_root + "/intro.frag.spv");
            matrix::RainConfig intro_rain_config = matrix::make_matrix_rain_config(asset_root, false);
            intro_rain_config.color = "#ff0000";
            intro_rain_config.surface_width = INTRO_RAIN_TEXTURE_WIDTH;
            intro_rain_config.surface_height = INTRO_RAIN_TEXTURE_HEIGHT;
            intro_rain = std::make_unique<matrix::Rain>(*this, std::move(intro_rain_config));
            reset_intro_screen();
            loading_step_index.store(0, std::memory_order_relaxed);
            game_resources_loaded.store(false, std::memory_order_relaxed);
            loading_failed.store(false, std::memory_order_relaxed);
        }

        void start_loading_async() {
            if (loading_thread.joinable()) {
                loading_thread.join();
            }
            loading_thread = std::thread([this]() {
                try {
                    preload_models();
                } catch (const std::exception &e) {
                    loading_error = e.what();
                    model_preload_failed.store(true, std::memory_order_release);
                } catch (...) {
                    loading_error = "unknown loading error";
                    model_preload_failed.store(true, std::memory_order_release);
                }
            });
        }

        void preload_models() {
            std::optional<mxvk::MXModel> ship_model_cpu;
            ship_model_cpu.emplace();
            ship_model_cpu->load(asset_root + "/data/starship.obj", 1.0f);

            std::array<std::optional<mxvk::MXModel>, MAX_ASTEROIDS> asteroid_models_cpu{};
            std::array<std::string, MAX_ASTEROIDS> asteroid_texture_paths{};

            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> rock_variant_dist(0, 2);
            for (std::size_t slot_index = 0; slot_index < MAX_ASTEROIDS; ++slot_index) {
                static constexpr std::array<const char *, 3> asteroid_paths = {
                    "data/asteroid.obj",
                    "data/asteroid2.obj",
                    "data/asteroid3.obj",
                };

                const std::size_t model_variant = slot_index % asteroid_paths.size();
                std::string texture_path;
                if (model_variant == 0) {
                    texture_path = asset_root + "/data/rock.tex";
                } else if (model_variant == 1) {
                    texture_path = asset_root + "/data/rock2.tex";
                } else {
                    texture_path = (rock_variant_dist(rng) == 0) ? asset_root + "/data/rock.tex" : asset_root + "/data/rock2.tex";
                }

                asteroid_models_cpu[slot_index].emplace();
                asteroid_models_cpu[slot_index]->load(asset_root + "/" + asteroid_paths[model_variant], 1.0f);
                asteroid_texture_paths[slot_index] = texture_path;
            }

            {
                std::lock_guard<std::mutex> lock(prepared_model_mutex);
                prepared_ship_model = std::move(ship_model_cpu);
                prepared_asteroid_models = std::move(asteroid_models_cpu);
                prepared_asteroid_texture_paths = std::move(asteroid_texture_paths);
            }

            model_preload_done.store(true, std::memory_order_release);
        }

        int lobby_item_count() const {
            if (lobby_page == LobbyPage::Host) {
                return 4;
            }
            if (lobby_page == LobbyPage::Join) {
                return 5;
            }
            return 3;
        }

        void stop_lobby_editing() {
            lobby_edit_field = -1;
            SDL_StopTextInput(getSDLWindow());
        }

        void activate_lobby_item() {
            if (lobby_page == LobbyPage::Main) {
                if (lobby_selection == 0) {
                    lobby_page = LobbyPage::Host;
                    lobby_selection = 0;
                    lobby_status = "Set your pilot name and listen port.";
                } else if (lobby_selection == 1) {
                    lobby_page = LobbyPage::Join;
                    lobby_selection = 0;
                    lobby_status = "Enter the host address to join the match.";
                } else {
                    exit();
                }
                return;
            }

            const bool host_page = lobby_page == LobbyPage::Host;
            const int action_item = host_page ? 2 : 3;
            const int back_item = host_page ? 3 : 4;
            if (lobby_selection < action_item) {
                lobby_edit_field = lobby_selection;
                SDL_StartTextInput(getSDLWindow());
                lobby_status = "Editing field. Press Enter when finished.";
            } else if (lobby_selection == action_item) {
                stop_lobby_editing();
                if (lobby_player_name.empty() || lobby_port.empty() || (!host_page && lobby_host_address.empty())) {
                    lobby_status = "Complete every field before continuing.";
                    return;
                }
                try {
                    if (host_page) {
                        multiplayer.host(lobby_port, lobby_player_name);
                        lobby_status = "Hosting 1v1 on port " + lobby_port + " - waiting for Player 2...";
                    } else {
                        multiplayer.join(lobby_host_address, lobby_port, lobby_player_name);
                        lobby_status = "Connecting to " + lobby_host_address + ":" + lobby_port + "...";
                    }
                } catch (const std::exception &error) {
                    multiplayer.stop();
                    lobby_status = std::string("Network error: ") + error.what();
                }
            } else if (lobby_selection == back_item) {
                stop_lobby_editing();
                lobby_page = LobbyPage::Main;
                lobby_selection = 0;
                lobby_status = "Choose how you want to play.";
            }
        }

        void handle_lobby_event(const SDL_Event &event) {
            if (event.type == SDL_EVENT_TEXT_INPUT && lobby_edit_field >= 0) {
                std::string *field = &lobby_player_name;
                if (lobby_page == LobbyPage::Join && lobby_edit_field == 1) {
                    field = &lobby_host_address;
                } else if ((lobby_page == LobbyPage::Host && lobby_edit_field == 1) ||
                           (lobby_page == LobbyPage::Join && lobby_edit_field == 2)) {
                    field = &lobby_port;
                }
                if (field->size() < 32U) {
                    *field += event.text.text;
                }
                return;
            }

            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (lobby_edit_field >= 0) {
                    std::string *field = &lobby_player_name;
                    if (lobby_page == LobbyPage::Join && lobby_edit_field == 1) {
                        field = &lobby_host_address;
                    } else if ((lobby_page == LobbyPage::Host && lobby_edit_field == 1) ||
                               (lobby_page == LobbyPage::Join && lobby_edit_field == 2)) {
                        field = &lobby_port;
                    }
                    if (event.key.key == SDLK_BACKSPACE && !field->empty()) {
                        field->pop_back();
                    } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                        stop_lobby_editing();
                        lobby_status = "Setup updated.";
                    }
                    return;
                }
                if (event.key.key == SDLK_UP || event.key.key == SDLK_W) {
                    lobby_selection = (lobby_selection + lobby_item_count() - 1) % lobby_item_count();
                } else if (event.key.key == SDLK_DOWN || event.key.key == SDLK_S) {
                    lobby_selection = (lobby_selection + 1) % lobby_item_count();
                } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_SPACE) {
                    activate_lobby_item();
                } else if (event.key.key == SDLK_ESCAPE) {
                    if (lobby_page == LobbyPage::Main) {
                        exit();
                    } else {
                        lobby_page = LobbyPage::Main;
                        lobby_selection = 0;
                        lobby_status = "Choose how you want to play.";
                    }
                }
                return;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                const VkExtent2D extent = getSwapchainExtent();
                const int panel_width = 760;
                const int panel_x = static_cast<int>(extent.width) / 2 - panel_width / 2;
                const int first_y = 250;
                const int item_height = 50;
                const float mouse_x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
                const float mouse_y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
                const int item = (static_cast<int>(mouse_y) - first_y) / item_height;
                if (mouse_x >= panel_x + 20 && mouse_x <= panel_x + panel_width - 20 &&
                    mouse_y >= first_y && item >= 0 && item < lobby_item_count()) {
                    lobby_selection = item;
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                        activate_lobby_item();
                    }
                }
            }
        }

        NetworkState make_network_state(bool match_started) const {
            NetworkState state{};
            state.position = {ship.position.x, ship.position.y, ship.position.z};
            state.rotation = {ship.rotation.x, ship.rotation.y, ship.rotation.z};
            state.exploding = ship.exploding ? 1U : 0U;
            state.match_started = match_started ? 1U : 0U;
            state.host_kills = host_kills;
            state.client_kills = client_kills;
            state.host_death_serial = host_death_serial;
            state.client_death_serial = client_death_serial;
            state.consumed_client_projectile_id = consumed_client_projectile_id;
            state.winner = multiplayer_winner;
            std::size_t output_index = 0;
            for (std::size_t projectile_index = 0; projectile_index < projectiles.size(); ++projectile_index) {
                const Projectile &projectile = projectiles[projectile_index];
                if (!projectile.active || output_index >= state.projectiles.size()) {
                    continue;
                }
                NetworkProjectile &output = state.projectiles[output_index++];
                output.id = projectile_ids[projectile_index];
                output.position = {projectile.position.x, projectile.position.y, projectile.position.z};
                output.velocity = {projectile.velocity.x, projectile.velocity.y, projectile.velocity.z};
                output.lifetime = projectile.lifetime;
                output.active = 1U;
            }
            if (multiplayer.is_host()) {
                std::size_t asteroid_output_index = 0;
                for (std::size_t asteroid_index = 0; asteroid_index < asteroids.size(); ++asteroid_index) {
                    const Asteroid &asteroid = asteroids[asteroid_index];
                    if (!asteroid.active || asteroid_output_index >= state.asteroids.size()) {
                        continue;
                    }
                    NetworkAsteroid &output = state.asteroids[asteroid_output_index++];
                    output.position = {asteroid.position.x, asteroid.position.y, asteroid.position.z};
                    output.rotation = {asteroid.rotation.x, asteroid.rotation.y, asteroid.rotation.z};
                    output.radius = asteroid.radius;
                    output.slot = static_cast<std::uint8_t>(asteroid_index);
                    output.active = 1U;
                }
            }
            return state;
        }

        void apply_remote_state(const NetworkState &state) {
            remote_ship.prev_position = remote_ship.position;
            remote_ship.position = {state.position[0], state.position[1], state.position[2]};
            remote_ship.rotation = {state.rotation[0], state.rotation[1], state.rotation[2]};
            remote_ship.visible = state.exploding == 0U;
            remote_projectiles = state.projectiles;
            if (!multiplayer.is_host()) {
                for (Asteroid &asteroid : asteroids) {
                    asteroid.active = false;
                }
                for (const NetworkAsteroid &network_asteroid : state.asteroids) {
                    if (network_asteroid.active == 0U || network_asteroid.slot >= asteroids.size()) {
                        continue;
                    }
                    Asteroid &asteroid = asteroids[network_asteroid.slot];
                    asteroid.position = {network_asteroid.position[0], network_asteroid.position[1], network_asteroid.position[2]};
                    asteroid.rotation = {network_asteroid.rotation[0], network_asteroid.rotation[1], network_asteroid.rotation[2]};
                    asteroid.radius = network_asteroid.radius;
                    asteroid.active = true;
                }
            }
        }

        void begin_multiplayer_match() {
            multiplayer_match = true;
            host_kills = 0;
            client_kills = 0;
            host_death_serial = 0;
            client_death_serial = 0;
            received_host_death_serial = 0;
            received_client_death_serial = 0;
            multiplayer_winner = 0;
            remote_ship_exploding = false;
            remote_explosion_timer = 0.0f;
            consumed_remote_projectiles.clear();
            consumed_client_projectile_id = 0;
            clear_round_state();
            ship.lives = 99;
            ship.position = multiplayer.is_host() ? glm::vec3(-24.0f, 0.0f, 0.0f) : glm::vec3(24.0f, 0.0f, 0.0f);
            ship.rotation.y = multiplayer.is_host() ? -90.0f : 90.0f;
            ship.prev_position = ship.position;
            remote_ship.position = -ship.position;
            remote_ship.rotation = {0.0f, -ship.rotation.y, 0.0f};
            remote_ship.visible = true;
            if (multiplayer.is_host()) {
                spawn_initial_asteroids();
            } else {
                for (Asteroid &asteroid : asteroids) {
                    asteroid.active = false;
                }
            }
            mode = GameMode::Playing;
            log_game("UDP 1v1 match started against " + multiplayer.peer_name() + ".");
        }

        void update_lobby_network() {
            if (!multiplayer.active()) {
                return;
            }
            const std::optional<NetworkState> remote = multiplayer.exchange(make_network_state(false), last_delta_time);
            if (remote.has_value()) {
                apply_remote_state(*remote);
            }
            if (multiplayer.is_host() && multiplayer.connected()) {
                begin_multiplayer_match();
                multiplayer.exchange(make_network_state(true), 1.0f / 20.0f);
            } else if (!multiplayer.is_host() && remote.has_value() && remote->match_started != 0U) {
                begin_multiplayer_match();
            }
        }

        static float projectile_distance_to_ship(const Projectile &projectile, const glm::vec3 &target) {
            const glm::vec3 segment = projectile.position - projectile.prev_position;
            const float length_squared = glm::dot(segment, segment);
            if (length_squared <= 1e-6f) {
                return glm::length(projectile.position - target);
            }
            const float amount = std::clamp(glm::dot(target - projectile.prev_position, segment) / length_squared, 0.0f, 1.0f);
            return glm::length(projectile.prev_position + segment * amount - target);
        }

        void start_multiplayer_local_explosion() {
            if (ship.exploding) {
                return;
            }
            start_ship_explosion();
            ship.lives = 99;
        }

        void finish_multiplayer_match(std::uint8_t winner) {
            multiplayer_winner = winner;
            multiplayer.exchange(make_network_state(true), 1.0f / 20.0f);
            mode = GameMode::MatchOver;
        }

        void update_multiplayer(float dt) {
            const std::optional<NetworkState> state = multiplayer.exchange(make_network_state(true), dt);
            if (state.has_value()) {
                apply_remote_state(*state);
                if (!multiplayer.is_host()) {
                    host_kills = state->host_kills;
                    client_kills = state->client_kills;
                    multiplayer_winner = state->winner;
                    if (state->consumed_client_projectile_id != 0U) {
                        for (std::size_t projectile_index = 0; projectile_index < projectile_ids.size(); ++projectile_index) {
                            if (projectile_ids[projectile_index] == state->consumed_client_projectile_id) {
                                projectiles[projectile_index].active = false;
                            }
                        }
                    }
                    if (state->client_death_serial != received_client_death_serial) {
                        received_client_death_serial = state->client_death_serial;
                        start_multiplayer_local_explosion();
                    }
                    if (state->host_death_serial != received_host_death_serial) {
                        received_host_death_serial = state->host_death_serial;
                        remote_ship_exploding = true;
                        remote_explosion_timer = 1.5f;
                        spawn_ship_explosion(remote_ship.position);
                    }
                    if (multiplayer_winner != 0U) {
                        mode = GameMode::MatchOver;
                        return;
                    }
                }
            }

            if (remote_ship_exploding) {
                remote_explosion_timer -= dt;
                if (remote_explosion_timer <= 0.0f) {
                    remote_ship_exploding = false;
                }
            }

            if (!multiplayer.is_host() || !state.has_value()) {
                return;
            }

            constexpr float SHIP_TO_SHIP_COLLISION_RADIUS = 2.4f;
            if (!ship.exploding && !remote_ship_exploding && remote_ship.visible &&
                glm::length(ship.position - remote_ship.position) < SHIP_TO_SHIP_COLLISION_RADIUS) {
                const glm::vec3 remote_collision_position = remote_ship.position;
                start_multiplayer_local_explosion();
                remote_ship_exploding = true;
                remote_ship.visible = false;
                remote_explosion_timer = 1.5f;
                ++client_death_serial;
                spawn_ship_explosion(remote_collision_position);
                log_game("Ship collision: both pilots destroyed. No kill awarded.", SDL_Color{255, 170, 80, 255});
                return;
            }

            if (!remote_ship_exploding) {
                for (const Asteroid &asteroid : asteroids) {
                    if (asteroid.active && glm::length(asteroid.position - remote_ship.position) < asteroid.radius + 1.8f) {
                        remote_ship_exploding = true;
                        remote_explosion_timer = 1.5f;
                        ++client_death_serial;
                        spawn_ship_explosion(remote_ship.position);
                        log_game("Enemy collided with an asteroid.", SDL_Color{255, 170, 80, 255});
                        break;
                    }
                }
            }

            for (const NetworkProjectile &projectile : remote_projectiles) {
                if (projectile.active == 0U || consumed_remote_projectiles.contains(projectile.id)) {
                    continue;
                }
                const glm::vec3 projectile_position{projectile.position[0], projectile.position[1], projectile.position[2]};
                for (Asteroid &asteroid : asteroids) {
                    if (asteroid.active && glm::length(projectile_position - asteroid.position) < asteroid.radius * ASTEROID_PROJECTILE_COLLISION_SCALE) {
                        consumed_remote_projectiles.insert(projectile.id);
                        consumed_client_projectile_id = projectile.id;
                        split_asteroid(asteroid);
                        break;
                    }
                }
            }

            if (!remote_ship_exploding) {
                for (Projectile &projectile : projectiles) {
                    if (projectile.active && projectile_distance_to_ship(projectile, remote_ship.position) < 1.8f) {
                        projectile.active = false;
                        remote_ship_exploding = true;
                        remote_explosion_timer = 1.5f;
                        ++host_kills;
                        ++client_death_serial;
                        spawn_ship_explosion(remote_ship.position);
                        log_game(std::format("Enemy destroyed. Score {}-{}.", host_kills, client_kills));
                        if (host_kills >= 5U) {
                            finish_multiplayer_match(1U);
                        }
                        break;
                    }
                }
            }

            if (!ship.exploding) {
                for (const NetworkProjectile &projectile : remote_projectiles) {
                    if (projectile.active == 0U || consumed_remote_projectiles.contains(projectile.id)) {
                        continue;
                    }
                    const glm::vec3 position{projectile.position[0], projectile.position[1], projectile.position[2]};
                    if (glm::length(position - ship.position) < 1.8f) {
                        consumed_remote_projectiles.insert(projectile.id);
                        consumed_client_projectile_id = projectile.id;
                        start_multiplayer_local_explosion();
                        ++client_kills;
                        log_game(std::format("You were destroyed. Score {}-{}.", host_kills, client_kills));
                        if (client_kills >= 5U) {
                            finish_multiplayer_match(2U);
                        }
                        break;
                    }
                }
            }
        }

        void draw_lobby(VkCommandBuffer cmd, uint32_t image_index, const VkExtent2D &extent, float aspect) {
            update_lobby_network();
            lobby_camera_distance += last_delta_time * 4.5f;
            camera_position = {
                std::sin(elapsed_seconds * 0.18f) * 1.8f,
                std::cos(elapsed_seconds * 0.13f) * 0.8f,
                4.0f - lobby_camera_distance,
            };
            const glm::vec3 lobby_camera_target = camera_position + glm::vec3(
                                                                        std::sin(elapsed_seconds * 0.11f) * 0.12f,
                                                                        std::cos(elapsed_seconds * 0.09f) * 0.08f,
                                                                        -1.0f);
            view_matrix = glm::lookAt(camera_position, lobby_camera_target, glm::vec3(0.0f, 1.0f, 0.0f));
            projection_matrix = glm::perspective(glm::radians(55.0f), aspect, 0.1f, 500.0f);
            projection_matrix[1][1] *= -1.0f;
            star_field.update(last_delta_time * 2.0f, camera_position, elapsed_seconds);
            star_field.setSprite(star_sprite);
            star_sprite->updateCamera(image_index, view_matrix, projection_matrix);
            star_field.draw();
            star_sprite->render(cmd, image_index);
            star_sprite->clearQueue();

            const int panel_width = 760;
            const int panel_x = static_cast<int>(extent.width) / 2 - panel_width / 2;
            draw_ui_rect(panel_x, 65, panel_width, 535, {0.015f, 0.025f, 0.08f, 0.90f});
            draw_ui_rect(panel_x, 65, panel_width, 3, {0.20f, 0.72f, 1.0f, 1.0f});
            draw_ui_rect(panel_x, 597, panel_width, 3, {0.20f, 0.72f, 1.0f, 1.0f});

            set_ui_font_size(22);
            printText("ASTEROIDS NET", panel_x + 284, 100, {120, 220, 255, 255});
            printText("MULTIPLAYER 1v1", panel_x + 270, 145, {255, 255, 255, 255});

            std::vector<std::string> labels;
            if (lobby_page == LobbyPage::Main) {
                labels = {"HOST 1v1 MATCH", "JOIN 1v1 MATCH", "QUIT"};
                printText("One pilot hosts. The other pilot connects.", panel_x + 165, 195, {170, 190, 220, 255});
            } else if (lobby_page == LobbyPage::Host) {
                labels = {"PILOT NAME:  " + lobby_player_name, "LISTEN PORT: " + lobby_port, "START HOSTING", "BACK"};
                printText("HOST SETUP", panel_x + 310, 195, {255, 210, 90, 255});
            } else {
                labels = {"PILOT NAME: " + lobby_player_name, "HOST:       " + lobby_host_address, "PORT:       " + lobby_port, "CONNECT", "BACK"};
                printText("JOIN SETUP", panel_x + 315, 195, {100, 255, 170, 255});
            }

            const int first_y = 250;
            for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
                const int y = first_y + index * 50;
                const bool selected = index == lobby_selection;
                draw_ui_rect(panel_x + 20, y, panel_width - 40, 42,
                             selected ? glm::vec4(0.08f, 0.30f, 0.48f, 0.95f) : glm::vec4(0.03f, 0.07f, 0.15f, 0.88f));
                if (selected) {
                    draw_ui_rect(panel_x + 20, y, 5, 42, {0.25f, 0.85f, 1.0f, 1.0f});
                }
                std::string text = labels[index];
                if (lobby_edit_field == index) {
                    text += "_";
                }
                printText(text, panel_x + 40, y + 9, selected ? SDL_Color{255, 255, 255, 255} : SDL_Color{170, 195, 220, 255});
            }

            printText(lobby_status, panel_x + 30, 525, {255, 210, 100, 255});
            printText("Arrow keys / W,S: select    Enter: confirm    Esc: back", panel_x + 30, 560, {135, 155, 185, 255});
        }

        void draw_intro(const VkExtent2D &extent) {
            if (intro_sprite == nullptr) {
                mode = GameMode::Loading;
                return;
            }

            const Uint32 current_ms = SDL_GetTicks();
            if ((current_ms - intro_last_update_ms) > 35U) {
                intro_last_update_ms = current_ms;
                intro_fade -= 0.01f;
            }

            if (intro_fade <= 0.0f) {
                intro_fade = 0.0f;
                if (restart_after_intro) {
                    restart_after_intro = false;
                    restart_game();
                    mode = GameMode::Playing;
                    intro_last_update_ms = SDL_GetTicks();
                    log_game("Intro finished. Restarting game.");
                } else {
                    mode = GameMode::Loading;
                    loading_step_index.store(0, std::memory_order_relaxed);
                    game_resources_loaded.store(false, std::memory_order_relaxed);
                    loading_failed.store(false, std::memory_order_relaxed);
                    model_preload_done.store(false, std::memory_order_relaxed);
                    model_preload_failed.store(false, std::memory_order_relaxed);
                    loading_black_frame_pending = false;
                    loading_black_frame_shown = false;
                    start_loading_async();
                    log_game("Intro finished. Loading game resources.");
                    draw_loading(extent);
                }
                return;
            }

            intro_sprite->setShaderParams(static_cast<float>(current_ms) / 1000.0f, 0.0f, 0.0f, intro_fade);
            intro_sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            if (intro_rain != nullptr) {
                intro_rain->update_and_render(*this, static_cast<int>(extent.width), static_cast<int>(extent.height));
            }
        }

        void draw_loading(const VkExtent2D &extent) {
            if (loading_failed.load(std::memory_order_relaxed) || model_preload_failed.load(std::memory_order_relaxed)) {
                if (intro_rain != nullptr) {
                    intro_rain->set_opacity(0.0f);
                }
                if (loading_thread.joinable()) {
                    loading_thread.join();
                }
                printText("Loading failed", 25, 25, {255, 100, 100, 255});
                return;
            }

            if (!game_resources_loaded.load(std::memory_order_relaxed)) {
                const int loading_progress_percent = std::clamp((loading_step_index.load(std::memory_order_relaxed) * 100) / loading_step_count, 0, 100);
                if (intro_rain != nullptr) {
                    const float target_rain_opacity = 1.0f - (static_cast<float>(loading_progress_percent) / 100.0f);
                    loading_rain_opacity = std::lerp(loading_rain_opacity, target_rain_opacity, 0.25f);
                    intro_rain->set_opacity(loading_rain_opacity);
                    intro_rain->update_and_render(*this, static_cast<int>(extent.width), static_cast<int>(extent.height));
                }
                set_ui_font_size(40);
                printText("Loading " + std::to_string(loading_progress_percent) + "%", 25, 25, {255, 255, 255, 255});
                load_next_game_resource_step();
                return;
            }

            if (loading_black_frame_pending) {
                if (!loading_black_frame_shown) {
                    loading_black_frame_shown = true;
                    if (intro_rain != nullptr) {
                        intro_rain->set_opacity(0.0f);
                    }
                    return;
                }

                loading_black_frame_pending = false;
                loading_black_frame_shown = false;
            }

            if (intro_rain != nullptr) {
                intro_rain->set_opacity(0.0f);
            }
            if (loading_thread.joinable()) {
                loading_thread.join();
            }
            intro_last_update_ms = SDL_GetTicks();
            mode = GameMode::Lobby;
            lobby_page = LobbyPage::Main;
            lobby_selection = 0;
            lobby_status = "Choose how you want to play.";
            log_game("Loading complete. Multiplayer lobby is ready.");
        }

        bool consume_prepared_ship_model(const std::string &model_vert, const std::string &model_frag) {
            std::optional<mxvk::MXModel> model_cpu;
            {
                std::lock_guard<std::mutex> lock(prepared_model_mutex);
                if (!prepared_ship_model.has_value()) {
                    return false;
                }
                model_cpu = std::move(prepared_ship_model);
                prepared_ship_model.reset();
            }

            ship_model.load(this, std::move(*model_cpu), "", asset_root + "/data", 1.0f);
            ship_model.setShaders(this, model_vert, model_frag);
            ship_model.setBackfaceCulling(false);
            return true;
        }

        bool consume_prepared_asteroid_model(std::size_t slot_index, const std::string &model_vert, const std::string &model_frag) {
            std::optional<mxvk::MXModel> model_cpu;
            std::string texture_path;
            {
                std::lock_guard<std::mutex> lock(prepared_model_mutex);
                if (slot_index >= prepared_asteroid_models.size() || !prepared_asteroid_models[slot_index].has_value()) {
                    return false;
                }
                model_cpu = std::move(prepared_asteroid_models[slot_index]);
                prepared_asteroid_models[slot_index].reset();
                texture_path = prepared_asteroid_texture_paths[slot_index];
                prepared_asteroid_texture_paths[slot_index].clear();
            }

            asteroids[slot_index].model_index = static_cast<int>(slot_index % 3U);
            asteroid_models[slot_index].load(this, std::move(*model_cpu), texture_path, asset_root + "/data", 1.0f);
            asteroid_models[slot_index].setShaders(this, model_vert, model_frag);
            asteroid_models[slot_index].setBackfaceCulling(false);
            return true;
        }

        void load_next_game_resource_step() {
            const std::string model_vert = shader_root + "/model.vert.spv";
            const std::string model_frag = shader_root + "/model.frag.spv";

            const int current_step = loading_step_index.load(std::memory_order_relaxed);
            if (current_step == 0) {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> ui_surface(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
                if (ui_surface == nullptr) {
                    throw mxvk::Exception("Failed to create asteroids3d UI pixel surface");
                }
                const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(ui_surface->format);
                if (format_details == nullptr || !SDL_FillSurfaceRect(ui_surface.get(), nullptr, SDL_MapRGBA(format_details, nullptr, 255, 255, 255, 255))) {
                    throw mxvk::Exception("Failed to initialize asteroids3d UI pixel surface");
                }
                ui_pixel = createSprite(ui_surface.get(), "", shader_root + "/fade_overlay.frag.spv");
                if (ui_pixel == nullptr) {
                    throw mxvk::Exception("Failed to create asteroids3d UI pixel sprite");
                }
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> star_surface(load_color_keyed_png(asset_root + "/data/particle_star.png", 12), SDL_DestroySurface);
                star_sprite = createSprite3D(star_surface.get());
                if (star_sprite == nullptr) {
                    throw mxvk::Exception("Failed to create star sprite batch");
                }
                star_sprite->setDepthTestEnabled(false);
                star_sprite->setDepthWriteEnabled(false);
                star_sprite->setAlphaDiscardThreshold(0.01f);
            } else if (current_step == 1) {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> fire_surface(load_color_keyed_png(asset_root + "/data/particle_explosion.png", 12), SDL_DestroySurface);
                projectile_sprite = createSprite3D(fire_surface.get());
                if (projectile_sprite == nullptr) {
                    throw mxvk::Exception("Failed to create projectile sprite batch");
                }
                projectile_sprite->setDepthTestEnabled(true);
                projectile_sprite->setDepthWriteEnabled(false);
                projectile_sprite->setAlphaDiscardThreshold(0.05f);
            } else if (current_step == 2) {
                std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> explosion_surface(load_color_keyed_png(asset_root + "/data/particle_explosion.png", 12), SDL_DestroySurface);
                effect_sprite = createSprite3D(explosion_surface.get());
                if (effect_sprite == nullptr) {
                    throw mxvk::Exception("Failed to create effect sprite batch");
                }
                effect_sprite->setDepthTestEnabled(true);
                effect_sprite->setDepthWriteEnabled(false);
                effect_sprite->setAlphaDiscardThreshold(0.05f);
            } else if (current_step == 3) {
                if (!consume_prepared_ship_model(model_vert, model_frag)) {
                    return;
                }
            } else if (current_step == 4) {
                if (!ship_model.isLoaded()) {
                    return;
                }
                remote_ship_model.load(this, asset_root + "/data/starship.obj", "", asset_root + "/data", 1.0f);
                remote_ship_model.setShaders(this, model_vert, model_frag);
                remote_ship_model.setBackfaceCulling(false);
            } else if (current_step == 5) {
                create_flame_resources();
            } else if (current_step >= 6 && current_step < 6 + MAX_ASTEROIDS) {
                if (!consume_prepared_asteroid_model(static_cast<std::size_t>(current_step - 6), model_vert, model_frag)) {
                    return;
                }
            } else if (current_step == 6 + MAX_ASTEROIDS) {
                star_field.init(GAME_STARS, 4.0f, 30.0f);
            } else if (current_step == 7 + MAX_ASTEROIDS) {
                restart_game();
            } else {
                game_resources_loaded.store(true, std::memory_order_release);
                intro_last_update_ms = SDL_GetTicks();
                loading_rain_opacity = 0.0f;
                loading_black_frame_pending = true;
                loading_black_frame_shown = false;
                if (intro_rain != nullptr) {
                    intro_rain->set_opacity(0.0f);
                }
                return;
            }

            loading_step_index.store(current_step + 1, std::memory_order_release);
            if (loading_step_index.load(std::memory_order_relaxed) >= loading_step_count) {
                game_resources_loaded.store(true, std::memory_order_release);
                intro_last_update_ms = SDL_GetTicks();
                loading_rain_opacity = 0.0f;
                loading_black_frame_pending = true;
                loading_black_frame_shown = false;
                if (intro_rain != nullptr) {
                    intro_rain->set_opacity(0.0f);
                }
                return;
            }
        }

        void load_asteroid_model_slot(std::size_t slot_index, const std::string &model_vert, const std::string &model_frag) {
            static constexpr std::array<const char *, 3> asteroid_paths = {
                "data/asteroid.obj",
                "data/asteroid2.obj",
                "data/asteroid3.obj",
            };

            const std::size_t model_variant = slot_index % asteroid_paths.size();
            std::string texture_path;
            if (model_variant == 0) {
                texture_path = asset_root + "/data/rock.tex";
            } else if (model_variant == 1) {
                texture_path = asset_root + "/data/rock2.tex";
            } else {
                texture_path = (random_int(0, 1) == 0) ? asset_root + "/data/rock.tex" : asset_root + "/data/rock2.tex";
            }

            asteroids[slot_index].model_index = static_cast<int>(model_variant);
            asteroid_models[slot_index].load(
                this,
                asset_root + "/" + asteroid_paths[model_variant],
                texture_path,
                asset_root + "/data",
                1.0f);
            asteroid_models[slot_index].setShaders(this, model_vert, model_frag);
            asteroid_models[slot_index].setBackfaceCulling(false);
        }

        void restart_game() {
            clear_round_state();
            spawn_initial_asteroids();
            round_time_remaining = ROUND_TIME_LIMIT_SECONDS;
            restart_after_intro = false;
            log_game("Game state reset: score=0 lives=5.");
        }

        void clear_round_state() {
            ship.position = glm::vec3(0.0f);
            ship.prev_position = ship.position;
            ship.velocity = glm::vec3(0.0f);
            ship.rotation = glm::vec3(0.0f);
            ship.current_speed = 1.0f;
            ship.visible = true;
            ship.exploding = false;
            ship.explosion_timer = 0;
            ship.lives = 5;
            ship.score = 0;
            ship.fire_cooldown = 0;
            ship.burst_count = 0;
            ship.continuous_fire_timer = 0;
            ship.overheated = false;
            ship.overheat_cooldown = 0;
            for (auto &projectile : projectiles) {
                projectile.active = false;
            }
            for (auto &particle : particles) {
                particle.active = false;
            }
            for (auto &asteroid : asteroids) {
                asteroid.active = false;
            }
            keyboard_yaw = 0.0f;
            keyboard_pitch = 0.0f;
            keyboard_roll = 0.0f;
            smooth_yaw = 0.0f;
            smooth_pitch = 0.0f;
            smooth_roll = 0.0f;
            ship_returning_to_field = false;
            return_message_cooldown = 0.0f;
            set_mouse_look_controls(false);
            first_person_camera = false;
            camera_transition_active = false;
            camera_transition_elapsed = 0.0f;
            camera_position = glm::vec3(0.0f, ship.camera_height, ship.camera_distance);
            camera_target_position = glm::vec3(0.0f, 0.0f, -6.0f);
            camera_up_vector = glm::vec3(0.0f, 1.0f, 0.0f);
            round_time_remaining = ROUND_TIME_LIMIT_SECONDS;
        }

        void spawn_initial_asteroids() {
            for (int i = 0; i < 7; ++i) {
                glm::vec3 position{0.0f};
                do {
                    position = glm::vec3(
                        random_float(-90.0f, 90.0f),
                        random_float(-50.0f, 50.0f),
                        random_float(-90.0f, 90.0f));
                } while (glm::length(position - ship.position) < 28.0f);

                spawn_asteroid(position,
                               glm::vec3(random_float(-0.8f, 0.8f), random_float(-0.8f, 0.8f), random_float(-0.8f, 0.8f)),
                               random_float(2.8f, 7.0f),
                               0,
                               random_int(0, 2));
            }
            log_game("Initial asteroid field spawned.");
        }

        void spawn_asteroid(const glm::vec3 &position, const glm::vec3 &velocity, float radius, int generation, int preferred_model_index = -1) {
            Asteroid *free_asteroid = find_free_asteroid(preferred_model_index);
            if (free_asteroid == nullptr && preferred_model_index >= 0) {
                free_asteroid = find_free_asteroid();
            }

            if (free_asteroid == nullptr) {
                log_game("Asteroid spawn skipped: no free asteroid slots.", SDL_Color{255, 190, 90, 255});
                return;
            }

            const int slot_model_index = free_asteroid->model_index;
            free_asteroid->position = position;
            free_asteroid->velocity = velocity;
            free_asteroid->radius = radius;
            free_asteroid->generation = generation;
            free_asteroid->rotation = glm::vec3(random_float(0.0f, 360.0f), random_float(0.0f, 360.0f), random_float(0.0f, 360.0f));
            free_asteroid->rotation_speed = glm::vec3(random_float(-45.0f, 45.0f), random_float(-45.0f, 45.0f), random_float(-45.0f, 45.0f));
            free_asteroid->model_index = slot_model_index;
            free_asteroid->active = true;
            if (generation == 0) {
                log_game(std::format("Asteroid spawned at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}.", position.x, position.y, position.z, radius));
            }
        }

        void handle_input(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            if (ship.lives <= 0) {
                return;
            }

            if (controller.getButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || controller.getButton(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
                increase_speed(dt);
            } else if (controller.getButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || controller.getButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
                decrease_speed(dt);
            }

            auto ramp_axis = [dt](float &value, float target, float rise_rate, float fall_rate) {
                const float rate = (std::fabs(target) > std::fabs(value)) ? rise_rate : fall_rate;
                const float step = rate * dt;
                if (value < target) {
                    value = std::min(value + step, target);
                } else if (value > target) {
                    value = std::max(value - step, target);
                }
            };

            float yaw_amount = 0.0f;
            float pitch_amount = 0.0f;
            float roll_amount = 0.0f;
            bool manual_roll_input = false;

            const float left_x = controller_axis(SDL_GAMEPAD_AXIS_LEFTX);
            if (std::fabs(left_x) > 0.001f) {
                yaw_amount = -left_x;
            }

            float keyboard_yaw_target = 0.0f;
            if (keys[SDL_SCANCODE_LEFT]) {
                keyboard_yaw_target = 1.0f;
            } else if (keys[SDL_SCANCODE_RIGHT]) {
                keyboard_yaw_target = -1.0f;
            }
            ramp_axis(keyboard_yaw, keyboard_yaw_target, 3.2f, 8.0f);
            if (std::fabs(keyboard_yaw) > 0.001f) {
                yaw_amount = keyboard_yaw;
            }

            float keyboard_pitch_target = 0.0f;
            if (!mouse_look_controls) {
                if (inverted_controls) {
                    if (keys[SDL_SCANCODE_W]) {
                        keyboard_pitch_target = -1.0f;
                    }
                    if (keys[SDL_SCANCODE_S]) {
                        keyboard_pitch_target = 1.0f;
                    }
                } else {
                    if (keys[SDL_SCANCODE_W]) {
                        keyboard_pitch_target = 1.0f;
                    }
                    if (keys[SDL_SCANCODE_S]) {
                        keyboard_pitch_target = -1.0f;
                    }
                }
            }
            ramp_axis(keyboard_pitch, keyboard_pitch_target, 2.8f, 8.0f);
            if (std::fabs(keyboard_pitch) > 0.001f) {
                pitch_amount = keyboard_pitch;
            }

            float keyboard_roll_target = 0.0f;
            if (keys[SDL_SCANCODE_A]) {
                keyboard_roll_target = -1.0f;
            } else if (keys[SDL_SCANCODE_D]) {
                keyboard_roll_target = 1.0f;
            }
            ramp_axis(keyboard_roll, keyboard_roll_target, 3.0f, 8.0f);
            if (std::fabs(keyboard_roll) > 0.001f) {
                roll_amount = keyboard_roll;
                manual_roll_input = true;
            }

            if (controller.getButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                yaw_amount = 1.0f;
            } else if (controller.getButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                yaw_amount = -1.0f;
            }

            const float right_x = controller_axis(SDL_GAMEPAD_AXIS_RIGHTX);
            if (std::fabs(right_x) > 0.001f) {
                roll_amount = right_x;
                manual_roll_input = true;
            }

            const float right_y = controller_axis(SDL_GAMEPAD_AXIS_RIGHTY);
            if (std::fabs(right_y) > 0.001f) {
                pitch_amount = inverted_controls ? right_y : -right_y;
            }

            const float smoothing = std::clamp(dt * 8.0f, 0.0f, 1.0f);
            smooth_yaw = glm::mix(smooth_yaw, yaw_amount, smoothing);
            smooth_pitch = glm::mix(smooth_pitch, pitch_amount, smoothing);
            smooth_roll = glm::mix(smooth_roll, roll_amount, smoothing);

            if (std::fabs(smooth_yaw) > 0.01f) {
                ship.rotation.y += smooth_yaw * ship.turn_speed * dt;
            }
            if (std::fabs(smooth_pitch) > 0.01f) {
                ship.rotation.x += smooth_pitch * ship.turn_speed * ship.pitch_speed_multiplier * dt;
            }
            if (manual_roll_input && std::fabs(smooth_roll) > 0.01f) {
                ship.rotation.z += smooth_roll * ship.turn_speed * dt;
            }
            if (!manual_roll_input && std::fabs(smooth_yaw) > 0.01f) {
                const float target_roll = -smooth_yaw * 35.0f;
                const float roll_diff = target_roll - ship.rotation.z;
                ship.rotation.z += roll_diff * 5.0f * dt;
            }
            if (!manual_roll_input && std::fabs(smooth_yaw) < 0.01f) {
                while (ship.rotation.z > 180.0f) {
                    ship.rotation.z -= 360.0f;
                }
                while (ship.rotation.z < -180.0f) {
                    ship.rotation.z += 360.0f;
                }
                ship.rotation.z = glm::mix(ship.rotation.z, 0.0f, 3.0f * dt);
            }

            const bool speed_up_key = mouse_look_controls ? keys[SDL_SCANCODE_W] : keys[SDL_SCANCODE_UP];
            const bool slow_down_key = mouse_look_controls ? keys[SDL_SCANCODE_S] : keys[SDL_SCANCODE_DOWN];
            if (speed_up_key) {
                increase_speed(dt);
            } else if (slow_down_key) {
                decrease_speed(dt);
            } else {
                if (ship.current_speed > 5.0f) {
                    decrease_speed(dt * 0.5f);
                } else if (ship.current_speed < 5.0f) {
                    increase_speed(dt * 0.5f);
                }
            }

            const bool firing = keys[SDL_SCANCODE_SPACE] ||
                                controller.getButton(SDL_GAMEPAD_BUTTON_SOUTH) ||
                                controller.getAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > CONTROLLER_DEAD_ZONE;
            if (firing) {
                if (can_fire()) {
                    fire_projectile();
                }
            } else {
                update_fire_timer(false);
            }
        }

        void increase_speed(float dt) {
            ship.current_speed += ship.turn_speed * dt * 0.2f;
            ship.current_speed = std::min(ship.current_speed, ship.max_speed);
        }

        void decrease_speed(float dt) {
            ship.current_speed -= ship.turn_speed * dt * 0.2f;
            ship.current_speed = std::max(ship.current_speed, ship.min_speed);
        }

        bool can_fire() {
            if (ship.overheated) {
                return false;
            }
            if (ship.fire_cooldown <= 0) {
                if (ship.burst_count < SHOTS_PER_BURST) {
                    ship.fire_cooldown = FIRE_DELAY;
                    ship.burst_count++;
                    return true;
                }
                ship.fire_cooldown = FIRE_COOLDOWN;
                ship.burst_count = 0;
                return false;
            }
            return false;
        }

        void update_fire_timer(bool firing) {
            if (firing && !ship.overheated) {
                ship.continuous_fire_timer++;
                ship.overheat_cooldown = 0;
                if (ship.continuous_fire_timer >= 180) {
                    ship.overheated = true;
                    ship.overheat_cooldown = 0;
                    ship.continuous_fire_timer = 0;
                    ship.burst_count = 0;
                    log_game("Weapons overheated.", SDL_Color{255, 150, 80, 255});
                }
            } else if (firing && ship.overheated) {
                ship.overheat_cooldown = 0;
            } else {
                if (ship.overheated) {
                    ship.overheat_cooldown++;
                    if (ship.overheat_cooldown >= 180) {
                        ship.overheated = false;
                        ship.overheat_cooldown = 0;
                        ship.continuous_fire_timer = 0;
                        log_game("Weapons cooled down.");
                    }
                } else if (ship.continuous_fire_timer > 0) {
                    ship.continuous_fire_timer--;
                }
            }
        }

        static float normalize_degrees(float degrees) {
            while (degrees > 180.0f) {
                degrees -= 360.0f;
            }
            while (degrees < -180.0f) {
                degrees += 360.0f;
            }
            return degrees;
        }

        static float ease_angle_degrees(float current, float target, float blend) {
            return current + normalize_degrees(target - current) * std::clamp(blend, 0.0f, 1.0f);
        }

        glm::vec3 asteroid_field_center() const {
            glm::vec3 sum{0.0f};
            int count = 0;
            for (const auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }
                sum += asteroid.position;
                ++count;
            }
            if (count == 0) {
                return glm::vec3(0.0f);
            }
            return sum / static_cast<float>(count);
        }

        bool ship_is_outside_return_volume() const {
            constexpr float RETURN_PADDING = 18.0f;
            return ship.position.x < BOUNDARY_X_MIN - RETURN_PADDING ||
                   ship.position.x > BOUNDARY_X_MAX + RETURN_PADDING ||
                   ship.position.y < BOUNDARY_Y_MIN - RETURN_PADDING ||
                   ship.position.y > BOUNDARY_Y_MAX + RETURN_PADDING ||
                   ship.position.z < BOUNDARY_Z_MIN - RETURN_PADDING ||
                   ship.position.z > BOUNDARY_Z_MAX + RETURN_PADDING;
        }

        void update_ship_return_to_field(float dt) {
            if (active_asteroids() == 0) {
                ship_returning_to_field = false;
                return;
            }

            constexpr float RETURN_START_DISTANCE = 145.0f;
            constexpr float RETURN_STOP_DISTANCE = 92.0f;
            const float nearest_distance = nearest_asteroid_distance();
            const bool outside_return_volume = ship_is_outside_return_volume();
            if (!ship_returning_to_field && (outside_return_volume || nearest_distance > RETURN_START_DISTANCE)) {
                ship_returning_to_field = true;
                if (return_message_cooldown <= 0.0f) {
                    log_game("Return assist engaged: steering back toward the asteroid field.", SDL_Color{120, 220, 255, 255});
                    return_message_cooldown = 3.0f;
                }
            } else if (ship_returning_to_field && !outside_return_volume && nearest_distance < RETURN_STOP_DISTANCE) {
                ship_returning_to_field = false;
                log_game("Return assist disengaged.");
            }

            if (!ship_returning_to_field) {
                return;
            }

            const glm::vec3 to_field = normalize_or_zero(asteroid_field_center() - ship.position);
            const float target_yaw = glm::degrees(std::atan2(-to_field.x, -to_field.z));
            const float target_pitch = glm::degrees(std::asin(std::clamp(to_field.y, -1.0f, 1.0f)));
            const float blend = 1.0f - std::exp(-dt * 1.8f);
            ship.rotation.y = ease_angle_degrees(ship.rotation.y, target_yaw, blend);
            ship.rotation.x = ease_angle_degrees(ship.rotation.x, target_pitch, blend);
            ship.rotation.z = ease_angle_degrees(ship.rotation.z, 0.0f, blend * 0.8f);
            ship.current_speed = std::max(ship.current_speed, 8.0f);
        }

        void fire_projectile() {
            const glm::vec3 forward = ship.forward();
            const float muzzle_offset = 0.08f;
            const glm::vec3 muzzle = ship.position + forward * muzzle_offset;
            for (std::size_t projectile_index = 0; projectile_index < projectiles.size(); ++projectile_index) {
                Projectile &projectile = projectiles[projectile_index];
                if (projectile.active) {
                    continue;
                }
                projectile.position = muzzle;
                projectile.prev_position = muzzle;
                projectile.velocity = forward * PROJECTILE_SPEED;
                projectile.color = PROJECTILE_COLOR;
                projectile.lifetime = 0.0f;
                projectile.active = true;
                projectile_ids[projectile_index] = next_projectile_id++;
                if (next_projectile_id == 0U) {
                    next_projectile_id = 1U;
                }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
                play_sound(cannon_sound);
#endif
                log_game(std::format("Projectile fired from ({:.1f}, {:.1f}, {:.1f}).", muzzle.x, muzzle.y, muzzle.z));
                return;
            }
            log_game("Projectile fire skipped: projectile pool full.", SDL_Color{255, 190, 90, 255});
        }

        void update_ship(float dt) {
            if (ship.exploding) {
                ship.explosion_timer--;
                if (ship.explosion_timer <= 0) {
                    ship.exploding = false;
                    ship.visible = true;
                    if (multiplayer_match) {
                        ship.position = multiplayer.is_host() ? glm::vec3(-24.0f, 0.0f, 0.0f) : glm::vec3(24.0f, 0.0f, 0.0f);
                        ship.rotation = {0.0f, multiplayer.is_host() ? -90.0f : 90.0f, 0.0f};
                    } else {
                        ship.position = glm::vec3(0.0f);
                        ship.rotation = glm::vec3(0.0f);
                    }
                    ship.prev_position = ship.position;
                    ship.velocity = glm::vec3(0.0f);
                    ship.current_speed = 1.0f;
                    ship_returning_to_field = false;
                    clear_particles();
                    log_game("Ship respawned at origin.");
                }
                return;
            }

            if (dt <= 0.0f) {
                ship.prev_position = ship.position;
                ship.velocity = glm::vec3(0.0f);
                return;
            }

            if (return_message_cooldown > 0.0f) {
                return_message_cooldown = std::max(0.0f, return_message_cooldown - dt);
            }
            update_ship_return_to_field(dt);
            const glm::vec3 forward = ship.forward();
            ship.prev_position = ship.position;
            ship.velocity = forward * ship.current_speed;
            ship.position += ship.velocity * dt;
            ship.rotation.x = std::clamp(ship.rotation.x, -75.0f, 75.0f);
            if (ship.rotation.z > 180.0f) {
                ship.rotation.z -= 360.0f;
            } else if (ship.rotation.z < -180.0f) {
                ship.rotation.z += 360.0f;
            }
            if (ship.fire_cooldown > 0) {
                ship.fire_cooldown--;
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

            if (multiplayer_match && !multiplayer.is_host()) {
                return;
            }

            for (auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }
                for (auto &projectile : projectiles) {
                    if (!projectile.active) {
                        continue;
                    }

                    const glm::vec3 segment = projectile.position - projectile.prev_position;
                    const float segment_length_sq = glm::dot(segment, segment);

                    glm::vec3 closest_point = projectile.prev_position;

                    if (segment_length_sq > 1e-6f) {
                        const glm::vec3 to_asteroid = asteroid.position - projectile.prev_position;
                        const float t = std::clamp(glm::dot(to_asteroid, segment) / segment_length_sq, 0.0f, 1.0f);
                        closest_point = projectile.prev_position + segment * t;
                    }

                    const float dist = glm::length(closest_point - asteroid.position);
                    const float projectile_hit_radius = asteroid.radius * ASTEROID_PROJECTILE_COLLISION_SCALE;

                    if (dist < projectile_hit_radius) {
                        projectile.active = false;
                        log_game(std::format("Projectile hit asteroid at ({:.1f}, {:.1f}, {:.1f}).", asteroid.position.x, asteroid.position.y, asteroid.position.z));
                        split_asteroid(asteroid);
                        break;
                    }
                }
            }
        }

        void split_asteroid(Asteroid &asteroid) {
            const glm::vec3 hit_position = asteroid.position;
            const int generation = asteroid.generation;
            const float radius = asteroid.radius * 0.5f;

            spawn_asteroid_explosion(hit_position);
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(asteroid_explosion_sound);
#endif

            if (generation >= MAX_GENERATIONS) {
                ship.score += SMALL_ASTEROID_POINTS;
                asteroid.active = false;
                log_game(std::format("Small asteroid destroyed. Score={}.", ship.score));
                return;
            }

            const int child_count = CHILDREN_PER_SPAWN;
            const float child_radius = asteroid.radius * 0.18f;
            const glm::vec3 view_forward = normalize_or_zero(hit_position - camera_position);
            glm::vec3 split_axis = glm::cross(view_forward, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length(split_axis) <= 1e-4f) {
                split_axis = glm::cross(view_forward, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            if (glm::length(split_axis) <= 1e-4f) {
                split_axis = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            split_axis = normalize_or_zero(split_axis);
            const glm::vec3 toward_camera = normalize_or_zero(camera_position - hit_position);
            const float child_separation = std::max(asteroid.radius * 0.45f, child_radius * 2.0f);
            const glm::vec3 depth_bias = toward_camera * (child_separation * 0.12f);
            std::array<glm::vec3, CHILDREN_PER_SPAWN> child_positions = {
                hit_position - split_axis * child_separation + depth_bias,
                hit_position + split_axis * child_separation + depth_bias,
            };

            for (int i = 0; i < child_count; ++i) {
                Asteroid *child = find_free_asteroid(asteroid.model_index);
                if (child == nullptr) {
                    log_game("Asteroid split skipped: no free slot for parent asteroid type.", SDL_Color{255, 190, 90, 255});
                    break;
                }

                const glm::vec3 child_offset = child_positions[static_cast<std::size_t>(i)] - hit_position;
                const glm::vec3 child_velocity = normalize_or_zero(child_offset) * random_float(5.0f, 9.0f);
                child->active = true;
                child->position = child_positions[static_cast<std::size_t>(i)];
                child->radius = child_radius;
                child->generation = generation + 1;
                child->rotation = glm::vec3(random_float(0.0f, 360.0f), random_float(0.0f, 360.0f), random_float(0.0f, 360.0f));
                child->rotation_speed = asteroid.rotation_speed * random_float(0.8f, 1.5f);
                child->model_index = asteroid.model_index;
                child->velocity = child_velocity;
                log_game(std::format(
                    "Asteroid child {} spawned at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}.",
                    i + 1,
                    child->position.x,
                    child->position.y,
                    child->position.z,
                    child->radius));
            }

            if (radius >= 25.0f) {
                ship.score += LARGE_ASTEROID_POINTS;
                log_game(std::format("Large asteroid split into {} pieces. Score={}.", child_count, ship.score));
            } else {
                ship.score += MEDIUM_ASTEROID_POINTS;
                log_game(std::format("Medium asteroid split into {} pieces. Score={}.", child_count, ship.score));
            }

            asteroid.active = false;
        }

        Asteroid *find_free_asteroid(int preferred_model_index = -1) {
            for (auto &asteroid : asteroids) {
                if (!asteroid.active && (preferred_model_index < 0 || asteroid.model_index == preferred_model_index)) {
                    return &asteroid;
                }
            }
            return nullptr;
        }

        void update_asteroids(float dt) {
            for (auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }
                asteroid.position += asteroid.velocity * dt;
                asteroid.rotation += asteroid.rotation_speed * dt;
                bool bounced = false;
                if (asteroid.position.x < BOUNDARY_X_MIN) {
                    asteroid.position.x = BOUNDARY_X_MIN;
                    asteroid.velocity.x = -asteroid.velocity.x * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                } else if (asteroid.position.x > BOUNDARY_X_MAX) {
                    asteroid.position.x = BOUNDARY_X_MAX;
                    asteroid.velocity.x = -asteroid.velocity.x * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                }
                if (asteroid.position.y < BOUNDARY_Y_MIN) {
                    asteroid.position.y = BOUNDARY_Y_MIN;
                    asteroid.velocity.y = -asteroid.velocity.y * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                } else if (asteroid.position.y > BOUNDARY_Y_MAX) {
                    asteroid.position.y = BOUNDARY_Y_MAX;
                    asteroid.velocity.y = -asteroid.velocity.y * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                }
                if (asteroid.position.z < BOUNDARY_Z_MIN) {
                    asteroid.position.z = BOUNDARY_Z_MIN;
                    asteroid.velocity.z = -asteroid.velocity.z * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                } else if (asteroid.position.z > BOUNDARY_Z_MAX) {
                    asteroid.position.z = BOUNDARY_Z_MAX;
                    asteroid.velocity.z = -asteroid.velocity.z * BOUNDARY_BOUNCE_FACTOR;
                    bounced = true;
                }
                if (bounced) {
                    asteroid.velocity += glm::vec3(random_float(-0.5f, 0.5f), random_float(-0.5f, 0.5f), random_float(-0.5f, 0.5f));
                }
                if (glm::length(asteroid.velocity) > 0.01f) {
                    asteroid.velocity *= 0.995f;
                }
                if (asteroid.rotation.x > 360.0f)
                    asteroid.rotation.x -= 360.0f;
                if (asteroid.rotation.y > 360.0f)
                    asteroid.rotation.y -= 360.0f;
                if (asteroid.rotation.z > 360.0f)
                    asteroid.rotation.z -= 360.0f;
            }

            for (auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }
                const float ship_distance = ship_asteroid_collision_distance(asteroid);
                if (ship_distance <= 0.0f) {
                    log_game(std::format("Ship collision with asteroid overlap {:.2f}.", -ship_distance), SDL_Color{255, 130, 90, 255});
                    start_ship_explosion();
                    break;
                }
            }
        }

        float ship_asteroid_collision_distance(const Asteroid &asteroid) const {
            static constexpr std::array<ShipCollisionSample, 5> ship_samples = {
                ShipCollisionSample{{0.0f, 0.0f, -0.55f}, 0.055f},
                ShipCollisionSample{{0.0f, 0.06f, -0.16f}, 0.160f},
                ShipCollisionSample{{0.0f, 0.08f, 0.34f}, 0.125f},
                ShipCollisionSample{{-0.42f, 0.03f, -0.02f}, 0.085f},
                ShipCollisionSample{{0.42f, 0.03f, -0.02f}, 0.085f},
            };

            const float asteroid_collision_radius = asteroid.radius * ASTEROID_SHIP_COLLISION_SCALE;
            float nearest_surface_distance = std::numeric_limits<float>::max();

            for (const ShipCollisionSample &sample : ship_samples) {
                const float ship_scale = rendered_ship_scale();
                const glm::vec3 offset = transform_ship_collision_offset(sample.local_position);
                const glm::vec3 previous_position = ship.prev_position + offset;
                const glm::vec3 current_position = ship.position + offset;
                const float center_distance = swept_point_distance_to_asteroid(previous_position, current_position, asteroid.position);
                nearest_surface_distance = std::min(nearest_surface_distance, center_distance - asteroid_collision_radius - (sample.radius * ship_scale));
            }

            return nearest_surface_distance;
        }

        glm::vec3 transform_ship_collision_offset(const glm::vec3 &local_position) const {
            const glm::mat4 model = build_model_matrix(
                glm::vec3(0.0f),
                ship.rotation,
                rendered_ship_scale(),
                ship_model.modelCenterOffset());
            return glm::vec3(model * glm::vec4(local_position, 1.0f));
        }

        float rendered_ship_scale() const {
            return SHIP_MODEL_SCALE * ship_model.modelRenderScale();
        }

        float swept_point_distance_to_asteroid(const glm::vec3 &previous_position,
                                               const glm::vec3 &current_position,
                                               const glm::vec3 &asteroid_position) const {
            const glm::vec3 segment = current_position - previous_position;
            const float segment_length_sq = glm::dot(segment, segment);
            glm::vec3 closest_point = current_position;

            if (segment_length_sq > 1e-6f) {
                const glm::vec3 to_asteroid = asteroid_position - previous_position;
                const float t = std::clamp(glm::dot(to_asteroid, segment) / segment_length_sq, 0.0f, 1.0f);
                closest_point = previous_position + segment * t;
            }

            return glm::length(closest_point - asteroid_position);
        }

        void start_ship_explosion() {
            if (ship.exploding) {
                return;
            }
            ship.exploding = true;
            ship.visible = false;
            ship.explosion_timer = EXPLOSION_DURATION_FRAMES;
            if (multiplayer_match && multiplayer.is_host()) {
                ++host_death_serial;
            }
            ship.lives--;
            ship.overheated = false;
            ship.overheat_cooldown = 0;
            ship.continuous_fire_timer = 0;
            ship.burst_count = 0;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            play_sound(crash_sound);
#endif
            log_game(std::format("Ship destroyed. Lives remaining: {}.", std::max(0, ship.lives)), SDL_Color{255, 120, 80, 255});
            if (ship.lives <= 0) {
                log_game(std::format("Game over. Final score: {}.", ship.score), SDL_Color{255, 90, 90, 255});
            }
            spawn_ship_explosion(ship.position);
        }

        void spawn_asteroid_explosion(const glm::vec3 &position) {
            spawn_gl_explosion(position);
        }

        void spawn_ship_explosion(const glm::vec3 &position) {
            spawn_gl_explosion(position);
        }

        void spawn_gl_explosion(const glm::vec3 &position) {
            struct ExplosionWave {
                float min_speed;
                float max_speed;
                float min_size;
                float max_size;
                float min_lifetime;
                float max_lifetime;
                glm::vec3 color;
            };

            constexpr int WAVE_COUNT = 4;
            constexpr int MAX_GL_EXPLOSIONS = 5;
            constexpr std::array<ExplosionWave, WAVE_COUNT> waves = {
                ExplosionWave{40.0f, 60.0f, 0.72f, 1.14f, 1.5f, 2.5f, {1.0f, 1.0f, 1.0f}},
                ExplosionWave{30.0f, 45.0f, 0.58f, 0.86f, 2.0f, 3.0f, {1.0f, 1.0f, 1.0f}},
                ExplosionWave{20.0f, 35.0f, 0.43f, 0.72f, 2.5f, 3.5f, {1.0f, 1.0f, 1.0f}},
                ExplosionWave{10.0f, 25.0f, 0.14f, 0.43f, 3.0f, 4.0f, {1.0f, 1.0f, 1.0f}},
            };

            const int particles_per_wave = MAX_PARTICLES / (WAVE_COUNT * MAX_GL_EXPLOSIONS);
            int spawned = 0;
            for (int wave_index = 0; wave_index < WAVE_COUNT; ++wave_index) {
                const ExplosionWave &wave = waves[static_cast<std::size_t>(wave_index)];
                for (int i = 0; i < particles_per_wave; ++i) {
                    Particle *particle = find_free_particle();
                    if (particle == nullptr) {
                        return;
                    }

                    const float theta = random_float(0.0f, 2.0f * PI);
                    const float phi = random_float(0.0f, PI);
                    const glm::vec3 dir{
                        std::sin(phi) * std::cos(theta),
                        std::sin(phi) * std::sin(theta),
                        std::cos(phi),
                    };

                    const float offset = 0.8f + 0.2f * static_cast<float>(wave_index) / static_cast<float>(WAVE_COUNT);
                    const float speed = random_float(wave.min_speed, wave.max_speed);
                    particle->position = position + dir * offset;
                    particle->velocity = dir * speed + glm::vec3(
                                                           random_float(-5.0f, 5.0f),
                                                           random_float(-5.0f, 5.0f),
                                                           random_float(-5.0f, 5.0f));
                    particle->color = glm::vec4(
                        wave.color.r * random_float(0.9f, 1.1f),
                        wave.color.g * random_float(0.9f, 1.1f),
                        wave.color.b * random_float(0.9f, 1.1f),
                        0.1f);
                    particle->size = random_float(wave.min_size, wave.max_size);
                    particle->lifetime = 0.0f;
                    particle->max_lifetime = random_float(wave.min_lifetime, wave.max_lifetime);
                    particle->active = true;
                    ++spawned;
                }
            }
            log_game(std::format("Explosion spawned {} particles.", spawned));
        }

        void spawn_particles(const glm::vec3 &position,
                             const glm::vec4 &color,
                             int count,
                             float min_speed,
                             float max_speed,
                             float min_size,
                             float max_size,
                             float min_lifetime,
                             float max_lifetime) {
            for (int i = 0; i < count; ++i) {
                Particle *particle = find_free_particle();
                if (particle == nullptr) {
                    return;
                }
                const glm::vec3 dir = normalize_or_zero(glm::vec3(
                    random_float(-1.0f, 1.0f),
                    random_float(-1.0f, 1.0f),
                    random_float(-1.0f, 1.0f)));
                particle->position = position;
                particle->velocity = dir * random_float(min_speed, max_speed);
                particle->color = color;
                particle->size = random_float(min_size, max_size);
                particle->lifetime = 0.0f;
                particle->max_lifetime = random_float(min_lifetime, max_lifetime);
                particle->active = true;
            }
        }

        Particle *find_free_particle() {
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
                particle.velocity *= 0.98f;
                particle.velocity.y -= 0.5f * dt;
                particle.lifetime += dt;

                const float life_ratio = particle.lifetime / particle.max_lifetime;
                if (life_ratio >= 1.0f) {
                    particle.active = false;
                    continue;
                }
                if (life_ratio < 0.2f) {
                    particle.color.a = life_ratio / 0.2f;
                } else if (life_ratio > 0.8f) {
                    particle.color.a = (1.0f - life_ratio) / 0.2f;
                } else {
                    particle.color.a = 1.0f;
                }
                if (life_ratio < 0.3f) {
                    particle.size *= 1.01f;
                } else {
                    particle.size *= 0.99f;
                }
                if (particle.color.a < 0.01f) {
                    particle.active = false;
                }
            }
        }

        void clear_particles() {
            for (auto &particle : particles) {
                particle.active = false;
            }
        }

        void prepare_restart_from_game_over() {
            clear_round_state();
            restart_after_intro = true;
            reset_intro_screen();
        }

        void reset_intro_screen() {
            mode = GameMode::Intro;
            intro_fade = 1.0f;
            intro_last_update_ms = SDL_GetTicks();
            loading_rain_opacity = 1.0f;
            if (intro_rain != nullptr) {
                intro_rain->set_opacity(1.0f);
                intro_rain->reset();
            }
        }

        void update_round_timer(float dt) {
            if (mode != GameMode::Playing) {
                return;
            }

            if (round_time_remaining <= 0.0f) {
                mode = GameMode::GameOver;
                return;
            }

            round_time_remaining = std::max(0.0f, round_time_remaining - dt);
            if (round_time_remaining <= 0.0f) {
                mode = GameMode::GameOver;
                ship.exploding = false;
                ship.visible = false;
                ship.fire_cooldown = 0;
                ship.burst_count = 0;
                ship.continuous_fire_timer = 0;
                ship.overheated = false;
                ship.overheat_cooldown = 0;
                log_game("Time expired. Game over.", SDL_Color{255, 90, 90, 255});
            }
        }

        void set_ui_font_size(int font_size) {
            if (font_size == last_font_size) {
                return;
            }

            last_font_size = font_size;
            setFont(asset_root + "/data/font.ttf", font_size);
            clearTextQueue();
        }

        std::string format_round_time() const {
            const int total_seconds = std::max(0, static_cast<int>(std::ceil(round_time_remaining)));
            const int minutes = total_seconds / 60;
            const int seconds = total_seconds % 60;
            return std::format("{:02d}:{:02d}", minutes, seconds);
        }

        glm::mat4 ship_rotation_matrix() const {
            glm::mat4 rotation(1.0f);
            rotation = glm::rotate(rotation, glm::radians(ship.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(ship.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(ship.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            return rotation;
        }

        struct CameraPose {
            glm::vec3 position{0.0f};
            glm::vec3 target{0.0f};
            glm::vec3 up{0.0f, 1.0f, 0.0f};
        };

        CameraPose chase_camera_pose(const glm::vec3 &ship_forward) const {
            CameraPose pose{};
            pose.position = ship.position - ship_forward * ship.camera_distance + glm::vec3(0.0f, ship.camera_height, 0.0f);
            pose.target = ship.position + ship_forward * 6.0f;
            pose.up = glm::vec3(0.0f, 1.0f, 0.0f);
            return pose;
        }

        CameraPose first_person_camera_pose(const glm::mat4 &ship_rotation_matrix, const glm::vec3 &ship_forward) const {
            constexpr glm::vec3 FIRST_PERSON_CAMERA_OFFSET{0.0f, 0.16f, -0.30f};

            CameraPose pose{};
            const glm::vec3 cockpit_offset = glm::vec3(ship_rotation_matrix * glm::vec4(FIRST_PERSON_CAMERA_OFFSET * rendered_ship_scale(), 0.0f));
            pose.position = ship.position + cockpit_offset;
            pose.target = pose.position + ship_forward * 8.0f;
            pose.up = normalize_or_zero(glm::vec3(ship_rotation_matrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            return pose;
        }

        static float smooth_camera_transition(float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        }

        void begin_camera_transition(bool target_first_person_camera) {
            first_person_camera = target_first_person_camera;
            camera_transition_active = true;
            camera_transition_elapsed = 0.0f;
            camera_transition_start_position = camera_position;
            camera_transition_start_target = camera_target_position;
            camera_transition_start_up = camera_up_vector;
        }

        void update_camera(float dt) {
            const glm::mat4 ship_rotation_matrix = this->ship_rotation_matrix();
            const glm::vec3 ship_forward = normalize_or_zero(glm::vec3(ship_rotation_matrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            const CameraPose target_pose = first_person_camera ? first_person_camera_pose(ship_rotation_matrix, ship_forward) : chase_camera_pose(ship_forward);

            if (camera_transition_active) {
                camera_transition_elapsed += dt;
                const float blend = smooth_camera_transition(camera_transition_elapsed / CAMERA_TRANSITION_SECONDS);
                camera_position = glm::mix(camera_transition_start_position, target_pose.position, blend);
                camera_target_position = glm::mix(camera_transition_start_target, target_pose.target, blend);
                camera_up_vector = normalize_or_zero(glm::mix(camera_transition_start_up, target_pose.up, blend));
                if (camera_transition_elapsed >= CAMERA_TRANSITION_SECONDS) {
                    camera_transition_active = false;
                    camera_position = target_pose.position;
                    camera_target_position = target_pose.target;
                    camera_up_vector = target_pose.up;
                }
            } else if (first_person_camera) {
                camera_position = target_pose.position;
                camera_target_position = target_pose.target;
                camera_up_vector = target_pose.up;
            } else {
                camera_position = glm::mix(camera_position, target_pose.position, 1.0f - std::exp(-dt * 10.0f));
                camera_target_position = target_pose.target;
                camera_up_vector = target_pose.up;
            }

            view_matrix = glm::lookAt(camera_position, camera_target_position, camera_up_vector);
        }

        void draw_ship(uint32_t image_index) {
            if (!ship.visible) {
                return;
            }

            mxvk::UniformBufferObject ubo{};
            ubo.model = build_model_matrix(ship.position, ship.rotation, rendered_ship_scale(), ship_model.modelCenterOffset());
            last_ship_model_matrix = ubo.model;
            ubo.view = view_matrix;
            ubo.proj = projection_matrix;
            ubo.fx = glm::vec4(camera_position, elapsed_seconds);
            ship_model.updateUBO(image_index, ubo);
            ship_model.render(current_command_buffer, image_index, false);
        }

        void draw_remote_ship(uint32_t image_index) {
            if (!remote_ship.visible || remote_ship_exploding || !remote_ship_model.isLoaded()) {
                return;
            }
            mxvk::UniformBufferObject ubo{};
            ubo.model = build_model_matrix(remote_ship.position, remote_ship.rotation, rendered_ship_scale(), remote_ship_model.modelCenterOffset());
            ubo.view = view_matrix;
            ubo.proj = projection_matrix;
            ubo.fx = glm::vec4(camera_position, elapsed_seconds);
            remote_ship_model.updateUBO(image_index, ubo);
            remote_ship_model.render(current_command_buffer, image_index, false);
        }

        void draw_asteroids(uint32_t image_index) {
            for (std::size_t i = 0; i < asteroids.size(); ++i) {
                const Asteroid &asteroid = asteroids[i];
                if (!asteroid.active) {
                    continue;
                }
                mxvk::UniformBufferObject ubo{};
                const float scale = asteroid.radius;
                mxvk::VKAbstractModel &asteroid_model = asteroid_models[i];
                ubo.model = build_model_matrix(asteroid.position, asteroid.rotation, scale * asteroid_model.modelRenderScale(),
                                               asteroid_model.modelCenterOffset());
                ubo.view = view_matrix;
                ubo.proj = projection_matrix;
                ubo.fx = glm::vec4(camera_position, elapsed_seconds);
                asteroid_model.updateUBO(image_index, ubo);
                asteroid_model.render(current_command_buffer, image_index, false);
            }
        }

        void draw_projectiles() {
            for (const auto &projectile : projectiles) {
                if (!projectile.active) {
                    continue;
                }
                const float life_factor = 1.0f - (projectile.lifetime / PROJECTILE_LIFETIME);
                const float pulse = (0.55f + 0.22f * (1.0f - life_factor)) * (0.9f + 0.1f * std::sin(elapsed_seconds * 12.0f));
                const glm::vec4 color = glm::vec4(
                    std::clamp(projectile.color.r * (0.9f + 0.1f * life_factor), 0.0f, 1.0f),
                    std::clamp(projectile.color.g * (0.9f + 0.1f * life_factor), 0.0f, 1.0f),
                    std::clamp(projectile.color.b * (0.9f + 0.1f * life_factor), 0.0f, 1.0f),
                    std::clamp(projectile.color.a * (0.65f + 0.35f * life_factor), 0.0f, 1.0f));
                projectile_sprite->drawSprite(projectile.position, glm::vec2(pulse), color);
            }
        }

        void draw_remote_projectiles() {
            for (const NetworkProjectile &projectile : remote_projectiles) {
                if (projectile.active == 0U) {
                    continue;
                }
                const glm::vec3 position{projectile.position[0], projectile.position[1], projectile.position[2]};
                projectile_sprite->drawSprite(position, glm::vec2(0.62f), {0.20f, 0.65f, 1.0f, 1.0f});
            }
        }

        void draw_particles() {
            for (const auto &particle : particles) {
                if (!particle.active) {
                    continue;
                }
                effect_sprite->drawSprite(particle.position,
                                          glm::vec2(particle.size),
                                          particle.color);
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
            constexpr float tip_z = 1.02f;
            constexpr float base_y = 0.040f;
            constexpr float outer_radius = 0.052f;
            constexpr float inner_radius = 0.026f;

            std::vector<FlameVertex> vertices{};
            vertices.reserve(static_cast<std::size_t>(segments) * 6U);

            const glm::vec4 outer_base_color{1.0f, 0.42f, 0.08f, 0.50f};
            const glm::vec4 outer_tip_color{0.7f, 0.08f, 0.0f, 0.0f};
            const glm::vec4 inner_base_color{1.0f, 0.92f, 0.45f, 0.72f};
            const glm::vec4 inner_tip_color{1.0f, 0.32f, 0.04f, 0.0f};

            auto add_cone = [&](float radius, const glm::vec4 &base_color, const glm::vec4 &tip_color) {
                const glm::vec3 tip{0.0f, base_y, tip_z};
                for (int i = 0; i < segments; ++i) {
                    const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
                    const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * PI;
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
            const VkDeviceSize buffer_size = sizeof(FlameVertex) * static_cast<VkDeviceSize>(vertices.size());
            create_buffer(buffer_size,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          flame_vertex_buffer,
                          flame_vertex_buffer_memory);

            void *data = nullptr;
            if (vkMapMemory(device, flame_vertex_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS || data == nullptr) {
                throw mxvk::Exception("Failed to map asteroids3d flame vertex buffer");
            }
            std::memcpy(data, vertices.data(), static_cast<std::size_t>(buffer_size));
            vkUnmapMemory(device, flame_vertex_buffer_memory);
        }

        void create_flame_pipeline() {
            cleanup_flame_swapchain_resources();

            const std::vector<char> vert_shader_code = loadSpv(shader_root + "/flame.vert.spv");
            const std::vector<char> frag_shader_code = loadSpv(shader_root + "/flame.frag.spv");

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
                binding_description.stride = sizeof(FlameVertex);
                binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(FlameVertex, pos);
                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[1].offset = offsetof(FlameVertex, color);

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
                push_range.size = sizeof(FlamePushConstants);

                VkPipelineLayoutCreateInfo layout_info{};
                layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layout_info.pushConstantRangeCount = 1;
                layout_info.pPushConstantRanges = &push_range;

                if (vkCreatePipelineLayout(device, &layout_info, nullptr, &flame_pipeline_layout) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to create asteroids3d flame pipeline layout");
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
                    throw mxvk::Exception("Failed to create asteroids3d flame pipeline");
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

        void draw_engine_flame(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (!ship.visible || ship.current_speed <= ship.min_speed * 1.2f) {
                return;
            }
            if (flame_pipeline == VK_NULL_HANDLE || flame_vertex_buffer == VK_NULL_HANDLE || flame_vertex_count == 0) {
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

            FlamePushConstants pc{};
            pc.mvp = projection_matrix * view_matrix * last_ship_model_matrix;
            pc.params = glm::vec4(elapsed_seconds, ship.current_speed / ship.max_speed, 0.0f, 0.0f);

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
                throw mxvk::Exception("Failed to create asteroids3d buffer");
            }

            VkMemoryRequirements mem_requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

            VkMemoryAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc_info.allocationSize = mem_requirements.size;

            try {
                alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);
                if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to allocate asteroids3d buffer memory");
                }
                if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS) {
                    throw mxvk::Exception("Failed to bind asteroids3d buffer memory");
                }
            } catch (...) {
                if (buffer_memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, buffer_memory, nullptr);
                    buffer_memory = VK_NULL_HANDLE;
                }
                if (buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, buffer, nullptr);
                    buffer = VK_NULL_HANDLE;
                }
                throw;
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

            throw mxvk::Exception("Failed to find asteroids3d memory type");
        }

        void draw_ui_rect(int x, int y, int width, int height, const glm::vec4 &color) {
            if (ui_pixel == nullptr || width <= 0 || height <= 0) {
                return;
            }
            ui_pixel->setShaderParams(color.r, color.g, color.b, color.a);
            ui_pixel->drawSpriteRect(x, y, width, height);
        }

        std::optional<glm::ivec2> project_world_to_screen(const glm::vec3 &world_position, const VkExtent2D &extent) const {
            const glm::vec4 clip = projection_matrix * view_matrix * glm::vec4(world_position, 1.0f);
            if (clip.w <= 0.0001f) {
                return std::nullopt;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < 0.0f || ndc.z > 1.0f) {
                return std::nullopt;
            }

            const int x = static_cast<int>((ndc.x * 0.5f + 0.5f) * static_cast<float>(extent.width));
            const int y = static_cast<int>((ndc.y * 0.5f + 0.5f) * static_cast<float>(extent.height));
            return glm::ivec2{x, y};
        }

        bool cannon_has_asteroid_target(const glm::vec3 &muzzle, const glm::vec3 &forward) const {
            constexpr float AIM_ASSIST_SCALE = 1.04f;
            const float max_range = PROJECTILE_SPEED * PROJECTILE_LIFETIME;
            for (const auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }

                const glm::vec3 to_asteroid = asteroid.position - muzzle;
                const float along_ray = glm::dot(to_asteroid, forward);
                if (along_ray < 0.0f || along_ray > max_range) {
                    continue;
                }

                const glm::vec3 closest_point = muzzle + forward * along_ray;
                const float hit_radius = asteroid.radius * ASTEROID_PROJECTILE_COLLISION_SCALE * AIM_ASSIST_SCALE;
                if (glm::length(closest_point - asteroid.position) <= hit_radius) {
                    return true;
                }
            }
            return false;
        }

        void draw_cannon_crosshair(const VkExtent2D &extent) {
            if (ui_pixel == nullptr || !ship.visible || ship.exploding || extent.width < 160U || extent.height < 120U) {
                return;
            }

            constexpr float AIM_DISTANCE = 120.0f;
            constexpr float MUZZLE_OFFSET = 0.08f;
            constexpr int ARM_LENGTH = 18;
            constexpr int GAP = 6;
            constexpr int THICKNESS = 2;
            const glm::vec3 forward = ship.forward();
            const glm::vec3 muzzle = ship.position + forward * MUZZLE_OFFSET;
            const glm::vec3 aim_position = muzzle + forward * AIM_DISTANCE;
            const std::optional<glm::ivec2> screen_position = project_world_to_screen(aim_position, extent);
            if (!screen_position.has_value()) {
                return;
            }

            const int x = std::clamp(screen_position->x, ARM_LENGTH + 2, static_cast<int>(extent.width) - ARM_LENGTH - 2);
            const int y = std::clamp(screen_position->y, ARM_LENGTH + 2, static_cast<int>(extent.height) - ARM_LENGTH - 2);
            const bool target_locked = cannon_has_asteroid_target(muzzle, forward);
            const glm::vec4 shadow = target_locked ? glm::vec4{0.0f, 0.02f, 0.07f, 0.7f} : glm::vec4{0.05f, 0.0f, 0.0f, 0.7f};
            const glm::vec4 crosshair_color = target_locked ? glm::vec4{0.12f, 0.58f, 1.0f, 0.98f} : glm::vec4{1.0f, 0.03f, 0.02f, 0.96f};

            draw_ui_rect(x - ARM_LENGTH - 1, y - THICKNESS / 2 - 1, ARM_LENGTH - GAP + 2, THICKNESS + 2, shadow);
            draw_ui_rect(x + GAP - 1, y - THICKNESS / 2 - 1, ARM_LENGTH - GAP + 2, THICKNESS + 2, shadow);
            draw_ui_rect(x - THICKNESS / 2 - 1, y - ARM_LENGTH - 1, THICKNESS + 2, ARM_LENGTH - GAP + 2, shadow);
            draw_ui_rect(x - THICKNESS / 2 - 1, y + GAP - 1, THICKNESS + 2, ARM_LENGTH - GAP + 2, shadow);

            draw_ui_rect(x - ARM_LENGTH, y - THICKNESS / 2, ARM_LENGTH - GAP, THICKNESS, crosshair_color);
            draw_ui_rect(x + GAP, y - THICKNESS / 2, ARM_LENGTH - GAP, THICKNESS, crosshair_color);
            draw_ui_rect(x - THICKNESS / 2, y - ARM_LENGTH, THICKNESS, ARM_LENGTH - GAP, crosshair_color);
            draw_ui_rect(x - THICKNESS / 2, y + GAP, THICKNESS, ARM_LENGTH - GAP, crosshair_color);
            draw_ui_rect(x - 1, y - 1, 3, 3, crosshair_color);
        }

        void draw_radar(const VkExtent2D &extent) {
            if (ui_pixel == nullptr || extent.width < 360U || extent.height < 280U) {
                return;
            }

            constexpr int BORDER = 2;
            constexpr float RADAR_RANGE = 180.0f;
            const int radar_size = std::clamp(static_cast<int>(std::min(extent.width, extent.height)) / 4, 150, 220);
            const int radar_x = 24;
            const int radar_y = std::max(230, static_cast<int>(extent.height) - radar_size - 24);
            const int inner_x = radar_x + BORDER;
            const int inner_y = radar_y + BORDER;
            const int inner_size = radar_size - BORDER * 2;
            const int center_x = inner_x + inner_size / 2;
            const int center_y = inner_y + inner_size / 2;
            const float half_size = static_cast<float>(inner_size) * 0.5f;

            draw_ui_rect(radar_x, radar_y, radar_size, radar_size, {0.01f, 0.025f, 0.045f, 0.74f});
            const glm::vec4 border_color = ship_returning_to_field ? glm::vec4{1.0f, 0.64f, 0.18f, 0.94f} : glm::vec4{0.16f, 0.66f, 0.82f, 0.9f};
            draw_ui_rect(radar_x, radar_y, radar_size, BORDER, border_color);
            draw_ui_rect(radar_x, radar_y + radar_size - BORDER, radar_size, BORDER, border_color);
            draw_ui_rect(radar_x, radar_y, BORDER, radar_size, border_color);
            draw_ui_rect(radar_x + radar_size - BORDER, radar_y, BORDER, radar_size, border_color);

            draw_ui_rect(center_x, inner_y, 1, inner_size, {0.10f, 0.30f, 0.38f, 0.7f});
            draw_ui_rect(inner_x, center_y, inner_size, 1, {0.10f, 0.30f, 0.38f, 0.7f});
            draw_ui_rect(center_x - inner_size / 4, inner_y, 1, inner_size, {0.08f, 0.22f, 0.28f, 0.45f});
            draw_ui_rect(center_x + inner_size / 4, inner_y, 1, inner_size, {0.08f, 0.22f, 0.28f, 0.45f});
            draw_ui_rect(inner_x, center_y - inner_size / 4, inner_size, 1, {0.08f, 0.22f, 0.28f, 0.45f});
            draw_ui_rect(inner_x, center_y + inner_size / 4, inner_size, 1, {0.08f, 0.22f, 0.28f, 0.45f});

            for (const auto &asteroid : asteroids) {
                if (!asteroid.active) {
                    continue;
                }
                glm::vec2 relative{asteroid.position.x - ship.position.x, asteroid.position.z - ship.position.z};
                const float distance = glm::length(relative);
                const bool clamped_to_edge = distance > RADAR_RANGE;
                if (clamped_to_edge && distance > 1e-4f) {
                    relative *= RADAR_RANGE / distance;
                }
                const int dot_x = center_x + static_cast<int>((relative.x / RADAR_RANGE) * half_size);
                const int dot_y = center_y + static_cast<int>((relative.y / RADAR_RANGE) * half_size);
                const int dot_size = std::clamp(static_cast<int>(asteroid.radius * 0.55f), 3, 8);
                const float altitude = std::clamp((asteroid.position.y - BOUNDARY_Y_MIN) / (BOUNDARY_Y_MAX - BOUNDARY_Y_MIN), 0.0f, 1.0f);
                const glm::vec4 dot_color = clamped_to_edge
                                                ? glm::vec4{1.0f, 0.38f, 0.16f, 0.9f}
                                                : glm::vec4{1.0f, 0.55f + altitude * 0.28f, 0.18f, 1.0f};
                draw_ui_rect(dot_x - dot_size / 2, dot_y - dot_size / 2, dot_size, dot_size, dot_color);
            }

            if (multiplayer_match) {
                glm::vec2 relative{remote_ship.position.x - ship.position.x, remote_ship.position.z - ship.position.z};
                const float distance = glm::length(relative);
                if (distance > RADAR_RANGE && distance > 1e-4f) {
                    relative *= RADAR_RANGE / distance;
                }
                const int opponent_x = center_x + static_cast<int>((relative.x / RADAR_RANGE) * half_size);
                const int opponent_y = center_y + static_cast<int>((relative.y / RADAR_RANGE) * half_size);
                const glm::vec4 opponent_color = remote_ship_exploding ? glm::vec4{1.0f, 0.35f, 0.12f, 1.0f} : glm::vec4{1.0f, 0.08f, 0.12f, 1.0f};
                draw_ui_rect(opponent_x - 6, opponent_y - 6, 12, 12, opponent_color);
                draw_ui_rect(opponent_x - 9, opponent_y - 1, 18, 3, opponent_color);
                draw_ui_rect(opponent_x - 1, opponent_y - 9, 3, 18, opponent_color);
            }

            draw_ui_rect(center_x - 5, center_y, 11, 2, {0.95f, 1.0f, 1.0f, 1.0f});
            draw_ui_rect(center_x, center_y - 5, 2, 11, {0.95f, 1.0f, 1.0f, 1.0f});

            const SDL_Color label_color = ship_returning_to_field ? SDL_Color{255, 180, 80, 255} : SDL_Color{120, 220, 255, 255};
            printText(multiplayer_match ? "RADAR - ENEMY RED" : (ship_returning_to_field ? "RADAR RETURN" : "RADAR"), radar_x, std::max(4, radar_y - 22), label_color);
        }

        void draw_hud([[maybe_unused]] float aspect) {
            set_ui_font_size(18);
            const SDL_Color white{255, 255, 255, 255};
            const SDL_Color red{220, 60, 60, 255};
            const SDL_Color yellow{255, 220, 120, 255};
            const VkExtent2D extent = getSwapchainExtent();
            draw_cannon_crosshair(extent);
            draw_radar(extent);
            const int right_x = std::max(25, static_cast<int>(extent.width) - 250);
            printText("MXVK Asteroids v1.0", right_x, 25, red);
            if (multiplayer_match) {
                const unsigned local_kills = multiplayer.is_host() ? host_kills : client_kills;
                const unsigned enemy_kills = multiplayer.is_host() ? client_kills : host_kills;
                printText("Kills: " + std::to_string(local_kills) + " / 5", right_x, 50, white);
                printText("Enemy: " + std::to_string(enemy_kills) + " / 5", right_x, 75, red);
                printText("Opponent: " + multiplayer.peer_name(), right_x, 100, white);
                printText("UDP host-authoritative", right_x, 125, yellow);
                return;
            }
            printText("Score: " + std::to_string(ship.score), right_x, 50, white);
            printText("Lives: " + std::to_string(std::max(0, ship.lives)), right_x, 75, white);
            printText("Asteroids: " + std::to_string(active_asteroids()), right_x, 100, white);
            printText("Time Left: " + format_round_time(), right_x, 125, round_time_remaining <= 30.0f ? yellow : white);
            printText("[F1 for Debug]", right_x, 150, white);
            printText(inverted_controls ? "[Inverted] F2/Y" : "[Arcade] F2/Y", right_x, 175, white);
            printText("[F3 for Console]", right_x, 200, white);
            printText(mouse_look_controls ? "[Mouse Look] F5" : "[Classic Keys] F5", right_x, 225, white);
            printText(first_person_camera ? "[First Person] F7" : "[Chase View] F7", right_x, 250, white);

            if (!debug_menu) {
                return;
            }

            const float fps = (last_delta_time > 0.0001f) ? (1.0f / last_delta_time) : 0.0f;
            printText("Ship X,Y,Z: " + vec3_string(ship.position), 25, 25, white);
            printText("Velocity X,Y,Z: " + vec3_string(ship.velocity), 25, 50, white);
            printText("FPS: " + std::to_string(fps), 25, 75, white);
            printText("Aseroids destroyed: " + std::to_string(MAX_ASTEROIDS - active_asteroids()), 25, 100, white);
            printText(mouse_look_controls ? "Controls: Mouse look, W/S speed, A/D roll, click/SPACE to shoot" : "Controls: Arrows to Move, W,S Tilt Up/Down - SPACE to shoot", 25, 125, white);
            printText("Nearest Object: " + std::to_string(nearest_asteroid_distance()), 25, 150, white);
            printText("Farthest Object: " + std::to_string(farthest_asteroid_distance()), 25, 175, white);
            printText("Speed: " + std::to_string(ship.current_speed) + " / " + std::to_string(ship.max_speed), 25, 200, white);
            printText("Controller: " + controller_status(), 25, 225, white);
            printText(std::string("Input: ") + (mouse_look_controls ? "Keyboard/mouse" : "Classic keyboard"), 25, 250, white);
            printText(std::string("Camera: ") + (first_person_camera ? "First person" : "Chase"), 25, 275, white);
            printText("Press ENTER to randomize asteroids", 25, 300, white);
        }

        int active_asteroids() const {
            int count = 0;
            for (const auto &asteroid : asteroids) {
                if (asteroid.active) {
                    ++count;
                }
            }
            return count;
        }

        void draw_multiplayer_end(const VkExtent2D &extent) {
            multiplayer.exchange(make_network_state(true), last_delta_time);
            set_ui_font_size(22);
            const bool local_won = (multiplayer.is_host() && multiplayer_winner == 1U) ||
                                   (!multiplayer.is_host() && multiplayer_winner == 2U);
            const int panel_width = 620;
            const int panel_x = static_cast<int>(extent.width) / 2 - panel_width / 2;
            const int panel_y = static_cast<int>(extent.height) / 2 - 150;
            draw_ui_rect(panel_x, panel_y, panel_width, 300, {0.015f, 0.025f, 0.08f, 0.94f});
            draw_ui_rect(panel_x, panel_y, panel_width, 4, local_won ? glm::vec4{0.2f, 1.0f, 0.55f, 1.0f} : glm::vec4{1.0f, 0.2f, 0.2f, 1.0f});
            printText(local_won ? "VICTORY" : "DEFEAT", panel_x + 235, panel_y + 48,
                      local_won ? SDL_Color{100, 255, 160, 255} : SDL_Color{255, 90, 90, 255});
            printText(std::format("Final score: {} - {}", multiplayer.is_host() ? host_kills : client_kills,
                                  multiplayer.is_host() ? client_kills : host_kills),
                      panel_x + 210, panel_y + 120, {255, 255, 255, 255});
            printText("Press ENTER to return to the multiplayer lobby", panel_x + 70, panel_y + 205, {255, 220, 120, 255});
        }

        void draw_end_screen([[maybe_unused]] uint32_t image_index,
                             [[maybe_unused]] float aspect,
                             const std::string &title,
                             const SDL_Color &title_color) {
            set_ui_font_size(32);
            const SDL_Color white{255, 255, 255, 255};
            const SDL_Color yellow{255, 220, 120, 255};
            const VkExtent2D extent = getSwapchainExtent();

            const std::string score_text = "Final Score: " + std::to_string(ship.score);
            const std::string prompt = "Press ENTER to start over";

            int title_w = 0;
            int title_h = 0;
            if (getTextDimensions(title.c_str(), title_w, title_h)) {
                printText(title.c_str(),
                          static_cast<int>(extent.width) / 2 - title_w / 2,
                          static_cast<int>(extent.height) / 2 - title_h,
                          title_color);
            } else {
                printText(title.c_str(), 24, 20, title_color);
            }

            int score_w = 0;
            int score_h = 0;
            if (getTextDimensions(score_text.c_str(), score_w, score_h)) {
                printText(score_text.c_str(),
                          static_cast<int>(extent.width) / 2 - score_w / 2,
                          static_cast<int>(extent.height) / 2 + 10,
                          white);
            } else {
                printText(score_text.c_str(), 24, 70, white);
            }

            int prompt_w = 0;
            int prompt_h = 0;
            if (getTextDimensions(prompt.c_str(), prompt_w, prompt_h)) {
                printText(prompt.c_str(),
                          static_cast<int>(extent.width) / 2 - prompt_w / 2,
                          static_cast<int>(extent.height) / 2 + score_h + 28,
                          yellow);
            } else {
                printText(prompt.c_str(), 24, 100, yellow);
            }
        }

        void draw_game_over([[maybe_unused]] uint32_t image_index, [[maybe_unused]] float aspect) {
            draw_end_screen(image_index, aspect, "Game over", SDL_Color{235, 60, 60, 255});
        }

        VkCommandBuffer current_command_buffer = VK_NULL_HANDLE;
        float last_delta_time = 1.0f / 60.0f;

        std::string vec3_string(const glm::vec3 &value) const {
            return std::to_string(value.x) + ", " + std::to_string(value.y) + ", " + std::to_string(value.z);
        }

        float nearest_asteroid_distance() const {
            float nearest = 999999.0f;
            for (const auto &asteroid : asteroids) {
                if (asteroid.active) {
                    nearest = std::min(nearest, glm::length(ship.position - asteroid.position));
                }
            }
            return nearest;
        }

        float farthest_asteroid_distance() const {
            float farthest = 0.0f;
            for (const auto &asteroid : asteroids) {
                if (asteroid.active) {
                    farthest = std::max(farthest, glm::length(ship.position - asteroid.position));
                }
            }
            return farthest;
        }
    };

} // namespace space

void space::run_asteroids3d(const Arguments &args) {
    Asteroids3DWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync, args.enable_crt);
    window.loop();
}
