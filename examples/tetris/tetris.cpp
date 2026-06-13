#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
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
        Multiplayer,
        HighScores,
        Credits,
    };

    class TetrisWindow final : public mxvk::VK_Window {
      public:
        TetrisWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ MXVK 3D Tetris ]-", width, height, fullscreen, MXVK_VALIDATION),
              assetRoot_((path.empty() || path == ".") ? std::string(tetris_ASSET_DIR) : path),
              dataRoot_(assetRoot_ + "/data") {
            std::random_device rd;
            rng_.seed(rd());
            setFont(tetris_FONT_PATH, 22);
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            tryOpenFirstGamepad();
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            music_ = std::make_unique<mxvk::VK_Mixer>();
            musicTrack_ = music_->loadMusic(dataRoot_ + "/music.ogg");
            ensureMusicPlaying();
#endif
            background_ = createSprite(dataRoot_ + "/psychedelic_background.png");
            backgroundTransitionSprite_ = createSprite(
                dataRoot_ + "/psychedelic_background.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_background_transition.frag.spv");
            menuBackgroundSprite_ = createSprite(
                dataRoot_ + "/start_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            highScoresBackgroundSprite_ = createSprite(
                dataRoot_ + "/high_scores_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            creditsBackgroundSprite_ = createSprite(
                dataRoot_ + "/credits_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            multiplayerBackgroundSprite_ = createSprite(
                dataRoot_ + "/multiplayer_screen.png",
                std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv",
                std::string(tetris_SHADER_DIR) + "/tetris_screen_fade.frag.spv");
            previewBorderSprite_ = createSprite(1, 1);
            const uint32_t whitePixel = 0xFFFFFFFFu;
            previewBorderSprite_->updateTexture(&whitePixel, 1, 1);
            for (std::size_t i = 0; i < blockPreviewSprites_.size(); ++i) {
                blockPreviewSprites_[i] = createSprite(dataRoot_ + "/" + blockTextureFiles[i]);
            }
            gameOverSprite_ = createSprite(dataRoot_ + "/gameover.png");
            introSprite_ = createSprite(dataRoot_ + "/intro.png",
                                        std::string(tetris_SHADER_DIR) + "/tetris_intro.vert.spv",
                                        std::string(tetris_SHADER_DIR) + "/tetris_intro.frag.spv");
            introStart_ = std::chrono::steady_clock::now();
            initModels();
            resetGame();
        }

        ~TetrisWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
            if (music_) {
                music_->stopMusic();
            }
#endif
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

            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!openGamepad(e.gdevice.which)) {
                    tryOpenFirstGamepad();
                }
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad_ != nullptr && e.gdevice.which == gamepadId_) {
                    closeGamepad();
                    tryOpenFirstGamepad();
                }
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                handleGamepadButtonDown(e.gbutton.button);
            }
        }

        void proc() override {
            ensureMusicPlaying();
            tryOpenFirstGamepad();
            updateIntroState();
            updateScreenTransition();
            updateInput();
            if (screen_ == AppScreen::Game) {
                updateGame();
            }
            drawHud();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            drawScreenBackdrop(cmd, extent);

            if (screen_ == AppScreen::Game) {
                const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
                glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.2f, cameraDistance_), glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridPitch_), glm::vec3(1.0f, 0.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridYaw_), glm::vec3(0.0f, 1.0f, 0.0f));
                view = glm::rotate(view, glm::radians(gridRoll_), glm::vec3(0.0f, 0.0f, 1.0f));

                glm::mat4 proj = glm::perspective(glm::radians(46.0f), aspect, 0.1f, 100.0f);
                proj[1][1] *= -1.0f;
                const bool blinkVisible = !lineClearActive_ || isLineClearVisible();

                for (LockedBlock &locked : lockedBlocks_) {
                    if (blinkVisible || !isClearingRow(locked.y)) {
                        drawBlock(cmd, imageIndex, locked.block, locked.x, locked.y, locked.block.color, view, proj);
                    }
                }

                if (!lineClearActive_) {
                    for (size_t i = 0; i < activeModels_.size(); ++i) {
                        const int x = active_.x + active_.blocks[i].x;
                        const int y = active_.y + active_.blocks[i].y;
                        drawBlock(cmd, imageIndex, activeModels_[i], x, y, active_.color, view, proj);
                    }
                }

                drawFrame(cmd, imageIndex, view, proj);
                drawNextPiecePreview(cmd, imageIndex, extent);
            }
            drawGameScreenTransitionOverlay(cmd, extent);
            drawIntroOverlay(cmd, extent);
            drawGameOverOverlay(cmd, extent);
        }

      private:
        std::string assetRoot_;
        std::string dataRoot_;
        std::array<std::array<Cell, boardWidth>, boardHeight> board_{};
        ActivePiece active_{};
        std::mt19937 rng_{};
        std::vector<LockedBlock> lockedBlocks_{};
        std::array<BlockModel, 4> activeModels_{};
        std::vector<BlockModel> frameModels_{};
        mxvk::VK_Sprite *background_ = nullptr;
        mxvk::VK_Sprite *backgroundTransitionSprite_ = nullptr;
        mxvk::VK_Sprite *menuBackgroundSprite_ = nullptr;
        mxvk::VK_Sprite *highScoresBackgroundSprite_ = nullptr;
        mxvk::VK_Sprite *creditsBackgroundSprite_ = nullptr;
        mxvk::VK_Sprite *multiplayerBackgroundSprite_ = nullptr;
        mxvk::VK_Sprite *previewBorderSprite_ = nullptr;
        std::array<mxvk::VK_Sprite *, 8> blockPreviewSprites_{};
        mxvk::VK_Sprite *introSprite_ = nullptr;
        mxvk::VK_Sprite *gameOverSprite_ = nullptr;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> music_{};
        int musicTrack_ = -1;
#endif
        SDL_Gamepad *gamepad_ = nullptr;
        SDL_JoystickID gamepadId_ = 0;
        std::chrono::steady_clock::time_point introStart_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastFall_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastInputUpdate_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lineClearStart_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point backgroundTransitionStart_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point screenTransitionStart_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point gameOverTransitionStart_{std::chrono::steady_clock::now()};
        bool introActive_ = true;
        float gridYaw_ = 0.0f;
        float gridPitch_ = 0.0f;
        float gridRoll_ = 0.0f;
        float cameraDistance_ = 2.45f;
        int score_ = 0;
        int bestScore_ = 0;
        int linesCleared_ = 0;
        int level_ = 1;
        int cursorPos = 0;
        float fallSeconds_ = 0.65f;
        float moveRepeatTimer_ = 0.0f;
        float softDropRepeatTimer_ = 0.0f;
        float gamepadMoveRepeatTimer_ = 0.0f;
        float gamepadSoftDropRepeatTimer_ = 0.0f;
        bool gameOver_ = false;
        bool hardDropHeld_ = false;
        bool rotateHeld_ = false;
        bool resetHeld_ = false;
        bool enterHeld_ = false;
        bool escapeHeld_ = false;
        bool menuUpHeld_ = false;
        bool menuDownHeld_ = false;
        bool menuEnterHeld_ = false;
        bool introSkipHeld_ = false;
        bool introFadeStarted_ = false;
        bool screenTransitionActive_ = false;
        bool gameOverTransitionActive_ = false;
        bool lineClearActive_ = false;
        bool backgroundTransitionActive_ = false;
        std::array<bool, boardHeight> clearingRows_{};
        PieceQueueEntry nextPiece_{};
        AppScreen screen_ = AppScreen::Intro;
        AppScreen transitionFromScreen_ = AppScreen::Intro;
        static constexpr float introHoldSeconds_ = 5.0f;
        static constexpr float introFadeSeconds_ = 1.0f;
        static constexpr float screenTransitionSeconds_ = 0.36f;
        static constexpr float gameOverTransitionSeconds_ = 0.5f;
        static constexpr float backgroundTransitionSeconds_ = 1.25f;
        static constexpr Sint16 gamepadDeadzone_ = 10000;
        static constexpr float gamepadMoveInitialDelaySeconds_ = 0.22f;
        static constexpr float gamepadMoveRepeatSeconds_ = 0.12f;
        static constexpr float gamepadSoftDropInitialDelaySeconds_ = 0.18f;
        static constexpr float gamepadSoftDropRepeatSeconds_ = 0.08f;
        static constexpr float gamepadStickRotateSpeed_ = 120.0f;
        static constexpr float gamepadStickPitchSpeed_ = 100.0f;
        static constexpr float gamepadStickScale_ = 1.0f / 32768.0f;
        int gamepadMoveDirection_ = 0;
        float gamepadMoveHeldSeconds_ = 0.0f;
        bool gamepadSoftDropHeld_ = false;

        void initModels() {
            frameModels_.reserve(boardWidth + (boardHeight * 2) + 2);
            for (int i = 0; i < boardWidth + (boardHeight * 2) + 2; ++i) {
                frameModels_.push_back(loadBlockModel(7));
            }
        }

        [[nodiscard]] BlockModel loadBlockModel(int color) {
            BlockModel block{};
            block.color = color;
            block.model = std::make_unique<mxvk::VKAbstractModel>();
            block.model->load(this, dataRoot_ + "/cube.mxmod.z", dataRoot_ + "/" + textureManifests[color], dataRoot_, 1.0f);
            block.model->setShaders(this,
                                    std::string(tetris_SHADER_DIR) + "/tetris_model.vert.spv",
                                    std::string(tetris_SHADER_DIR) + "/tetris_model.frag.spv");
            return block;
        }

        void cleanupModels() {
            forEachModel([this](mxvk::VKAbstractModel &model) {
                model.cleanup(this);
            });
            lockedBlocks_.clear();
        }

        template <typename Fn>
        void forEachModel(Fn fn) {
            for (LockedBlock &locked : lockedBlocks_) {
                if (locked.block.model) {
                    fn(*locked.block.model);
                }
            }
            for (BlockModel &block : activeModels_) {
                if (block.model) {
                    fn(*block.model);
                }
            }
            for (BlockModel &block : frameModels_) {
                if (block.model) {
                    fn(*block.model);
                }
            }
        }

        void resetGame() {
            clearLockedBlocks();
            for (auto &row : board_) {
                for (Cell &cell : row) {
                    cell.color = -1;
                }
            }
            gameOver_ = false;
            gameOverTransitionActive_ = false;
            lineClearActive_ = false;
            backgroundTransitionActive_ = false;
            clearingRows_.fill(false);
            score_ = 0;
            linesCleared_ = 0;
            updateDifficulty();
            lastFall_ = std::chrono::steady_clock::now();
            nextPiece_ = randomPiece();
            spawnPiece();
        }

        void clearLockedBlocks() {
            for (LockedBlock &locked : lockedBlocks_) {
                if (locked.block.model) {
                    locked.block.model->cleanup(this);
                }
            }
            lockedBlocks_.clear();
        }

        void spawnPiece() {
            active_.blocks = nextPiece_.blocks;
            active_.x = boardWidth / 2;
            active_.y = boardHeight - 2;
            active_.color = nextPiece_.color;
            reloadActiveModels(active_.color);
            gameOver_ = collides(active_.x, active_.y, active_.blocks);
            if (gameOver_) {
                bestScore_ = std::max(bestScore_, score_);
                gameOverTransitionActive_ = true;
                gameOverTransitionStart_ = std::chrono::steady_clock::now();
            }
            nextPiece_ = randomPiece();
        }

        void reloadActiveModels(int color) {
            for (BlockModel &block : activeModels_) {
                if (block.model) {
                    block.model->cleanup(this);
                }
                block = loadBlockModel(color);
            }
        }

        [[nodiscard]] PieceQueueEntry randomPiece() {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(pieceDefinitions.size()) - 1);
            const PieceDefinition &definition = pieceDefinitions[dist(rng_)];
            return PieceQueueEntry{definition.blocks, definition.color};
        }

        void updateDifficulty() {
            const int previousLevel = level_;
            level_ = (linesCleared_ / 8) + 1;
            static constexpr std::array<float, 16> arcadeFallSeconds{
                0.72f, 0.66f, 0.60f, 0.54f,
                0.48f, 0.43f, 0.38f, 0.34f,
                0.30f, 0.26f, 0.23f, 0.20f,
                0.18f, 0.16f, 0.14f, 0.12f,
            };

            const std::size_t index = std::min<std::size_t>(arcadeFallSeconds.size() - 1, static_cast<std::size_t>(std::max(level_ - 1, 0)));
            fallSeconds_ = arcadeFallSeconds[index];
            if (level_ > previousLevel) {
                triggerBackgroundTransition();
            }
        }

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        void ensureMusicPlaying() {
            if (!music_) {
                return;
            }
            if (musicTrack_ < 0) {
                return;
            }
            if (!music_->isMusicPlaying(musicTrack_)) {
                if (music_->playMusic(musicTrack_, -1) != 0) {
                    throw mxvk::Exception("Could not start Tetris background music");
                }
            }
        }
#else
        void ensureMusicPlaying() {}
#endif

        void updateInput() {
            const auto now = std::chrono::steady_clock::now();
            float deltaSeconds = std::chrono::duration<float>(now - lastInputUpdate_).count();
            lastInputUpdate_ = now;
            deltaSeconds = std::clamp(deltaSeconds, 0.0f, 0.05f);

            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            if (introActive_) {
                const bool enterDown = keys[SDL_SCANCODE_RETURN];
                const bool spaceDown = keys[SDL_SCANCODE_SPACE];
                const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
                const bool skipDown = enterDown || spaceDown;
                if (skipDown && !introSkipHeld_) {
                    const auto now = std::chrono::steady_clock::now();
                    introStart_ = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(introHoldSeconds_));
                    introFadeStarted_ = false;
                    requestScreen(AppScreen::Menu);
                }
                if (escapeDown && !escapeHeld_) {
                    exit();
                }
                introSkipHeld_ = skipDown;
                escapeHeld_ = escapeDown;
                return;
            }

            if (screenTransitionActive_) {
                return;
            }

            if (screen_ != AppScreen::Game) {
                handleMenuKeys(keys);
                return;
            }

            handleHeldViewRotation(keys, deltaSeconds);
            handleHeldPieceMovement(keys, deltaSeconds);
            handleOneShotKeys(keys);
            handleGamepadInput(deltaSeconds);
        }

        void handleMenuKeys(const bool *keys) {
            if (screenTransitionActive_) {
                menuUpHeld_ = keys[SDL_SCANCODE_UP];
                menuDownHeld_ = keys[SDL_SCANCODE_DOWN];
                menuEnterHeld_ = keys[SDL_SCANCODE_RETURN];
                escapeHeld_ = keys[SDL_SCANCODE_ESCAPE];
                return;
            }

            const bool upDown = keys[SDL_SCANCODE_UP];
            const bool downDown = keys[SDL_SCANCODE_DOWN];
            const bool enterDown = keys[SDL_SCANCODE_RETURN];
            const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];

            if (screen_ == AppScreen::Menu) {
                if (upDown && !menuUpHeld_) {
                    cursorPos = (cursorPos + 3) % 4;
                }
                if (downDown && !menuDownHeld_) {
                    cursorPos = (cursorPos + 1) % 4;
                }
            }
            if (enterDown && !menuEnterHeld_) {
                if (screen_ == AppScreen::Menu) {
                    if (cursorPos == 0) {
                        startGame();
                    } else if (cursorPos == 1) {
                        goToMultiplayer();
                    } else if (cursorPos == 2) {
                        goToHighScores();
                    } else if (cursorPos == 3) {
                        goToCredits();
                    }
                } else {
                    goToMenu();
                }
            }
            if (escapeDown && !escapeHeld_) {
                if (screen_ == AppScreen::Menu) {
                    exit();
                } else {
                    goToMenu();
                }
            }

            menuUpHeld_ = upDown;
            menuDownHeld_ = downDown;
            menuEnterHeld_ = enterDown;
            escapeHeld_ = escapeDown;
        }

        void handleGamepadInput(float deltaSeconds) {
            if (gamepad_ == nullptr || introActive_ || screen_ != AppScreen::Game || gameOver_ || lineClearActive_) {
                gamepadMoveDirection_ = 0;
                gamepadMoveHeldSeconds_ = 0.0f;
                gamepadSoftDropHeld_ = false;
                gamepadMoveRepeatTimer_ = 0.0f;
                gamepadSoftDropRepeatTimer_ = 0.0f;
                return;
            }

            const Sint16 leftX = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
            const Sint16 leftY = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
            const Sint16 rightX = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX);
            const Sint16 rightY = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY);

            const int moveDirection = (leftX < -gamepadDeadzone_) ? -1 : (leftX > gamepadDeadzone_) ? 1
                                                                                                    : 0;
            if (moveDirection == 0) {
                gamepadMoveDirection_ = 0;
                gamepadMoveHeldSeconds_ = 0.0f;
                gamepadMoveRepeatTimer_ = 0.0f;
            } else {
                if (moveDirection != gamepadMoveDirection_) {
                    gamepadMoveDirection_ = moveDirection;
                    gamepadMoveHeldSeconds_ = 0.0f;
                    gamepadMoveRepeatTimer_ = 0.0f;
                    movePiece(gamepadMoveDirection_, 0);
                } else {
                    gamepadMoveHeldSeconds_ += deltaSeconds;
                    const float threshold = (gamepadMoveHeldSeconds_ < gamepadMoveInitialDelaySeconds_)
                                                ? gamepadMoveInitialDelaySeconds_
                                                : gamepadMoveRepeatSeconds_;
                    gamepadMoveRepeatTimer_ += deltaSeconds;
                    if (gamepadMoveRepeatTimer_ >= threshold) {
                        movePiece(gamepadMoveDirection_, 0);
                        gamepadMoveRepeatTimer_ = 0.0f;
                    }
                }
            }

            const bool softDropDown = leftY > gamepadDeadzone_;
            if (!softDropDown) {
                gamepadSoftDropHeld_ = false;
                gamepadSoftDropRepeatTimer_ = 0.0f;
            } else {
                const float threshold = gamepadSoftDropHeld_ ? gamepadSoftDropRepeatSeconds_ : gamepadSoftDropInitialDelaySeconds_;
                gamepadSoftDropRepeatTimer_ += deltaSeconds;
                if (gamepadSoftDropRepeatTimer_ >= threshold) {
                    softDrop();
                    gamepadSoftDropRepeatTimer_ = 0.0f;
                    lastFall_ = std::chrono::steady_clock::now();
                    gamepadSoftDropHeld_ = true;
                }
            }

            if (std::abs(rightX) > gamepadDeadzone_) {
                gridYaw_ += static_cast<float>(rightX) * gamepadStickScale_ * gamepadStickRotateSpeed_ * deltaSeconds;
            }
            if (std::abs(rightY) > gamepadDeadzone_) {
                gridPitch_ = std::clamp(gridPitch_ - static_cast<float>(rightY) * gamepadStickScale_ * gamepadStickPitchSpeed_ * deltaSeconds,
                                        -70.0f,
                                        70.0f);
            }

            constexpr float gamepadZoomSpeed = 3.2f;
            if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                cameraDistance_ = std::min(9.0f, cameraDistance_ + gamepadZoomSpeed * deltaSeconds);
            }
            if (SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                cameraDistance_ = std::max(1.65f, cameraDistance_ - gamepadZoomSpeed * deltaSeconds);
            }
        }

        void handleGamepadButtonDown(Uint8 button) {
            if (gamepad_ == nullptr) {
                return;
            }
            if (screenTransitionActive_) {
                return;
            }

            if (introActive_) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    introActive_ = false;
                    requestScreen(AppScreen::Menu);
                }
                return;
            }

            switch (screen_) {
            case AppScreen::Menu:
                if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
                    cursorPos = (cursorPos + 3) % 4;
                } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                    cursorPos = (cursorPos + 1) % 4;
                } else if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    if (cursorPos == 0) {
                        startGame();
                    } else if (cursorPos == 1) {
                        goToMultiplayer();
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
                if (gameOver_) {
                    if (button == SDL_GAMEPAD_BUTTON_START || button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        restartIntroSequence();
                    } else if (button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                        goToMenu();
                    }
                    return;
                }
                if (lineClearActive_) {
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
            case AppScreen::Multiplayer:
            case AppScreen::HighScores:
            case AppScreen::Credits:
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START || button == SDL_GAMEPAD_BUTTON_BACK || button == SDL_GAMEPAD_BUTTON_EAST) {
                    goToMenu();
                }
                break;
            case AppScreen::Intro:
                break;
            }
        }

        bool openGamepad(SDL_JoystickID id) {
            if (gamepad_ != nullptr && gamepadId_ == id) {
                return true;
            }
            closeGamepad();
            gamepad_ = SDL_OpenGamepad(id);
            if (gamepad_ == nullptr) {
                return false;
            }
            gamepadId_ = id;
            return true;
        }

        void closeGamepad() {
            if (gamepad_ != nullptr) {
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
                gamepadId_ = 0;
            }
        }

        void tryOpenFirstGamepad() {
            if (gamepad_ != nullptr) {
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
            if (!introActive_) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - introStart_).count();
            if (!introFadeStarted_ && elapsed >= introHoldSeconds_) {
                introFadeStarted_ = true;
                requestScreen(AppScreen::Menu);
            }
            if (elapsed >= (introHoldSeconds_ + introFadeSeconds_)) {
                introActive_ = false;
                introFadeStarted_ = false;
                lastFall_ = now;
            }
        }

        void startGame() {
            const AppScreen previousScreen = screen_;
            resetGame();
            introActive_ = false;
            screen_ = AppScreen::Game;
            screenTransitionActive_ = previousScreen != AppScreen::Game;
            transitionFromScreen_ = previousScreen;
            screenTransitionStart_ = std::chrono::steady_clock::now();
            lastInputUpdate_ = std::chrono::steady_clock::now();
            resetMenuLatchState();
        }

        void restartIntroSequence() {
            resetGame();
            introActive_ = true;
            introFadeStarted_ = false;
            introStart_ = std::chrono::steady_clock::now();
            screen_ = AppScreen::Intro;
            screenTransitionActive_ = false;
            transitionFromScreen_ = AppScreen::Intro;
            lastInputUpdate_ = std::chrono::steady_clock::now();
            resetMenuLatchState();
            introSkipHeld_ = true;
        }

        void goToMenu() {
            if (screen_ == AppScreen::Game) {
                gameOver_ = false;
            }
            cursorPos = 0;
            requestScreen(AppScreen::Menu);
        }

        void goToHighScores() {
            requestScreen(AppScreen::HighScores);
        }

        void goToCredits() {
            requestScreen(AppScreen::Credits);
        }

        void goToMultiplayer() {
            requestScreen(AppScreen::Multiplayer);
        }

        void requestScreen(AppScreen nextScreen) {
            if (screen_ == nextScreen && !screenTransitionActive_) {
                return;
            }
            if (nextScreen == AppScreen::Game) {
                screenTransitionActive_ = false;
                screen_ = nextScreen;
                return;
            }
            if (screen_ == AppScreen::Game) {
                screenTransitionActive_ = false;
                screen_ = nextScreen;
                return;
            }
            transitionFromScreen_ = screen_;
            screen_ = nextScreen;
            screenTransitionActive_ = true;
            screenTransitionStart_ = std::chrono::steady_clock::now();
            resetMenuLatchState();
        }

        void updateScreenTransition() {
            if (!screenTransitionActive_) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - screenTransitionStart_).count();
            if (elapsed >= screenTransitionSeconds_) {
                screenTransitionActive_ = false;
                transitionFromScreen_ = screen_;
                resetMenuLatchState();
            }
        }

        float screenFadeAlpha() const {
            if (!screenTransitionActive_) {
                return 1.0f;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - screenTransitionStart_).count();
            return std::clamp(elapsed / screenTransitionSeconds_, 0.0f, 1.0f);
        }

        float gameOverFadeAlpha() const {
            if (!gameOverTransitionActive_) {
                return 1.0f;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - gameOverTransitionStart_).count();
            return std::clamp(elapsed / gameOverTransitionSeconds_, 0.0f, 1.0f);
        }

        void resetMenuLatchState() {
            menuUpHeld_ = false;
            menuDownHeld_ = false;
            menuEnterHeld_ = false;
            introSkipHeld_ = false;
        }

        void updateMenuInputLatchState() {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }
            menuUpHeld_ = keys[SDL_SCANCODE_UP];
            menuDownHeld_ = keys[SDL_SCANCODE_DOWN];
            menuEnterHeld_ = keys[SDL_SCANCODE_RETURN];
            escapeHeld_ = keys[SDL_SCANCODE_ESCAPE];
        }

        void handleHeldViewRotation(const bool *keys, float deltaSeconds) {
            constexpr float yawSpeed = 115.0f;
            constexpr float pitchSpeed = 90.0f;
            constexpr float rollSpeed = 100.0f;
            constexpr float zoomSpeed = 3.2f;

            if (keys[SDL_SCANCODE_A]) {
                gridYaw_ -= yawSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_D]) {
                gridYaw_ += yawSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_W]) {
                gridPitch_ = std::clamp(gridPitch_ + pitchSpeed * deltaSeconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_S]) {
                gridPitch_ = std::clamp(gridPitch_ - pitchSpeed * deltaSeconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_Q]) {
                gridRoll_ -= rollSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_E]) {
                gridRoll_ += rollSpeed * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_PAGEUP]) {
                cameraDistance_ = std::max(1.65f, cameraDistance_ - zoomSpeed * deltaSeconds);
            }
            if (keys[SDL_SCANCODE_PAGEDOWN]) {
                cameraDistance_ = std::min(9.0f, cameraDistance_ + zoomSpeed * deltaSeconds);
            }
        }

        void handleHeldPieceMovement(const bool *keys, float deltaSeconds) {
            if (gameOver_ || lineClearActive_) {
                moveRepeatTimer_ = 0.0f;
                softDropRepeatTimer_ = 0.0f;
                return;
            }

            constexpr float horizontalRepeatSeconds = 0.14f;
            constexpr float softDropRepeatSeconds = 0.075f;

            moveRepeatTimer_ += deltaSeconds;
            softDropRepeatTimer_ += deltaSeconds;

            int horizontalDirection = 0;
            if (keys[SDL_SCANCODE_LEFT] && !keys[SDL_SCANCODE_RIGHT]) {
                horizontalDirection = -1;
            } else if (keys[SDL_SCANCODE_RIGHT] && !keys[SDL_SCANCODE_LEFT]) {
                horizontalDirection = 1;
            }

            if (horizontalDirection != 0) {
                if (moveRepeatTimer_ >= horizontalRepeatSeconds) {
                    movePiece(horizontalDirection, 0);
                    moveRepeatTimer_ = 0.0f;
                }
            } else {
                moveRepeatTimer_ = horizontalRepeatSeconds;
            }

            if (keys[SDL_SCANCODE_DOWN]) {
                if (softDropRepeatTimer_ >= softDropRepeatSeconds) {
                    softDrop();
                    softDropRepeatTimer_ = 0.0f;
                    lastFall_ = std::chrono::steady_clock::now();
                }
            } else {
                softDropRepeatTimer_ = softDropRepeatSeconds;
            }
        }

        void handleOneShotKeys(const bool *keys) {
            const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
            const bool hardDropDown = keys[SDL_SCANCODE_Z];
            const bool rotateDown = keys[SDL_SCANCODE_UP];
            const bool resetDown = keys[SDL_SCANCODE_R];
            const bool enterDown = keys[SDL_SCANCODE_RETURN];

            if (escapeDown && !escapeHeld_) {
                goToMenu();
            }
            if (gameOver_ && enterDown && !enterHeld_) {
                restartIntroSequence();
                escapeHeld_ = escapeDown;
                hardDropHeld_ = hardDropDown;
                rotateHeld_ = rotateDown;
                resetHeld_ = resetDown;
                enterHeld_ = enterDown;
                return;
            }
            if (hardDropDown && !hardDropHeld_) {
                hardDrop();
            }
            if (rotateDown && !rotateHeld_) {
                rotatePiece();
            }
            if (resetDown && !resetHeld_) {
                resetGame();
            }

            escapeHeld_ = escapeDown;
            hardDropHeld_ = hardDropDown;
            rotateHeld_ = rotateDown;
            resetHeld_ = resetDown;
            enterHeld_ = enterDown;
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
                if (board_[y][x].color >= 0) {
                    return true;
                }
            }
            return false;
        }

        void movePiece(int dx, int dy) {
            if (!gameOver_ && !lineClearActive_ && !collides(active_.x + dx, active_.y + dy, active_.blocks)) {
                active_.x += dx;
                active_.y += dy;
            }
        }

        void softDrop() {
            if (gameOver_ || lineClearActive_) {
                return;
            }
            if (collides(active_.x, active_.y - 1, active_.blocks)) {
                lockPiece();
            } else {
                --active_.y;
            }
        }

        void hardDrop() {
            if (gameOver_ || lineClearActive_) {
                return;
            }
            while (!collides(active_.x, active_.y - 1, active_.blocks)) {
                --active_.y;
            }
            lockPiece();
        }

        void rotatePiece() {
            if (gameOver_ || lineClearActive_) {
                return;
            }
            const auto rotated = rotatedBlocks(active_);
            if (!collides(active_.x, active_.y, rotated)) {
                active_.blocks = rotated;
                return;
            }
            if (!collides(active_.x - 1, active_.y, rotated)) {
                --active_.x;
                active_.blocks = rotated;
                return;
            }
            if (!collides(active_.x + 1, active_.y, rotated)) {
                ++active_.x;
                active_.blocks = rotated;
            }
        }

        void updateGame() {
            if (screen_ != AppScreen::Game || introActive_) {
                return;
            }
            if (gameOver_) {
                return;
            }

            if (lineClearActive_) {
                const auto now = std::chrono::steady_clock::now();
                const float elapsed = std::chrono::duration<float>(now - lineClearStart_).count();
                if (elapsed >= 1.0f) {
                    finalizeLineClear();
                }
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - lastFall_).count();
            if (elapsed >= fallSeconds_) {
                softDrop();
                lastFall_ = now;
            }
        }

        void lockPiece() {
            for (const Block &block : active_.blocks) {
                const int x = active_.x + block.x;
                const int y = active_.y + block.y;
                if (x >= 0 && x < boardWidth && y >= 0 && y < boardHeight) {
                    board_[y][x].color = active_.color;
                    LockedBlock locked{};
                    locked.block = loadBlockModel(active_.color);
                    locked.x = x;
                    locked.y = y;
                    lockedBlocks_.push_back(std::move(locked));
                }
            }
            const auto fullRows = findFullRows();
            if (std::any_of(fullRows.begin(), fullRows.end(), [](bool value) { return value; })) {
                startLineClear(fullRows);
            } else {
                spawnPiece();
            }
            lastFall_ = std::chrono::steady_clock::now();
        }

        [[nodiscard]] std::array<bool, boardHeight> findFullRows() const {
            std::array<bool, boardHeight> fullRows{};
            for (int y = 0; y < boardHeight; ++y) {
                bool full = true;
                for (int x = 0; x < boardWidth; ++x) {
                    full = full && board_[y][x].color >= 0;
                }
                fullRows[y] = full;
            }
            return fullRows;
        }

        void startLineClear(const std::array<bool, boardHeight> &fullRows) {
            clearingRows_ = fullRows;
            lineClearActive_ = true;
            lineClearStart_ = std::chrono::steady_clock::now();
        }

        void finalizeLineClear() {
            int cleared = 0;
            for (bool row : clearingRows_) {
                if (row) {
                    ++cleared;
                }
            }

            int writeY = 0;
            for (int readY = 0; readY < boardHeight; ++readY) {
                if (clearingRows_[readY]) {
                    continue;
                }
                if (writeY != readY) {
                    board_[writeY] = board_[readY];
                }
                ++writeY;
            }
            for (; writeY < boardHeight; ++writeY) {
                for (Cell &cell : board_[writeY]) {
                    cell.color = -1;
                }
            }

            eraseClearedModels(clearingRows_);
            clearingRows_.fill(false);
            lineClearActive_ = false;
            if (cleared > 0) {
                score_ += cleared * 10;
                if (cleared > 1) {
                    score_ += 5;
                }
                linesCleared_ += cleared;
                updateDifficulty();
            }
            spawnPiece();
            lastFall_ = std::chrono::steady_clock::now();
        }

        void eraseClearedModels(const std::array<bool, boardHeight> &fullRows) {
            std::vector<LockedBlock> remaining{};
            remaining.reserve(lockedBlocks_.size());
            for (LockedBlock &locked : lockedBlocks_) {
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
            lockedBlocks_ = std::move(remaining);
        }

        [[nodiscard]] bool isClearingRow(int y) const {
            return lineClearActive_ && y >= 0 && y < boardHeight && clearingRows_[y];
        }

        [[nodiscard]] bool isLineClearVisible() const {
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - lineClearStart_).count();
            constexpr float blinkIntervalSeconds = 0.12f;
            return (static_cast<int>(elapsed / blinkIntervalSeconds) % 2) == 0;
        }

        void drawIntroOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (!introActive_ || introSprite_ == nullptr || sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - introStart_).count();
            const float fadeProgress = std::clamp((elapsed - introHoldSeconds_) / introFadeSeconds_, 0.0f, 1.0f);
            const float alpha = 1.0f - fadeProgress;

            introSprite_->setShaderParams(elapsed, 0.0f, 0.0f, alpha);
            introSprite_->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
            introSprite_->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
            introSprite_->clearQueue();
        }

        void drawGameOverOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (!gameOver_ || gameOverSprite_ == nullptr || sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
                return;
            }

            const float alpha = gameOverFadeAlpha();
            gameOverSprite_->setShaderParams(0.0f, 0.0f, 0.0f, alpha);
            gameOverSprite_->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
            gameOverSprite_->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
            gameOverSprite_->clearQueue();
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
                drawFrameBlock(cmd, imageIndex, frameModels_[index++], -1, y, view, proj);
                drawFrameBlock(cmd, imageIndex, frameModels_[index++], boardWidth, y, view, proj);
            }
            for (int x = -1; x <= boardWidth; ++x) {
                drawFrameBlock(cmd, imageIndex, frameModels_[index++], x, -1, view, proj);
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
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
            sprite->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
            sprite->clearQueue();
        }

        void drawNextPiecePreview(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent) {
            if (screen_ != AppScreen::Game || introActive_ || previewBorderSprite_ == nullptr || sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
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

            drawSpriteRect(previewBorderSprite_, cmd, extent, panelX, panelY, panelW, border);
            drawSpriteRect(previewBorderSprite_, cmd, extent, panelX, panelY + panelH - border, panelW, border);
            drawSpriteRect(previewBorderSprite_, cmd, extent, panelX, panelY, border, panelH);
            drawSpriteRect(previewBorderSprite_, cmd, extent, panelX + panelW - border, panelY, border, panelH);

            printText("Next", panelX + 12, panelY - 28, SDL_Color{255, 255, 255, 255});

            int minX = nextPiece_.blocks[0].x;
            int maxX = nextPiece_.blocks[0].x;
            int minY = nextPiece_.blocks[0].y;
            int maxY = nextPiece_.blocks[0].y;
            for (const Block &block : nextPiece_.blocks) {
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
            mxvk::VK_Sprite *blockSprite = blockPreviewSprites_[static_cast<std::size_t>(nextPiece_.color)];

            for (std::size_t i = 0; i < nextPiece_.blocks.size(); ++i) {
                const Block &pieceBlock = nextPiece_.blocks[i];
                const float x = originX + static_cast<float>(pieceBlock.x - minX) * blockSize;
                const float y = originY + static_cast<float>(pieceBlock.y - minY) * blockSize;
                drawSpriteRect(blockSprite, cmd, extent, static_cast<int>(x), static_cast<int>(y), blockSize, blockSize);
            }
        }

        void drawHud() {
            if (introActive_) {
                return;
            }
            const VkExtent2D extent = getSwapchainExtent();
            const int centerX = static_cast<int>(extent.width) / 2;
            const float alpha = screenFadeAlpha();
            const auto withAlpha = [alpha](SDL_Color color) {
                color.a = static_cast<Uint8>(static_cast<float>(color.a) * alpha);
                return color;
            };

            switch (screen_) {
            case AppScreen::Menu: {
                const int titleY = static_cast<int>(static_cast<float>(extent.height) * 0.18f);
                const int menuY = static_cast<int>(static_cast<float>(extent.height) * 0.40f);
                const int spacing = static_cast<int>(std::max(34.0f, static_cast<float>(extent.height) * 0.07f));
                printCenteredText("MXVK 3D Tetris", centerX, titleY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText("Choose a mode", centerX, titleY + 42, withAlpha(SDL_Color{255, 255, 255, 255}));

                const char *items[] = {"New Game", "New Multiplayer", "High Scores", "Credits"};
                for (int i = 0; i < 4; ++i) {
                    const SDL_Color color = (i == cursorPos) ? SDL_Color{255, 255, 0, 255} : SDL_Color{255, 255, 255, 255};
                    printCenteredText(items[i], centerX, menuY + i * spacing, withAlpha(color));
                }
                printCenteredText("Use arrows and Enter", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.86f), withAlpha(SDL_Color{180, 180, 180, 255}));
                break;
            }
            case AppScreen::HighScores: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.2f);
                printCenteredText("High Scores", centerX, baseY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText(std::format("Best score: {}", bestScore_), centerX, baseY + 48, withAlpha(SDL_Color{255, 255, 255, 255}));
                printCenteredText(std::format("Current score: {}", score_), centerX, baseY + 84, withAlpha(SDL_Color{255, 220, 120, 255}));
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
            case AppScreen::Multiplayer: {
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.2f);
                printCenteredText("Multiplayer", centerX, baseY, withAlpha(SDL_Color{255, 255, 0, 255}));
                printCenteredText("Local multiplayer is not implemented yet", centerX, baseY + 48, withAlpha(SDL_Color{255, 255, 255, 255}));
                printCenteredText("Press Enter or Escape to return", centerX, static_cast<int>(static_cast<float>(extent.height) * 0.82f), withAlpha(SDL_Color{200, 200, 200, 255}));
                break;
            }
            case AppScreen::Game:
                printText(std::format("Score: {}", score_), 15, 15, withAlpha(SDL_Color{255, 255, 255, 255}));
                printText(std::format("Lines Cleared: {}", linesCleared_), 15, 45, withAlpha(SDL_Color{255, 220, 120, 255}));
                printText(std::format("Level: {}", level_), 15, 75, withAlpha(SDL_Color{120, 220, 255, 255}));
                if (gameOver_) {
                    const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.58f);
                    const float gameOverAlpha = gameOverFadeAlpha();
                    const auto withGameOverAlpha = [gameOverAlpha](SDL_Color color) {
                        color.a = static_cast<Uint8>(static_cast<float>(color.a) * gameOverAlpha);
                        return color;
                    };
                    printCenteredText(std::format("Final Score: {}", score_), centerX, baseY, withGameOverAlpha(SDL_Color{255, 255, 255, 255}));
                    printCenteredText("Press Enter to restart", centerX, baseY + 40, withGameOverAlpha(SDL_Color{255, 220, 120, 255}));
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
            backgroundTransitionActive_ = true;
            backgroundTransitionStart_ = std::chrono::steady_clock::now();
        }

        void updateBackgroundTransitionState() {
            if (!backgroundTransitionActive_) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - backgroundTransitionStart_).count();
            if (elapsed >= backgroundTransitionSeconds_) {
                backgroundTransitionActive_ = false;
            }
        }

        [[nodiscard]] mxvk::VK_Sprite *screenSpriteFor(AppScreen screen) const {
            switch (screen) {
            case AppScreen::Menu:
                return menuBackgroundSprite_;
            case AppScreen::HighScores:
                return highScoresBackgroundSprite_;
            case AppScreen::Credits:
                return creditsBackgroundSprite_;
            case AppScreen::Multiplayer:
                return multiplayerBackgroundSprite_;
            case AppScreen::Intro:
            case AppScreen::Game:
                return nullptr;
            }
            return nullptr;
        }

        void drawFadedSprite(mxvk::VK_Sprite *sprite, VkCommandBuffer cmd, const VkExtent2D &extent, float alpha) {
            if (sprite == nullptr || sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
                return;
            }
            sprite->setShaderParams(0.0f, 0.0f, 0.0f, alpha);
            sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
            sprite->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
            sprite->clearQueue();
        }

        void drawGameScreenTransitionOverlay(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (screen_ != AppScreen::Game || !screenTransitionActive_) {
                return;
            }
            drawFadedSprite(screenSpriteFor(transitionFromScreen_), cmd, extent, 1.0f - screenFadeAlpha());
        }

        void drawScreenBackdrop(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (screen_ == AppScreen::Game) {
                if (sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
                    return;
                }
                updateBackgroundTransitionState();

                mxvk::VK_Sprite *sprite = background_;
                if (backgroundTransitionActive_ && backgroundTransitionSprite_ != nullptr) {
                    sprite = backgroundTransitionSprite_;
                }
                if (sprite == nullptr) {
                    return;
                }

                if (sprite == backgroundTransitionSprite_) {
                    const auto now = std::chrono::steady_clock::now();
                    const float elapsed = std::chrono::duration<float>(now - backgroundTransitionStart_).count();
                    sprite->setShaderParams(elapsed, 0.0f, 0.0f, 0.0f);
                }

                sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
                sprite->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
                sprite->clearQueue();
                return;
            }

            const float alpha = screenFadeAlpha();
            if (screenTransitionActive_) {
                drawFadedSprite(screenSpriteFor(transitionFromScreen_), cmd, extent, 1.0f - alpha);
            }
            drawFadedSprite(screenSpriteFor(screen_), cmd, extent, alpha);
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
