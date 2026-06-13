#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <format>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#ifndef tetris_ASSET_DIR
#define tetris_ASSET_DIR "."
#endif

#ifndef tetris_SHADER_DIR
#define tetris_SHADER_DIR "."
#endif

#ifndef tetris_FONT_PATH
#define tetris_FONT_PATH "."
#endif

namespace {

    constexpr int boardWidth = 10;
    constexpr int boardHeight = 20;
    constexpr float cubeScale = 0.085f;
    constexpr float cubeSpacing = cubeScale * 1.0f;
    constexpr std::size_t nameMaxLength = 10;
    constexpr char nameCharacters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    constexpr std::size_t nameCharacterCount = sizeof(nameCharacters) - 1;

    struct Cell {
        int color = -1;
    };

    struct Block {
        int x = 0;
        int y = 0;
    };

    struct PieceDefinition {
        std::array<Block, 4> blocks;
        int color = 0;
    };

    struct ActivePiece {
        std::array<Block, 4> blocks;
        int x = 0;
        int y = 0;
        int color = 0;
    };

    struct PieceQueueEntry {
        std::array<Block, 4> blocks{};
        int color = 0;
    };

    struct BlockModel {
        std::unique_ptr<mxvk::VKAbstractModel> model;
        int color = 0;
    };

    struct LockedBlock {
        BlockModel block;
        int x = 0;
        int y = 0;
    };

    struct HighScoreEntry {
        std::string name;
        int score = 0;
    };

    class HighScores {
      public:
        explicit HighScores(std::filesystem::path filePath)
            : filePath(std::move(filePath)) {
            load();
        }

        void addScore(std::string name, int score) {
            normalizeName(name);
            scoreEntries.push_back({std::move(name), score});
            sortAndTrim();
            write();
        }

        [[nodiscard]] bool qualifies(int score) const {
            if (scoreEntries.size() < kMaxEntries) {
                return true;
            }
            return score > scoreEntries.back().score;
        }

        [[nodiscard]] const std::vector<HighScoreEntry> &entries() const {
            return scoreEntries;
        }

        [[nodiscard]] int bestScore() const {
            return scoreEntries.empty() ? 0 : scoreEntries.front().score;
        }

        void write() const {
            std::ofstream out(filePath, std::ios::trunc);
            if (!out.is_open()) {
                return;
            }

            for (const HighScoreEntry &entry : scoreEntries) {
                out << entry.name << ':' << entry.score << '\n';
            }
        }

      private:
        static constexpr std::size_t kMaxEntries = 10;
        static constexpr std::size_t kMaxNameBytes = 16;

        std::filesystem::path filePath;
        std::vector<HighScoreEntry> scoreEntries{};

        static void normalizeName(std::string &name) {
            if (name.empty()) {
                name = "Anonymous";
            }

            for (char &ch : name) {
                if (ch == '\n' || ch == '\r' || ch == ':') {
                    ch = '_';
                }
            }

            if (name.size() > kMaxNameBytes) {
                name.resize(kMaxNameBytes);
            }
        }

        void sortAndTrim() {
            std::sort(scoreEntries.begin(), scoreEntries.end(), [](const HighScoreEntry &a, const HighScoreEntry &b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                return a.name < b.name;
            });

            if (scoreEntries.size() > kMaxEntries) {
                scoreEntries.resize(kMaxEntries);
            }
        }

        void load() {
            scoreEntries.clear();

            std::ifstream in(filePath);
            if (!in.is_open()) {
                return;
            }

            std::string line;
            while (std::getline(in, line)) {
                const std::size_t separator = line.find(':');
                if (separator == std::string::npos) {
                    continue;
                }

                HighScoreEntry entry{};
                entry.name = line.substr(0, separator);
                entry.score = static_cast<int>(std::strtol(line.substr(separator + 1).c_str(), nullptr, 10));
                normalizeName(entry.name);
                scoreEntries.push_back(std::move(entry));
            }

            sortAndTrim();
        }
    };

    [[nodiscard]] std::filesystem::path resolveScoreFilePath() {
        if (const char *basePath = SDL_GetBasePath(); basePath != nullptr && basePath[0] != '\0') {
            return std::filesystem::path(basePath) / "scores.dat";
        }

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            return cwd / "scores.dat";
        }

        return std::filesystem::path("scores.dat");
    }

    const std::array<PieceDefinition, 7> pieceDefinitions{{
        {{{{-1, 0}, {0, 0}, {1, 0}, {2, 0}}}, 0},
        {{{{-1, 0}, {0, 0}, {1, 0}, {-1, 1}}}, 1},
        {{{{-1, 0}, {0, 0}, {1, 0}, {1, 1}}}, 2},
        {{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}, 3},
        {{{{-1, 0}, {0, 0}, {0, 1}, {1, 1}}}, 4},
        {{{{-1, 0}, {0, 0}, {1, 0}, {0, 1}}}, 5},
        {{{{-1, 1}, {0, 1}, {0, 0}, {1, 0}}}, 6},
    }};

    const std::array<std::string, 8> textureManifests{{
        "manifest_cyan.txt",
        "manifest_blue.txt",
        "manifest_orange.txt",
        "manifest_yellow.txt",
        "manifest_green.txt",
        "manifest_purple.txt",
        "manifest_red.txt",
        "manifest_gray.txt",
    }};

    const std::array<std::string, 8> blockTextureFiles{{
        "block_ltblue.png",
        "block_dblue.png",
        "block_orange.png",
        "block_yellow.png",
        "block_green.png",
        "block_purple.png",
        "block_red.png",
        "block_gray.png",
    }};

    const std::array<glm::vec3, 8> colorTints{{
        {0.65f, 0.95f, 1.0f},
        {0.45f, 0.56f, 1.0f},
        {1.0f, 0.63f, 0.25f},
        {1.0f, 0.92f, 0.35f},
        {0.48f, 1.0f, 0.45f},
        {0.82f, 0.48f, 1.0f},
        {1.0f, 0.42f, 0.42f},
        {0.55f, 0.58f, 0.62f},
    }};

    [[nodiscard]] std::array<Block, 4> rotatedBlocks(const ActivePiece &piece) {
        std::array<Block, 4> rotated = piece.blocks;
        for (Block &block : rotated) {
            const int oldX = block.x;
            block.x = -block.y;
            block.y = oldX;
        }
        return rotated;
    }

    enum class AppScreen {
        Intro,
        Menu,
        Game,
        GameOver,
        NetworkMultiplayer,
        HighScores,
        Credits,
    };

    class TetrisWindow final : public mxvk::VK_Window {
      public:
        TetrisWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ MXVK 3D Tetris ]-", width, height, fullscreen, MXVK_VALIDATION),
              assetRoot((path.empty() || path == ".") ? std::string(tetris_ASSET_DIR) : path),
              dataRoot(assetRoot + "/data"),
              highScores(resolveScoreFilePath()) {
            std::random_device rd;
            rng.seed(rd());
            setFont(tetris_FONT_PATH, 22);
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            tryOpenFirstGamepad();
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            music = std::make_unique<mxvk::VK_Mixer>();
            musicTrack = music->loadMusic(dataRoot + "/music.ogg");
            ensureMusicPlaying();
#endif
            background = createSprite(dataRoot + "/psychedelic_background.png");
            backgroundTransitionSprite = createSprite(
                dataRoot + "/psychedelic_background.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_background_transition.frag.spv");
            menuBackgroundSprite = createSprite(
                dataRoot + "/start_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            highScoresBackgroundSprite = createSprite(
                dataRoot + "/high_scores_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            creditsBackgroundSprite = createSprite(
                dataRoot + "/credits_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            multiplayerBackgroundSprite = createSprite(
                dataRoot + "/multiplayer_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            previewBorderSprite = createSprite(1, 1);
            const uint32_t whitePixel = 0xFFFFFFFFu;
            previewBorderSprite->updateTexture(&whitePixel, 1, 1);
            for (std::size_t i = 0; i < blockPreviewSprites.size(); ++i) {
                blockPreviewSprites[i] = createSprite(dataRoot + "/" + blockTextureFiles[i]);
            }
            gameOverSprite = createSprite(dataRoot + "/gameover.png");
            introSprite = createSprite(dataRoot + "/intro.png",
                                        std::string(tetris_SHADER_DIR) + "/tetris_intro.vert.spv",
                                        std::string(tetris_SHADER_DIR) + "/tetris_intro.frag.spv");
            introStart = std::chrono::steady_clock::now();
            initModels();
            initCreditsModel();
            resetGame();
        }

        ~TetrisWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (music) {
                music->stopMusic();
            }
#endif
            stopNameEntry();
            closeGamepad();
            cleanupModels();
        }

        void onSwapchainRecreated() override {
            forEachModel([this](mxvk::VKAbstractModel &model) {
                model.resize(this);
            });
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_QUIT) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.repeat) {
                    return;
                }
                if (screen == AppScreen::GameOver && enteringName) {
                    handleNameEntryKey(e.key.key);
                    return;
                }
            }

            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!openGamepad(e.gdevice.which)) {
                    tryOpenFirstGamepad();
                }
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
                if (screen == AppScreen::GameOver && enteringName) {
                    handleNameEntryButton(e.gbutton.button);
                    return;
                }
                handleGamepadButtonDown(e.gbutton.button);
            }
        }

        void proc() override {
            ensureMusicPlaying();
            tryOpenFirstGamepad();
            updateIntroState();
            updateScreenTransition();
            updateInput();
            if (screen == AppScreen::Game) {
                updateGame();
            }
            drawHud();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            drawScreenBackdrop(cmd, extent);

            if (screen == AppScreen::Game || screen == AppScreen::GameOver) {
                const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
                glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.2f, cameraDistance), glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridPitch), glm::vec3(1.0f, 0.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridYaw), glm::vec3(0.0f, 1.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridRoll), glm::vec3(0.0f, 0.0f, 1.0f));

                glm::mat4 proj = glm::perspective(glm::radians(46.0f), aspect, 0.1f, 100.0f);
                proj[1][1] *= -1.0f;
                const bool blinkVisible = !lineClearActive || isLineClearVisible();

                for (LockedBlock &locked : lockedBlocks) {
                    if (blinkVisible || !isClearingRow(locked.y)) {
                        drawBlock(cmd, imageIndex, locked.block, locked.x, locked.y, locked.block.color, view, proj);
                    }
                }

                if (!lineClearActive) {
                    for (size_t i = 0; i < activeModels.size(); ++i) {
                        const int x = active.x + active.blocks[i].x;
                        const int y = active.y + active.blocks[i].y;
                        drawBlock(cmd, imageIndex, activeModels[i], x, y, active.color, view, proj);
                    }
                }

                drawFrame(cmd, imageIndex, view, proj);
                drawNextPiecePreview(cmd, imageIndex, extent);
            }
            if (screen == AppScreen::Credits) {
                drawCreditsModel(cmd, imageIndex, extent);
            }
            drawGameScreenTransitionOverlay(cmd, extent);
            drawIntroOverlay(cmd, extent);
            drawGameOverOverlay(cmd, extent);
        }

      private:
        std::string assetRoot;
        std::string dataRoot;
        std::array<std::array<Cell, boardWidth>, boardHeight> board{};
        ActivePiece active{};
        std::mt19937 rng{};
        std::vector<LockedBlock> lockedBlocks{};
        std::array<BlockModel, 4> activeModels{};
        std::vector<BlockModel> frameModels{};
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VK_Sprite *backgroundTransitionSprite = nullptr;
        mxvk::VK_Sprite *menuBackgroundSprite = nullptr;
        mxvk::VK_Sprite *highScoresBackgroundSprite = nullptr;
        mxvk::VK_Sprite *creditsBackgroundSprite = nullptr;
        mxvk::VK_Sprite *multiplayerBackgroundSprite = nullptr;
        mxvk::VK_Sprite *previewBorderSprite = nullptr;
        std::array<mxvk::VK_Sprite *, 8> blockPreviewSprites{};
        mxvk::VK_Sprite *introSprite = nullptr;
        mxvk::VK_Sprite *gameOverSprite = nullptr;
        std::unique_ptr<mxvk::VKAbstractModel> creditsTuxModel{};
        HighScores highScores;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> music{};
        int musicTrack = -1;
#endif
        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepadId = 0;
        std::chrono::steady_clock::time_point introStart{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastFall{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastInputUpdate{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lineClearStart{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point backgroundTransitionStart{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point screenTransitionStart{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point gameOverTransitionStart{std::chrono::steady_clock::now()};
        bool introActive = true;
        float gridYaw = 0.0f;
        float gridPitch = 0.0f;
        float gridRoll = 0.0f;
        float cameraDistance = 2.45f;
        int score = 0;
        int linesCleared = 0;
        int level = 1;
        int cursorPos = 0;
        float fallSeconds = 0.65f;
        float moveRepeatTimer = 0.0f;
        float softDropRepeatTimer = 0.0f;
        float gamepadMoveRepeatTimer = 0.0f;
        float gamepadSoftDropRepeatTimer = 0.0f;
        bool gameOver = false;
        bool enteringName = false;
        bool highScoresAfterSave = false;
        std::size_t nameCharIndex = 0;
        std::string playerName{};
        bool hardDropHeld = false;
        bool rotateHeld = false;
        bool resetHeld = false;
        bool enterHeld = false;
        bool escapeHeld = false;
        bool backspaceHeld = false;
        bool menuUpHeld = false;
        bool menuDownHeld = false;
        bool menuEnterHeld = false;
        bool introSkipHeld = false;
        bool introFadeStarted = false;
        bool screenTransitionActive = false;
        bool gameOverTransitionActive = false;
        bool lineClearActive = false;
        bool backgroundTransitionActive = false;
        std::array<bool, boardHeight> clearingRows{};
        PieceQueueEntry nextPiece{};
        AppScreen screen = AppScreen::Intro;
        AppScreen transitionFromScreen = AppScreen::Intro;
        static constexpr float introHoldSeconds = 5.0f;
        static constexpr float introFadeSeconds = 1.0f;
        static constexpr float screenTransitionSeconds = 0.36f;
        static constexpr float gameOverTransitionSeconds = 0.5f;
        static constexpr float backgroundTransitionSeconds = 1.25f;
        static constexpr Sint16 gamepadDeadzone = 10000;
        static constexpr float gamepadMoveInitialDelaySeconds = 0.22f;
        static constexpr float gamepadMoveRepeatSeconds = 0.12f;
        static constexpr float gamepadSoftDropInitialDelaySeconds = 0.18f;
        static constexpr float gamepadSoftDropRepeatSeconds = 0.08f;
        static constexpr float gamepadStickRotateSpeed = 120.0f;
        static constexpr float gamepadStickPitchSpeed = 100.0f;
        static constexpr float gamepadStickScale = 1.0f / 32768.0f;
        int gamepadMoveDirection = 0;
        float gamepadMoveHeldSeconds = 0.0f;
        bool gamepadSoftDropHeld = false;

        void initModels() {
            frameModels.reserve(boardWidth + (boardHeight * 2) + 2);
            for (int i = 0; i < boardWidth + (boardHeight * 2) + 2; ++i) {
                frameModels.push_back(loadBlockModel(7));
            }
        }

        [[nodiscard]] BlockModel loadBlockModel(int color) {
            BlockModel block{};
            block.color = color;
            block.model = std::make_unique<mxvk::VKAbstractModel>();
            block.model->load(this, dataRoot + "/cube.mxmod.z", dataRoot + "/" + textureManifests[color], dataRoot, 1.0f);
            block.model->setShaders(this,
                                    std::string(tetris_SHADER_DIR) + "/tetris_model.vert.spv",
                                    std::string(tetris_SHADER_DIR) + "/tetris_model.frag.spv");
            return block;
        }

        void cleanupModels() {
            forEachModel([this](mxvk::VKAbstractModel &model) {
                model.cleanup(this);
            });
            lockedBlocks.clear();
        }

        template <typename Fn>
        void forEachModel(Fn fn) {
            for (LockedBlock &locked : lockedBlocks) {
                if (locked.block.model) {
                    fn(*locked.block.model);
                }
            }
            for (BlockModel &block : activeModels) {
                if (block.model) {
                    fn(*block.model);
                }
            }
            for (BlockModel &block : frameModels) {
                if (block.model) {
                    fn(*block.model);
                }
            }
            if (creditsTuxModel) {
                fn(*creditsTuxModel);
            }
        }

        void initCreditsModel() {
            creditsTuxModel = std::make_unique<mxvk::VKAbstractModel>();
            creditsTuxModel->load(this,
                                   dataRoot + "/tux/tux.obj",
                                   dataRoot + "/tux/tux.mtl",
                                   dataRoot + "/tux",
                                   0.35f);
            creditsTuxModel->setShaders(this,
                                         std::string(tetris_SHADER_DIR) + "/tetris_model.vert.spv",
                                         std::string(tetris_SHADER_DIR) + "/tetris_model.frag.spv");
        }

        void resetGame() {
            stopNameEntry();
            clearLockedBlocks();
            for (auto &row : board) {
                for (Cell &cell : row) {
                    cell.color = -1;
                }
            }
            gameOver = false;
            gameOverTransitionActive = false;
            lineClearActive = false;
            backgroundTransitionActive = false;
            clearingRows.fill(false);
            score = 0;
            linesCleared = 0;
            updateDifficulty();
            lastFall = std::chrono::steady_clock::now();
            nextPiece = randomPiece();
            spawnPiece();
        }

        void clearLockedBlocks() {
            for (LockedBlock &locked : lockedBlocks) {
                if (locked.block.model) {
                    locked.block.model->cleanup(this);
                }
            }
            lockedBlocks.clear();
        }

        void spawnPiece() {
            active.blocks = nextPiece.blocks;
            active.x = boardWidth / 2;
            active.y = boardHeight - 2;
            active.color = nextPiece.color;
            reloadActiveModels(active.color);
            gameOver = collides(active.x, active.y, active.blocks);
            if (gameOver) {
                enterGameOverState();
            }
            nextPiece = randomPiece();
        }

        void reloadActiveModels(int color) {
            for (BlockModel &block : activeModels) {
                if (block.model) {
                    block.model->cleanup(this);
                }
                block = loadBlockModel(color);
            }
        }

        [[nodiscard]] PieceQueueEntry randomPiece() {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(pieceDefinitions.size()) - 1);
            const PieceDefinition &definition = pieceDefinitions[dist(rng)];
            return PieceQueueEntry{definition.blocks, definition.color};
        }

        void updateDifficulty() {
            const int previousLevel = level;
            level = (linesCleared / 8) + 1;
            static constexpr std::array<float, 16> arcadeFallSeconds{
                0.72f, 0.66f, 0.60f, 0.54f,
                0.48f, 0.43f, 0.38f, 0.34f,
                0.30f, 0.26f, 0.23f, 0.20f,
                0.18f, 0.16f, 0.14f, 0.12f,
            };

            const std::size_t index = std::min<std::size_t>(arcadeFallSeconds.size() - 1, static_cast<std::size_t>(std::max(level - 1, 0)));
            fallSeconds = arcadeFallSeconds[index];
            if (level > previousLevel) {
                triggerBackgroundTransition();
            }
        }

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        void ensureMusicPlaying() {
            if (!music) {
                return;
            }
            if (musicTrack < 0) {
                return;
            }
            if (!music->isMusicPlaying(musicTrack)) {
                if (music->playMusic(musicTrack, -1) != 0) {
                    throw mxvk::Exception("Could not start Tetris background music");
                }
            }
        }
#else
        void ensureMusicPlaying() {}
#endif

        void updateInput() {
            const auto now = std::chrono::steady_clock::now();
            float deltaSeconds = std::chrono::duration<float>(now - lastInputUpdate).count();
            lastInputUpdate = now;
            deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);

            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            if (introActive) {
                const bool enterDown = keys[SDL_SCANCODE_RETURN];
                const bool spaceDown = keys[SDL_SCANCODE_SPACE];
                const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
                const bool skipDown = enterDown || spaceDown;
                if (skipDown && !introSkipHeld) {
                    const auto now = std::chrono::steady_clock::now();
                    introStart = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(introHoldSeconds));
                    introFadeStarted = false;
                    requestScreen(AppScreen::Menu);
                }
                if (escapeDown && !escapeHeld) {
                    exit();
                }
                introSkipHeld = skipDown;
                escapeHeld = escapeDown;
                return;
            }

            if (screenTransitionActive) {
                return;
            }

            if (screen == AppScreen::GameOver) {
                if (enteringName) {
                    return;
                }
                handleGameOverKeys(keys);
                return;
            }

            if (screen != AppScreen::Game) {
                handleMenuKeys(keys);
                return;
            }

            handleHeldViewRotation(keys, deltaSeconds);
            handleHeldPieceMovement(keys, deltaSeconds);
            handleOneShotKeys(keys);
            handleGamepadInput(deltaSeconds);
        }

        void handleMenuKeys(const bool *keys) {
            if (screenTransitionActive) {
                menuUpHeld = keys[SDL_SCANCODE_UP];
                menuDownHeld = keys[SDL_SCANCODE_DOWN];
                menuEnterHeld = keys[SDL_SCANCODE_RETURN];
                escapeHeld = keys[SDL_SCANCODE_ESCAPE];
                return;
            }

            const bool upDown = keys[SDL_SCANCODE_UP];
            const bool downDown = keys[SDL_SCANCODE_DOWN];
            const bool enterDown = keys[SDL_SCANCODE_RETURN];
            const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];

            if (screen == AppScreen::Menu) {
                if (upDown && !menuUpHeld) {
                    cursorPos = (cursorPos + 3) % 4;
                }
                if (downDown && !menuDownHeld) {
                    cursorPos = (cursorPos + 1) % 4;
                }
            }
            if (enterDown && !menuEnterHeld) {
                if (screen == AppScreen::Menu) {
                    if (cursorPos == 0) {
                        startGame();
                    } else if (cursorPos == 1) {
                        goToNetworkMultiplayer();
                    } else if (cursorPos == 2) {
                        goToHighScores();
                    } else if (cursorPos == 3) {
                        goToCredits();
                    }
                } else if (screen == AppScreen::HighScores && highScoresAfterSave) {
                    highScoresAfterSave = false;
                    restartIntroSequence();
                } else {
                    goToMenu();
                }
            }
            if (escapeDown && !escapeHeld) {
                if (screen == AppScreen::Menu) {
                    exit();
                } else {
                    highScoresAfterSave = false;
                    goToMenu();
                }
            }

            menuUpHeld = upDown;
            menuDownHeld = downDown;
            menuEnterHeld = enterDown;
            escapeHeld = escapeDown;
        }

        void handleGamepadInput(float deltaSeconds) {
            if (gamepad == nullptr || introActive || (screen != AppScreen::Game && screen != AppScreen::GameOver) || gameOver || lineClearActive) {
                gamepadMoveDirection = 0;
                gamepadMoveHeldSeconds = 0.0f;
                gamepadSoftDropHeld = false;
                gamepadMoveRepeatTimer = 0.0f;
                gamepadSoftDropRepeatTimer = 0.0f;
                return;
            }

            const Sint16 leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
            const Sint16 leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
            const Sint16 rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
            const Sint16 rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);

            const int moveDirection = (leftX < -gamepadDeadzone) ? -1 : (leftX > gamepadDeadzone) ? 1
                                                                                                    : 0;
            if (moveDirection == 0) {
                gamepadMoveDirection = 0;
                gamepadMoveHeldSeconds = 0.0f;
                gamepadMoveRepeatTimer = 0.0f;
            } else {
                if (moveDirection != gamepadMoveDirection) {
                    gamepadMoveDirection = moveDirection;
                    gamepadMoveHeldSeconds = 0.0f;
                    gamepadMoveRepeatTimer = 0.0f;
                    movePiece(gamepadMoveDirection, 0);
                } else {
                    gamepadMoveHeldSeconds += deltaSeconds;
                    const float threshold = (gamepadMoveHeldSeconds < gamepadMoveInitialDelaySeconds)
                                                ? gamepadMoveInitialDelaySeconds
                                                : gamepadMoveRepeatSeconds;
                    gamepadMoveRepeatTimer += deltaSeconds;
                    if (gamepadMoveRepeatTimer >= threshold) {
                        movePiece(gamepadMoveDirection, 0);
                        gamepadMoveRepeatTimer = 0.0f;
                    }
                }
            }

            const bool softDropDown = leftY > gamepadDeadzone;
            if (!softDropDown) {
                gamepadSoftDropHeld = false;
                gamepadSoftDropRepeatTimer = 0.0f;
            } else {
                const float threshold = gamepadSoftDropHeld ? gamepadSoftDropRepeatSeconds : gamepadSoftDropInitialDelaySeconds;
                gamepadSoftDropRepeatTimer += deltaSeconds;
                if (gamepadSoftDropRepeatTimer >= threshold) {
                    softDrop();
                    gamepadSoftDropRepeatTimer = 0.0f;
                    lastFall = std::chrono::steady_clock::now();
                    gamepadSoftDropHeld = true;
                }
            }

            if (std::abs(rightX) > gamepadDeadzone) {
                gridYaw += static_cast<float>(rightX) * gamepadStickScale * gamepadStickRotateSpeed * deltaSeconds;
            }
            if (std::abs(rightY) > gamepadDeadzone) {
                gridPitch = std::clamp(gridPitch - static_cast<float>(rightY) * gamepadStickScale * gamepadStickPitchSpeed * deltaSeconds,
                                        -70.0f,
                                        70.0f);
            }

            constexpr float gamepadZoomSpeed = 3.2f;
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                cameraDistance = std::min(9.0f, cameraDistance + gamepadZoomSpeed * deltaSeconds);
            }
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                cameraDistance = std::max(1.65f, cameraDistance - gamepadZoomSpeed * deltaSeconds);
            }
        }

        void handleGamepadButtonDown(Uint8 button) {
            if (gamepad == nullptr) {
                return;
            }
            if (screenTransitionActive) {
                return;
            }

            if (introActive) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    introActive = false;
                    requestScreen(AppScreen::Menu);
                }
                return;
            }

            switch (screen) {
            case AppScreen::Menu:
                if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    cursorPos = (cursorPos + 3) % 4;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    cursorPos = (cursorPos + 1) % 4;
                } else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    if (cursorPos == 0) {
                        startGame();
                    } else if (cursorPos == 1) {
                        goToNetworkMultiplayer();
                    } else if (cursorPos == 2) {
                        goToHighScores();
                    } else if (cursorPos == 3) {
                        goToCredits();
                    }
                } else if (button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                    exit();
                }
                break;
            case AppScreen::Game:
                if (gameOver) {
                    if (button == SDL_GAMEPAD_BUTTON_START || button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        restartIntroSequence();
                    } else if (button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                        goToMenu();
                    }
                    return;
                }
                if (lineClearActive) {
                    return;
                }
                if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    rotatePiece();
                } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                    hardDrop();
                } else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    goToMenu();
                }
                break;
            case AppScreen::GameOver:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    if (enteringName) {
                        commitScoreEntry();
                    } else {
                        restartIntroSequence();
                    }
                } else if (button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                    highScoresAfterSave = false;
                    goToMenu();
                }
                break;
            case AppScreen::NetworkMultiplayer:
            case AppScreen::HighScores:
            case AppScreen::Credits:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START || button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                    highScoresAfterSave = false;
                    goToMenu();
                }
                break;
            case AppScreen::Intro:
                break;
            }
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

        void closeGamepad() {
            if (gamepad != nullptr) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                gamepadId = 0;
            }
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

        void updateIntroState() {
            if (!introActive) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - introStart).count();
            if (!introFadeStarted && elapsed >= introHoldSeconds) {
                introFadeStarted = true;
                requestScreen(AppScreen::Menu);
            }
            if (elapsed >= (introHoldSeconds + introFadeSeconds)) {
                introActive = false;
                introFadeStarted = false;
                lastFall = now;
            }
        }

        void startGame() {
            const AppScreen previousScreen = screen;
            resetGame();
            introActive = false;
            screen = AppScreen::Game;
            screenTransitionActive = previousScreen != AppScreen::Game;
            transitionFromScreen = previousScreen;
            screenTransitionStart = std::chrono::steady_clock::now();
            lastInputUpdate = std::chrono::steady_clock::now();
            resetMenuLatchState();
        }

        void restartIntroSequence() {
            resetGame();
            highScoresAfterSave = false;
            introActive = true;
            introFadeStarted = false;
            introStart = std::chrono::steady_clock::now();
            screen = AppScreen::Intro;
            screenTransitionActive = false;
            transitionFromScreen = AppScreen::Intro;
            lastInputUpdate = std::chrono::steady_clock::now();
            resetMenuLatchState();
            introSkipHeld = true;
        }

        void enterGameOverState() {
            screen = AppScreen::GameOver;
            gameOverTransitionActive = true;
            gameOverTransitionStart = std::chrono::steady_clock::now();
            nameCharIndex = 0;
            playerName.clear();
            enteringName = highScores.qualifies(score);
            highScoresAfterSave = false;
            resetMenuLatchState();
        }

        void stopNameEntry() {
            enteringName = false;
            nameCharIndex = 0;
            playerName.clear();
        }

        void commitScoreEntry() {
            highScores.addScore(playerName, score);
            stopNameEntry();
            highScoresAfterSave = true;
            menuEnterHeld = true;
            goToHighScores();
        }

        [[nodiscard]] char currentNameCharacter() const {
            return nameCharacters[nameCharIndex % nameCharacterCount];
        }

        void cycleNameCharacter(int delta) {
            if (!enteringName) {
                return;
            }
            const int count = static_cast<int>(nameCharacterCount);
            const int next = (static_cast<int>(nameCharIndex) + delta + count) % count;
            nameCharIndex = static_cast<std::size_t>(next);
        }

        void appendCurrentNameCharacter() {
            if (!enteringName || playerName.size() >= nameMaxLength) {
                return;
            }
            playerName += currentNameCharacter();
        }

        void deleteNameCharacter() {
            if (!enteringName || playerName.empty()) {
                return;
            }
            playerName.pop_back();
        }

        void handleNameEntryKey(SDL_Keycode key) {
            if (key == SDLK_UP) {
                cycleNameCharacter(-1);
            } else if (key == SDLK_DOWN) {
                cycleNameCharacter(1);
            } else if (key == SDLK_SPACE) {
                appendCurrentNameCharacter();
            } else if (key == SDLK_BACKSPACE) {
                deleteNameCharacter();
            } else if (key == SDLK_RETURN) {
                commitScoreEntry();
            } else if (key == SDLK_ESCAPE) {
                highScoresAfterSave = false;
                goToMenu();
            }
        }

        void handleNameEntryButton(Uint8 button) {
            if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                cycleNameCharacter(-1);
            } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                cycleNameCharacter(1);
            } else if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                appendCurrentNameCharacter();
            } else if (button == SDL_GAMEPAD_BUTTON_WEST) {
                commitScoreEntry();
            } else if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
                deleteNameCharacter();
            }
        }

        void goToMenu() {
            if (screen == AppScreen::Game || screen == AppScreen::GameOver) {
                gameOver = false;
            }
            gameOverTransitionActive = false;
            highScoresAfterSave = false;
            stopNameEntry();
            cursorPos = 0;
            requestScreen(AppScreen::Menu);
        }

        void goToHighScores() {
            gameOver = false;
            stopNameEntry();
            requestScreen(AppScreen::HighScores);
        }

        void goToCredits() {
            requestScreen(AppScreen::Credits);
        }

        void goToNetworkMultiplayer() {
            requestScreen(AppScreen::NetworkMultiplayer);
        }

        void requestScreen(AppScreen nextScreen) {
            if (screen == nextScreen && !screenTransitionActive) {
                return;
            }
            if (nextScreen == AppScreen::Game) {
                screenTransitionActive = false;
                screen = nextScreen;
                return;
            }
            if (screen == AppScreen::Game || screen == AppScreen::GameOver) {
                screenTransitionActive = false;
                screen = nextScreen;
                return;
            }
            transitionFromScreen = screen;
            screen = nextScreen;
            screenTransitionActive = true;
            screenTransitionStart = std::chrono::steady_clock::now();
            resetMenuLatchState();
        }

        void updateScreenTransition() {
            if (!screenTransitionActive) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - screenTransitionStart).count();
            if (elapsed >= screenTransitionSeconds) {
                screenTransitionActive = false;
                transitionFromScreen = screen;
                resetMenuLatchState();
            }
        }

        float screenFadeAlpha() const {
            if (!screenTransitionActive) {
                return 1.0f;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - screenTransitionStart).count();
            return std::clamp(elapsed / screenTransitionSeconds, 0.0f, 1.0f);
        }

        float gameOverFadeAlpha() const {
            if (!gameOverTransitionActive) {
                return 1.0f;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - gameOverTransitionStart).count();
            return std::clamp(elapsed / gameOverTransitionSeconds, 0.0f, 1.0f);
        }

        void resetMenuLatchState() {
            menuUpHeld = false;
            menuDownHeld = false;
            menuEnterHeld = false;
            introSkipHeld = false;
            backspaceHeld = false;
        }

        void updateMenuInputLatchState() {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }
            menuUpHeld = keys[SDL_SCANCODE_UP];
            menuDownHeld = keys[SDL_SCANCODE_DOWN];
            menuEnterHeld = keys[SDL_SCANCODE_RETURN];
            escapeHeld = keys[SDL_SCANCODE_ESCAPE];
        }

        void handleHeldViewRotation(const bool *keys, float deltaSeconds) {
            constexpr float yawSpeed = 115.0f;
            constexpr float pitchSpeed = 90.0f;
            constexpr float rollSpeed = 100.0f;
            constexpr float zoomSpeed = 3.2f;

            if (keys[SDL_SCANCODE_A]) {
                gridYaw -= yawSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_D]) {
                gridYaw += yawSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_W]) {
                gridPitch = std::clamp(gridPitch + pitchSpeed * deltaSeconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_S]) {
                gridPitch = std::clamp(gridPitch - pitchSpeed * deltaSeconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_Q]) {
                gridRoll -= rollSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_E]) {
                gridRoll += rollSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_PAGEUP]) {
                cameraDistance = std::max(1.65f, cameraDistance - zoomSpeed * deltaSeconds);
            }
            if (keys[SDL_SCANCODE_PAGEDOWN]) {
                cameraDistance = std::min(9.0f, cameraDistance + zoomSpeed * deltaSeconds);
            }
        }

        void handleHeldPieceMovement(const bool *keys, float deltaSeconds) {
            if (gameOver || lineClearActive) {
                moveRepeatTimer = 0.0f;
                softDropRepeatTimer = 0.0f;
                return;
            }

            constexpr float horizontalRepeatSeconds = 0.14f;
            constexpr float softDropRepeatSeconds = 0.075f;

            moveRepeatTimer += deltaSeconds;
            softDropRepeatTimer += deltaSeconds;

            int horizontalDirection = 0;
            if (keys[SDL_SCANCODE_LEFT] && !keys[SDL_SCANCODE_RIGHT]) {
                horizontalDirection = -1;
            } else if (keys[SDL_SCANCODE_RIGHT] && !keys[SDL_SCANCODE_LEFT]) {
                horizontalDirection = 1;
            }

            if (horizontalDirection != 0) {
                if (moveRepeatTimer >= horizontalRepeatSeconds) {
                    movePiece(horizontalDirection, 0);
                    moveRepeatTimer = 0.0f;
                }
            } else {
                moveRepeatTimer = horizontalRepeatSeconds;
            }

            if (keys[SDL_SCANCODE_DOWN]) {
                if (softDropRepeatTimer >= softDropRepeatSeconds) {
                    softDrop();
                    softDropRepeatTimer = 0.0f;
                    lastFall = std::chrono::steady_clock::now();
                }
            } else {
                softDropRepeatTimer = softDropRepeatSeconds;
            }
        }

        void handleOneShotKeys(const bool *keys) {
            const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
            const bool hardDropDown = keys[SDL_SCANCODE_Z];
            const bool rotateDown = keys[SDL_SCANCODE_UP];
            const bool resetDown = keys[SDL_SCANCODE_R];
            const bool enterDown = keys[SDL_SCANCODE_RETURN];

            if (escapeDown && !escapeHeld) {
                goToMenu();
            }
            if (gameOver && enterDown && !enterHeld) {
                restartIntroSequence();
                escapeHeld = escapeDown;
                hardDropHeld = hardDropDown;
                rotateHeld = rotateDown;
                resetHeld = resetDown;
                enterHeld = enterDown;
                return;
            }
            if (hardDropDown && !hardDropHeld) {
                hardDrop();
            }
            if (rotateDown && !rotateHeld) {
                rotatePiece();
            }
            if (resetDown && !resetHeld) {
                resetGame();
            }

            escapeHeld = escapeDown;
            hardDropHeld = hardDropDown;
            rotateHeld = rotateDown;
            resetHeld = resetDown;
            enterHeld = enterDown;
        }

        void handleGameOverKeys(const bool *keys) {
            const bool enterDown = keys[SDL_SCANCODE_RETURN];
            const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
            const bool resetDown = keys[SDL_SCANCODE_R];
            const bool highScoresDown = keys[SDL_SCANCODE_H];
            const bool backspaceDown = keys[SDL_SCANCODE_BACKSPACE];

            if (enteringName) {
                if (backspaceDown && !backspaceHeld && !playerName.empty()) {
                    playerName.pop_back();
                }
                if (enterDown && !enterHeld) {
                    commitScoreEntry();
                    backspaceHeld = backspaceDown;
                    enterHeld = enterDown;
                    escapeHeld = escapeDown;
                    resetHeld = resetDown;
                    menuEnterHeld = highScoresDown;
                    return;
                }

                backspaceHeld = backspaceDown;
                enterHeld = enterDown;
                escapeHeld = escapeDown;
                resetHeld = resetDown;
                menuEnterHeld = highScoresDown;
                return;
            } else {
                if (enterDown && !enterHeld) {
                    restartIntroSequence();
                    backspaceHeld = backspaceDown;
                    enterHeld = enterDown;
                    escapeHeld = escapeDown;
                    resetHeld = resetDown;
                    menuEnterHeld = highScoresDown;
                    return;
                }
            }

            if (resetDown && !resetHeld) {
                restartIntroSequence();
            }
            if (highScoresDown && !menuEnterHeld) {
                stopNameEntry();
                goToHighScores();
            }
            if (escapeDown && !escapeHeld) {
                goToMenu();
            }

            backspaceHeld = backspaceDown;
            enterHeld = enterDown;
            escapeHeld = escapeDown;
            resetHeld = resetDown;
            menuEnterHeld = highScoresDown;
        }

        [[nodiscard]] bool collides(int pieceX, int pieceY, const std::array<Block, 4> &blocks) const {
            for (const Block &block : blocks) {
                const int x = pieceX + block.x;
                const int y = pieceY + block.y;
                if (x < 0 || x >= boardWidth || y < 0) {
                    return true;
                }
                if (y >= boardHeight) {
                    continue;
                }
                if (board[y][x].color >= 0) {
                    return true;
                }
            }
            return false;
        }

        void movePiece(int dx, int dy) {
            if (!gameOver && !lineClearActive && !collides(active.x + dx, active.y + dy, active.blocks)) {
                active.x += dx;
                active.y += dy;
            }
        }

        void softDrop() {
            if (gameOver || lineClearActive) {
                return;
            }
            if (collides(active.x, active.y - 1, active.blocks)) {
                lockPiece();
            } else {
                --active.y;
            }
        }

        void hardDrop() {
            if (gameOver || lineClearActive) {
                return;
            }
            while (!collides(active.x, active.y - 1, active.blocks)) {
                --active.y;
            }
            lockPiece();
        }

        void rotatePiece() {
            if (gameOver || lineClearActive) {
                return;
            }
            const auto rotated = rotatedBlocks(active);
            if (!collides(active.x, active.y, rotated)) {
                active.blocks = rotated;
                return;
            }
            if (!collides(active.x - 1, active.y, rotated)) {
                --active.x;
                active.blocks = rotated;
                return;
            }
            if (!collides(active.x + 1, active.y, rotated)) {
                ++active.x;
                active.blocks = rotated;
            }
        }

        void updateGame() {
            if (screen != AppScreen::Game || introActive) {
                return;
            }
            if (gameOver) {
                return;
            }

            if (lineClearActive) {
                const auto now = std::chrono::steady_clock::now();
                const float elapsed = std::chrono::duration<float>(now - lineClearStart).count();
                if (elapsed >= 1.0f) {
                    finalizeLineClear();
                }
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - lastFall).count();
            if (elapsed >= fallSeconds) {
                softDrop();
                lastFall = now;
            }
        }

        void lockPiece() {
            for (const Block &block : active.blocks) {
                const int x = active.x + block.x;
                const int y = active.y + block.y;
                if (x >= 0 && x < boardWidth && y >= 0 && y < boardHeight) {
                    board[y][x].color = active.color;
                    LockedBlock locked{};
                    locked.block = loadBlockModel(active.color);
                    locked.x = x;
                    locked.y = y;
                    lockedBlocks.push_back(std::move(locked));
                }
            }
            const auto fullRows = findFullRows();
            if (std::any_of(fullRows.begin(), fullRows.end(), [](bool value) { return value; })) {
                startLineClear(fullRows);
            } else {
                spawnPiece();
            }
            lastFall = std::chrono::steady_clock::now();
        }

        [[nodiscard]] std::array<bool, boardHeight> findFullRows() const {
            std::array<bool, boardHeight> fullRows{};
            for (int y = 0; y < boardHeight; ++y) {
                bool full = true;
                for (int x = 0; x < boardWidth; ++x) {
                    full = full && board[y][x].color >= 0;
                }
                fullRows[y] = full;
            }
            return fullRows;
        }

        void startLineClear(const std::array<bool, boardHeight> &fullRows) {
            clearingRows = fullRows;
            lineClearActive = true;
            lineClearStart = std::chrono::steady_clock::now();
        }

        void finalizeLineClear() {
            int cleared = 0;
            for (bool row : clearingRows) {
                if (row) {
                    ++cleared;
                }
            }

            int writeY = 0;
            for (int readY = 0; readY < boardHeight; ++readY) {
                if (clearingRows[readY]) {
                    continue;
                }
                if (writeY != readY) {
                    board[writeY] = board[readY];
                }
                ++writeY;
            }
            for (; writeY < boardHeight; ++writeY) {
                for (Cell &cell : board[writeY]) {
                    cell.color = -1;
                }
            }

            eraseClearedModels(clearingRows);
            clearingRows.fill(false);
            lineClearActive = false;
            if (cleared > 0) {
                score += cleared * 10;
                if (cleared > 1) {
                    score += 5;
                }
                linesCleared += cleared;
                updateDifficulty();
            }
            spawnPiece();
            lastFall = std::chrono::steady_clock::now();
        }

        void eraseClearedModels(const std::array<bool, boardHeight> &fullRows) {
            std::vector<LockedBlock> remaining{};
            remaining.reserve(lockedBlocks.size());
            for (LockedBlock &locked : lockedBlocks) {
                if (locked.y >= 0 && locked.y < boardHeight && fullRows[locked.y]) {
                    if (locked.block.model) {
                        locked.block.model->cleanup(this);
                    }
                    continue;
                }

                int rowsBelow = 0;
                for (int y = 0; y < locked.y; ++y) {
                    if (fullRows[y]) {
                        ++rowsBelow;
                    }
                }
                locked.y -= rowsBelow;
                remaining.push_back(std::move(locked));
            }
            lockedBlocks = std::move(remaining);
        }

        [[nodiscard]] bool isClearingRow(int y) const {
            return lineClearActive && y >= 0 && y < boardHeight && clearingRows[y];
        }

        [[nodiscard]] bool isLineClearVisible() const {
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - lineClearStart).count();
            constexpr float blinkIntervalSeconds = 0.12f;
            return (static_cast<int>(elapsed / blinkIntervalSeconds) % 2) == 0;
        }

        void drawIntroOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (!introActive || introSprite == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - introStart).count();
            const float fadeProgress = std::clamp((elapsed - introHoldSeconds) / introFadeSeconds, 0.0f, 1.0f);
            const float alpha = 1.0f - fadeProgress;

            introSprite->setShaderParams(elapsed, 0.0f, 0.0f, alpha);
            introSprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            introSprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            introSprite->clearQueue();
        }

        void drawGameOverOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            const bool shouldShow = (screen == AppScreen::Game && gameOver) || screen == AppScreen::GameOver;
            if (!shouldShow || gameOverSprite == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                return;
            }

            const float alpha = gameOverFadeAlpha();
            gameOverSprite->setShaderParams(0.0f, 0.0f, 0.0f, alpha);
            gameOverSprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            gameOverSprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            gameOverSprite->clearQueue();
        }

        [[nodiscard]] glm::vec3 blockPosition(int x, int y) const {
            const float worldX = (static_cast<float>(x) - (static_cast<float>(boardWidth) - 1.0f) * 0.5f) * cubeSpacing;
            const float worldY = (static_cast<float>(y) - (static_cast<float>(boardHeight) - 1.0f) * 0.5f) * cubeSpacing;
            return glm::vec3(worldX, worldY, 0.0f);
        }

        [[nodiscard]] glm::mat4 blockMatrix(int x, int y, float scale = cubeScale) const {
            glm::mat4 model(1.0f);
            model = glm::translate(model, blockPosition(x, y));
            model = glm::scale(model, glm::vec3(scale));
            return model;
        }

        void drawBlock(VkCommandBuffer cmd,
                       uint32_t imageIndex,
                       BlockModel &block,
                       int x,
                       int y,
                       int color,
                       const glm::mat4 &view,
                       const glm::mat4 &proj) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = blockMatrix(x, y);
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(colorTints[color], 1.0f);
            block.model->updateUBO(imageIndex, ubo);
            block.model->render(cmd, imageIndex, false);
        }

        void drawFrame(VkCommandBuffer cmd, uint32_t imageIndex, const glm::mat4 &view, const glm::mat4 &proj) {
            size_t index = 0;
            for (int y = 0; y < boardHeight; ++y) {
                drawFrameBlock(cmd, imageIndex, frameModels[index++], -1, y, view, proj);
                drawFrameBlock(cmd, imageIndex, frameModels[index++], boardWidth, y, view, proj);
            }
            for (int x = -1; x <= boardWidth; ++x) {
                drawFrameBlock(cmd, imageIndex, frameModels[index++], x, -1, view, proj);
            }
        }

        void drawFrameBlock(VkCommandBuffer cmd,
                            uint32_t imageIndex,
                            BlockModel &block,
                            int x,
                            int y,
                            const glm::mat4 &view,
                            const glm::mat4 &proj) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = blockMatrix(x, y, cubeScale * 0.82f);
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(colorTints[7], 1.0f);
            block.model->updateUBO(imageIndex, ubo);
            block.model->render(cmd, imageIndex, false);
        }

        void drawSpriteRect(mxvk::VK_Sprite *sprite, VkCommandBuffer cmd, const VkExtent2D &extent, int x, int y, int w, int h) {
            if (sprite == nullptr) {
                return;
            }
            sprite->drawSpriteRect(x, y, w, h);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            sprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            sprite->clearQueue();
        }

        void drawNextPiecePreview(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent) {
            if (screen != AppScreen::Game || introActive || previewBorderSprite == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                return;
            }
            (void)imageIndex;

            const int panelSize = std::min({220, static_cast<int>(static_cast<float>(extent.width) * 0.28f), static_cast<int>(static_cast<float>(extent.height) * 0.34f)});
            const int panelW = panelSize;
            const int panelH = panelSize;
            const int margin = 24;
            const int panelX = static_cast<int>(extent.width) - panelW - margin;
            const int panelY = 88;
            const int border = 4;

            drawSpriteRect(previewBorderSprite, cmd, extent, panelX, panelY, panelW, border);
            drawSpriteRect(previewBorderSprite, cmd, extent, panelX, panelY + panelH - border, panelW, border);
            drawSpriteRect(previewBorderSprite, cmd, extent, panelX, panelY, border, panelH);
            drawSpriteRect(previewBorderSprite, cmd, extent, panelX + panelW - border, panelY, border, panelH);

            printText("Next", panelX + 12, panelY - 28, SDL_Color{255, 255, 255, 255});

            int minX = nextPiece.blocks[0].x;
            int maxX = nextPiece.blocks[0].x;
            int minY = nextPiece.blocks[0].y;
            int maxY = nextPiece.blocks[0].y;
            for (const Block &block : nextPiece.blocks) {
                minX = std::min(minX, block.x);
                maxX = std::max(maxX, block.x);
                minY = std::min(minY, block.y);
                maxY = std::max(maxY, block.y);
            }

            const float innerPadding = 28.0f;
            const float innerSize = static_cast<float>(panelSize) - innerPadding * 2.0f;
            const int blockSize = static_cast<int>(std::min(34.0f, innerSize / static_cast<float>(std::max(maxX - minX + 1, maxY - minY + 1))));
            const float pieceW = static_cast<float>(maxX - minX + 1) * blockSize;
            const float pieceH = static_cast<float>(maxY - minY + 1) * blockSize;
            const float centerX = static_cast<float>(panelX) + static_cast<float>(panelW) * 0.5f;
            const float centerY = static_cast<float>(panelY) + static_cast<float>(panelH) * 0.5f;
            const float originX = centerX - pieceW * 0.5f;
            const float originY = centerY - pieceH * 0.5f;
            mxvk::VK_Sprite *blockSprite = blockPreviewSprites[static_cast<std::size_t>(nextPiece.color)];

            for (std::size_t i = 0; i < nextPiece.blocks.size(); ++i) {
                const Block &pieceBlock = nextPiece.blocks[i];
                const float x = originX + static_cast<float>(pieceBlock.x - minX) * blockSize;
                const float y = originY + static_cast<float>(pieceBlock.y - minY) * blockSize;
                drawSpriteRect(blockSprite, cmd, extent, static_cast<int>(x), static_cast<int>(y), blockSize, blockSize);
            }
        }

        void drawCreditsModel(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent) {
            if (creditsTuxModel == nullptr) {
                return;
            }

            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.25f, 2.8f), glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            view = glm::rotate(view, glm::radians(-10.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(40.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            const float elapsed = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            glm::mat4 model(1.0f);
            model = glm::translate(model, glm::vec3(0.0f, -0.34f, 0.0f));
            model = glm::rotate(model, elapsed * 0.75f, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, std::sin(elapsed * 0.6f) * 0.08f, glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::scale(model, glm::vec3(0.228f));

            mxvk::UniformBufferObject ubo{};
            ubo.model = model;
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(1.0f, 1.0f, 1.0f, elapsed);
            creditsTuxModel->updateUBO(imageIndex, ubo);
            creditsTuxModel->render(cmd, imageIndex, false);
        }

        void drawHud() {
            if (introActive) {
                return;
            }
            const VkExtent2D extent = getSwapchainExtent();
            const int centerX = static_cast<int>(extent.width) / 2;
            const float alpha = screenFadeAlpha();
            const auto withAlpha = [alpha](SDL_Color color) {
                color.a = static_cast<Uint8>(static_cast<float>(color.a) * alpha);
                return color;
            };

            switch (screen) {
            case AppScreen::Menu: {
                const int titleY = static_cast<int>(static_cast<float>(extent.height) * 0.18f);
                const int menuY = static_cast<int>(static_cast<float>(extent.height) * 0.40f);
                const int spacing = static_cast<int>(std::max(34.0f, static_cast<float>(extent.height) * 0.07f));
                printCenteredText("MXVK 3D Tetris", centerX, titleY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText("Choose a mode", centerX, titleY + 42, withAlpha(SDL_Color{255, 255, 255, 255}));

                const char *items[] = {"New Game", "Network Multiplayer", "High Scores", "Credits"};
                for (int i = 0; i < 4; ++i) {
                    const SDL_Color color = (i == cursorPos) ? SDL_Color{255, 255, 0, 255} : SDL_Color{255, 255, 255, 255};
                    printCenteredText(items[i], centerX, menuY + i * spacing, withAlpha(color));
                }
                printCenteredText("Use arrows and Enter", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.86f), withAlpha(SDL_Color{180, 180, 180, 255}));
                break;
            }
            case AppScreen::HighScores: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.16f);
                printCenteredText("High Scores", centerX, baseY, withAlpha(SDL_Color{255, 255, 0, 255}));
                const auto &scores = highScores.entries();
                const int listLeftX = static_cast<int>(static_cast<float>(extent.width) * 0.34f);
                const int listRightX = static_cast<int>(static_cast<float>(extent.width) * 0.70f);
                const int lineHeight = 30;
                for (std::size_t i = 0; i < 10U; ++i) {
                    const int rowY = baseY + 54 + static_cast<int>(i) * lineHeight;
                    const bool hasScore = i < scores.size();
                    const std::string nameText = hasScore ? scores[i].name : "---";
                    const std::string scoreText = hasScore ? std::format("{}", scores[i].score) : "---";
                    printText(std::format("{:>2}. {}", i + 1U, nameText),
                              listLeftX,
                              rowY,
                              withAlpha(SDL_Color{255, 255, 255, 255}));
                    int scoreWidth = 0;
                    int scoreHeight = 0;
                    if (getTextDimensions(scoreText, scoreWidth, scoreHeight)) {
                        printText(scoreText, listRightX - scoreWidth, rowY, withAlpha(SDL_Color{255, 255, 255, 255}));
                    } else {
                        printText(scoreText, listRightX, rowY, withAlpha(SDL_Color{255, 255, 255, 255}));
                    }
                }
                printCenteredText("Press Enter or Escape to return", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.82f), withAlpha(SDL_Color{200, 200, 200, 255}));
                break;
            }
            case AppScreen::Credits: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.2f);
                printCenteredText("Credits", centerX, baseY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText("MXVK 3D Tetris", centerX, baseY + 48, withAlpha(SDL_Color{255, 255, 255, 255}));
                printCenteredText("Built with Vulkan and SDL3", centerX, baseY + 84, withAlpha(SDL_Color{255, 220, 120, 255}));
                printCenteredText("Press Enter or Escape to return", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.82f), withAlpha(SDL_Color{200, 200, 200, 255}));
                break;
            }
            case AppScreen::NetworkMultiplayer: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.2f);
                printCenteredText("Network Multiplayer", centerX, baseY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText("Matchmaking and session flow go here", centerX, baseY + 48, withAlpha(SDL_Color{255, 255, 255, 255}));
                printCenteredText("Host, join, or connect to a server", centerX, baseY + 84, withAlpha(SDL_Color{255, 220, 120, 255}));
                printCenteredText("Press Enter or Escape to return", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.82f), withAlpha(SDL_Color{200, 200, 200, 255}));
                break;
            }
            case AppScreen::GameOver: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.20f);
                const float gameOverAlpha = gameOverFadeAlpha();
                const auto withGameOverAlpha = [gameOverAlpha](SDL_Color color) {
                    color.a = static_cast<Uint8>(static_cast<float>(color.a) * gameOverAlpha);
                    return color;
                };

                printCenteredText("Game Over", centerX, baseY, withGameOverAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText(std::format("Final Score: {}", score), centerX, baseY + 42, withGameOverAlpha(SDL_Color{255, 255, 255, 255}));
                if (enteringName) {
                    printCenteredText("New high score", centerX, baseY + 82, withGameOverAlpha(SDL_Color{255, 220, 120, 255}));
                    printCenteredText(std::format("Name: {}", playerName.empty() ? "_" : playerName),
                                      centerX,
                                      baseY + 118,
                                      withGameOverAlpha(SDL_Color{255, 255, 255, 255}));
                    printCenteredText(std::format("Pick: [{}]", currentNameCharacter()), centerX, baseY + 154, withGameOverAlpha(SDL_Color{120, 255, 255, 255}));
                    printCenteredText("Up/Down choose, A/Space add", centerX, baseY + 190, withGameOverAlpha(SDL_Color{200, 200, 200, 255}));
                    printCenteredText("X/Enter save, B/Backspace delete", centerX, baseY + 224, withGameOverAlpha(SDL_Color{200, 200, 200, 255}));
                } else {
                    printCenteredText("R to restart from intro", centerX, baseY + 92, withGameOverAlpha(SDL_Color{255, 220, 120, 255}));
                    printCenteredText("H for high scores", centerX, baseY + 126, withGameOverAlpha(SDL_Color{200, 200, 200, 255}));
                    printCenteredText("Enter to restart", centerX, baseY + 160, withGameOverAlpha(SDL_Color{200, 200, 200, 255}));
                }
                break;
            }
            case AppScreen::Game:
                printText(std::format("Score: {}", score), 15, 15, withAlpha(SDL_Color{255, 255, 255, 255}));
                printText(std::format("Lines Cleared: {}", linesCleared), 15, 45, withAlpha(SDL_Color{255, 220, 120, 255}));
                printText(std::format("Level: {}", level), 15, 75, withAlpha(SDL_Color{120, 220, 255, 255}));
                if (gameOver) {
                    const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.58f);
                    const float gameOverAlpha = gameOverFadeAlpha();
                    const auto withGameOverAlpha = [gameOverAlpha](SDL_Color color) {
                        color.a = static_cast<Uint8>(static_cast<float>(color.a) * gameOverAlpha);
                        return color;
                    };
                    printCenteredText(std::format("Final Score: {}", score), centerX, baseY, withGameOverAlpha(SDL_Color{255, 255, 255, 255}));
                    printCenteredText("Game over", centerX, baseY + 40, withGameOverAlpha(SDL_Color{255, 220, 120, 255}));
                }
                break;
            case AppScreen::Intro:
                break;
            }
        }

        void printCenteredText(const std::string &text, int centerX, int y, const SDL_Color &color) {
            int textWidth = 0;
            int textHeight = 0;
            if (getTextDimensions(text, textWidth, textHeight)) {
                printText(text, centerX - textWidth / 2, y, color);
                return;
            }
            printText(text, centerX, y, color);
        }

        void triggerBackgroundTransition() {
            backgroundTransitionActive = true;
            backgroundTransitionStart = std::chrono::steady_clock::now();
        }

        void updateBackgroundTransitionState() {
            if (!backgroundTransitionActive) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - backgroundTransitionStart).count();
            if (elapsed >= backgroundTransitionSeconds) {
                backgroundTransitionActive = false;
            }
        }

        [[nodiscard]] mxvk::VK_Sprite *screenSpriteFor(AppScreen screen) const {
            switch (screen) {
            case AppScreen::Menu:
                return menuBackgroundSprite;
            case AppScreen::HighScores:
                return highScoresBackgroundSprite;
            case AppScreen::Credits:
                return creditsBackgroundSprite;
            case AppScreen::NetworkMultiplayer:
                return multiplayerBackgroundSprite;
            case AppScreen::Intro:
            case AppScreen::Game:
            case AppScreen::GameOver:
                return nullptr;
            }
            return nullptr;
        }

        void drawFadedSprite(mxvk::VK_Sprite *sprite, VkCommandBuffer cmd, const VkExtent2D &extent, float alpha) {
            if (sprite == nullptr || sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                return;
            }
            sprite->setShaderParams(0.0f, 0.0f, 0.0f, alpha);
            sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
            sprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            sprite->clearQueue();
        }

        void drawGameScreenTransitionOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (screen != AppScreen::Game || !screenTransitionActive) {
                return;
            }
            drawFadedSprite(screenSpriteFor(transitionFromScreen), cmd, extent, 1.0f - screenFadeAlpha());
        }

        void drawScreenBackdrop(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (screen == AppScreen::Game) {
                if (sprite_pipeline == VK_NULL_HANDLE || sprite_pipeline_layout == VK_NULL_HANDLE) {
                    return;
                }
                updateBackgroundTransitionState();

                mxvk::VK_Sprite *sprite = background;
                if (backgroundTransitionActive && backgroundTransitionSprite != nullptr) {
                    sprite = backgroundTransitionSprite;
                }
                if (sprite == nullptr) {
                    return;
                }

                if (sprite == backgroundTransitionSprite) {
                    const auto now = std::chrono::steady_clock::now();
                    const float elapsed = std::chrono::duration<float>(now - backgroundTransitionStart).count();
                    sprite->setShaderParams(elapsed, 0.0f, 0.0f, 0.0f);
                }

                sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline);
                sprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
                sprite->clearQueue();
                return;
            }

            const float alpha = screenFadeAlpha();
            if (screenTransitionActive) {
                drawFadedSprite(screenSpriteFor(transitionFromScreen), cmd, extent, 1.0f - alpha);
            }
            drawFadedSprite(screenSpriteFor(screen), cmd, extent, alpha);
        }
    };

} // namespace

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        TetrisWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
