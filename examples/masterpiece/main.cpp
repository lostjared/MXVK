#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr int board_rows = 18;
    constexpr int board_cols = 8;
    constexpr int piece_height = 3;
    constexpr int max_scores = 8;
    constexpr int base_width = 1440;
    constexpr int base_height = 1080;
    constexpr int game_base_width = 640;
    constexpr int game_base_height = 480;
    constexpr int game_board_start_x = 184;
    constexpr int game_board_start_y = 78;
    constexpr int game_block_width = 31;
    constexpr int game_block_height = 14;
    constexpr int game_block_spacing = 1;
    constexpr int game_next_panel_x = 450;
    constexpr int game_next_panel_y = 180;
    constexpr int menu_item_count = 4;
    constexpr int title_screen_time_ms = 1500;
    constexpr int flash_time_ms = 180;
    constexpr int lines_per_speedup = 10;
    constexpr int score_points_per_match = 6;
    constexpr int max_name_length = 16;
    constexpr Uint32 joy_repeat_delay_ms = 180;
    constexpr Sint16 joystick_dead_zone = 16000;

    enum class Screen {
        Intro,
        Menu,
        Game,
        Scores,
        Credits,
        NameEntry,
    };

    struct Cell {
        int color = 0;
        Uint64 flash_until = 0;
    };

    struct Piece {
        int x = 3;
        int y = 0;
        std::array<int, piece_height> colors{};
        std::array<int, piece_height> next_colors{};
    };

    struct ScoreEntry {
        std::string name;
        int score = 0;
    };

    bool hasResolutionArgument(int argc, char **argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i] == nullptr ? std::string{} : std::string(argv[i]);
            if (arg == "-r" || arg == "-R" || arg == "--resolution") {
                return true;
            }
        }
        return false;
    }

    std::filesystem::path resolveAssetRoot(const std::string &path) {
        if (!path.empty() && path != "." && path != "./") {
            return std::filesystem::path(path);
        }
        return std::filesystem::path(MASTERPIECE_ASSET_DIR);
    }

    std::filesystem::path resolvePuzzleAssetRoot(const std::filesystem::path &asset_root) {
        const std::filesystem::path shared_root = asset_root.parent_path() / "puzzle";
        if (std::filesystem::exists(shared_root / "data" / "gamebg.png")) {
            return shared_root;
        }
        return asset_root;
    }

    std::filesystem::path scorePath(const std::filesystem::path &asset_root) {
        return asset_root / "data" / "scores.dat";
    }

    class HighScores {
      public:
        explicit HighScores(std::filesystem::path file_path)
            : file_path(std::move(file_path)) {
            load();
        }

        void add(std::string name, int score) {
            normalize(name);
            entries.push_back({std::move(name), score});
            sortAndTrim();
            save();
        }

        [[nodiscard]] bool qualifies(int score) const {
            if (entries.size() < max_scores) {
                return true;
            }
            return score > entries.back().score;
        }

        [[nodiscard]] const std::vector<ScoreEntry> &list() const {
            return entries;
        }

      private:
        std::filesystem::path file_path;
        std::vector<ScoreEntry> entries;

        static void normalize(std::string &name) {
            std::string cleaned;
            cleaned.reserve(name.size());
            for (unsigned char ch : name) {
                if (ch >= 32 && ch < 127 && ch != ':') {
                    cleaned.push_back(static_cast<char>(ch));
                }
            }

            if (cleaned.empty()) {
                cleaned = "Player";
            }
            if (cleaned.size() > max_name_length) {
                cleaned.resize(max_name_length);
            }
            name = std::move(cleaned);
        }

        void sortAndTrim() {
            std::sort(entries.begin(), entries.end(), [](const ScoreEntry &a, const ScoreEntry &b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                return a.name < b.name;
            });

            if (entries.size() > max_scores) {
                entries.resize(max_scores);
            }
        }

        void initDefaults() {
            entries.clear();
            for (int i = 0; i < max_scores; ++i) {
                entries.push_back({"Anonymous", 0});
            }
        }

        void load() {
            entries.clear();

            std::ifstream in(file_path);
            if (!in.is_open()) {
                initDefaults();
                return;
            }

            std::string line;
            while (std::getline(in, line)) {
                const std::size_t sep = line.find(':');
                if (sep == std::string::npos) {
                    continue;
                }

                ScoreEntry entry{};
                entry.name = line.substr(0, sep);
                entry.score = static_cast<int>(std::strtol(line.substr(sep + 1).c_str(), nullptr, 10));
                normalize(entry.name);
                entries.push_back(std::move(entry));
            }

            if (entries.empty()) {
                initDefaults();
                save();
                return;
            }

            sortAndTrim();
        }

        void save() const {
            std::error_code ec;
            std::filesystem::create_directories(file_path.parent_path(), ec);

            std::ofstream out(file_path, std::ios::trunc);
            if (!out.is_open()) {
                return;
            }

            for (const ScoreEntry &entry : entries) {
                out << entry.name << ':' << entry.score << '\n';
            }
        }
    };

    struct Layout {
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        float game_scale = 1.0f;
        int width = base_width;
        int height = base_height;
        int game_x = 0;
        int game_y = 0;
        int game_w = base_width;
        int game_h = base_height;
        int board_x = 185;
        int board_y = 95;
        int cell_w = 32;
        int cell_h = 16;
        int next_x = 510;
        int next_y = 200;
        int menu_x = 505;
        int menu_y = 400;
        int menu_w = 430;
        int menu_h = 82;
        int menu_step = 118;
    };

} // namespace

namespace example {

    class MasterPieceWindow final : public mxvk::VK_Window {
      public:
        MasterPieceWindow(const std::string &path, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MasterPiece", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              asset_root(resolveAssetRoot(path)),
              puzzle_asset_root(resolvePuzzleAssetRoot(resolveAssetRoot(path))),
              high_scores(scorePath(asset_root)) {
            setClearColor(0.02f, 0.02f, 0.03f, 1.0f);

            const std::string font_path = dataPath("font.ttf");
            title_font.reset(font_path, 34);
            ui_font.reset(font_path, 20);
            setFont(font_path, 20);

            loadSprites();
            resetGame();
            setScreen(Screen::Intro);
            tryOpenFirstGamepad();
        }

        ~MasterPieceWindow() override {
            closeGamepad();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_QUIT) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                openGamepad(e.gdevice.which);
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad != nullptr && e.gdevice.which == gamepadId) {
                    closeGamepad();
                    tryOpenFirstGamepad();
                }
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                handleControllerButton(e.gbutton.button);
                return;
            }

            if (screen == Screen::NameEntry && e.type == SDL_EVENT_TEXT_INPUT) {
                handleNameText(e.text.text);
                return;
            }

            if (e.type != SDL_EVENT_KEY_DOWN) {
                return;
            }

            switch (screen) {
            case Screen::Intro:
                if (isConfirmKey(e.key.key) || e.key.key == SDLK_ESCAPE) {
                    setScreen(Screen::Menu);
                }
                break;
            case Screen::Menu:
                handleMenuKey(e.key.key);
                break;
            case Screen::Game:
                handleGameKey(e.key.key);
                break;
            case Screen::Scores:
            case Screen::Credits:
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_ESCAPE) {
                    setScreen(Screen::Menu);
                }
                break;
            case Screen::NameEntry:
                handleNameKey(e.key.key);
                break;
            }
        }

        void proc() override {
            const Uint64 now = SDL_GetTicks();
            layout = computeLayout();
            pollController(now);

            switch (screen) {
            case Screen::Intro:
                if (!drawIntro(now)) {
                    break;
                }
                drawMenu();
                break;
            case Screen::Menu:
                drawMenu();
                break;
            case Screen::Game:
                updateGame(now);
                if (screen == Screen::Game) {
                    drawGame(now);
                } else if (screen == Screen::Scores) {
                    drawScores(false);
                } else if (screen == Screen::Credits) {
                    drawCredits();
                } else if (screen == Screen::NameEntry) {
                    drawScores(true);
                } else if (screen == Screen::Menu) {
                    drawMenu();
                }
                break;
            case Screen::Scores:
                drawScores(false);
                break;
            case Screen::Credits:
                drawCredits();
                break;
            case Screen::NameEntry:
                drawScores(true);
                break;
            }
        }

      private:
        static constexpr std::array<const char *, 11> block_files{{
            "block_black.png",
            "block_yellow.png",
            "block_orange.png",
            "block_ltblue.png",
            "block_dblue.png",
            "block_purple.png",
            "block_pink.png",
            "block_gray.png",
            "block_red.png",
            "block_green.png",
            "block_clear.png",
        }};

        static constexpr std::array<const char *, menu_item_count> menu_files{{
            "menu_new_game.png",
            "menu_high_scores.png",
            "menu_credits.png",
            "menu_quit.png",
        }};

        std::filesystem::path asset_root;
        std::filesystem::path puzzle_asset_root;
        HighScores high_scores;
        Screen screen = Screen::Intro;
        Layout layout{};
        std::mt19937 rng{std::random_device{}()};
        std::array<std::array<Cell, board_cols>, board_rows> board{};
        Piece piece{};
        mxvk::Font title_font{};
        mxvk::Font ui_font{};
        mxvk::VK_Sprite *background_intro = nullptr;
        mxvk::VK_Sprite *mxvk_logo = nullptr;
        mxvk::VK_Sprite *background_menu = nullptr;
        mxvk::VK_Sprite *background_game = nullptr;
        mxvk::VK_Sprite *cursor = nullptr;
        std::array<mxvk::VK_Sprite *, block_files.size()> blocks{};
        std::array<mxvk::VK_Sprite *, menu_files.size()> menu_items{};
        mxvk::VK_Sprite *panel = nullptr;
        mxvk::VK_Sprite *overlay = nullptr;
        std::string player_name;
        int menu_selection = 0;
        int score = 0;
        int lines = 0;
        int speed_level = 0;
        int lines_toward_speedup = 0;
        int fall_delay_ms = 520;
        Uint64 intro_start_ms = 0;
        Uint64 last_update_ms = 0;
        Uint64 fall_accumulator_ms = 0;
        Uint32 joy_repeat_left_ms = 0;
        Uint32 joy_repeat_right_ms = 0;
        Uint32 joy_repeat_up_ms = 0;
        Uint32 joy_repeat_down_ms = 0;
        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepadId = 0;
        bool paused = false;
        bool awaiting_name = false;
        bool score_added = false;
        bool waiting_for_spawn = false;

        static bool isConfirmKey(SDL_Keycode key) {
            return key == SDLK_RETURN || key == SDLK_SPACE;
        }

        static int scaled(int value, float scale) {
            return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * scale)));
        }

        static int scaledPos(int value, float scale) {
            return static_cast<int>(std::lround(static_cast<float>(value) * scale));
        }

        static int flashingSpriteIndex(int x, int y, Uint64 now) {
            const Uint64 tick = now / 18U;
            const Uint64 mixed = tick + static_cast<Uint64>(x * 37 + y * 101);
            return static_cast<int>((mixed % 9U) + 1U);
        }

        [[nodiscard]] Layout computeLayout() const {
            Layout result{};
            const VkExtent2D extent = getSwapchainExtent();
            result.width = extent.width == 0U ? base_width : static_cast<int>(extent.width);
            result.height = extent.height == 0U ? base_height : static_cast<int>(extent.height);
            result.scale_x = static_cast<float>(result.width) / static_cast<float>(base_width);
            result.scale_y = static_cast<float>(result.height) / static_cast<float>(base_height);
            result.game_scale = std::min(result.scale_x, result.scale_y);
            result.game_w = scaled(base_width, result.game_scale);
            result.game_h = scaled(base_height, result.game_scale);
            result.game_x = (result.width - result.game_w) / 2;
            result.game_y = (result.height - result.game_h) / 2;
            result.board_x = result.game_x + scaledPos(185, result.game_scale);
            result.board_y = result.game_y + scaledPos(95, result.game_scale);
            result.cell_w = scaled(32, result.game_scale);
            result.cell_h = scaled(16, result.game_scale);
            result.next_x = result.game_x + scaledPos(510, result.game_scale);
            result.next_y = result.game_y + scaledPos(200, result.game_scale);
            result.menu_x = scaled(505, result.scale_x);
            result.menu_y = scaled(400, result.scale_y);
            result.menu_w = scaled(430, result.scale_x);
            result.menu_h = scaled(82, result.scale_y);
            result.menu_step = scaled(118, result.scale_y);
            return result;
        }

        std::string dataPath(const char *name) const {
            return (asset_root / "data" / name).string();
        }

        std::string puzzleDataPath(const char *name) const {
            const std::filesystem::path shared_path = puzzle_asset_root / "data" / name;
            if (std::filesystem::exists(shared_path)) {
                return shared_path.string();
            }
            return dataPath(name);
        }

        mxvk::VK_Sprite *loadPngSprite(const char *name) {
            return createSprite(dataPath(name));
        }

        mxvk::VK_Sprite *loadEffectSprite(const char *name) {
            return createSprite(dataPath(name), "", dataPath("intro.frag.spv"));
        }

        mxvk::VK_Sprite *makeSolidPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::array<std::uint8_t, 4> pixel{r, g, b, a};
            mxvk::VK_Sprite *sprite = createSprite(1, 1);
            sprite->updateTexture(pixel.data(), 1, 1, 4);
            return sprite;
        }

        void loadSprites() {
            background_intro = loadEffectSprite("intro.png");
            background_menu = loadEffectSprite("start.png");
            background_game = createSprite(puzzleDataPath("gamebg.png"));
            mxvk_logo = createSprite(puzzleDataPath("mxvk_logo.png"));
            cursor = loadPngSprite("cursor.png");

            for (std::size_t i = 0; i < block_files.size(); ++i) {
                blocks[i] = loadPngSprite(block_files[i]);
            }

            for (std::size_t i = 0; i < menu_files.size(); ++i) {
                menu_items[i] = loadPngSprite(menu_files[i]);
            }

            panel = makeSolidPixel(0, 0, 0, 192);
            overlay = makeSolidPixel(0, 0, 0, 128);
        }

        void setScreen(Screen next) {
            if (screen == Screen::NameEntry && next != Screen::NameEntry) {
                SDL_StopTextInput(window.get());
            }

            screen = next;
            awaiting_name = (screen == Screen::NameEntry);
            if (screen == Screen::NameEntry) {
                player_name.clear();
                if (!score_added) {
                    SDL_StartTextInput(window.get());
                }
            }

            if (screen == Screen::Intro) {
                intro_start_ms = SDL_GetTicks();
            }
        }

        void resetGame() {
            for (auto &row : board) {
                for (Cell &cell : row) {
                    cell = {};
                }
            }

            score = 0;
            lines = 0;
            speed_level = 0;
            lines_toward_speedup = 0;
            fall_delay_ms = 520;
            last_update_ms = 0;
            fall_accumulator_ms = 0;
            paused = false;
            awaiting_name = false;
            score_added = false;
            waiting_for_spawn = false;
            piece.x = board_cols / 2 - 1;
            piece.y = 0;
            piece.next_colors = randomColors();
            spawnPiece();
        }

        std::array<int, piece_height> randomColors() {
            std::uniform_int_distribution<int> dist(1, 9);
            std::array<int, piece_height> colors{dist(rng), dist(rng), dist(rng)};
            while (colors[0] == colors[1] && colors[1] == colors[2]) {
                colors[1] = dist(rng);
            }
            return colors;
        }

        bool canPlacePiece(int x, int y) const {
            if (x < 0 || x >= board_cols) {
                return false;
            }

            for (int i = 0; i < piece_height; ++i) {
                const int row = y + i;
                if (row < 0 || row >= board_rows) {
                    return false;
                }
                if (board[static_cast<std::size_t>(row)][static_cast<std::size_t>(x)].color != 0) {
                    return false;
                }
            }
            return true;
        }

        bool movePiece(int dx, int dy) {
            const int next_x = piece.x + dx;
            const int next_y = piece.y + dy;
            if (!canPlacePiece(next_x, next_y)) {
                return false;
            }

            piece.x = next_x;
            piece.y = next_y;
            return true;
        }

        void dropPiece() {
            while (movePiece(0, 1)) {
            }
            lockPiece(SDL_GetTicks());
        }

        void rotatePieceColors(bool forward) {
            if (forward) {
                const int temp = piece.colors.back();
                piece.colors[2] = piece.colors[1];
                piece.colors[1] = piece.colors[0];
                piece.colors[0] = temp;
            } else {
                const int temp = piece.colors.front();
                piece.colors[0] = piece.colors[1];
                piece.colors[1] = piece.colors[2];
                piece.colors[2] = temp;
            }
        }

        void handleControllerButton(Uint8 button) {
            switch (screen) {
            case Screen::Intro:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    setScreen(Screen::Menu);
                }
                break;
            case Screen::Menu:
                if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    menu_selection = (menu_selection + menu_item_count - 1) % menu_item_count;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    menu_selection = (menu_selection + 1) % menu_item_count;
                } else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    handleMenuSelection();
                } else if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                    exit();
                }
                break;
            case Screen::Game:
                if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    setScreen(Screen::Menu);
                } else if (button == SDL_GAMEPAD_BUTTON_START) {
                    paused = !paused;
                } else if (paused || awaiting_name) {
                    break;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                    movePiece(-1, 0);
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                    movePiece(1, 0);
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    if (!movePiece(0, 1)) {
                        lockPiece(SDL_GetTicks());
                    }
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    rotatePieceColors(true);
                } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                    rotatePieceColors(false);
                } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
                    dropPiece();
                }
                break;
            case Screen::Scores:
                if (awaiting_name) {
                    if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                        commitScore();
                    } else if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                        score_added = true;
                        SDL_StopTextInput(window.get());
                        setScreen(Screen::Scores);
                    }
                } else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START ||
                           button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                    setScreen(Screen::Menu);
                }
                break;
            case Screen::Credits:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START ||
                    button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                    setScreen(Screen::Menu);
                }
                break;
            case Screen::NameEntry:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    commitScore();
                } else if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                    score_added = true;
                    SDL_StopTextInput(window.get());
                    setScreen(Screen::Scores);
                }
                break;
            }
        }

        void handleMenuSelection() {
            switch (menu_selection) {
            case 0:
                resetGame();
                setScreen(Screen::Game);
                break;
            case 1:
                setScreen(Screen::Scores);
                break;
            case 2:
                setScreen(Screen::Credits);
                break;
            case 3:
                exit();
                break;
            default:
                break;
            }
        }

        void handleMenuKey(SDL_Keycode key) {
            if (key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (key == SDLK_UP) {
                menu_selection = (menu_selection + menu_item_count - 1) % menu_item_count;
                return;
            }

            if (key == SDLK_DOWN) {
                menu_selection = (menu_selection + 1) % menu_item_count;
                return;
            }

            if (!isConfirmKey(key)) {
                return;
            }

            handleMenuSelection();
        }

        void handleGameKey(SDL_Keycode key) {
            if (key == SDLK_ESCAPE) {
                setScreen(Screen::Menu);
                return;
            }

            if (key == 'p' || key == 'P') {
                paused = !paused;
                return;
            }

            if (paused || awaiting_name) {
                return;
            }

            if (key == SDLK_LEFT) {
                movePiece(-1, 0);
            } else if (key == SDLK_RIGHT) {
                movePiece(1, 0);
            } else if (key == SDLK_DOWN) {
                if (!movePiece(0, 1)) {
                    lockPiece(SDL_GetTicks());
                }
            } else if (key == 'a' || key == 'A' || key == SDLK_UP) {
                rotatePieceColors(true);
            } else if (key == 's' || key == 'S') {
                rotatePieceColors(false);
            }
        }

        void handleNameKey(SDL_Keycode key) {
            if (key == SDLK_ESCAPE) {
                score_added = true;
                SDL_StopTextInput(window.get());
                setScreen(Screen::Scores);
                return;
            }

            if (key == SDLK_BACKSPACE) {
                if (!player_name.empty()) {
                    player_name.pop_back();
                }
                return;
            }

            if (key == SDLK_RETURN) {
                commitScore();
            }
        }

        void handleNameText(const char *text) {
            if (text == nullptr) {
                return;
            }

            while (*text != '\0' && static_cast<int>(player_name.size()) < max_name_length) {
                const unsigned char ch = static_cast<unsigned char>(*text++);
                if (ch >= 32 && ch < 127) {
                    player_name.push_back(static_cast<char>(ch));
                }
            }
        }

        void commitScore() {
            if (!score_added) {
                if (player_name.empty()) {
                    player_name = "Player";
                }
                high_scores.add(player_name, score);
                score_added = true;
            }

            SDL_StopTextInput(window.get());
            setScreen(Screen::Scores);
        }

        void spawnPiece() {
            piece.colors = piece.next_colors;
            piece.next_colors = randomColors();
            piece.x = board_cols / 2 - 1;
            piece.y = 0;

            if (!canPlacePiece(piece.x, piece.y)) {
                handleGameOver();
            }
        }

        bool boardHasFlashCells() const {
            for (const auto &row : board) {
                for (const Cell &cell : row) {
                    if (cell.flash_until != 0U) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool updateFlashState(Uint64 now) {
            if (!boardHasFlashCells()) {
                return false;
            }

            bool expired_any = false;
            for (auto &row : board) {
                for (Cell &cell : row) {
                    if (cell.flash_until != 0U && now >= cell.flash_until) {
                        cell = {};
                        expired_any = true;
                    }
                }
            }

            if (boardHasFlashCells()) {
                return true;
            }

            if (expired_any) {
                applyGravity();
                resolveMatches(now);
            }

            if (!boardHasFlashCells() && waiting_for_spawn) {
                waiting_for_spawn = false;
                spawnPiece();
            }

            return true;
        }

        void updateGame(Uint64 now) {
            if (paused || awaiting_name) {
                return;
            }

            if (last_update_ms == 0U) {
                last_update_ms = now;
                return;
            }

            if (updateFlashState(now)) {
                last_update_ms = now;
                return;
            }

            if (waiting_for_spawn) {
                waiting_for_spawn = false;
                spawnPiece();
                last_update_ms = now;
                return;
            }

            const Uint64 delta = now - last_update_ms;
            last_update_ms = now;
            fall_accumulator_ms += delta;

            while (fall_accumulator_ms >= static_cast<Uint64>(fall_delay_ms)) {
                fall_accumulator_ms -= static_cast<Uint64>(fall_delay_ms);
                if (!movePiece(0, 1)) {
                    lockPiece(now);
                    break;
                }
            }
        }

        bool resolveMatches(Uint64 now) {
            std::array<std::array<bool, board_cols>, board_rows> marked{};
            int matches_found = 0;

            auto mark_run = [&](int x, int y, int dx, int dy) {
                const int color = board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].color;
                int length = 0;
                int cx = x;
                int cy = y;

                while (cx >= 0 && cy >= 0 && cx < board_cols && cy < board_rows &&
                       board[static_cast<std::size_t>(cy)][static_cast<std::size_t>(cx)].color == color &&
                       board[static_cast<std::size_t>(cy)][static_cast<std::size_t>(cx)].flash_until == 0U) {
                    ++length;
                    cx += dx;
                    cy += dy;
                }

                if (length < 3) {
                    return;
                }

                ++matches_found;
                cx = x;
                cy = y;
                for (int i = 0; i < length; ++i) {
                    marked[static_cast<std::size_t>(cy)][static_cast<std::size_t>(cx)] = true;
                    cx += dx;
                    cy += dy;
                }
            };

            for (int y = 0; y < board_rows; ++y) {
                for (int x = 0; x < board_cols; ++x) {
                    const Cell &cell = board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                    if (cell.color == 0 || cell.flash_until != 0U) {
                        continue;
                    }

                    if (x == 0 || board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x - 1)].color != cell.color) {
                        mark_run(x, y, 1, 0);
                    }
                    if (y == 0 || board[static_cast<std::size_t>(y - 1)][static_cast<std::size_t>(x)].color != cell.color) {
                        mark_run(x, y, 0, 1);
                    }
                    if (x == 0 || y == 0 ||
                        board[static_cast<std::size_t>(y - 1)][static_cast<std::size_t>(x - 1)].color != cell.color) {
                        mark_run(x, y, 1, 1);
                    }
                    if (x == board_cols - 1 || y == 0 ||
                        board[static_cast<std::size_t>(y - 1)][static_cast<std::size_t>(x + 1)].color != cell.color) {
                        mark_run(x, y, -1, 1);
                    }
                }
            }

            if (matches_found == 0) {
                return false;
            }

            for (int y = 0; y < board_rows; ++y) {
                for (int x = 0; x < board_cols; ++x) {
                    if (marked[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) {
                        board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].flash_until = now + flash_time_ms;
                    }
                }
            }

            score += matches_found * score_points_per_match;
            lines += matches_found;
            lines_toward_speedup += matches_found;
            while (lines_toward_speedup >= lines_per_speedup) {
                lines_toward_speedup -= lines_per_speedup;
                ++speed_level;
                fall_delay_ms = std::max(140, 520 - speed_level * 40);
            }

            return true;
        }

        void applyGravity() {
            for (int x = 0; x < board_cols; ++x) {
                int write_row = board_rows - 1;
                for (int y = board_rows - 1; y >= 0; --y) {
                    if (board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].color != 0) {
                        if (write_row != y) {
                            board[static_cast<std::size_t>(write_row)][static_cast<std::size_t>(x)] =
                                board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                            board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = {};
                        }
                        --write_row;
                    }
                }

                for (int y = write_row; y >= 0; --y) {
                    board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = {};
                }
            }
        }

        void lockPiece(Uint64 now) {
            if (piece.y <= 0) {
                handleGameOver();
                return;
            }

            for (int i = 0; i < piece_height; ++i) {
                const int row = piece.y + i;
                board[static_cast<std::size_t>(row)][static_cast<std::size_t>(piece.x)].color =
                    std::clamp(piece.colors[static_cast<std::size_t>(i)], 1, 9);
            }

            if (resolveMatches(now)) {
                waiting_for_spawn = true;
                return;
            }

            spawnPiece();
        }

        void handleGameOver() {
            if (high_scores.qualifies(score)) {
                score_added = false;
                player_name.clear();
                setScreen(Screen::NameEntry);
            } else {
                setScreen(Screen::Scores);
            }
        }

        void drawSprite(mxvk::VK_Sprite *sprite, int x, int y, int w, int h) {
            if (sprite != nullptr) {
                sprite->drawSpriteRect(x, y, w, h);
            }
        }

        void drawCenteredText(const std::string &text, int y, const SDL_Color &color, const mxvk::Font &font) {
            int w = 0;
            int h = 0;
            if (!getTextDimensions(text, w, h, font)) {
                printText(text, scaled(32, layout.scale_x), y, color, font);
                return;
            }

            const int x = std::max(16, (layout.width - w) / 2);
            printText(text, x, y, color, font);
        }

        bool drawIntro(Uint64 now) {
            const float elapsed = intro_start_ms == 0U ? 0.0f : static_cast<float>(now - intro_start_ms) / 1000.0f;
            background_intro->setShaderParams(elapsed, 0.0f, 0.0f, 1.0f);
            drawSprite(background_intro, 0, 0, layout.width, layout.height);

            if (intro_start_ms == 0U) {
                intro_start_ms = now;
            }

            if (now - intro_start_ms > title_screen_time_ms) {
                setScreen(Screen::Menu);
                return true;
            }

            return false;
        }

        void drawMenu() {
            const float elapsed = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            background_menu->setShaderParams(elapsed, 0.0f, 0.0f, 1.0f);
            drawSprite(background_menu, 0, 0, layout.width, layout.height);

            for (int i = 0; i < menu_item_count; ++i) {
                const int y = layout.menu_y + i * layout.menu_step;
                if (i == menu_selection) {
                    drawSprite(cursor, layout.menu_x - scaled(94, layout.scale_x), y + scaled(12, layout.scale_y), scaled(78, layout.scale_x), scaled(58, layout.scale_y));
                }
                drawSprite(menu_items[static_cast<std::size_t>(i)], layout.menu_x, y, layout.menu_w, layout.menu_h);
            }
        }

        void drawScores(bool entering_name) {
            const float elapsed = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            background_menu->setShaderParams(elapsed, 0.0f, 0.0f, 1.0f);
            drawSprite(background_menu, 0, 0, layout.width, layout.height);
            drawSprite(overlay, scaled(30, layout.scale_x), scaled(70, layout.scale_y),
                       layout.width - scaled(60, layout.scale_x), layout.height - scaled(120, layout.scale_y));

            drawCenteredText("High Scores", scaled(72, layout.scale_y), SDL_Color{255, 245, 200, 255}, title_font);

            const auto &entries = high_scores.list();
            const int start_y = scaled(140, layout.scale_y);
            const int step_y = scaled(30, layout.scale_y);
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const std::string line = std::format("{:>2}. {:<16} {}", i + 1, entries[i].name, entries[i].score);
                printText(line, scaled(70, layout.scale_x), start_y + static_cast<int>(i) * step_y, SDL_Color{255, 255, 255, 255}, ui_font);
            }

            if (entering_name) {
                printText("Type your name and press Enter", scaled(70, layout.scale_x), scaled(450, layout.scale_y), SDL_Color{240, 220, 220, 255}, ui_font);
                printText("Name:", scaled(70, layout.scale_x), scaled(490, layout.scale_y), SDL_Color{255, 245, 200, 255}, ui_font);
                printText(player_name + "_", scaled(150, layout.scale_x), scaled(490, layout.scale_y), SDL_Color{255, 255, 255, 255}, ui_font);
            } else {
                printText("Press Enter to return to the menu", scaled(70, layout.scale_x), scaled(490, layout.scale_y), SDL_Color{240, 240, 220, 255}, ui_font);
            }
        }

        void drawCredits() {
            const float elapsed = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            background_menu->setShaderParams(elapsed, 0.0f, 0.0f, 1.0f);
            drawSprite(background_menu, 0, 0, layout.width, layout.height);
            drawSprite(overlay, scaled(30, layout.scale_x), scaled(85, layout.scale_y),
                       layout.width - scaled(60, layout.scale_x), scaled(260, layout.scale_y));
            const int logo_w = layout.width / 2;
            const int logo_h = static_cast<int>(std::lround(
                static_cast<float>(logo_w) * static_cast<float>(mxvk_logo->getHeight()) /
                static_cast<float>(mxvk_logo->getWidth())));
            const int logo_x = (layout.width - logo_w) / 2;
            const int logo_y = (layout.height - logo_h) / 2;
            drawSprite(mxvk_logo, logo_x, logo_y, logo_w, logo_h);
            drawCenteredText("Credits", scaled(96, layout.scale_y), SDL_Color{255, 245, 200, 255}, title_font);
            printText("Original game: MasterPiece.SDL", scaled(70, layout.scale_x), scaled(180, layout.scale_y), SDL_Color{255, 255, 255, 255}, ui_font);
            printText("MXVK port and cleanup: 2D Vulkan example", scaled(70, layout.scale_x), scaled(214, layout.scale_y), SDL_Color{255, 255, 255, 255}, ui_font);
            printText("Press Enter or Escape to return", scaled(70, layout.scale_x), scaled(270, layout.scale_y), SDL_Color{240, 240, 220, 255}, ui_font);
        }

        void drawGame(Uint64 now) {
            const float scaleX = static_cast<float>(layout.width) / static_cast<float>(game_base_width);
            const float scaleY = static_cast<float>(layout.height) / static_cast<float>(game_base_height);
            drawSprite(background_game, 0, 0, layout.width, layout.height);
            drawBoard(now, scaleX, scaleY);
            drawNextPiece(scaleX, scaleY);
            drawHud(scaleX, scaleY);

            if (paused) {
                const char *pausedText = "PAUSED - Press P to Continue";
                int pausedWidth = 0;
                int pausedHeight = 0;
                if (!getTextDimensions(pausedText, pausedWidth, pausedHeight, title_font)) {
                    pausedWidth = static_cast<int>(std::strlen(pausedText)) * 16;
                }
                printText(pausedText, layout.width / 2 - pausedWidth / 2, layout.height / 2, SDL_Color{255, 255, 0, 255}, title_font);
            }
        }

        void drawBoard(Uint64 now, float scaleX, float scaleY) {
            for (int i = 0; i < board_cols; ++i) {
                for (int j = 0; j < board_rows; ++j) {
                    const Cell &cell = board[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)];
                    if (cell.color == 0) {
                        continue;
                    }

                    const int sprite_index = cell.flash_until != 0U ? flashingSpriteIndex(i, j, now) : std::clamp(cell.color, 0, 9);
                    const int x = game_board_start_x + i * (game_block_width + game_block_spacing);
                    const int y = game_board_start_y + j * (game_block_height + game_block_spacing) + 10;

                    blocks[static_cast<std::size_t>(sprite_index)]->drawSpriteRect(
                        static_cast<int>(static_cast<float>(x) * scaleX),
                        static_cast<int>(static_cast<float>(y) * scaleY) + 10,
                        static_cast<int>(static_cast<float>(game_block_width) * scaleX),
                        static_cast<int>(static_cast<float>(game_block_height) * scaleY));
                }
            }

            if (screen != Screen::Game) {
                return;
            }

            for (int i = 0; i < piece_height; ++i) {
                const int row = piece.y + i;
                if (row < 0 || row >= board_rows || piece.x < 0 || piece.x >= board_cols) {
                    continue;
                }

                const int x = game_board_start_x + piece.x * (game_block_width + game_block_spacing);
                const int y = game_board_start_y + row * (game_block_height + game_block_spacing) + 10;
                blocks[static_cast<std::size_t>(std::clamp(piece.colors[static_cast<std::size_t>(i)], 0, 9))]->drawSpriteRect(
                    static_cast<int>(static_cast<float>(x) * scaleX),
                    static_cast<int>(static_cast<float>(y) * scaleY) + 10,
                    static_cast<int>(static_cast<float>(game_block_width) * scaleX),
                    static_cast<int>(static_cast<float>(game_block_height) * scaleY));
            }
        }

        void drawNextPiece(float scaleX, float scaleY) {
            const int bx = game_next_panel_x + 70;
            const int by = game_next_panel_y + 15;

            for (int i = 0; i < piece_height; ++i) {
                const int sprite_index = std::clamp(piece.next_colors[static_cast<std::size_t>(i)], 0, 9);
                blocks[static_cast<std::size_t>(sprite_index)]->drawSpriteRect(
                    static_cast<int>(static_cast<float>(bx) * scaleX),
                    static_cast<int>(static_cast<float>(by + i * (game_block_height + game_block_spacing)) * scaleY),
                    static_cast<int>(static_cast<float>(game_block_width) * scaleX),
                    static_cast<int>(static_cast<float>(game_block_height) * scaleY));
            }
        }

        void drawHud(float scaleX, float scaleY) {
            printText(std::format("Score: {}", score),
                      static_cast<int>(200.0f * scaleX),
                      static_cast<int>(80.0f * scaleY) - 24,
                      SDL_Color{255, 255, 255, 255},
                      ui_font);
            printText(std::format("Tabs: {}", lines),
                      static_cast<int>(310.0f * scaleX),
                      static_cast<int>(80.0f * scaleY) - 24,
                      SDL_Color{255, 255, 255, 255},
                      ui_font);
        }

        void handleControllerAxis(Sint16 lx, Sint16 ly) {
            const Uint32 now = SDL_GetTicks();

            if (screen == Screen::Menu) {
                if (ly < -joystick_dead_zone) {
                    if (now - joy_repeat_up_ms > joy_repeat_delay_ms) {
                        menu_selection = (menu_selection + menu_item_count - 1) % menu_item_count;
                        joy_repeat_up_ms = now;
                    }
                } else {
                    joy_repeat_up_ms = 0U;
                }

                if (ly > joystick_dead_zone) {
                    if (now - joy_repeat_down_ms > joy_repeat_delay_ms) {
                        menu_selection = (menu_selection + 1) % menu_item_count;
                        joy_repeat_down_ms = now;
                    }
                } else {
                    joy_repeat_down_ms = 0U;
                }
                return;
            }

            if (screen != Screen::Game || paused || awaiting_name) {
                return;
            }

            if (lx < -joystick_dead_zone) {
                if (now - joy_repeat_left_ms > joy_repeat_delay_ms) {
                    movePiece(-1, 0);
                    joy_repeat_left_ms = now;
                }
            } else {
                joy_repeat_left_ms = 0U;
            }

            if (lx > joystick_dead_zone) {
                if (now - joy_repeat_right_ms > joy_repeat_delay_ms) {
                    movePiece(1, 0);
                    joy_repeat_right_ms = now;
                }
            } else {
                joy_repeat_right_ms = 0U;
            }

            if (ly > joystick_dead_zone) {
                if (now - joy_repeat_down_ms > joy_repeat_delay_ms) {
                    if (!movePiece(0, 1)) {
                        lockPiece(now);
                    }
                    joy_repeat_down_ms = now;
                }
            } else {
                joy_repeat_down_ms = 0U;
            }

            if (lx == 0 && ly == 0) {
                joy_repeat_up_ms = 0U;
            }
        }

        void pollController([[maybe_unused]] Uint64 now) {
            if (gamepad == nullptr) {
                return;
            }

            const Sint16 lx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
            const Sint16 ly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
            handleControllerAxis(lx, ly);
        }

        bool openGamepad(SDL_JoystickID id) {
            if (gamepad != nullptr && gamepadId == id) {
                return true;
            }

            closeGamepad();
            gamepad = SDL_OpenGamepad(id);
            if (gamepad == nullptr) {
                return false;
            }

            gamepadId = id;
            return true;
        }

        void tryOpenFirstGamepad() {
            if (gamepad != nullptr) {
                return;
            }

            int count = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&count);
            if (ids == nullptr || count <= 0) {
                if (ids != nullptr) {
                    SDL_free(ids);
                }
                return;
            }

            openGamepad(ids[0]);
            SDL_free(ids);
        }

        void closeGamepad() {
            if (gamepad != nullptr) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
            }
            gamepadId = 0;
        }
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const bool explicit_resolution = hasResolutionArgument(argc, argv);
        Arguments args = proc_args(argc, argv);
        if (!explicit_resolution) {
            args.width = base_width;
            args.height = base_height;
        }

        example::MasterPieceWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
