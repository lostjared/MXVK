#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

#ifndef puzzle_drop_ASSET_DIR
#define puzzle_drop_ASSET_DIR "."
#endif

#ifndef puzzle_drop_TETRIS_DATA_DIR
#define puzzle_drop_TETRIS_DATA_DIR "."
#endif

#ifndef puzzle_drop_FONT_PATH
#define puzzle_drop_FONT_PATH "."
#endif

#ifndef puzzle_drop_SHADER_DIR
#define puzzle_drop_SHADER_DIR "."
#endif

namespace {
    constexpr int BOARD_WIDTH = 20;
    constexpr int BOARD_HEIGHT = 22;
    constexpr float CUBE_SCALE = 0.048f;
    constexpr float CUBE_SPACING = CUBE_SCALE * 1.12f;
    constexpr std::array<float, 3> FALL_SECONDS{0.86f, 0.68f, 0.50f};
    constexpr float INTRO_FADE_STEP = 0.01f;
    constexpr Uint32 INTRO_FADE_INTERVAL_MS = 35U;
    constexpr float INTRO_START_FADE = 1.0f;
    constexpr size_t FRAME_TEXTURE_INDEX = 10;

    enum class BlockType {
        Null = 0,
        Clear,
        Red1,
        Red2,
        Red3,
        Green1,
        Green2,
        Green3,
        Blue1,
        Blue2,
        Blue3,
        Match,
    };

    enum class ShiftDirection {
        Down,
        Up,
    };

    struct Block {
        int x = 0;
        int y = 0;
        BlockType type = BlockType::Null;
    };

    struct Piece {
        std::array<Block, 3> blocks{};
        int position = 0;

        void new_piece(int start_x, int start_y, std::mt19937 &rng) {
            blocks[0] = {start_x, start_y, random_type(rng)};
            blocks[1] = {start_x, start_y + 1, random_type(rng)};
            blocks[2] = {start_x, start_y + 2, random_type(rng)};
            position = 0;
        }

        void shift(ShiftDirection direction) {
            const std::array<BlockType, 3> types{blocks[0].type, blocks[1].type, blocks[2].type};
            if (direction == ShiftDirection::Down) {
                blocks[0].type = types[2];
                blocks[1].type = types[0];
                blocks[2].type = types[1];
            } else {
                blocks[0].type = types[1];
                blocks[1].type = types[2];
                blocks[2].type = types[0];
            }
        }

        void move_left() {
            for (Block &block : blocks) {
                --block.x;
            }
        }

        void move_right() {
            for (Block &block : blocks) {
                ++block.x;
            }
        }

        void move_down() {
            for (Block &block : blocks) {
                ++block.y;
            }
        }

        void rotate_left() {
            if (position == 0) {
                blocks[1].y -= 1;
                blocks[1].x -= 1;
                blocks[2].x -= 2;
                blocks[2].y -= 2;
                position = 1;
            } else if (position == 1) {
                blocks[1].y += 1;
                blocks[1].x += 1;
                blocks[2].y += 2;
                blocks[2].x += 2;
                position = 0;
            }
        }

        void rotate_right() {
            if (position == 0) {
                blocks[1].x += 1;
                blocks[1].y -= 1;
                blocks[2].x += 2;
                blocks[2].y -= 2;
                position = 2;
            } else if (position == 2) {
                blocks[1].x -= 1;
                blocks[1].y += 1;
                blocks[2].x -= 2;
                blocks[2].y += 2;
                position = 0;
            }
        }

      private:
        [[nodiscard]] static BlockType random_type(std::mt19937 &rng) {
            std::uniform_int_distribution<int> dist(static_cast<int>(BlockType::Red1), static_cast<int>(BlockType::Match));
            return static_cast<BlockType>(dist(rng));
        }
    };

    struct Cell {
        BlockType type = BlockType::Null;
        int clear_value = 0;
        int flash_counter = 0;
    };

    const std::array<glm::vec3, 10> BLOCK_TINTS{{
        {1.00f, 0.48f, 0.42f},
        {1.00f, 0.58f, 0.50f},
        {1.00f, 0.72f, 0.62f},
        {0.42f, 0.95f, 0.55f},
        {0.55f, 1.00f, 0.65f},
        {0.70f, 1.00f, 0.78f},
        {0.38f, 0.62f, 1.00f},
        {0.50f, 0.74f, 1.00f},
        {0.66f, 0.86f, 1.00f},
        {1.00f, 0.92f, 0.35f},
    }};

    [[nodiscard]] bool is_play_block(BlockType type) {
        return type >= BlockType::Red1 && type <= BlockType::Match;
    }

    [[nodiscard]] int texture_index(BlockType type) {
        if (!is_play_block(type)) {
            return 0;
        }
        return static_cast<int>(type) - static_cast<int>(BlockType::Red1);
    }

    [[nodiscard]] bool same_or_match(BlockType actual, BlockType expected) {
        return actual == expected || actual == BlockType::Match;
    }

    class PuzzleDropWindow final : public mxvk::VK_Window {
      public:
        PuzzleDropWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ MXVK 3D PuzzleDrop ]-", width, height, fullscreen, MXVK_VALIDATION),
              asset_root((path.empty() || path == ".") ? std::string(puzzle_drop_ASSET_DIR) : path),
              data_root(asset_root + "/data"),
              tetris_data_root(puzzle_drop_TETRIS_DATA_DIR) {
            std::random_device rd;
            rng.seed(rd());
            setClearColor(0.03f, 0.04f, 0.05f, 1.0f);
            setFont(puzzle_drop_FONT_PATH, 22);
            for (int i = 0; i < static_cast<int>(backgrounds.size()); ++i) {
                backgrounds[i] = createSprite(
                    std::format("{}/level{}.png", data_root, i + 1),
                    std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                    std::string(puzzle_drop_SHADER_DIR) + "/puzzle_drop_background.frag.spv");
            }
            intro_sprite = createSprite(
                std::format("{}/intro1.png", data_root),
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(puzzle_drop_SHADER_DIR) + "/intro.frag.spv");
            init_cube_model();
            reset_game();
            reset_intro_screen();
        }

        ~PuzzleDropWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            cleanup_models();
        }

        void onSwapchainRecreated() override {
            if (cube_model) {
                cube_model->resize(this);
            }
            if (grid_backdrop_model) {
                grid_backdrop_model->resize(this);
            }
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_QUIT) {
                exit();
                return;
            }
            if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) {
                return;
            }

            if (intro_active && (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                skip_intro();
                return;
            }

            switch (e.key.key) {
            case SDLK_ESCAPE:
                exit();
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (game_over) {
                    reset_game();
                    game_started = true;
                }
                break;
            case SDLK_SPACE:
                if (!game_over) {
                    reset_game();
                    game_started = true;
                }
                break;
            case SDLK_1:
                difficulty = 0;
                reset_game();
                game_started = true;
                break;
            case SDLK_2:
                difficulty = 1;
                reset_game();
                game_started = true;
                break;
            case SDLK_3:
                difficulty = 2;
                reset_game();
                game_started = true;
                break;
            case SDLK_Z:
                rotate_left();
                break;
            case SDLK_X:
                rotate_right();
                break;
            default:
                break;
            }
        }

        void proc() override {
            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - last_input_update).count();
            last_input_update = now;
            randomize_wildcard_color();

            if (intro_active) {
                update_intro(now);
            } else {
                background_time += delta_seconds;
                const bool *keys = SDL_GetKeyboardState(nullptr);
                if (keys != nullptr) {
                    handle_view_controls(keys, delta_seconds);
                    handle_piece_controls(keys, delta_seconds);
                }
            }

            if (game_started && !game_over) {
                if (std::chrono::duration<float>(now - last_fall).count() >= FALL_SECONDS[difficulty]) {
                    key_down();
                    last_fall = now;
                }
                if (std::chrono::duration<float>(now - last_process).count() >= 0.018f) {
                    proc_blocks();
                    proc_move_down();
                    last_process = now;
                }
            }

            const SDL_Color primary{255, 244, 223, 255};
            const SDL_Color secondary{218, 229, 232, 255};
            if (intro_active) {
                printText("Press Enter", 24, 54, secondary);
                return;
            }

            if (game_over) {
                print_game_over_text(secondary);
                return;
            }

            printText(std::format("Level {}   Lines {}   Difficulty {}", level, lines, difficulty + 1), 24, 22, primary);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            const VkExtent2D extent = getSwapchainExtent();
            if (intro_active) {
                render_intro(cmd, extent);
                return;
            }

            draw_background(cmd, extent);

            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.12f, camera_distance), glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            view = glm::rotate(view, glm::radians(grid_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
            view = glm::rotate(view, glm::radians(grid_yaw), glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            draw_grid_backdrop(cmd, image_index, view, proj);
            draw_frame(cmd, image_index, view, proj);
            for (int y = 0; y < BOARD_HEIGHT; ++y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    Cell &cell = board[y][x];
                    if (cell.type == BlockType::Null) {
                        continue;
                    }
                    if (cell.type == BlockType::Clear && ((cell.flash_counter / 6) % 2) != 0) {
                        continue;
                    }
                    draw_cube(cmd, image_index, cell.type, x, y, view, proj);
                }
            }

            if (game_started && !game_over) {
                for (const Block &block : piece.blocks) {
                    draw_cube(cmd, image_index, block.type, block.x, block.y, view, proj);
                }
            }
        }

      private:
        std::string asset_root;
        std::string data_root;
        std::string tetris_data_root;
        std::mt19937 rng{};
        std::array<std::array<Cell, BOARD_WIDTH>, BOARD_HEIGHT> board{};
        Piece piece{};
        std::unique_ptr<mxvk::VKAbstractModel> cube_model{};
        std::unique_ptr<mxvk::VKAbstractModel> grid_backdrop_model{};
        std::array<mxvk::VK_Sprite *, 8> backgrounds{};
        mxvk::VK_Sprite *intro_sprite = nullptr;
        std::chrono::steady_clock::time_point last_fall{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_process{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_input_update{std::chrono::steady_clock::now()};
        float horizontal_move_timer = 0.0f;
        float soft_drop_timer = 0.0f;
        float cycle_timer = 0.0f;
        int horizontal_move_direction = 0;
        bool soft_drop_held = false;
        bool cycle_held = false;
        int difficulty = 0;
        int level = 1;
        int lines = 0;
        glm::vec3 wildcard_color{1.0f, 0.0f, 1.0f};
        bool intro_active = true;
        float intro_fade = INTRO_START_FADE;
        std::chrono::steady_clock::time_point intro_last_update{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point intro_start{std::chrono::steady_clock::now()};
        float background_time = 0.0f;
        bool game_started = false;
        bool game_over = false;
        float grid_yaw = -10.0f;
        float grid_pitch = -8.0f;
        float camera_distance = 2.32f;

        void randomize_wildcard_color() {
            std::uniform_int_distribution<int> dist(0, 254);
            wildcard_color = glm::vec3(
                static_cast<float>(dist(rng)) / 255.0f,
                static_cast<float>(dist(rng)) / 255.0f,
                static_cast<float>(dist(rng)) / 255.0f);
        }

        void handle_view_controls(const bool *keys, float delta_seconds) {
            constexpr float YAW_SPEED = 115.0f;
            constexpr float PITCH_SPEED = 90.0f;
            constexpr float ZOOM_SPEED = 3.2f;
            if (keys[SDL_SCANCODE_A]) {
                grid_yaw -= YAW_SPEED * delta_seconds;
            }
            if (keys[SDL_SCANCODE_D]) {
                grid_yaw += YAW_SPEED * delta_seconds;
            }
            if (keys[SDL_SCANCODE_W]) {
                grid_pitch = std::clamp(grid_pitch + PITCH_SPEED * delta_seconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_S]) {
                grid_pitch = std::clamp(grid_pitch - PITCH_SPEED * delta_seconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_PAGEUP]) {
                camera_distance = std::max(1.35f, camera_distance - ZOOM_SPEED * delta_seconds);
            }
            if (keys[SDL_SCANCODE_PAGEDOWN]) {
                camera_distance = std::min(7.0f, camera_distance + ZOOM_SPEED * delta_seconds);
            }
        }

        void handle_piece_controls(const bool *keys, float delta_seconds) {
            if (!game_started || game_over) {
                reset_held_piece_input();
                return;
            }

            const bool left = keys[SDL_SCANCODE_LEFT];
            const bool right = keys[SDL_SCANCODE_RIGHT];
            const int direction = (left == right) ? 0 : (left ? -1 : 1);
            if (direction == 0) {
                horizontal_move_direction = 0;
                horizontal_move_timer = 0.0f;
            } else {
                constexpr float INITIAL_DELAY_SECONDS = 0.16f;
                constexpr float REPEAT_SECONDS = 0.065f;
                if (horizontal_move_direction != direction) {
                    horizontal_move_direction = direction;
                    horizontal_move_timer = -INITIAL_DELAY_SECONDS;
                    move_piece_horizontal(direction);
                } else {
                    horizontal_move_timer += delta_seconds;
                    while (horizontal_move_timer >= 0.0f) {
                        horizontal_move_timer -= REPEAT_SECONDS;
                        move_piece_horizontal(direction);
                    }
                }
            }

            if (keys[SDL_SCANCODE_DOWN]) {
                constexpr float SOFT_DROP_REPEAT_SECONDS = 0.045f;
                if (!soft_drop_held) {
                    soft_drop_held = true;
                    soft_drop_timer = 0.0f;
                    key_down();
                    last_fall = std::chrono::steady_clock::now();
                } else {
                    soft_drop_timer += delta_seconds;
                    while (soft_drop_timer >= SOFT_DROP_REPEAT_SECONDS) {
                        soft_drop_timer -= SOFT_DROP_REPEAT_SECONDS;
                        key_down();
                        last_fall = std::chrono::steady_clock::now();
                    }
                }
            } else {
                soft_drop_held = false;
                soft_drop_timer = 0.0f;
            }

            if (keys[SDL_SCANCODE_UP]) {
                constexpr float CYCLE_INITIAL_DELAY_SECONDS = 0.16f;
                constexpr float CYCLE_REPEAT_SECONDS = 0.11f;
                if (!cycle_held) {
                    cycle_held = true;
                    cycle_timer = -CYCLE_INITIAL_DELAY_SECONDS;
                    cycle_piece_blocks();
                } else {
                    cycle_timer += delta_seconds;
                    while (cycle_timer >= 0.0f) {
                        cycle_timer -= CYCLE_REPEAT_SECONDS;
                        cycle_piece_blocks();
                    }
                }
            } else {
                cycle_held = false;
                cycle_timer = 0.0f;
            }
        }

        void reset_held_piece_input() {
            horizontal_move_timer = 0.0f;
            soft_drop_timer = 0.0f;
            cycle_timer = 0.0f;
            horizontal_move_direction = 0;
            soft_drop_held = false;
            cycle_held = false;
        }

        void move_piece_horizontal(int direction) {
            if (!check_piece(piece, direction, 0)) {
                return;
            }
            if (direction < 0) {
                piece.move_left();
            } else {
                piece.move_right();
            }
        }

        void cycle_piece_blocks() {
            piece.shift(ShiftDirection::Up);
        }

        void reset_game() {
            reset_held_piece_input();
            for (auto &row : board) {
                for (Cell &cell : row) {
                    cell.type = BlockType::Null;
                    cell.clear_value = 0;
                    cell.flash_counter = 0;
                }
            }
            level = 1;
            lines = 0;
            game_over = false;
            piece.new_piece(BOARD_WIDTH / 2, 0, rng);
            last_fall = std::chrono::steady_clock::now();
            last_process = last_fall;
        }

        void key_down() {
            if (check_piece(piece, 0, 1)) {
                piece.move_down();
                return;
            }
            set_piece();
            piece.new_piece(BOARD_WIDTH / 2, 0, rng);
            if (!check_piece(piece, 0, 0)) {
                game_over = true;
            }
        }

        [[nodiscard]] bool check_piece(const Piece &test_piece, int offset_x, int offset_y) const {
            for (const Block &block : test_piece.blocks) {
                const int x = block.x + offset_x;
                const int y = block.y + offset_y;
                if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
                    return false;
                }
                const BlockType type = board[y][x].type;
                if (type != BlockType::Null && type != BlockType::Clear) {
                    return false;
                }
            }
            return true;
        }

        void set_piece() {
            for (const Block &block : piece.blocks) {
                if (block.x < 0 || block.x >= BOARD_WIDTH || block.y < 0 || block.y >= BOARD_HEIGHT) {
                    continue;
                }
                Cell &cell = board[block.y][block.x];
                cell.type = block.type;
                cell.clear_value = 0;
                cell.flash_counter = 0;
                if (block.y == 0) {
                    game_over = true;
                }
            }
        }

        void rotate_left() {
            if (!game_started || game_over) {
                return;
            }
            Piece test_piece = piece;
            test_piece.rotate_left();
            if (check_piece(test_piece, 0, 0)) {
                piece.rotate_left();
            }
        }

        void rotate_right() {
            if (!game_started || game_over) {
                return;
            }
            Piece test_piece = piece;
            test_piece.rotate_right();
            if (check_piece(test_piece, 0, 0)) {
                piece.rotate_right();
            }
        }

        bool proc_blocks() {
            constexpr std::array<std::array<int, 2>, 4> directions{{
                {{1, 0}},
                {{0, 1}},
                {{1, 1}},
                {{1, -1}},
            }};
            constexpr std::array<BlockType, 3> color_starts{BlockType::Red1, BlockType::Green1, BlockType::Blue1};

            for (int y = 0; y < BOARD_HEIGHT; ++y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    for (const auto &direction : directions) {
                        for (BlockType start : color_starts) {
                            const BlockType one = start;
                            const BlockType two = static_cast<BlockType>(static_cast<int>(start) + 1);
                            const BlockType three = static_cast<BlockType>(static_cast<int>(start) + 2);
                            if (check_sequence(x, y, direction[0], direction[1], one, two, three) ||
                                check_sequence(x, y, direction[0], direction[1], three, two, one)) {
                                mark_clear(x, y, direction[0], direction[1]);
                                add_score();
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        bool proc_move_down() {
            for (int y = BOARD_HEIGHT - 2; y >= 0; --y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    Cell &source = board[y][x];
                    Cell &target = board[y + 1][x];
                    if (is_play_block(source.type) && target.type == BlockType::Null) {
                        target.type = source.type;
                        target.clear_value = source.clear_value;
                        target.flash_counter = source.flash_counter;
                        source.type = BlockType::Null;
                        source.clear_value = 0;
                        source.flash_counter = 0;
                        return true;
                    }
                }
            }

            bool updated = false;
            for (auto &row : board) {
                for (Cell &cell : row) {
                    if (cell.type == BlockType::Clear) {
                        ++cell.clear_value;
                        ++cell.flash_counter;
                        if (cell.clear_value > 50) {
                            cell.type = BlockType::Null;
                            cell.clear_value = 0;
                            cell.flash_counter = 0;
                        }
                        updated = true;
                    }
                }
            }
            return updated;
        }

        [[nodiscard]] bool check_sequence(int x, int y, int dx, int dy, BlockType first, BlockType second, BlockType third) const {
            return check_block(x, y, first) && check_block(x + dx, y + dy, second) && check_block(x + dx * 2, y + dy * 2, third);
        }

        [[nodiscard]] bool check_block(int x, int y, BlockType expected) const {
            if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
                return false;
            }
            return same_or_match(board[y][x].type, expected);
        }

        void mark_clear(int x, int y, int dx, int dy) {
            for (int i = 0; i < 3; ++i) {
                Cell &cell = board[y + dy * i][x + dx * i];
                cell.type = BlockType::Clear;
                cell.clear_value = 1;
                cell.flash_counter = 0;
            }
        }

        void add_score() {
            ++lines;
            if ((lines % 6) == 0 && level < 7) {
                ++level;
            }
        }

        void init_cube_model() {
            cube_model = std::make_unique<mxvk::VKAbstractModel>();
            cube_model->load(this,
                             tetris_data_root + "/cube.mxmod.z",
                             data_root + "/cube_textures.txt",
                             data_root,
                             1.0f);
            cube_model->setShaders(this,
                                   std::string(puzzle_drop_SHADER_DIR) + "/puzzle_drop_piece.vert.spv",
                                   std::string(puzzle_drop_SHADER_DIR) + "/puzzle_drop_piece.frag.spv");

            grid_backdrop_model = std::make_unique<mxvk::VKAbstractModel>();
            grid_backdrop_model->load(this,
                                      tetris_data_root + "/cube.mxmod.z",
                                      tetris_data_root + "/manifest_gray.txt",
                                      tetris_data_root,
                                      1.0f);
            grid_backdrop_model->setShaders(this,
                                            std::string(puzzle_drop_SHADER_DIR) + "/puzzle_drop_piece.vert.spv",
                                            std::string(puzzle_drop_SHADER_DIR) + "/puzzle_drop_piece.frag.spv");
            grid_backdrop_model->setAlphaBlending(true);
        }

        void cleanup_models() {
            if (cube_model) {
                cube_model->cleanup(this);
                cube_model.reset();
            }
            if (grid_backdrop_model) {
                grid_backdrop_model->cleanup(this);
                grid_backdrop_model.reset();
            }
        }

        void print_game_over_text(const SDL_Color &color) {
            const std::string text = std::format("Game Over: Lines cleared: {} [Press Enter to Restart]", lines);
            const VkExtent2D extent = getSwapchainExtent();
            int text_width = 0;
            int text_height = 0;
            if (!getTextDimensions(text, text_width, text_height)) {
                printText(text, 24, 54, color);
                return;
            }

            const int screen_width = static_cast<int>(extent.width);
            const int screen_height = static_cast<int>(extent.height);
            const int x = std::max(0, (screen_width - text_width) / 2);
            const int y = std::max(0, (screen_height - text_height) / 2);
            printText(text, x, y, color);
        }

        void reset_intro_screen() {
            intro_active = true;
            intro_fade = INTRO_START_FADE;
            intro_last_update = std::chrono::steady_clock::now();
            intro_start = intro_last_update;
            game_started = false;
            reset_held_piece_input();
        }

        void skip_intro() {
            intro_fade = std::min(intro_fade, 0.02f);
            intro_last_update = std::chrono::steady_clock::now() - std::chrono::milliseconds(INTRO_FADE_INTERVAL_MS);
        }

        void finish_intro(const std::chrono::steady_clock::time_point &now) {
            intro_active = false;
            intro_fade = 0.0f;
            game_started = true;
            last_fall = now;
            last_process = now;
            last_input_update = now;
            reset_held_piece_input();
        }

        void update_intro(const std::chrono::steady_clock::time_point &now) {
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - intro_last_update).count();
            if (elapsed_ms > static_cast<long long>(INTRO_FADE_INTERVAL_MS)) {
                intro_last_update = now;
                intro_fade -= INTRO_FADE_STEP;
            }
            if (intro_fade <= 0.0f) {
                finish_intro(now);
            }
        }

        void render_intro(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (intro_sprite == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                finish_intro(std::chrono::steady_clock::now());
                return;
            }

            const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - intro_start).count();
            intro_sprite->setShaderParams(elapsed, 0.0f, 0.0f, std::clamp(intro_fade, 0.0f, 1.0f));
            intro_sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            intro_sprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            intro_sprite->clearQueue();
        }

        void draw_background(VkCommandBuffer cmd, const VkExtent2D &extent) {
            const int index = std::clamp(level - 1, 0, static_cast<int>(backgrounds.size()) - 1);
            mxvk::VK_Sprite *background = backgrounds[index];
            if (background == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                return;
            }
            background->setShaderParams(background_time, 0.0f, 0.0f, 1.0f);
            background->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            background->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            background->clearQueue();
        }

        [[nodiscard]] glm::vec3 block_position(int x, int y) const {
            const float world_x = (static_cast<float>(x) - (static_cast<float>(BOARD_WIDTH) - 1.0f) * 0.5f) * CUBE_SPACING;
            const float world_y = ((static_cast<float>(BOARD_HEIGHT) - 1.0f) * 0.5f - static_cast<float>(y)) * CUBE_SPACING;
            return glm::vec3(world_x, world_y, 0.0f);
        }

        void draw_grid_backdrop(VkCommandBuffer cmd, uint32_t image_index, const glm::mat4 &view, const glm::mat4 &proj) {
            if (!grid_backdrop_model) {
                return;
            }

            mxvk::UniformBufferObject ubo{};
            glm::mat4 model(1.0f);
            const float width = static_cast<float>(BOARD_WIDTH) * CUBE_SPACING;
            const float height = static_cast<float>(BOARD_HEIGHT) * CUBE_SPACING;
            model = glm::translate(model, glm::vec3(0.0f, 0.0f, -CUBE_SCALE * 0.68f));
            model = glm::scale(model, glm::vec3(width + CUBE_SPACING * 0.35f, height + CUBE_SPACING * 0.35f, CUBE_SCALE * 0.10f));
            ubo.model = model;
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(0.16f, 0.16f, 0.17f, 0.62f);
            grid_backdrop_model->renderWithPushConstants(cmd, image_index, 0, ubo, false);
        }

        [[nodiscard]] glm::mat4 block_matrix(int x, int y, float scale = CUBE_SCALE) const {
            glm::mat4 model(1.0f);
            model = glm::translate(model, block_position(x, y));
            model = glm::scale(model, glm::vec3(scale));
            return model;
        }

        void draw_cube(VkCommandBuffer cmd,
                       uint32_t image_index,
                       BlockType type,
                       int x,
                       int y,
                       const glm::mat4 &view,
                       const glm::mat4 &proj) const {
            if (!cube_model) {
                return;
            }

            mxvk::UniformBufferObject ubo{};
            ubo.model = block_matrix(x, y);
            ubo.view = view;
            ubo.proj = proj;
            const int index = texture_index(type);
            glm::vec3 tint = BLOCK_TINTS[index];
            if (type == BlockType::Clear) {
                tint = BLOCK_TINTS[(x + y + lines) % 9];
            } else if (type == BlockType::Match) {
                tint = wildcard_color;
            }
            ubo.fx = glm::vec4(tint, type == BlockType::Match ? 2.0f : 1.0f);
            cube_model->renderWithPushConstants(cmd, image_index, static_cast<size_t>(index), ubo, false);
        }

        void draw_frame(VkCommandBuffer cmd, uint32_t image_index, const glm::mat4 &view, const glm::mat4 &proj) const {
            for (int y = 0; y < BOARD_HEIGHT; ++y) {
                draw_frame_cube(cmd, image_index, -1, y, view, proj);
                draw_frame_cube(cmd, image_index, BOARD_WIDTH, y, view, proj);
            }
            for (int x = -1; x <= BOARD_WIDTH; ++x) {
                draw_frame_cube(cmd, image_index, x, BOARD_HEIGHT, view, proj);
            }
        }

        void draw_frame_cube(VkCommandBuffer cmd,
                             uint32_t image_index,
                             int x,
                             int y,
                             const glm::mat4 &view,
                             const glm::mat4 &proj) const {
            if (!cube_model) {
                return;
            }

            mxvk::UniformBufferObject ubo{};
            ubo.model = block_matrix(x, y, CUBE_SCALE * 0.82f);
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(0.58f, 0.62f, 0.66f, 1.0f);
            cube_model->renderWithPushConstants(cmd, image_index, FRAME_TEXTURE_INDEX, ubo, false);
        }
    };
} // namespace

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        PuzzleDropWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << std::format("puzzle_drop: Exception: {}\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
