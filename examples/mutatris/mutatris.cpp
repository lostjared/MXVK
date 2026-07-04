#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_console.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#ifndef mutatris_ASSET_DIR
#define mutatris_ASSET_DIR "."
#endif

#ifndef mutatris_FONT_PATH
#define mutatris_FONT_PATH "."
#endif

#ifndef mutatris_SHADER_DIR
#define mutatris_SHADER_DIR "."
#endif

namespace {

    constexpr int DESIGN_WIDTH = 1280;
    constexpr int DESIGN_HEIGHT = 720;
    constexpr int BLOCK_WIDTH = 32;
    constexpr int BLOCK_HEIGHT = 16;
    constexpr int BLOCK_DRAW_WIDTH = 30;
    constexpr int BLOCK_DRAW_HEIGHT = 14;
    constexpr int GRID_WIDTH = 12;
    constexpr int TOP_GRID_HEIGHT = (DESIGN_HEIGHT / BLOCK_HEIGHT / 2) + 1;
    constexpr int SIDE_GRID_HEIGHT = 28;
    constexpr int BOTTOM_GRID_HEIGHT = DESIGN_HEIGHT / BLOCK_HEIGHT / 2;
    constexpr Uint32 STARTUP_FADE_MS = 2520;
    constexpr Uint32 STARTUP_HOLD_MS = 900;
    constexpr Uint32 KEY_REPEAT_MS = 120;
    constexpr Uint32 CLEAR_ANIMATION_MS = 400;
    constexpr int CLEAR_ANIMATION_FRAMES = 24;

    enum class Screen {
        Startup,
        Title,
        Difficulty,
        Playing,
        GameOver,
    };

    struct Block {
        int color = 0;
        Uint32 clearElapsedMs = 0;
    };

    class GameGrid;

    class Piece {
      public:
        explicit Piece(GameGrid *owner)
            : grid(owner) {
        }

        void reset();
        void shiftColors();
        void moveLeft();
        void moveRight();
        [[nodiscard]] bool moveDown();
        [[nodiscard]] bool drop();
        void shiftDirection();
        [[nodiscard]] bool setBlock();
        [[nodiscard]] bool checkLocation(int next_x, int next_y) const;

        [[nodiscard]] int getX() const { return x; }
        [[nodiscard]] int getY() const { return y; }
        [[nodiscard]] int getDirection() const { return direction; }
        [[nodiscard]] const Block *at(int index) const {
            return index >= 0 && index < static_cast<int>(blocks.size()) ? &blocks[static_cast<std::size_t>(index)] : nullptr;
        }

      private:
        std::array<Block, 3> blocks{};
        int x = 0;
        int y = 0;
        int direction = 0;
        GameGrid *grid = nullptr;
    };

    class GameGrid {
      public:
        GameGrid()
            : gamePiece(this) {
        }

        void initGrid(int width, int height) {
            gridWidth = width;
            gridHeight = height;
            cells.assign(static_cast<std::size_t>(width * height), {});
            gamePiece.reset();
        }

        [[nodiscard]] Block *at(int x, int y) {
            if (x < 0 || x >= gridWidth || y < 0 || y >= gridHeight) {
                return nullptr;
            }
            return &cells[static_cast<std::size_t>(y * gridWidth + x)];
        }

        [[nodiscard]] const Block *at(int x, int y) const {
            if (x < 0 || x >= gridWidth || y < 0 || y >= gridHeight) {
                return nullptr;
            }
            return &cells[static_cast<std::size_t>(y * gridWidth + x)];
        }

        [[nodiscard]] int width() const { return gridWidth; }
        [[nodiscard]] int height() const { return gridHeight; }
        [[nodiscard]] bool canMoveDown() const {
            return gamePiece.getDirection() == 3 || gamePiece.checkLocation(gamePiece.getX(), gamePiece.getY()) || gamePiece.getY() != 0;
        }

        Piece gamePiece;

      private:
        int gridWidth = 0;
        int gridHeight = 0;
        std::vector<Block> cells{};
    };

    void Piece::reset() {
        x = grid->width() / 2;
        y = 0;
        do {
            for (Block &block : blocks) {
                block.color = 1 + (std::rand() % 7);
            }
        } while (blocks[0].color == blocks[1].color && blocks[0].color == blocks[2].color);
        direction = 0;
    }

    void Piece::shiftColors() {
        const std::array<Block, 3> previous = blocks;
        blocks[0] = previous[2];
        blocks[1] = previous[0];
        blocks[2] = previous[1];
    }

    bool Piece::checkLocation(int next_x, int next_y) const {
        std::array<const Block *, 3> target{};
        switch (direction) {
        case 0:
            target = {grid->at(next_x, next_y), grid->at(next_x, next_y + 1), grid->at(next_x, next_y + 2)};
            break;
        case 1:
            target = {grid->at(next_x, next_y), grid->at(next_x + 1, next_y), grid->at(next_x + 2, next_y)};
            break;
        case 2:
            target = {grid->at(next_x, next_y), grid->at(next_x - 1, next_y), grid->at(next_x - 2, next_y)};
            break;
        case 3:
            target = {grid->at(next_x, next_y), grid->at(next_x, next_y - 1), grid->at(next_x, next_y - 2)};
            break;
        default:
            break;
        }

        return std::all_of(target.begin(), target.end(), [](const Block *block) {
            return block != nullptr && block->color == 0;
        });
    }

    void Piece::moveLeft() {
        if (checkLocation(x - 1, y)) {
            --x;
        }
    }

    void Piece::moveRight() {
        if (checkLocation(x + 1, y)) {
            ++x;
        }
    }

    bool Piece::moveDown() {
        if (checkLocation(x, y + 1)) {
            ++y;
            return false;
        } else {
            return setBlock();
        }
    }

    bool Piece::drop() {
        while (checkLocation(x, y + 1)) {
            ++y;
        }
        return setBlock();
    }

    void Piece::shiftDirection() {
        const int oldDirection = direction;
        direction = (direction + 1) % 4;
        if (!checkLocation(x, y)) {
            direction = oldDirection;
        }
    }

    bool Piece::setBlock() {
        std::array<Block *, 3> target{};
        switch (direction) {
        case 0:
            target = {grid->at(x, y), grid->at(x, y + 1), grid->at(x, y + 2)};
            break;
        case 1:
            target = {grid->at(x, y), grid->at(x + 1, y), grid->at(x + 2, y)};
            break;
        case 2:
            target = {grid->at(x, y), grid->at(x - 1, y), grid->at(x - 2, y)};
            break;
        case 3:
            target = {grid->at(x, y), grid->at(x, y - 1), grid->at(x, y - 2)};
            break;
        default:
            break;
        }

        for (std::size_t i = 0; i < target.size(); ++i) {
            if (target[i] == nullptr) {
                return false;
            }
            target[i]->color = blocks[i].color;
            target[i]->clearElapsedMs = 0;
        }
        reset();
        return true;
    }

    class PuzzleGame {
      public:
        explicit PuzzleGame(int difficulty, std::function<void()> lineSoundCallback)
            : playLineSound(std::move(lineSoundCallback)) {
            newGame(difficulty);
        }

        void newGame(int difficulty) {
            std::srand(static_cast<unsigned int>(std::time(nullptr)));
            score = 0;
            clears = 0;
            level = 0;
            timeout = difficulty == 0 ? 1200U : difficulty == 1 ? 900U
                                                                : 650U;
            grid[0].initGrid(GRID_WIDTH, TOP_GRID_HEIGHT);
            grid[1].initGrid(GRID_WIDTH, SIDE_GRID_HEIGHT);
            grid[2].initGrid(GRID_WIDTH, BOTTOM_GRID_HEIGHT);
            grid[3].initGrid(GRID_WIDTH, SIDE_GRID_HEIGHT);
        }

        void procBlocks() {
            level = std::clamp((1200 - static_cast<int>(timeout)) / 100, 0, 10);
            if (clearVisualRun()) {
                return;
            }
            for (GameGrid &focusGrid : grid) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    for (int y = 0; y < focusGrid.height(); ++y) {
                        if (clearRun(focusGrid, x, y, 0, 1) ||
                            clearRun(focusGrid, x, y, 1, 0) ||
                            clearRun(focusGrid, x, y, 1, 1) ||
                            clearRun(focusGrid, x, y, -1, 1)) {
                            return;
                        }
                    }
                }
            }
            clearTopBottomSeams();
            moveDownBlocks();
        }

        std::array<GameGrid, 4> grid{};
        int score = 0;
        unsigned int timeout = 1200;
        int clears = 0;
        int level = 0;

      private:
        struct CellRef {
            int gridIndex = 0;
            int x = 0;
            int y = 0;
        };

        struct CellBounds {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        [[nodiscard]] Block *blockAt(const CellRef &cell) {
            if (cell.gridIndex < 0 || cell.gridIndex >= static_cast<int>(grid.size())) {
                return nullptr;
            }
            return grid[static_cast<std::size_t>(cell.gridIndex)].at(cell.x, cell.y);
        }

        [[nodiscard]] const Block *blockAt(const CellRef &cell) const {
            if (cell.gridIndex < 0 || cell.gridIndex >= static_cast<int>(grid.size())) {
                return nullptr;
            }
            return grid[static_cast<std::size_t>(cell.gridIndex)].at(cell.x, cell.y);
        }

        [[nodiscard]] static CellBounds cellBounds(const CellRef &cell) {
            if (cell.gridIndex == 0) {
                return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH, cell.y * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT};
            }
            if (cell.gridIndex == 1) {
                return {cell.y * BLOCK_HEIGHT, (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH, BLOCK_HEIGHT, BLOCK_WIDTH};
            }
            if (cell.gridIndex == 2) {
                return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH,
                        (DESIGN_HEIGHT / 2) + 5 + (BOTTOM_GRID_HEIGHT - 1 - cell.y) * BLOCK_HEIGHT,
                        BLOCK_WIDTH,
                        BLOCK_HEIGHT};
            }
            return {DESIGN_WIDTH - (SIDE_GRID_HEIGHT * BLOCK_HEIGHT) + (SIDE_GRID_HEIGHT - 1 - cell.y) * BLOCK_HEIGHT,
                    (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + (GRID_WIDTH - 1 - cell.x) * BLOCK_WIDTH,
                    BLOCK_HEIGHT,
                    BLOCK_WIDTH};
        }

        [[nodiscard]] bool sameCell(const CellRef &a, const CellRef &b) const {
            return a.gridIndex == b.gridIndex && a.x == b.x && a.y == b.y;
        }

        [[nodiscard]] bool findNextVisualMatch(const CellRef &current, int color, int dx, int dy, CellRef &next) const {
            const CellBounds currentBounds = cellBounds(current);
            const int currentCenterX2 = currentBounds.x * 2 + currentBounds.width;
            const int currentCenterY2 = currentBounds.y * 2 + currentBounds.height;
            constexpr int TOLERANCE2 = BLOCK_HEIGHT + 2;
            int bestError = TOLERANCE2 * TOLERANCE2 * 4 + 1;
            bool found = false;

            for (const int gridIndex : {0, 2}) {
                const GameGrid &focusGrid = grid[static_cast<std::size_t>(gridIndex)];
                for (int y = 0; y < focusGrid.height(); ++y) {
                    for (int x = 0; x < focusGrid.width(); ++x) {
                        const CellRef candidate{gridIndex, x, y};
                        if (sameCell(current, candidate)) {
                            continue;
                        }
                        const Block *block = blockAt(candidate);
                        if (block == nullptr || block->color != color) {
                            continue;
                        }

                        const CellBounds candidateBounds = cellBounds(candidate);
                        const int candidateCenterX2 = candidateBounds.x * 2 + candidateBounds.width;
                        const int candidateCenterY2 = candidateBounds.y * 2 + candidateBounds.height;
                        const int expectedDeltaX2 = dx == 0 ? 0 : dx * (currentBounds.width + candidateBounds.width);
                        const int expectedDeltaY2 = dy == 0 ? 0 : dy * (currentBounds.height + candidateBounds.height);
                        const int errorX = std::abs((candidateCenterX2 - currentCenterX2) - expectedDeltaX2);
                        const int errorY = std::abs((candidateCenterY2 - currentCenterY2) - expectedDeltaY2);
                        if (errorX > TOLERANCE2 || errorY > TOLERANCE2) {
                            continue;
                        }

                        const int error = errorX * errorX + errorY * errorY;
                        if (error < bestError) {
                            bestError = error;
                            next = candidate;
                            found = true;
                        }
                    }
                }
            }

            return found;
        }

        [[nodiscard]] bool clearVisualRun() {
            constexpr std::array<std::pair<int, int>, 4> directions{{{1, 0}, {0, 1}, {1, 1}, {-1, 1}}};
            for (const int gridIndex : {0, 2}) {
                GameGrid &focusGrid = grid[static_cast<std::size_t>(gridIndex)];
                for (int y = 0; y < focusGrid.height(); ++y) {
                    for (int x = 0; x < focusGrid.width(); ++x) {
                        const CellRef start{gridIndex, x, y};
                        const Block *startBlock = blockAt(start);
                        if (startBlock == nullptr || startBlock->color <= 0) {
                            continue;
                        }

                        for (const auto &[dx, dy] : directions) {
                            CellRef previous{};
                            if (findNextVisualMatch(start, startBlock->color, -dx, -dy, previous)) {
                                continue;
                            }

                            std::vector<CellRef> run{start};
                            CellRef current = start;
                            while (findNextVisualMatch(current, startBlock->color, dx, dy, current)) {
                                if (std::any_of(run.begin(), run.end(), [&](const CellRef &cell) {
                                        return sameCell(cell, current);
                                    })) {
                                    break;
                                }
                                run.push_back(current);
                            }

                            if (run.size() < 3U) {
                                continue;
                            }

                            for (const CellRef &cell : run) {
                                if (Block *block = blockAt(cell)) {
                                    markClearing(*block);
                                }
                            }
                            ++score;
                            ++clears;
                            if ((clears % 4) == 0 && timeout > 125) {
                                timeout -= 25;
                            }
                            playClearSound();
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool clearRun(GameGrid &focusGrid, int x, int y, int dx, int dy) {
            Block *start = focusGrid.at(x, y);
            if (start == nullptr || start->color <= 0) {
                return false;
            }

            const int color = start->color;
            const Block *previous = focusGrid.at(x - dx, y - dy);
            if (previous != nullptr && previous->color == color) {
                return false;
            }

            std::vector<Block *> blocks{};
            int cx = x;
            int cy = y;
            while (Block *block = focusGrid.at(cx, cy)) {
                if (block->color != color) {
                    break;
                }
                blocks.push_back(block);
                cx += dx;
                cy += dy;
            }

            if (blocks.size() < 3U) {
                return false;
            }

            for (Block *block : blocks) {
                markClearing(*block);
            }
            ++score;
            ++clears;
            if ((clears % 4) == 0 && timeout > 125) {
                timeout -= 25;
            }
            playClearSound();
            return true;
        }

        void clearTopBottomSeams() {
            for (int x = 0; x < grid[0].width(); ++x) {
                const int topY = grid[0].height() - 1;
                const int bottomY = grid[2].height() - 1;
                if (clearSeam({grid[0].at(x, topY), grid[0].at(x, topY - 1), grid[2].at(x, bottomY), grid[2].at(x, bottomY - 1)})) {
                    return;
                }
                if (clearSeam({grid[2].at(x, bottomY), grid[0].at(x, topY), grid[2].at(x, bottomY - 1), grid[0].at(x, topY - 1)})) {
                    return;
                }
            }
        }

        bool clearSeam(std::array<Block *, 4> blocks) {
            if (blocks[0] == nullptr || blocks[1] == nullptr || blocks[2] == nullptr || blocks[0]->color <= 0) {
                return false;
            }
            if (blocks[0]->color != blocks[1]->color || blocks[0]->color != blocks[2]->color) {
                return false;
            }
            if (blocks[3] != nullptr && blocks[3]->color == blocks[0]->color) {
                markClearing(*blocks[3]);
                score += 10;
                playClearSound();
            }
            markClearing(*blocks[0]);
            markClearing(*blocks[1]);
            markClearing(*blocks[2]);
            ++score;
            ++clears;
            if ((clears % 4) == 0 && timeout > 125) {
                timeout -= 25;
            }
            playClearSound();
            return true;
        }

        void moveDownBlocks() {
            for (GameGrid &focusGrid : grid) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    for (int y = focusGrid.height() - 2; y >= 0; --y) {
                        Block *current = focusGrid.at(x, y);
                        Block *below = focusGrid.at(x, y + 1);
                        if (current != nullptr && below != nullptr && current->color > 0 && below->color == 0) {
                            std::swap(current->color, below->color);
                            return;
                        }
                    }
                }
            }
        }

        static void markClearing(Block &block) {
            block.color = -1;
            block.clearElapsedMs = 0;
        }

        void playClearSound() {
            if (playLineSound) {
                playLineSound();
            }
        }

        std::function<void()> playLineSound;
    };

    class MutatrisWindow final : public mxvk::VK_Window {
      public:
        MutatrisWindow(const std::string &path, int width, int height, bool fullscreen, bool enableVsync)
            : mxvk::VK_Window("Mutatris", width, height, fullscreen, MXVK_VALIDATION, enableVsync),
              assetRoot((path.empty() || path == ".") ? std::string(mutatris_ASSET_DIR) : path),
              dataRoot(assetRoot + "/data"),
              shaderRoot(mutatris_SHADER_DIR) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            setFont(mutatris_FONT_PATH, 24);
            configureConsole();
            loadSprites();
            loadSoundEffects();
            playSound(openSound);
            startupStartTick = SDL_GetTicks();
            logMutatris(std::format("Mutatris ready. {} shader effect(s) available.", effectShaders.size()));
        }

        void event(SDL_Event &e) override {
            const bool wasConsoleVisible = console.isVisible();
            const bool consoleToggle = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3;
            console.handleEvent(e);
            if (consoleToggle) {
                logMutatris(console.isVisible() ? "Console opened." : "Console closed.");
                return;
            }
            if (wasConsoleVisible) {
                return;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                logMutatris("Exit requested.");
                exit();
                return;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                handleKey(e.key.key);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN) {
                handleConfirm();
            } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                handleGamepad(e.gbutton.button);
            }
        }

        void proc() override {
            const VkExtent2D extent = getSwapchainExtent();
            width = extent.width > 0U ? static_cast<int>(extent.width) : DESIGN_WIDTH;
            height = extent.height > 0U ? static_cast<int>(extent.height) : DESIGN_HEIGHT;

            ensureMusicPlaying();
            switch (screen) {
            case Screen::Startup:
                drawStartup();
                break;
            case Screen::Title:
                drawTitle();
                break;
            case Screen::Difficulty:
                drawDifficulty();
                break;
            case Screen::Playing:
                updatePlaying();
                break;
            case Screen::GameOver:
                drawGameOver();
                break;
            }
            console.draw();
        }

      private:
        std::string assetRoot;
        std::string dataRoot;
        std::string shaderRoot;
        std::array<mxvk::VK_Sprite *, 8> blocks{};
        std::array<mxvk::VK_Sprite *, 11> backgrounds{};
        std::vector<std::string> effectShaders;
        mxvk::VK_Console console;
        mxvk::VK_Sprite *intro = nullptr;
        mxvk::VK_Sprite *start = nullptr;
        mxvk::VK_Sprite *lostLogo = nullptr;
        mxvk::VK_Sprite *jbLogo = nullptr;
        mxvk::VK_Sprite *gameOver = nullptr;
        mxvk::VK_Sprite *highScore = nullptr;
        mxvk::Font titleFont;
        mxvk::Font titleLargeFont;
        mxvk::Font startMediumFont;
        mxvk::Font startSmallFont;
        mxvk::Font difficultyFont;
        std::unique_ptr<PuzzleGame> game;
        Screen screen = Screen::Startup;
        int difficulty = 0;
        int focus = 0;
        Uint32 startupStartTick = 0;
        int introFontSize = 0;
        Uint32 lastDropTick = 0;
        Uint32 lastInputTick = 0;
        Uint32 lastClearAnimationTick = 0;
        int finalScore = 0;
        int finalClears = 0;
        int width = DESIGN_WIDTH;
        int height = DESIGN_HEIGHT;
        std::mt19937 shaderRandom{std::random_device{}()};
        int shaderLevel = -1;
        int shaderIndex = -1;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> soundEffects{};
#endif
        int musicTrack = -1;
        int lineSound = -1;
        int openSound = -1;

        void configureConsole() {
            console.attach(*this, mutatris_FONT_PATH, 24);
            console.setSpriteYOriginTopLeft(true);
            console.setPrompt("mutatris> ");
            console.printLine("Press F3 to open/close the console.");
            console.printLine("Type 'help' for commands.");
            console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
                if (args.empty()) {
                    return true;
                }

                if (args[0] == "help") {
                    out << "Commands:\n"
                        << "  help            Show this help message\n"
                        << "  clear           Clear the console output\n"
                        << "  echo <text>     Print text to the console\n"
                        << "  switch_shader   Pick a random background shader\n"
                        << "  about           Show app information\n"
                        << "  quit | exit     Close Mutatris";
                    return true;
                }

                if (args[0] == "echo") {
                    for (std::size_t i = 1; i < args.size(); ++i) {
                        if (i > 1) {
                            out << ' ';
                        }
                        out << args[i];
                    }
                    return true;
                }

                if (args[0] == "switch_shader") {
                    const std::string shaderName = switchBackgroundShader(true);
                    if (shaderName.empty()) {
                        out << "No shader effects are available.";
                    } else {
                        logMutatris(std::format("Console command switch_shader selected {}", shaderName));
                        out << std::format("Switched shader to {}", shaderName);
                    }
                    return true;
                }

                if (args[0] == "about") {
                    out << "Mutatris: rotating-grid puzzle game built with MXVK.";
                    return true;
                }

                if (args[0] == "quit" || args[0] == "exit") {
                    out << "Closing Mutatris...";
                    logMutatris("Exit requested from console.");
                    exit();
                    return true;
                }

                return false;
            });
        }

        void logMutatris(const std::string &message) {
            std::cout << std::format("mutatris: {}\n", message);
            console.printLine(message, SDL_Color{180, 220, 255, 255});
        }

        [[nodiscard]] const char *screenName(Screen value) const {
            switch (value) {
            case Screen::Startup:
                return "Startup";
            case Screen::Title:
                return "Title";
            case Screen::Difficulty:
                return "Difficulty";
            case Screen::Playing:
                return "Playing";
            case Screen::GameOver:
                return "GameOver";
            }
            return "Unknown";
        }

        [[nodiscard]] const char *focusName(int value) const {
            switch (value) {
            case 0:
                return "Top";
            case 1:
                return "Left";
            case 2:
                return "Bottom";
            case 3:
                return "Right";
            default:
                return "Unknown";
            }
        }

        void setScreen(Screen nextScreen, const std::string &reason) {
            if (screen == nextScreen) {
                return;
            }
            const Screen previousScreen = screen;
            screen = nextScreen;
            logMutatris(std::format("Screen changed {} -> {} ({})", screenName(previousScreen), screenName(nextScreen), reason));
        }

        void loadSprites() {
            const std::string backgroundVertexShader = shaderRoot + "/background.vert.spv";
            const std::string backgroundShader = shaderRoot + "/background.frag.spv";
            const std::string fadeShader = shaderRoot + "/fade.frag.spv";
            loadEffectShaders();
            for (std::size_t i = 0; i < backgrounds.size(); ++i) {
                backgrounds[i] = createSprite(dataRoot + "/blocks" + (i == 0 ? std::string("") : std::to_string(i)) + ".png", backgroundVertexShader, backgroundShader);
            }
            intro = createSprite(dataRoot + "/intro.png", backgroundVertexShader, fadeShader);
            start = createSprite(dataRoot + "/start.png");
            lostLogo = createSprite(dataRoot + "/lostlogo.png", backgroundVertexShader, fadeShader);
            jbLogo = createSprite(dataRoot + "/jblogo.png", backgroundVertexShader, fadeShader);
            gameOver = createSprite(dataRoot + "/gameover.png");
            highScore = createSprite(dataRoot + "/highscore.png");

            const std::array<std::string, 8> blockFiles{{
                "block_black.png",
                "block_clear.png",
                "block_dblue.png",
                "block_green.png",
                "block_ltblue.png",
                "block_orange.png",
                "block_red.png",
                "block_yellow.png",
            }};
            for (std::size_t i = 0; i < blockFiles.size(); ++i) {
                blocks[i] = createSprite(dataRoot + "/" + blockFiles[i]);
            }
        }

        void loadEffectShaders() {
            effectShaders.clear();
            const std::filesystem::path effectDir = std::filesystem::path(shaderRoot) / "effects";
            if (!std::filesystem::exists(effectDir)) {
                return;
            }
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(effectDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                    effectShaders.push_back(entry.path().string());
                }
            }
            std::sort(effectShaders.begin(), effectShaders.end());
            std::cout << std::format("mutatris: loaded {} shader effect(s)\n", effectShaders.size());
        }

        void loadSoundEffects() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            soundEffects = std::make_unique<mxvk::VK_Mixer>();
            musicTrack = soundEffects->loadMusic(dataRoot + "/music.ogg");
            lineSound = soundEffects->loadWav(dataRoot + "/line.wav");
            openSound = soundEffects->loadWav(dataRoot + "/open.wav");
            logMutatris("Loaded original Mutatris sound effects.");
            ensureMusicPlaying();
#endif
        }

        void playSound(int soundId) {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (soundEffects == nullptr || soundId < 0) {
                return;
            }
            soundEffects->playWav(soundId);
#else
            (void)soundId;
#endif
        }

        void ensureMusicPlaying() {
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (soundEffects == nullptr || musicTrack < 0 || soundEffects->isMusicPlaying(musicTrack)) {
                return;
            }
            if (soundEffects->playMusic(musicTrack, -1) != 0) {
                throw mxvk::Exception("Could not start Mutatris background music");
            }
#endif
        }

        void handleKey(SDL_Keycode key) {
            if (screen == Screen::Startup) {
                setScreen(Screen::Title, "startup confirmed");
                return;
            }
            if (screen == Screen::Title && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)) {
                setScreen(Screen::Difficulty, "difficulty select opened");
                return;
            }
            if (screen == Screen::Difficulty) {
                if (key == SDLK_LEFT && difficulty > 0) {
                    --difficulty;
                    logMutatris(std::format("Difficulty changed to {}", difficulty));
                } else if (key == SDLK_RIGHT && difficulty < 2) {
                    ++difficulty;
                    logMutatris(std::format("Difficulty changed to {}", difficulty));
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
                    startGame();
                }
                return;
            }
            if (screen == Screen::GameOver && (key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
                setScreen(Screen::Title, "returned from game over");
                return;
            }
            if (screen != Screen::Playing || game == nullptr) {
                return;
            }
            if (key == SDLK_W) {
                game->grid[focus].gamePiece.shiftColors();
                return;
            }
            if (key == SDLK_A || key == SDLK_SPACE) {
                game->grid[focus].gamePiece.shiftDirection();
                return;
            }
            if (key == SDLK_S) {
                if (game->grid[focus].gamePiece.drop()) {
                    processGrid();
                    advanceFocus();
                }
                return;
            }
            handleDirectionalKey(key);
        }

        void handleGamepad(Uint8 button) {
            if (button == SDL_GAMEPAD_BUTTON_BACK) {
                logMutatris("Exit requested.");
                exit();
                return;
            }
            if (screen != Screen::Playing) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    handleConfirm();
                } else if (screen == Screen::Difficulty && button == SDL_GAMEPAD_BUTTON_DPAD_LEFT && difficulty > 0) {
                    --difficulty;
                    logMutatris(std::format("Difficulty changed to {}", difficulty));
                } else if (screen == Screen::Difficulty && button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT && difficulty < 2) {
                    ++difficulty;
                    logMutatris(std::format("Difficulty changed to {}", difficulty));
                }
                return;
            }
            if (game == nullptr) {
                return;
            }
            if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                game->grid[focus].gamePiece.shiftColors();
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                game->grid[focus].gamePiece.shiftDirection();
            } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
                if (game->grid[focus].gamePiece.drop()) {
                    processGrid();
                    advanceFocus();
                }
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
                handleDirectionalKey(SDLK_LEFT);
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
                handleDirectionalKey(SDLK_RIGHT);
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                handleDirectionalKey(SDLK_UP);
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                handleDirectionalKey(SDLK_DOWN);
            }
        }

        void handleConfirm() {
            if (screen == Screen::Startup) {
                setScreen(Screen::Title, "startup confirmed");
            } else if (screen == Screen::Title) {
                setScreen(Screen::Difficulty, "difficulty select opened");
            } else if (screen == Screen::Difficulty) {
                startGame();
            } else if (screen == Screen::GameOver) {
                setScreen(Screen::Title, "returned from game over");
            }
        }

        void handleDirectionalKey(SDL_Keycode key) {
            if (game == nullptr) {
                return;
            }
            const Uint32 now = SDL_GetTicks();
            if (now - lastInputTick < KEY_REPEAT_MS) {
                return;
            }
            lastInputTick = now;
            Piece &piece = game->grid[focus].gamePiece;
            if (focus == 0) {
                if (key == SDLK_LEFT) {
                    piece.moveLeft();
                } else if (key == SDLK_RIGHT) {
                    piece.moveRight();
                } else if (key == SDLK_UP) {
                    piece.shiftColors();
                } else if (key == SDLK_DOWN) {
                    if (piece.moveDown()) {
                        processGrid();
                        advanceFocus();
                    }
                }
            } else if (focus == 1) {
                if (key == SDLK_DOWN) {
                    piece.moveRight();
                } else if (key == SDLK_UP) {
                    piece.moveLeft();
                } else if (key == SDLK_LEFT) {
                    piece.shiftColors();
                } else if (key == SDLK_RIGHT) {
                    if (piece.moveDown()) {
                        processGrid();
                        advanceFocus();
                    }
                }
            } else if (focus == 2) {
                if (key == SDLK_LEFT) {
                    piece.moveLeft();
                } else if (key == SDLK_RIGHT) {
                    piece.moveRight();
                } else if (key == SDLK_DOWN) {
                    piece.shiftColors();
                } else if (key == SDLK_UP) {
                    if (piece.moveDown()) {
                        processGrid();
                        advanceFocus();
                    }
                }
            } else if (focus == 3) {
                if (key == SDLK_DOWN) {
                    piece.moveLeft();
                } else if (key == SDLK_UP) {
                    piece.moveRight();
                } else if (key == SDLK_RIGHT) {
                    piece.shiftColors();
                } else if (key == SDLK_LEFT) {
                    if (piece.moveDown()) {
                        processGrid();
                        advanceFocus();
                    }
                }
            }
        }

        void processGrid() {
            if (game == nullptr) {
                return;
            }
            const int previousScore = game->score;
            const int previousClears = game->clears;
            const int previousLevel = game->level;
            const unsigned int previousTimeout = game->timeout;
            for (int i = 0; i < 4; ++i) {
                game->procBlocks();
            }
            if (game->score != previousScore || game->clears != previousClears || game->level != previousLevel || game->timeout != previousTimeout) {
                logMutatris(std::format("Grid processed. score {} -> {}, clears {} -> {}, level {} -> {}, timeout {} -> {}",
                                        previousScore,
                                        game->score,
                                        previousClears,
                                        game->clears,
                                        previousLevel + 1,
                                        game->level + 1,
                                        previousTimeout,
                                        game->timeout));
            }
        }

        void advanceFocus() {
            const int previousFocus = focus;
            if (focus == 0) {
                focus = 3;
            } else if (focus == 3) {
                focus = 2;
            } else if (focus == 2) {
                focus = 1;
            } else {
                focus = 0;
            }
            lastDropTick = SDL_GetTicks();
            logMutatris(std::format("Focus advanced {} -> {}.", focusName(previousFocus), focusName(focus)));
        }

        void startGame() {
            game = std::make_unique<PuzzleGame>(difficulty, [this]() {
                playSound(lineSound);
            });
            focus = 0;
            lastDropTick = SDL_GetTicks();
            lastClearAnimationTick = lastDropTick;
            shaderLevel = -1;
            shaderIndex = -1;
            setScreen(Screen::Playing, "new game");
            logMutatris(std::format("New game started. difficulty={} timeout={}", difficulty, game->timeout));
        }

        void drawStartup() {
            const Uint32 now = SDL_GetTicks();
            const Uint32 elapsed = now - startupStartTick;
            constexpr Uint32 FIRST_FADE_IN_END = STARTUP_FADE_MS;
            constexpr Uint32 FIRST_FADE_OUT_END = FIRST_FADE_IN_END + STARTUP_FADE_MS;
            constexpr Uint32 SECOND_FADE_IN_END = FIRST_FADE_OUT_END + STARTUP_FADE_MS;
            constexpr Uint32 SECOND_HOLD_END = SECOND_FADE_IN_END + STARTUP_HOLD_MS;
            constexpr Uint32 SECOND_FADE_OUT_END = SECOND_HOLD_END + STARTUP_FADE_MS;
            constexpr Uint32 TITLE_FADE_IN_END = SECOND_FADE_OUT_END + STARTUP_FADE_MS;

            if (elapsed < FIRST_FADE_IN_END) {
                drawStartupLogo(lostLogo, static_cast<float>(elapsed) / static_cast<float>(STARTUP_FADE_MS));
            } else if (elapsed < FIRST_FADE_OUT_END) {
                const Uint32 phaseElapsed = elapsed - FIRST_FADE_IN_END;
                drawStartupLogo(lostLogo, 1.0f - (static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS)));
            } else if (elapsed < SECOND_FADE_IN_END) {
                const Uint32 phaseElapsed = elapsed - FIRST_FADE_OUT_END;
                drawStartupLogo(jbLogo, static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS));
            } else if (elapsed < SECOND_HOLD_END) {
                drawStartupLogo(jbLogo, 1.0f);
            } else if (elapsed < SECOND_FADE_OUT_END) {
                const Uint32 phaseElapsed = elapsed - SECOND_HOLD_END;
                drawStartupLogo(jbLogo, 1.0f - (static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS)));
            } else if (elapsed < TITLE_FADE_IN_END) {
                const Uint32 phaseElapsed = elapsed - SECOND_FADE_OUT_END;
                drawTitleWithAlpha(static_cast<float>(phaseElapsed) / static_cast<float>(STARTUP_FADE_MS));
            } else {
                setScreen(Screen::Title, "startup sequence complete");
            }
        }

        void drawStartupLogo(mxvk::VK_Sprite *sprite, float alphaValue) {
            if (sprite == nullptr) {
                return;
            }
            sprite->setShaderParams(std::clamp(alphaValue, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
            sprite->drawSpriteRect(0, 0, width, height);
        }

        void drawTitle() {
            drawTitleWithAlpha(1.0f);
        }

        void drawTitleWithAlpha(float alphaValue) {
            ensureIntroFonts();
            const Uint8 alphaByte = static_cast<Uint8>(std::clamp(static_cast<int>(std::lround(alphaValue * 255.0f)), 0, 255));
            if (intro != nullptr) {
                intro->setShaderParams(std::clamp(alphaValue, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
                intro->drawSpriteRect(0, 0, width, height);
            }
            printScaledText("[Press Enter to Play]", 25, 25, {255, 255, 255, alphaByte}, titleFont);
            printScaledText("Mutatris", 50, 250, {255, 255, 255, alphaByte}, titleLargeFont);
            printScaledText("lostsidedead.biz", 780, 670, {80, 120, 255, alphaByte}, titleFont);
        }

        void drawDifficulty() {
            ensureIntroFonts();
            if (start != nullptr) {
                start->drawSpriteRect(0, 0, width, height);
            }
            printScaledText("Mutatris", 560, 325, {255, 255, 255, 255}, startMediumFont);
            printScaledText("Press Space", 600, 595, {255, 255, 255, 255}, startSmallFont);
            const std::array<std::string, 3> labels{{"Easy", "Medium", "Hard"}};
            for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
                const SDL_Color color = i == difficulty ? SDL_Color{255, 230, 64, 255} : SDL_Color{255, 255, 255, 255};
                printScaledText(labels[static_cast<std::size_t>(i)], 430 + i * 180, 475, color, difficultyFont);
            }
        }

        void updatePlaying() {
            if (game == nullptr) {
                startGame();
            }
            processGrid();
            updateBackgroundShaderForLevel();
            mxvk::VK_Sprite *background = backgrounds[static_cast<std::size_t>(std::clamp(game->level, 0, static_cast<int>(backgrounds.size()) - 1))];
            if (background != nullptr) {
                background->setShaderParams(static_cast<float>(SDL_GetTicks()) / 1000.0f, 0.0f, 0.0f, 0.0f);
                background->drawSpriteRect(0, 0, width, height);
            }
            twistClearedBlocks();
            drawGrid(0);
            drawGrid(1);
            drawGrid(2);
            drawGrid(3);
            if (!console.isVisible()) {
                printScaledText("Level: " + std::to_string(game->level + 1) + "  Timeout: " + std::to_string(game->timeout), 25, 25, {255, 255, 255, 255});
                printScaledText("Score: " + std::to_string(game->score), 25, 55, {255, 255, 255, 255});
                printScaledText("Direction: " + std::to_string(focus), 25, 85, {255, 255, 255, 255});
                printScaledText("Arrows move  W shift  A rotate  S drop", 25, 670, {220, 220, 220, 255});
            }

            const Uint32 now = SDL_GetTicks();
            if (now - lastDropTick >= game->timeout) {
                if (game->grid[focus].canMoveDown()) {
                    if (game->grid[focus].gamePiece.moveDown()) {
                        logMutatris(std::format("Timer drop advanced piece on {} grid.", focusName(focus)));
                        processGrid();
                        advanceFocus();
                    }
                    lastDropTick = now;
                } else {
                    finalScore = game->score;
                    finalClears = game->clears;
                    setScreen(Screen::GameOver, "active grid blocked");
                    logMutatris(std::format("Game over. score={} clears={}", finalScore, finalClears));
                }
            }
        }

        void updateBackgroundShaderForLevel() {
            if (game == nullptr || game->level == shaderLevel) {
                return;
            }
            shaderLevel = game->level;
            const std::string shaderName = switchBackgroundShader(false);
            if (!shaderName.empty()) {
                logMutatris(std::format("Level {} selected shader {}", game->level + 1, shaderName));
            }
        }

        std::string switchBackgroundShader(bool force) {
            if (effectShaders.empty()) {
                return {};
            }
            std::uniform_int_distribution<int> shaderDistribution(0, static_cast<int>(effectShaders.size()) - 1);
            int nextShaderIndex = shaderDistribution(shaderRandom);
            if (effectShaders.size() > 1U) {
                while (nextShaderIndex == shaderIndex) {
                    nextShaderIndex = shaderDistribution(shaderRandom);
                }
            }
            shaderIndex = nextShaderIndex;

            const int targetLevel = game != nullptr ? game->level : 0;
            const int backgroundIndex = std::clamp(targetLevel, 0, static_cast<int>(backgrounds.size()) - 1);
            mxvk::VK_Sprite *background = backgrounds[static_cast<std::size_t>(backgroundIndex)];
            if (background != nullptr) {
                background->setFragmentShaderPath(effectShaders[static_cast<std::size_t>(shaderIndex)]);
            }
            if (force) {
                shaderLevel = targetLevel;
            }
            return std::filesystem::path(effectShaders[static_cast<std::size_t>(shaderIndex)]).filename().string();
        }

        void drawGameOver() {
            mxvk::VK_Sprite *sprite = finalScore >= 200 ? highScore : gameOver;
            if (sprite != nullptr) {
                sprite->drawSpriteRect(0, 0, width, height);
            }
            printScaledText((finalScore >= 200 ? "High Score: " : "Game Over Score: ") + std::to_string(finalScore) + "  Clears: " + std::to_string(finalClears), 25, 25, {255, 255, 255, 255});
            printScaledText("[ Press Space ]", 25, finalScore >= 200 ? 400 : 100, {255, 255, 255, 255});
        }

        void twistClearedBlocks() {
            const Uint32 now = SDL_GetTicks();
            const Uint32 elapsed = lastClearAnimationTick == 0U ? 0U : now - lastClearAnimationTick;
            lastClearAnimationTick = now;
            for (GameGrid &focusGrid : game->grid) {
                for (int y = 0; y < focusGrid.height(); ++y) {
                    for (int x = 0; x < focusGrid.width(); ++x) {
                        Block *block = focusGrid.at(x, y);
                        if (block != nullptr && block->color < 0) {
                            block->clearElapsedMs += elapsed;
                            if (block->clearElapsedMs >= CLEAR_ANIMATION_MS) {
                                block->color = 0;
                                block->clearElapsedMs = 0;
                            } else {
                                const Uint32 frame = std::min<Uint32>((block->clearElapsedMs * CLEAR_ANIMATION_FRAMES) / CLEAR_ANIMATION_MS, CLEAR_ANIMATION_FRAMES - 1U);
                                block->color = -static_cast<int>(frame + 1U);
                            }
                        }
                    }
                }
            }
        }

        void drawGrid(int gridIndex) {
            const GameGrid &focusGrid = game->grid[static_cast<std::size_t>(gridIndex)];
            const BoardLayout layout = boardLayout(gridIndex);
            drawGridFrame(layout, gridIndex == focus);
            for (int y = 0; y < focusGrid.height(); ++y) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    const Block *block = focusGrid.at(x, y);
                    if (block != nullptr) {
                        drawCell(layout, x, y, block->color);
                    }
                }
            }
            if (gridIndex == focus) {
                const Piece &piece = focusGrid.gamePiece;
                for (int i = 0; i < 3; ++i) {
                    int x = piece.getX();
                    int y = piece.getY();
                    if (piece.getDirection() == 0) {
                        y += i;
                    } else if (piece.getDirection() == 1) {
                        x += i;
                    } else if (piece.getDirection() == 2) {
                        x -= i;
                    } else if (piece.getDirection() == 3) {
                        y -= i;
                    }
                    const Block *block = piece.at(i);
                    if (block != nullptr) {
                        drawCell(layout, x, y, block->color);
                    }
                }
            }
        }

        struct BoardLayout {
            int x = 0;
            int y = 0;
            int gridIndex = 0;
        };

        [[nodiscard]] BoardLayout boardLayout(int gridIndex) const {
            if (gridIndex == 0) {
                return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), 0, gridIndex};
            }
            if (gridIndex == 1) {
                return {0, (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), gridIndex};
            }
            if (gridIndex == 2) {
                return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), (DESIGN_HEIGHT / 2) + 5, gridIndex};
            }
            return {DESIGN_WIDTH - (SIDE_GRID_HEIGHT * BLOCK_HEIGHT), (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2), gridIndex};
        }

        void drawGridFrame(const BoardLayout &layout, bool selected) {
            const bool sideGrid = layout.gridIndex == 1 || layout.gridIndex == 3;
            const int rows = sideGrid ? SIDE_GRID_HEIGHT : TOP_GRID_HEIGHT;
            const int boardW = sideGrid ? rows * BLOCK_HEIGHT : GRID_WIDTH * BLOCK_WIDTH;
            const int boardH = sideGrid ? GRID_WIDTH * BLOCK_WIDTH : rows * BLOCK_HEIGHT;
            const int thickness = selected ? 4 : 2;
            mxvk::VK_Sprite *pixel = blocks[0];
            pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y - thickness), scaleX(boardW + thickness * 2), scaleY(thickness));
            pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y + boardH), scaleX(boardW + thickness * 2), scaleY(thickness));
            pixel->drawSpriteRect(scaleX(layout.x - thickness), scaleY(layout.y), scaleX(thickness), scaleY(boardH));
            pixel->drawSpriteRect(scaleX(layout.x + boardW), scaleY(layout.y), scaleX(thickness), scaleY(boardH));
        }

        void drawCell(const BoardLayout &layout, int cellX, int cellY, int color) {
            if (cellX < 0 || cellY < 0) {
                return;
            }
            const int spriteIndex = color < 0 ? 1 + (std::abs(color) % 6) : std::clamp(color, 0, 7);
            mxvk::VK_Sprite *sprite = blocks[static_cast<std::size_t>(spriteIndex)];
            if (sprite == nullptr) {
                return;
            }
            int px = 0;
            int py = 0;
            int pw = BLOCK_DRAW_WIDTH;
            int ph = BLOCK_DRAW_HEIGHT;
            if (layout.gridIndex == 0) {
                px = layout.x + cellX * BLOCK_WIDTH;
                py = layout.y + cellY * BLOCK_HEIGHT;
            } else if (layout.gridIndex == 1) {
                px = layout.x + cellY * BLOCK_HEIGHT;
                py = layout.y + cellX * BLOCK_WIDTH;
                pw = BLOCK_DRAW_HEIGHT;
                ph = BLOCK_DRAW_WIDTH;
            } else if (layout.gridIndex == 2) {
                px = layout.x + cellX * BLOCK_WIDTH;
                py = layout.y + (BOTTOM_GRID_HEIGHT - 1 - cellY) * BLOCK_HEIGHT;
            } else {
                px = layout.x + (SIDE_GRID_HEIGHT - 1 - cellY) * BLOCK_HEIGHT;
                py = layout.y + (GRID_WIDTH - 1 - cellX) * BLOCK_WIDTH;
                pw = BLOCK_DRAW_HEIGHT;
                ph = BLOCK_DRAW_WIDTH;
            }
            sprite->drawSpriteRect(scaleX(px), scaleY(py), scaleX(pw), scaleY(ph));
        }

        void ensureIntroFonts() {
            const float scale = std::max(0.2f, std::min(static_cast<float>(width) / static_cast<float>(DESIGN_WIDTH), static_cast<float>(height) / static_cast<float>(DESIGN_HEIGHT)));
            const int scaledBaseSize = std::max(1, static_cast<int>(std::lround(54.0f * scale)));
            if (scaledBaseSize == introFontSize) {
                return;
            }
            introFontSize = scaledBaseSize;
            resetScaledFont(titleFont, 44, scale);
            resetScaledFont(titleLargeFont, 220, scale);
            resetScaledFont(startMediumFont, 36, scale);
            resetScaledFont(startSmallFont, 18, scale);
            resetScaledFont(difficultyFont, 30, scale);
        }

        void resetScaledFont(mxvk::Font &font, int designSize, float scale) {
            font.reset(mutatris_FONT_PATH, std::max(1, static_cast<int>(std::lround(static_cast<float>(designSize) * scale))));
        }

        void drawTextCentered(const std::string &text, int y, SDL_Color color) {
            int textWidth = 0;
            int textHeight = 0;
            if (!getTextDimensions(text, textWidth, textHeight)) {
                textWidth = static_cast<int>(text.size()) * 14;
            }
            printScaledText(text, (DESIGN_WIDTH - textWidth) / 2, y, color);
        }

        void printScaledText(const std::string &text, int x, int y, SDL_Color color) {
            printText(text, scaleX(x), scaleY(y), color);
        }

        void printScaledText(const std::string &text, int x, int y, SDL_Color color, const mxvk::Font &font) {
            printText(text, scaleX(x), scaleY(y), color, font);
        }

        [[nodiscard]] int scaleX(int value) const {
            return static_cast<int>(static_cast<float>(value) * static_cast<float>(width) / static_cast<float>(DESIGN_WIDTH));
        }

        [[nodiscard]] int scaleY(int value) const {
            return static_cast<int>(static_cast<float>(value) * static_cast<float>(height) / static_cast<float>(DESIGN_HEIGHT));
        }
    };

} // namespace

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        MutatrisWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mutatris: MXVK exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mutatris: argument exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
