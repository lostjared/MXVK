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
            background_ = createSprite(dataRoot_ + "/psychedelic_background.png");
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
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    exit();
                    return;
                }
                handleGamepadButtonDown(e.gbutton.button);
            }
        }

        void proc() override {
            tryOpenFirstGamepad();
            updateIntroState();
            updateInput();
            updateGame();
            drawHud();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.2f, cameraDistance_), glm::vec3(0.0f, 0.1f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            view = glm::rotate(view, glm::radians(gridPitch_), glm::vec3(1.0f, 0.0f, 0.0f));
            view = glm::rotate(view, glm::radians(gridYaw_), glm::vec3(0.0f, 1.0f, 0.0f));
            view = glm::rotate(view, glm::radians(gridRoll_), glm::vec3(0.0f, 0.0f, 1.0f));

            glm::mat4 proj = glm::perspective(glm::radians(46.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;
            const bool blinkVisible = !lineClearActive_ || isLineClearVisible();

            drawBackground(cmd, extent);

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
        mxvk::VK_Sprite *introSprite_ = nullptr;
        mxvk::VK_Sprite *gameOverSprite_ = nullptr;
        SDL_Gamepad *gamepad_ = nullptr;
        SDL_JoystickID gamepadId_ = 0;
        std::chrono::steady_clock::time_point introStart_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastFall_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastInputUpdate_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lineClearStart_{std::chrono::steady_clock::now()};
        bool introActive_ = true;
        float gridYaw_ = 0.0f;
        float gridPitch_ = 0.0f;
        float gridRoll_ = 0.0f;
        float cameraDistance_ = 2.45f;
        int score_ = 0;
        int linesCleared_ = 0;
        float fallSeconds_ = 0.65f;
        float moveRepeatTimer_ = 0.0f;
        float softDropRepeatTimer_ = 0.0f;
        bool gameOver_ = false;
        bool hardDropHeld_ = false;
        bool rotateHeld_ = false;
        bool resetHeld_ = false;
        bool enterHeld_ = false;
        bool escapeHeld_ = false;
        bool lineClearActive_ = false;
        std::array<bool, boardHeight> clearingRows_{};
        static constexpr float introHoldSeconds_ = 5.0f;
        static constexpr float introFadeSeconds_ = 1.0f;
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
            fallSeconds_ = 0.65f;
            gameOver_ = false;
            lineClearActive_ = false;
            clearingRows_.fill(false);
            score_ = 0;
            linesCleared_ = 0;
            lastFall_ = std::chrono::steady_clock::now();
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
            std::uniform_int_distribution<int> dist(0, static_cast<int>(pieceDefinitions.size()) - 1);
            const PieceDefinition &definition = pieceDefinitions[dist(rng_)];
            active_.blocks = definition.blocks;
            active_.x = boardWidth / 2;
            active_.y = boardHeight - 2;
            active_.color = definition.color;
            reloadActiveModels(active_.color);
            gameOver_ = collides(active_.x, active_.y, active_.blocks);
        }

        void reloadActiveModels(int color) {
            for (BlockModel &block : activeModels_) {
                if (block.model) {
                    block.model->cleanup(this);
                }
                block = loadBlockModel(color);
            }
        }

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
                const bool escapeDown = keys[SDL_SCANCODE_ESCAPE];
                if (escapeDown && !escapeHeld_) {
                    exit();
                }
                escapeHeld_ = escapeDown;
                return;
            }

            handleHeldViewRotation(keys, deltaSeconds);
            handleHeldPieceMovement(keys, deltaSeconds);
            handleOneShotKeys(keys);
            handleGamepadInput(deltaSeconds);
        }

        void handleGamepadInput(float deltaSeconds) {
            if (gamepad_ == nullptr || introActive_ || gameOver_ || lineClearActive_) {
                gamepadMoveDirection_ = 0;
                gamepadMoveHeldSeconds_ = 0.0f;
                gamepadSoftDropHeld_ = false;
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
                moveRepeatTimer_ = 0.0f;
            } else {
                if (moveDirection != gamepadMoveDirection_) {
                    gamepadMoveDirection_ = moveDirection;
                    gamepadMoveHeldSeconds_ = 0.0f;
                    moveRepeatTimer_ = 0.0f;
                    movePiece(gamepadMoveDirection_, 0);
                } else {
                    gamepadMoveHeldSeconds_ += deltaSeconds;
                    const float threshold = (gamepadMoveHeldSeconds_ < gamepadMoveInitialDelaySeconds_)
                                                ? gamepadMoveInitialDelaySeconds_
                                                : gamepadMoveRepeatSeconds_;
                    moveRepeatTimer_ += deltaSeconds;
                    if (moveRepeatTimer_ >= threshold) {
                        movePiece(gamepadMoveDirection_, 0);
                        moveRepeatTimer_ = 0.0f;
                    }
                }
            }

            const bool softDropDown = leftY > gamepadDeadzone_;
            if (!softDropDown) {
                gamepadSoftDropHeld_ = false;
                softDropRepeatTimer_ = 0.0f;
            } else {
                const float threshold = gamepadSoftDropHeld_ ? gamepadSoftDropRepeatSeconds_ : gamepadSoftDropInitialDelaySeconds_;
                softDropRepeatTimer_ += deltaSeconds;
                if (softDropRepeatTimer_ >= threshold) {
                    softDrop();
                    softDropRepeatTimer_ = 0.0f;
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

            if (gameOver_) {
                if (button == SDL_GAMEPAD_BUTTON_START) {
                    resetGame();
                }
                return;
            }

            if (introActive_ || lineClearActive_) {
                return;
            }

            if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                rotatePiece();
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                hardDrop();
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
            if (elapsed >= (introHoldSeconds_ + introFadeSeconds_)) {
                introActive_ = false;
                lastFall_ = now;
            }
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
                exit();
            }
            if (gameOver_ && enterDown && !enterHeld_) {
                resetGame();
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
            if (introActive_) {
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
                fallSeconds_ = std::max(0.16f, fallSeconds_ - static_cast<float>(cleared) * 0.025f);
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

        void drawBackground(VkCommandBuffer cmd, const VkExtent2D &extent) {
            if (background_ == nullptr || sprite_pipeline_ == VK_NULL_HANDLE || sprite_pipeline_layout_ == VK_NULL_HANDLE) {
                return;
            }
            background_->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sprite_pipeline_);
            background_->renderSprites(cmd, sprite_pipeline_layout_, extent.width, extent.height);
            background_->clearQueue();
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

        void drawHud() {
            if (introActive_) {
                return;
            }
            printText(std::format("Score: {}", score_), 15, 15, SDL_Color{255, 255, 255, 255});
            printText(std::format("Lines Cleared: {}", linesCleared_), 15, 45, SDL_Color{255, 220, 120, 255});
            if (gameOver_) {
                const VkExtent2D extent = getSwapchainExtent();
                const int centerX = static_cast<int>(extent.width) / 2;
                const int baseY = static_cast<int>(static_cast<float>(extent.height) * 0.58f);
                printCenteredText(std::format("Final Score: {}", score_), centerX, baseY, SDL_Color{255, 255, 255, 255});
                printCenteredText("Press Enter to start a new game", centerX, baseY + 34, SDL_Color{255, 220, 120, 255});
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
