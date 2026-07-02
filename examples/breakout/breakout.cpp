#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk_controller.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#ifndef breakout_ASSET_DIR
#define breakout_ASSET_DIR "."
#endif

#ifndef breakout_SHADER_DIR
#define breakout_SHADER_DIR "."
#endif

namespace {
    constexpr float GAME_LEFT = -5.0f;
    constexpr float GAME_RIGHT = 5.0f;
    constexpr float GAME_TOP = 3.75f;
    constexpr float GAME_BOTTOM = -3.75f;
    constexpr float PADDLE_SPEED = 5.0f;
    constexpr float BALL_SPEED = 4.0f;
    constexpr float MODEL_SCALE = 1.0f;
    constexpr float ROTATION_SPEED = 50.0f;
    constexpr float DEFAULT_ZOOM = 0.8f;
    constexpr float MIN_ZOOM = 0.45f;
    constexpr float MAX_ZOOM = 1.6f;
    constexpr float ZOOM_SPEED = 0.8f;
    constexpr Sint16 GAMEPAD_DEAD_ZONE = 8000;
    constexpr int MAX_MISSES = 3;
    constexpr int BLOCK_ROWS = 5;
    constexpr int BLOCK_COLUMNS = 11;

    struct Box {
        glm::vec3 position{0.0f};
        glm::vec3 size{1.0f};
        glm::vec3 velocity{0.0f};
        float rotation = 0.0f;
    };

    struct Block {
        Box box{};
        std::unique_ptr<mxvk::VKAbstractModel> model{};
        std::string_view texture_manifest{};
        bool destroyed = false;
        bool rotating = false;
        float current_rotation = 0.0f;
    };

    struct Ball {
        Box box{};
        float radius = 0.25f;
        bool stuck = true;
    };

    [[nodiscard]] float clampf(float value, float low, float high) {
        return std::max(low, std::min(value, high));
    }

    [[nodiscard]] bool check_collision(const Ball &ball, const Box &object) {
        const glm::vec3 half_extents(object.size.x * 0.5f, object.size.y * 0.5f, 0.0f);
        const glm::vec3 difference = ball.box.position - object.position;
        const glm::vec3 clamped = glm::clamp(difference, -half_extents, half_extents);
        const glm::vec3 closest = object.position + clamped;
        return glm::length(closest - ball.box.position) < ball.radius;
    }

    class BreakoutWindow final : public mxvk::VK_Window {
      public:
        BreakoutWindow(const std::string &path, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MXVK 3D Breakout", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              asset_root((path.empty() || path == ".") ? std::string(breakout_ASSET_DIR) : path),
              data_root(asset_root + "/data") {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            setFont(data_root + "/font.ttf", 24);

            const std::string sprite_vertex = std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv";
            const std::string background_fragment = std::string(breakout_SHADER_DIR) + "/breakout_background.frag.spv";
            background = createSprite(data_root + "/intro.png", sprite_vertex, background_fragment);
            game_background = createSprite(data_root + "/bg.png", sprite_vertex, background_fragment);
            intro_model = load_model("intro_manifest.txt");
            paddle_model = load_model("texture_manifest.txt");
            ball_model = load_model("texture_manifest.txt");
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            mixer = std::make_unique<mxvk::VK_Mixer>();
            ping_sound = mixer->loadWav(data_root + "/ping.wav");
            clear_sound = mixer->loadWav(data_root + "/pop.wav");
            die_sound = mixer->loadWav(data_root + "/die.wav");
#endif
            open_controller();
            reset_game();
        }

        ~BreakoutWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            cleanup_models();
        }

        void onSwapchainRecreated() override {
            if (intro_model) {
                intro_model->resize(this);
            }
            if (paddle_model) {
                paddle_model->resize(this);
            }
            if (ball_model) {
                ball_model->resize(this);
            }
            for (Block &block : blocks) {
                if (block.model) {
                    block.model->resize(this);
                }
            }
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (controller.connectEvent(e)) {
                if (!controller.active()) {
                    open_controller();
                }
                return;
            }

            if (screen == Screen::Intro) {
                if ((e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)) ||
                    (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) ||
                    e.type == SDL_EVENT_FINGER_DOWN ||
                    is_gamepad_start_button(e)) {
                    screen = Screen::Game;
                    reset_game();
                }
                return;
            }

            if (screen == Screen::GameOver) {
                if ((e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN) || is_gamepad_start_button(e)) {
                    reset_game();
                    screen = Screen::Intro;
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN) {
                    ball.stuck = false;
                } else if (e.key.key == SDLK_R) {
                    reset_game();
                    screen = Screen::Game;
                } else if (e.key.key == SDLK_BACKSPACE) {
                    screen = Screen::Intro;
                } else if (e.key.key == SDLK_1) {
                    grid_rotation = -33.4f;
                    grid_y_rotation = -18.4f;
                } else if (e.key.key == SDLK_2) {
                    grid_rotation = -21.4f;
                    grid_y_rotation = 31.15f;
                } else if (e.key.key == SDLK_3) {
                    grid_rotation = -47.25f;
                    grid_y_rotation = 0.85f;
                }
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                const auto button = static_cast<SDL_GamepadButton>(e.gbutton.button);
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    ball.stuck = false;
                } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                    reset_game();
                    screen = Screen::Game;
                } else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    screen = Screen::Intro;
                } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
                    grid_rotation = 0.0f;
                    grid_y_rotation = 0.0f;
                    zoom = DEFAULT_ZOOM;
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                ball.stuck = false;
            }

            if (e.type == SDL_EVENT_FINGER_DOWN) {
                const Uint64 current_tap_time = SDL_GetTicks();
                if (current_tap_time - last_tap_time <= DOUBLE_TAP_THRESHOLD_MS) {
                    ball.stuck = false;
                }
                last_tap_time = current_tap_time;
            } else if (e.type == SDL_EVENT_FINGER_MOTION) {
                paddle.position.x = (e.tfinger.x * (GAME_RIGHT - GAME_LEFT)) + GAME_LEFT;
                clamp_paddle();
            }
        }

        void proc() override {
            const auto now = std::chrono::steady_clock::now();
            delta_seconds = std::chrono::duration<float>(now - last_frame).count();
            delta_seconds = std::min(delta_seconds, 0.05f);
            last_frame = now;
            background_time += delta_seconds;

            const int width = screen_width();
            if (screen == Screen::Intro) {
                printText("Press Enter or Tap to Start", 25, 25, {255, 255, 255, 255});
                return;
            }

            if (screen == Screen::GameOver) {
                print_centered_game_over(width);
                return;
            }

            update_game(delta_seconds);
            print_centered_score(width);
            print_tries();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            const VkExtent2D extent = getSwapchainExtent();
            draw_background(cmd, extent);

            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            if (screen == Screen::Intro) {
                glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                glm::mat4 proj = glm::perspective(glm::radians(10.0f), aspect, 0.1f, 100.0f);
                proj[1][1] *= -1.0f;
                intro_rotation += delta_seconds * 50.0f;
                draw_model(cmd, image_index, *intro_model, Box{glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f), intro_rotation}, view, proj, glm::vec3(1.0f));
                return;
            }

            glm::mat4 view(1.0f);
            view = glm::rotate(view, glm::radians(grid_rotation), glm::vec3(1.0f, 0.0f, 0.0f));
            view = glm::rotate(view, glm::radians(grid_y_rotation), glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 proj = glm::orthoRH_ZO(GAME_LEFT / zoom, GAME_RIGHT / zoom, GAME_BOTTOM / zoom, GAME_TOP / zoom, -100.0f, 100.0f);
            proj[1][1] *= -1.0f;

            Box paddle_draw = paddle;
            paddle_draw.rotation = paddle_current_rotation;
            draw_model(cmd, image_index, *paddle_model, paddle_draw, view, proj, glm::vec3(1.0f));
            draw_model(cmd, image_index, *ball_model, ball.box, view, proj, glm::vec3(1.0f));
            for (const Block &block : blocks) {
                if (block.destroyed) {
                    continue;
                }
                Box draw = block.box;
                draw.rotation = block.current_rotation;
                draw_model(cmd, image_index, *block.model, draw, view, proj, glm::vec3(1.0f));
            }
        }

      private:
        enum class Screen {
            Intro,
            Game,
            GameOver,
            Complete
        };

        std::string asset_root;
        std::string data_root;
        Screen screen = Screen::Intro;
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VK_Sprite *game_background = nullptr;
        std::unique_ptr<mxvk::VKAbstractModel> intro_model{};
        std::unique_ptr<mxvk::VKAbstractModel> paddle_model{};
        std::unique_ptr<mxvk::VKAbstractModel> ball_model{};
        Box paddle{glm::vec3(0.0f, -3.5f, 0.0f), glm::vec3(2.0f, 0.5f, 1.0f)};
        Ball ball{};
        std::vector<Block> blocks{};
        int score = 0;
        int misses = 0;
        float grid_rotation = 0.0f;
        float grid_y_rotation = 0.0f;
        float intro_rotation = 0.0f;
        float paddle_current_rotation = 0.0f;
        float background_time = 0.0f;
        float delta_seconds = 0.0f;
        float zoom = DEFAULT_ZOOM;
        bool paddle_rotating = false;
        mxvk::VK_Controller controller{};
        Uint64 last_tap_time = 0;
        std::chrono::steady_clock::time_point last_frame{std::chrono::steady_clock::now()};
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> mixer{};
        int ping_sound = -1;
        int clear_sound = -1;
        int die_sound = -1;
#endif
        static constexpr Uint64 DOUBLE_TAP_THRESHOLD_MS = 300;

        void reset_game() {
            score = 0;
            misses = 0;
            paddle.position = glm::vec3(0.0f, -3.5f, 0.0f);
            paddle_current_rotation = 0.0f;
            paddle_rotating = false;
            reset_ball();
            cleanup_block_models();
            blocks.clear();
            blocks.reserve(BLOCK_ROWS * BLOCK_COLUMNS);
            for (int row = 0; row < BLOCK_ROWS; ++row) {
                for (int column = 0; column < BLOCK_COLUMNS; ++column) {
                    Block block{};
                    block.box.position = glm::vec3(GAME_LEFT + static_cast<float>(column), 3.0f - static_cast<float>(row) * 0.6f, 0.0f);
                    block.box.size = glm::vec3(1.0f, 0.5f, 1.0f);
                    block.texture_manifest = block_manifest(std::rand() % 4);
                    block.model = load_model(block.texture_manifest);
                    blocks.push_back(std::move(block));
                }
            }
        }

        void reset_ball() {
            ball.radius = 0.25f;
            ball.stuck = true;
            ball.box.size = glm::vec3(ball.radius * 2.0f);
            ball.box.position = paddle.position + glm::vec3(0.0f, 0.5f, 0.0f);
            ball.box.velocity = glm::vec3(2.5f, 2.5f, 0.0f);
        }

        void update_game(float delta) {
            if (screen == Screen::Complete || screen == Screen::GameOver) {
                return;
            }

            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_H]) {
                paddle.position.x -= PADDLE_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_L]) {
                paddle.position.x += PADDLE_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_W]) {
                grid_rotation -= ROTATION_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_S]) {
                grid_rotation += ROTATION_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_A]) {
                grid_y_rotation -= ROTATION_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_D]) {
                grid_y_rotation += ROTATION_SPEED * delta;
            }
            if (keys[SDL_SCANCODE_PAGEUP]) {
                adjust_zoom(ZOOM_SPEED * delta);
            }
            if (keys[SDL_SCANCODE_PAGEDOWN]) {
                adjust_zoom(-ZOOM_SPEED * delta);
            }
            if (keys[SDL_SCANCODE_Q]) {
                grid_rotation = 0.0f;
                grid_y_rotation = 0.0f;
                zoom = DEFAULT_ZOOM;
            }
            update_gamepad_controls(delta);
            if (grid_rotation >= 360.0f) {
                grid_rotation -= 360.0f;
            }
            if (grid_rotation <= -360.0f) {
                grid_rotation += 360.0f;
            }
            if (grid_y_rotation >= 360.0f) {
                grid_y_rotation -= 360.0f;
            }
            if (grid_y_rotation <= -360.0f) {
                grid_y_rotation += 360.0f;
            }
            clamp_paddle();

            if (paddle_rotating) {
                paddle_current_rotation += 360.0f * delta;
                if (paddle_current_rotation >= 360.0f) {
                    paddle_current_rotation = 0.0f;
                    paddle_rotating = false;
                }
            }

            if (ball.stuck) {
                ball.box.position = paddle.position + glm::vec3(0.0f, 0.5f, 0.0f);
            } else {
                move_ball(delta);
            }

            bool all_destroyed = true;
            for (Block &block : blocks) {
                if (block.rotating) {
                    block.current_rotation += 360.0f * delta;
                    if (block.current_rotation >= 360.0f) {
                        block.current_rotation = 0.0f;
                        block.rotating = false;
                        block.destroyed = true;
                    }
                }
                all_destroyed = all_destroyed && block.destroyed;
            }
            if (all_destroyed) {
                screen = Screen::Intro;
            }
        }

        void move_ball(float delta) {
            ball.box.position += ball.box.velocity * delta;
            if (ball.box.position.x <= GAME_LEFT + ball.radius) {
                ball.box.position.x = GAME_LEFT + ball.radius;
                ball.box.velocity.x = std::abs(ball.box.velocity.x);
            }
            if (ball.box.position.x >= GAME_RIGHT - ball.radius) {
                ball.box.position.x = GAME_RIGHT - ball.radius;
                ball.box.velocity.x = -std::abs(ball.box.velocity.x);
            }
            if (ball.box.position.y >= GAME_TOP - ball.radius) {
                ball.box.position.y = GAME_TOP - ball.radius;
                ball.box.velocity.y = -std::abs(ball.box.velocity.y);
            }

            if (check_collision(ball, paddle) && ball.box.velocity.y < 0.0f) {
                const float distance = (ball.box.position.x - paddle.position.x) / (paddle.size.x * 0.5f);
                ball.box.velocity.x = clampf(distance, -1.0f, 1.0f) * BALL_SPEED;
                ball.box.velocity.y = std::abs(ball.box.velocity.y);
                ball.box.position.y = paddle.position.y + paddle.size.y * 0.5f + ball.radius;
                normalize_ball_velocity();
                if (!paddle_rotating) {
                    paddle_current_rotation = 0.0f;
                    paddle_rotating = true;
                }
                play_ping();
            }

            for (Block &block : blocks) {
                if (block.destroyed || block.rotating || !check_collision(ball, block.box)) {
                    continue;
                }
                block.rotating = true;
                block.current_rotation = 0.0f;
                score += 10;
                ball.box.velocity.y = -ball.box.velocity.y;
                if (ball.box.position.y > block.box.position.y) {
                    ball.box.position.y += ball.radius;
                } else {
                    ball.box.position.y -= ball.radius;
                }
                play_clear();
                break;
            }

            if (ball.box.position.y <= GAME_BOTTOM) {
                ++misses;
                play_die();
                if (misses >= MAX_MISSES) {
                    ball.stuck = true;
                    screen = Screen::GameOver;
                    return;
                }
                reset_ball();
            }
        }

        void normalize_ball_velocity() {
            const float speed = glm::length(ball.box.velocity);
            if (speed > 0.001f) {
                ball.box.velocity = (ball.box.velocity / speed) * BALL_SPEED;
            }
        }

        void clamp_paddle() {
            paddle.position.x = clampf(paddle.position.x, GAME_LEFT, GAME_RIGHT);
        }

        void adjust_zoom(float delta) {
            zoom = clampf(zoom + delta, MIN_ZOOM, MAX_ZOOM);
        }

        [[nodiscard]] static float normalized_gamepad_axis(Sint16 value) {
            if (std::abs(static_cast<int>(value)) <= GAMEPAD_DEAD_ZONE) {
                return 0.0f;
            }
            if (value < 0) {
                return static_cast<float>(value) / 32768.0f;
            }
            return static_cast<float>(value) / 32767.0f;
        }

        [[nodiscard]] bool gamepad_button(SDL_GamepadButton button) const {
            return controller.active() && controller.getButton(button);
        }

        void update_gamepad_controls(float delta) {
            if (!controller.active()) {
                return;
            }

            const float left_x = normalized_gamepad_axis(controller.getAxis(SDL_GAMEPAD_AXIS_LEFTX));
            const float right_x = normalized_gamepad_axis(controller.getAxis(SDL_GAMEPAD_AXIS_RIGHTX));
            const float right_y = normalized_gamepad_axis(controller.getAxis(SDL_GAMEPAD_AXIS_RIGHTY));

            paddle.position.x += left_x * PADDLE_SPEED * delta;
            if (gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                paddle.position.x -= PADDLE_SPEED * delta;
            }
            if (gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                paddle.position.x += PADDLE_SPEED * delta;
            }

            grid_y_rotation += right_x * ROTATION_SPEED * delta;
            grid_rotation += right_y * ROTATION_SPEED * delta;

            if (gamepad_button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                adjust_zoom(-ZOOM_SPEED * delta);
            }
            if (gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                adjust_zoom(ZOOM_SPEED * delta);
            }
        }

        [[nodiscard]] static bool is_gamepad_start_button(const SDL_Event &e) {
            if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                return false;
            }
            const auto button = static_cast<SDL_GamepadButton>(e.gbutton.button);
            return button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START;
        }

        bool open_controller() {
            for (int index = 0; index < mxvk::VK_Controller::joysticks(); ++index) {
                if (controller.open(index)) {
                    return true;
                }
            }
            return false;
        }

        void draw_model(VkCommandBuffer cmd,
                        uint32_t image_index,
                        mxvk::VKAbstractModel &model,
                        const Box &box,
                        const glm::mat4 &view,
                        const glm::mat4 &proj,
                        const glm::vec3 &tint) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::translate(glm::mat4(1.0f), box.position);
            ubo.model = glm::rotate(ubo.model, glm::radians(box.rotation), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, box.size * MODEL_SCALE);
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(tint, 1.0f);
            model.updateUBO(image_index, ubo);
            model.render(cmd, image_index, false);
        }

        void draw_background(VkCommandBuffer cmd, const VkExtent2D &extent) {
            mxvk::VK_Sprite *sprite = (screen == Screen::Intro) ? background : game_background;
            if (sprite == nullptr || extent.width == 0U || extent.height == 0U) {
                return;
            }
            sprite->setShaderParams(background_time, 0.7f, 0.0f, 0.0f);
            sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            renderStandaloneSprite(*sprite, cmd);
            sprite->clearQueue();
        }

        void print_centered_score(int width) {
            const std::string text = std::format("Score: {}", score);
            int text_width = 0;
            int text_height = 0;
            if (getTextDimensions(text, text_width, text_height)) {
                printText(text, (width - text_width) / 2, 0, {255, 255, 255, 255});
                return;
            }
            printText(text, width / 2 - 60, 0, {255, 255, 255, 255});
        }

        void print_tries() {
            const int tries_left = std::max(0, MAX_MISSES - misses);
            printText(std::format("Tries: {}", tries_left), 25, 0, {255, 255, 255, 255});
        }

        void print_centered_game_over(int width) {
            const std::string text = "Game Over - Press Enter to Restart";
            int text_width = 0;
            int text_height = 0;
            if (getTextDimensions(text, text_width, text_height)) {
                printText(text, (width - text_width) / 2, screen_height() / 2 - text_height / 2, {255, 255, 255, 255});
                return;
            }
            printText(text, width / 2 - 200, screen_height() / 2, {255, 255, 255, 255});
        }

        [[nodiscard]] std::unique_ptr<mxvk::VKAbstractModel> load_model(std::string_view manifest) {
            auto model = std::make_unique<mxvk::VKAbstractModel>();
            model->load(this, data_root + "/cube.mxmod.z", data_root + "/" + std::string(manifest), data_root, 1.0f);
            model->setShaders(this,
                              std::string(breakout_SHADER_DIR) + "/breakout_model.vert.spv",
                              std::string(breakout_SHADER_DIR) + "/breakout_model.frag.spv");
            return model;
        }

        [[nodiscard]] static std::string_view block_manifest(int index) {
            switch (index) {
            case 0:
                return "texture0_manifest.txt";
            case 1:
                return "texture1_manifest.txt";
            case 2:
                return "texture2_manifest.txt";
            default:
                return "texture3_manifest.txt";
            }
        }

        void cleanup_block_models() {
            for (Block &block : blocks) {
                if (block.model) {
                    block.model->cleanup(this);
                }
            }
        }

        void cleanup_models() {
            cleanup_block_models();
            if (intro_model) {
                intro_model->cleanup(this);
            }
            if (paddle_model) {
                paddle_model->cleanup(this);
            }
            if (ball_model) {
                ball_model->cleanup(this);
            }
        }

        [[nodiscard]] int screen_width() const {
            return swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : 1280;
        }

        [[nodiscard]] int screen_height() const {
            return swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : 720;
        }

        void play_ping() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (mixer != nullptr && ping_sound >= 0) {
                mixer->playWav(ping_sound, 0, 0);
            }
#endif
        }

        void play_clear() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (mixer != nullptr && clear_sound >= 0) {
                mixer->playWav(clear_sound, 0, 1);
            }
#endif
        }

        void play_die() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (mixer != nullptr && die_sound >= 0) {
                mixer->playWav(die_sound, 0, 2);
            }
#endif
        }
    };
} // namespace

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        BreakoutWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
