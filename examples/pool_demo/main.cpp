#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"
#include "mxvk/argz.hpp"

namespace {

    constexpr float kTableW = 12.0f;
    constexpr float kTableH = 6.0f;
    constexpr float kTableHalfW = kTableW / 2.0f;
    constexpr float kTableHalfH = kTableH / 2.0f;
    constexpr float kPocketR = 0.25f;
    constexpr float kBallRadius = 0.18f;
    constexpr float kCueLength = 2.5f;
    constexpr float kCueThickness = 0.03f;
    constexpr float kAimLength = 2.0f;
    constexpr float kAimThicknessY = 0.008f;
    constexpr float kAimThicknessZ = 0.015f;
    constexpr int kNumBalls = 16;
    constexpr float kFriction = 0.9988f;
    constexpr float kMinSpeed = 0.001f;
    constexpr float kMaxPower = 1.5f;
    constexpr float kCamBaseTotalScale = 1.359411f;
    constexpr float kSinkDuration = 0.45f;
    constexpr float kPi = 3.14159265358979323846f;

    constexpr float kPocketInset = 0.25f;
    const std::array<glm::vec2, 6> kPockets = {
        glm::vec2{-kTableHalfW + kPocketInset, kTableHalfH - kPocketInset},
        glm::vec2{0.0f, kTableHalfH - 0.15f},
        glm::vec2{kTableHalfW - kPocketInset, kTableHalfH - kPocketInset},
        glm::vec2{-kTableHalfW + kPocketInset, -kTableHalfH + kPocketInset},
        glm::vec2{0.0f, -kTableHalfH + 0.15f},
        glm::vec2{kTableHalfW - kPocketInset, -kTableHalfH + kPocketInset},
    };

    const std::array<glm::vec3, kNumBalls> kBallColors = {
        glm::vec3{1.0f, 1.0f, 1.0f},
        glm::vec3{1.0f, 0.84f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.8f},
        glm::vec3{0.9f, 0.0f, 0.0f},
        glm::vec3{0.5f, 0.0f, 0.5f},
        glm::vec3{1.0f, 0.5f, 0.0f},
        glm::vec3{0.0f, 0.5f, 0.0f},
        glm::vec3{0.55f, 0.0f, 0.0f},
        glm::vec3{0.1f, 0.1f, 0.1f},
        glm::vec3{1.0f, 0.84f, 0.0f},
        glm::vec3{0.0f, 0.0f, 0.8f},
        glm::vec3{0.9f, 0.0f, 0.0f},
        glm::vec3{0.5f, 0.0f, 0.5f},
        glm::vec3{1.0f, 0.5f, 0.0f},
        glm::vec3{0.0f, 0.5f, 0.0f},
        glm::vec3{0.55f, 0.0f, 0.0f},
    };

    float randFloat(float mn, float mx) {
        static std::random_device rd;
        static std::default_random_engine eng(rd());
        std::uniform_real_distribution<float> dist(mn, mx);
        return dist(eng);
    }

    enum class GameScreen { Intro, Scores, Game, Start };
    enum class GamePhase { Aiming, Charging, Rolling, Placing, GameOver };

    struct PoolBall {
        glm::vec2 pos{0.0f};
        glm::vec2 vel{0.0f};
        bool active = true;
        bool pocketed = false;
        int number = 0;
        float spinAngle = 0.0f;

        bool isMoving() const {
            return glm::length(vel) > kMinSpeed;
        }
    };

    struct SinkAnim {
        glm::vec2 pocketPos{0.0f};
        glm::vec3 color{1.0f};
        float spinAngle = 0.0f;
        float timer = 0.0f;
        int ballIndex = 0;
    };

    struct Score {
        std::string name;
        int shots = 0;
    };

    class HighScores {
      public:
        explicit HighScores(std::string filePath)
            : filePath_(std::move(filePath)) {
            read();
        }

        ~HighScores() {
            write();
        }

        void addScore(const std::string &name, int shots) {
            entries_.push_back({name, shots});
            sort();
            if (entries_.size() > 10) {
                entries_.resize(10);
            }
        }

        [[nodiscard]] bool qualifies(int shots) const {
            if (entries_.size() < 10) {
                return true;
            }
            return shots < entries_.back().shots;
        }

        [[nodiscard]] const std::vector<Score> &list() const {
            return entries_;
        }

        void write() const {
            std::ofstream out(filePath_);
            if (!out.is_open()) {
                return;
            }
            for (const auto &entry : entries_) {
                out << entry.name << ':' << entry.shots << '\n';
            }
        }

      private:
        void sort() {
            std::sort(entries_.begin(), entries_.end(), [](const Score &a, const Score &b) {
                return a.shots < b.shots;
            });
        }

        void initDefaults() {
            entries_.clear();
            for (int i = 1; i <= 10; ++i) {
                entries_.push_back({"Anonymous", i * 20});
            }
        }

        void read() {
            std::ifstream in(filePath_);
            if (!in.is_open()) {
                initDefaults();
                return;
            }

            std::string line;
            int count = 0;
            while (std::getline(in, line) && count < 10) {
                const std::size_t pos = line.find(':');
                if (pos == std::string::npos) {
                    continue;
                }

                const std::string name = line.substr(0, pos);
                const int shots = static_cast<int>(std::strtol(line.substr(pos + 1).c_str(), nullptr, 10));
                entries_.push_back({name, shots});
                ++count;
            }

            if (entries_.empty()) {
                initDefaults();
            }
            sort();
        }

        std::string filePath_;
        std::vector<Score> entries_;
    };

} // namespace

namespace demo {

    class PoolWindow final : public mxvk::VK_Window {
      public:
        PoolWindow(int width, int height, bool fullscreen, std::string assetRoot)
            : mxvk::VK_Window("3D Pool / MXVK", width, height, fullscreen, MXVK_VALIDATION),
              assetRoot_(std::move(assetRoot)),
              highScores_((std::filesystem::path(assetRoot_) / "pool_scores.dat").string()),
              fallbackWidth_(width),
              fallbackHeight_(height) {
            setFont(assetRoot_ + "/font.ttf", 24);
            initSprites();
            initModels();
            resetGame();
            tryOpenFirstGamepad();
        }

        ~PoolWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            closeGamepad();
            cleanupModels();
        }

        void proc() override {
            const VkExtent2D extent = getSwapchainExtent();
            const int renderW = (extent.width > 0U) ? static_cast<int>(extent.width) : fallbackWidth_;
            const int renderH = (extent.height > 0U) ? static_cast<int>(extent.height) : fallbackHeight_;
            fallbackWidth_ = renderW;
            fallbackHeight_ = renderH;

            switch (screen_) {
            case GameScreen::Intro:
                procIntro(renderW, renderH);
                break;
            case GameScreen::Start:
                procStart(renderW, renderH);
                break;
            case GameScreen::Game:
                procGame(renderW, renderH);
                break;
            case GameScreen::Scores:
                procScores(renderW, renderH);
                break;
            }
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
                if (gamepad_ != nullptr && e.gdevice.which == gamepadId_) {
                    closeGamepad();
                    tryOpenFirstGamepad();
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    if (screen_ == GameScreen::Game && mouseCaptured_) {
                        setMouseCapture(false);
                        return;
                    }
                    if (screen_ == GameScreen::Scores) {
                        setScreen(GameScreen::Intro);
                    } else {
                        exit();
                    }
                    return;
                }

                if (screen_ == GameScreen::Scores) {
                    if (enteringName_) {
                        if (e.key.key == SDLK_RETURN && !playerName_.empty()) {
                            commitScoreEntry();
                        } else if (e.key.key == SDLK_BACKSPACE && !playerName_.empty()) {
                            playerName_.pop_back();
                        }
                    } else if (e.key.key == SDLK_RETURN) {
                        setScreen(GameScreen::Intro);
                    }
                    return;
                }

                if (screen_ == GameScreen::Start) {
                    if (e.key.key == SDLK_RETURN) {
                        resetGame();
                        setScreen(GameScreen::Game);
                    } else if (e.key.key == SDLK_SPACE) {
                        setScreen(GameScreen::Scores);
                    }
                    return;
                }

                if (screen_ == GameScreen::Game) {
                    if (e.key.key == SDLK_R) {
                        resetGame();
                    } else if (e.key.key == SDLK_SPACE && phase_ == GamePhase::Aiming) {
                        phase_ = GamePhase::Charging;
                        chargeAmount_ = 0.0f;
                    } else if (e.key.key == SDLK_RETURN && phase_ == GamePhase::Placing && canPlaceCueBall()) {
                        phase_ = GamePhase::Aiming;
                    }
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_UP && screen_ == GameScreen::Game && e.key.key == SDLK_SPACE && phase_ == GamePhase::Charging) {
                shootCueBall();
                return;
            }

            if (e.type == SDL_EVENT_TEXT_INPUT && screen_ == GameScreen::Scores && enteringName_) {
                if (playerName_.size() < 15U) {
                    playerName_ += e.text.text;
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL && screen_ == GameScreen::Game) {
                const float dy = (e.wheel.y != 0.0f) ? e.wheel.y : e.wheel.integer_y;
                camZoom_ -= dy * 1.2f;
                camZoom_ = glm::clamp(camZoom_, 5.0f, 25.0f);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT && screen_ == GameScreen::Game) {
                if (!mouseCaptured_) {
                    setMouseCapture(true);
                }
                mouseCamDragging_ = true;
                mouseCamLastX_ = static_cast<int>(e.button.x);
                mouseCamLastY_ = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (screen_ == GameScreen::Game && !mouseCaptured_) {
                    setMouseCapture(true);
                    consumeNextMouseLeftUp_ = true;
                    return;
                }
                onPointerDown(static_cast<int>(e.button.x), static_cast<int>(e.button.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                mouseCamDragging_ = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                if (consumeNextMouseLeftUp_) {
                    consumeNextMouseLeftUp_ = false;
                    return;
                }
                if (screen_ == GameScreen::Game && phase_ == GamePhase::Charging && pointerCharging_) {
                    shootCueBall();
                    return;
                }
                onPointerUp(static_cast<int>(e.button.x), static_cast<int>(e.button.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (screen_ == GameScreen::Game && mouseCaptured_) {
                    if (mouseCamDragging_) {
                        camAngle_ += static_cast<float>(e.motion.xrel) * 0.01f;
                        camPitch_ -= static_cast<float>(e.motion.yrel) * 0.006f;
                        camPitch_ = glm::clamp(camPitch_, 0.30f, 1.25f);
                        return;
                    }
                    onMouseRelativeMove(e.motion.xrel, e.motion.yrel);
                    return;
                }
                if (mouseCamDragging_ && screen_ == GameScreen::Game) {
                    const int nx = static_cast<int>(e.motion.x);
                    const int ny = static_cast<int>(e.motion.y);
                    const int dx = nx - mouseCamLastX_;
                    const int dy = ny - mouseCamLastY_;
                    camAngle_ += static_cast<float>(dx) * 0.01f;
                    camPitch_ -= static_cast<float>(dy) * 0.006f;
                    camPitch_ = glm::clamp(camPitch_, 0.30f, 1.25f);
                    mouseCamLastX_ = nx;
                    mouseCamLastY_ = ny;
                    return;
                }
                onPointerMove(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_FINGER_DOWN) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth_));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight_));
                touchPoints_[static_cast<int64_t>(e.tfinger.fingerID)] = SDL_FPoint{static_cast<float>(px), static_cast<float>(py)};
                if (screen_ == GameScreen::Game && touchPoints_.size() == 2U) {
                    auto it = touchPoints_.begin();
                    const SDL_FPoint a = it->second;
                    ++it;
                    const SDL_FPoint b = it->second;
                    const float dx = a.x - b.x;
                    const float dy = a.y - b.y;
                    touchPinchDistance_ = std::sqrt(dx * dx + dy * dy);
                    touchPinchActive_ = true;
                    if (phase_ == GamePhase::Charging) {
                        phase_ = GamePhase::Aiming;
                        chargeAmount_ = 0.0f;
                    }
                    pointerCharging_ = false;
                    pointerDown_ = false;
                    activePointerId_ = std::numeric_limits<int64_t>::min();
                    return;
                }
                onPointerDown(px, py, static_cast<int64_t>(e.tfinger.fingerID));
                return;
            }

            if (e.type == SDL_EVENT_FINGER_MOTION) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth_));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight_));
                touchPoints_[static_cast<int64_t>(e.tfinger.fingerID)] = SDL_FPoint{static_cast<float>(px), static_cast<float>(py)};
                if (screen_ == GameScreen::Game && touchPinchActive_ && touchPoints_.size() >= 2U) {
                    auto it = touchPoints_.begin();
                    const SDL_FPoint a = it->second;
                    ++it;
                    const SDL_FPoint b = it->second;
                    const float dx = a.x - b.x;
                    const float dy = a.y - b.y;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float delta = dist - touchPinchDistance_;
                    touchPinchDistance_ = dist;
                    camZoom_ -= delta * 0.02f;
                    camZoom_ = glm::clamp(camZoom_, 5.0f, 25.0f);
                    return;
                }
                onPointerMove(px, py, static_cast<int64_t>(e.tfinger.fingerID));
                return;
            }

            if (e.type == SDL_EVENT_FINGER_UP) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth_));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight_));
                const bool wasPinching = touchPinchActive_;
                touchPoints_.erase(static_cast<int64_t>(e.tfinger.fingerID));
                if (touchPinchActive_ && touchPoints_.size() < 2U) {
                    touchPinchActive_ = false;
                    touchPinchDistance_ = 0.0f;
                }
                if (wasPinching) {
                    pointerCharging_ = false;
                    pointerDown_ = false;
                    activePointerId_ = std::numeric_limits<int64_t>::min();
                    return;
                }
                onPointerUp(px, py, static_cast<int64_t>(e.tfinger.fingerID));
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                handleGamepadButtonDown(e.gbutton.button);
                return;
            }

            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
                handleGamepadButtonUp(e.gbutton.button);
                return;
            }
        }

        void onSwapchainRecreated() override {
            feltModel_.resize(this);
            woodModel_.resize(this);
            pocketModel_.resize(this);
            for (auto &ballModel : ballModels_) {
                ballModel.resize(this);
            }
            cueStickModel_.resize(this);
            cueAimModel_.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (screen_ != GameScreen::Game) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            const float time = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            const glm::vec3 camPos = getCameraPosition();
            const glm::mat4 view = glm::lookAt(camPos, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            drawModel(feltModel_, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, time);
            drawModel(woodModel_, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{0.4f, 0.22f, 0.05f, 1.0f}, time);
            drawModel(pocketModel_, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{0.08f, 0.08f, 0.08f, 1.0f}, time);

            for (int i = 0; i < kNumBalls; ++i) {
                if (!balls_[i].active || balls_[i].pocketed) {
                    continue;
                }
                glm::mat4 m{1.0f};
                m = glm::translate(m, glm::vec3{balls_[i].pos.x, kBallRadius, balls_[i].pos.y});
                m = glm::rotate(m, balls_[i].spinAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                m = glm::scale(m, glm::vec3{kBallRadius});
                drawModel(ballModels_[static_cast<std::size_t>(i)],
                          imageIndex,
                          cmd,
                          view,
                          proj,
                          m,
                          glm::vec4{kBallColors[static_cast<std::size_t>(i)], 1.0f},
                          time);
            }

            for (const auto &anim : sinkAnims_) {
                const float t = anim.timer / kSinkDuration;
                const float y = glm::mix(kBallRadius, -kBallRadius * 3.5f, t);
                const float sc = kBallRadius * (1.0f - t * 0.75f);
                glm::mat4 m{1.0f};
                m = glm::translate(m, glm::vec3{anim.pocketPos.x, y, anim.pocketPos.y});
                m = glm::rotate(m, anim.spinAngle + t * 6.0f, glm::vec3{0.0f, 1.0f, 0.0f});
                m = glm::scale(m, glm::vec3{sc});
                const std::size_t sinkIndex = static_cast<std::size_t>(glm::clamp(anim.ballIndex, 0, kNumBalls - 1));
                drawModel(ballModels_[sinkIndex], imageIndex, cmd, view, proj, m, glm::vec4{anim.color, 1.0f}, time);
            }

            if ((phase_ == GamePhase::Aiming || phase_ == GamePhase::Charging) && balls_[0].active) {
                const float offset = (phase_ == GamePhase::Charging) ? (chargeAmount_ / kMaxPower) : 0.0f;
                const float stickDist = kBallRadius + 0.3f + offset;
                const float worldCueAngle = cueAngle_ + camAngle_;
                const glm::vec2 dir{std::cos(worldCueAngle), std::sin(worldCueAngle)};

                const glm::vec2 stickCenter = balls_[0].pos - dir * (stickDist + kCueLength * 0.5f);
                glm::mat4 cueTransform{1.0f};
                cueTransform = glm::translate(cueTransform, glm::vec3{stickCenter.x, kBallRadius + 0.05f, stickCenter.y});
                cueTransform = glm::rotate(cueTransform, -worldCueAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                cueTransform = glm::scale(cueTransform, glm::vec3{kCueLength * 0.5f, kCueThickness, kCueThickness});

                const float pct = chargeAmount_ / kMaxPower;
                const glm::vec4 cueColor = (phase_ == GamePhase::Charging)
                                               ? glm::vec4{0.55f + pct * 0.45f, 0.27f * (1.0f - pct), 0.07f * (1.0f - pct), 1.0f}
                                               : glm::vec4{0.55f, 0.27f, 0.07f, 1.0f};
                drawModel(cueStickModel_, imageIndex, cmd, view, proj, cueTransform, cueColor, time);

                const glm::vec2 aimCenter = balls_[0].pos + dir * (kBallRadius + kAimLength * 0.5f);
                glm::mat4 aimTransform{1.0f};
                aimTransform = glm::translate(aimTransform, glm::vec3{aimCenter.x, kBallRadius + 0.06f, aimCenter.y});
                aimTransform = glm::rotate(aimTransform, -worldCueAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                aimTransform = glm::scale(aimTransform, glm::vec3{kAimLength * 0.5f, kAimThicknessY, kAimThicknessZ});
                drawModel(cueAimModel_, imageIndex, cmd, view, proj, aimTransform, glm::vec4{1.0f, 1.0f, 0.0f, 1.0f}, time);
            }
        }

      private:
        static glm::mat4 composeTransform(const glm::vec3 &pos, const glm::vec3 &scale) {
            glm::mat4 m{1.0f};
            m = glm::translate(m, pos);
            m = glm::scale(m, scale);
            return m;
        }

        void initSprites() {
            backgroundSprite_ = createSprite(assetRoot_ + "/data/background.png", assetRoot_ + "/data/sprite_vert.spv", assetRoot_ + "/data/sprite_frag.spv");
            startSprite_ = createSprite(assetRoot_ + "/data/start.png", assetRoot_ + "/data/sprite_vert.spv", assetRoot_ + "/data/sprite_frag.spv");
            scoresSprite_ = createSprite(assetRoot_ + "/data/scores.png", assetRoot_ + "/data/sprite_vert.spv", assetRoot_ + "/data/sprite_frag.spv");
            introSprite_ = createSprite(assetRoot_ + "/data/logo.png", assetRoot_ + "/data/sprite_vert.spv", assetRoot_ + "/data/bend_dir.spv");
        }

        void initModels() {
            loadModel(feltModel_, assetRoot_ + "/data/pooltable_felt.mxmod.z");
            loadModel(woodModel_, assetRoot_ + "/data/pooltable_wood.mxmod.z");
            loadModel(pocketModel_, assetRoot_ + "/data/pooltable_pocket.mxmod.z");

            for (auto &ballModel : ballModels_) {
                loadModel(ballModel, assetRoot_ + "/data/geosphere.mxmod.z");
            }
            loadModel(cueStickModel_, assetRoot_ + "/data/cube.mxmod.z", 2.0f);
            loadModel(cueAimModel_, assetRoot_ + "/data/cube.mxmod.z", 2.0f);

            applyPrimaryTexture(feltModel_, assetRoot_ + "/data/table.png");
        }

        void cleanupModels() {
            feltModel_.cleanup(this);
            woodModel_.cleanup(this);
            pocketModel_.cleanup(this);
            for (auto &ballModel : ballModels_) {
                ballModel.cleanup(this);
            }
            cueStickModel_.cleanup(this);
            cueAimModel_.cleanup(this);
        }

        void loadModel(mxvk::VKAbstractModel &model, const std::string &path, float scale = 1.0f) {
            model.load(this, path, "", assetRoot_ + "/data", scale);
            model.setShaders(this,
                             std::string(POOL_DEMO_SHADER_DIR) + "/model.vert.spv",
                             std::string(POOL_DEMO_SHADER_DIR) + "/model.frag.spv");
        }

        void drawModel(mxvk::VKAbstractModel &model,
                       uint32_t imageIndex,
                       VkCommandBuffer cmd,
                       const glm::mat4 &view,
                       const glm::mat4 &proj,
                       const glm::mat4 &transform,
                       const glm::vec4 &fx,
                       float time) {
            mxvk::UniformBufferObject ubo{};
            // Keep pool-world coordinates aligned with legacy gameplay physics units.
            ubo.model = transform;
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(fx.r, fx.g, fx.b, time);
            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

        void applyPrimaryTexture(mxvk::VKAbstractModel &model, const std::string &texturePath) {
            SDL_Surface *surface = mxvk::LoadPNG(texturePath.c_str());
            if (surface == nullptr) {
                return;
            }

            SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(surface);
            if (rgba == nullptr) {
                return;
            }

            const int pitch = static_cast<int>(rgba->pitch);
            (void)model.updatePrimaryTexture(rgba->pixels, rgba->w, rgba->h, pitch);
            SDL_DestroySurface(rgba);
        }

        void setScreen(GameScreen next) {
            if (screen_ == GameScreen::Scores && next != GameScreen::Scores) {
                SDL_StopTextInput(window.get());
                setFont(assetRoot_ + "/font.ttf", 24);
                scoreFontSize_ = 0;
            }
            if (screen_ != GameScreen::Scores && next == GameScreen::Scores && enteringName_) {
                SDL_StartTextInput(window.get());
            }
            screen_ = next;
            if (screen_ == GameScreen::Game) {
                setMouseCapture(true);
            } else {
                setMouseCapture(false);
            }
        }

        void setMouseCapture(bool enabled) {
            if (mouseCaptured_ == enabled) {
                return;
            }
            mouseCaptured_ = enabled;
            (void)SDL_SetWindowMouseGrab(window.get(), enabled);
            (void)SDL_SetWindowRelativeMouseMode(window.get(), enabled);
            if (enabled) {
                SDL_HideCursor();
            } else {
                SDL_ShowCursor();
            }
            mouseCamDragging_ = false;
        }

        void procIntro(int width, int height) {
            if (startTicks_ == 0U) {
                startTicks_ = SDL_GetTicks();
            }

            const Uint64 elapsed = SDL_GetTicks() - startTicks_;
            constexpr Uint64 kTotal = 5000;
            constexpr Uint64 kFadeDur = 1500;

            float fadeOut = 1.0f;
            if (elapsed > (kTotal - kFadeDur)) {
                fadeOut = 1.0f - static_cast<float>(elapsed - (kTotal - kFadeDur)) / static_cast<float>(kFadeDur);
            }

            if (startSprite_ != nullptr) {
                startSprite_->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                startSprite_->drawSpriteRect(0, 0, width, height);
            }
            if (introSprite_ != nullptr) {
                introSprite_->setShaderParams(fadeOut, 1.0f, 1.0f, static_cast<float>(SDL_GetTicks()) / 1000.0f);
                introSprite_->drawSpriteRect(0, 0, width, height);
            }

            if (elapsed >= kTotal) {
                startTicks_ = 0U;
                setScreen(GameScreen::Start);
            }
        }

        void procStart(int width, int height) {
            if (startSprite_ != nullptr) {
                startSprite_->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                startSprite_->drawSpriteRect(0, 0, width, height);
            }

            const char *hint = "ENTER / A - Play";
            int tw = 0;
            int th = 0;
            (void)getTextDimensions(hint, tw, th);
            const int x = width / 2 - tw / 2;
            const int y = height - (th * 3) + 20;
            printText(hint, x, y, SDL_Color{220, 220, 100, 255});
            updateStartClickTargets();
        }

        void procScores(int width, int height) {
            if (scoresSprite_ != nullptr) {
                scoresSprite_->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                scoresSprite_->drawSpriteRect(0, 0, width, height);
            }

            const int feltL = static_cast<int>(width * 0.125f);
            const int feltT = static_cast<int>(height * 0.19f);
            const int feltB = static_cast<int>(height * 0.87f);
            const int feltH = feltB - feltT;
            const int feltCX = static_cast<int>(width * 0.48f);
            const int fs = std::max(10, feltH / 15);

            if (fs != scoreFontSize_) {
                setFont(assetRoot_ + "/font.ttf", fs);
                scoreFontSize_ = fs;
            }

            const int lineH = fs + fs / 3;
            const auto &list = highScores_.list();
            for (std::size_t i = 0; i < list.size() && i < 10U; ++i) {
                std::ostringstream ss;
                ss << (i + 1U) << ". " << list[i].name << "  " << list[i].shots << " shots";
                SDL_Color color{255, 255, 255, 255};
                printText(ss.str(), feltL + fs / 2, feltT + static_cast<int>(i) * lineH, color);
            }

            if (enteringName_) {
                const int entryY = feltT + 10 * lineH + lineH / 2;
                std::ostringstream ys;
                ys << "Your score: " << finalScore_ << " shots";

                int tw = 0;
                int th = 0;
                (void)getTextDimensions(ys.str(), tw, th);
                printText(ys.str(), feltCX - tw / 2, entryY, SDL_Color{255, 255, 0, 255});

                const std::string dn = playerName_ + "_";
                (void)getTextDimensions(dn, tw, th);
                printText(dn, feltCX - tw / 2, entryY + lineH, SDL_Color{0, 255, 255, 255});

                const std::string confirm = "ENTER to confirm";
                const std::string del = "BACKSPACE to delete";
                int cw = 0;
                int ch = 0;
                (void)getTextDimensions(confirm, cw, ch);
                printText(confirm, feltCX - cw - fs / 3, entryY + lineH * 2, SDL_Color{200, 200, 200, 255});
                int dw = 0;
                int dh = 0;
                (void)getTextDimensions(del, dw, dh);
                printText(del, feltCX + fs / 3, entryY + lineH * 2, SDL_Color{200, 200, 200, 255});

                scoresConfirmRect_ = makeTextRect(confirm, feltCX - cw - fs / 3, entryY + lineH * 2, 10);
                scoresDeleteRect_ = makeTextRect(del, feltCX + fs / 3, entryY + lineH * 2, 10);
                scoresReturnRect_ = SDL_Rect{0, 0, 0, 0};
            } else {
                const std::string ret = "Press ENTER to return to intro";
                int tw = 0;
                int th = 0;
                (void)getTextDimensions(ret, tw, th);
                const int x = feltCX - tw / 2;
                const int y = feltB - lineH;
                printText(ret, x, y, SDL_Color{255, 255, 0, 255});
                scoresReturnRect_ = makeTextRect(ret, x, y, 12);
                scoresConfirmRect_ = SDL_Rect{0, 0, 0, 0};
                scoresDeleteRect_ = SDL_Rect{0, 0, 0, 0};
            }
        }

        void procGame(int width, int height) {
            if (backgroundSprite_ != nullptr) {
                backgroundSprite_->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                backgroundSprite_->drawSpriteRect(0, 0, width, height);
            }

            const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            float dt = now - lastTime_;
            if (dt > 0.05f) {
                dt = 0.05f;
            }
            lastTime_ = now;

            handleGameState(dt);
            handleCameraAndInput(dt);

            printText("Shots: " + std::to_string(shotCount_), 15, 45, SDL_Color{255, 255, 0, 255});
            int rem = 0;
            for (int i = 1; i < kNumBalls; ++i) {
                if (balls_[i].active && !balls_[i].pocketed) {
                    ++rem;
                }
            }
            printText("Balls: " + std::to_string(rem), 15, 75, SDL_Color{200, 200, 200, 255});

            if (phase_ == GamePhase::Aiming) {
                printText("Mouse: move aim + hold/release | Right-drag: rotate table | Wheel/Pinch: zoom", 15, height - 40,
                          SDL_Color{180, 180, 180, 255});
            } else if (phase_ == GamePhase::Charging) {
                const int pct = static_cast<int>(chargeAmount_ / kMaxPower * 100.0f);
                printText("Power: " + std::to_string(pct) + "%", 15, 105,
                          SDL_Color{255, static_cast<Uint8>(std::max(0, 255 - pct * 2)), 0, 255});
            } else if (phase_ == GamePhase::Placing) {
                printText("Mouse move: place cue by camera direction | Click/Enter/A/B: place", 15, height - 40,
                          SDL_Color{255, 100, 100, 255});
            }
        }

        void handleGameState(float dt) {
            switch (phase_) {
            case GamePhase::Aiming:
                handleAiming(dt);
                break;
            case GamePhase::Charging:
                handleCharging(dt);
                break;
            case GamePhase::Rolling:
                updatePhysics(dt);
                if (!anyBallMoving()) {
                    if (balls_[0].pocketed) {
                        phase_ = GamePhase::Placing;
                        balls_[0].active = true;
                        balls_[0].pocketed = false;
                        balls_[0].pos = glm::vec2{-kTableHalfW * 0.5f, 0.0f};
                        balls_[0].vel = glm::vec2{0.0f};
                    } else {
                        phase_ = GamePhase::Aiming;
                    }
                    checkGameOver();
                }
                break;
            case GamePhase::Placing:
                handlePlacing(dt);
                break;
            case GamePhase::GameOver:
                break;
            }

            for (auto &ball : balls_) {
                if (ball.active && ball.isMoving()) {
                    ball.spinAngle += glm::length(ball.vel) * 5.0f * dt;
                }
            }

            for (auto &anim : sinkAnims_) {
                anim.timer += dt;
            }
            sinkAnims_.erase(
                std::remove_if(sinkAnims_.begin(), sinkAnims_.end(), [](const SinkAnim &anim) {
                    return anim.timer >= kSinkDuration;
                }),
                sinkAnims_.end());
        }

        void handleCameraAndInput(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_A]) {
                camAngle_ -= 1.2f * dt;
            }
            if (keys[SDL_SCANCODE_S]) {
                camAngle_ += 1.2f * dt;
            }
            if (keys[SDL_SCANCODE_W]) {
                camZoom_ -= 5.0f * dt;
            }
            if (keys[SDL_SCANCODE_E]) {
                camZoom_ += 5.0f * dt;
            }
            camZoom_ = glm::clamp(camZoom_, 5.0f, 25.0f);

            if (gamepad_ == nullptr) {
                return;
            }

            constexpr float dead = 0.15f;
            const auto readAxis = [this](SDL_GamepadAxis axis) {
                return static_cast<float>(SDL_GetGamepadAxis(gamepad_, axis)) / 32767.0f;
            };

            const float rx = readAxis(SDL_GAMEPAD_AXIS_RIGHTX);
            const float ry = readAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            if (std::fabs(rx) > dead) {
                camAngle_ += rx * 1.5f * dt;
            }
            if (std::fabs(ry) > dead) {
                camZoom_ += ry * 5.0f * dt;
            }
            camZoom_ = glm::clamp(camZoom_, 5.0f, 25.0f);

            const float lx = readAxis(SDL_GAMEPAD_AXIS_LEFTX);
            const float ly = readAxis(SDL_GAMEPAD_AXIS_LEFTY);
            if (phase_ == GamePhase::Aiming || phase_ == GamePhase::Charging) {
                if (std::fabs(lx) > dead) {
                    cueAngle_ += lx * 1.5f * dt;
                }
            } else if (phase_ == GamePhase::Placing) {
                const float s = 3.0f * dt;
                const glm::vec2 camRight{std::cos(camAngle_), -std::sin(camAngle_)};
                const glm::vec2 camFwd{-std::sin(camAngle_), -std::cos(camAngle_)};
                if (std::fabs(lx) > dead) {
                    balls_[0].pos += camRight * (lx * s);
                }
                if (std::fabs(ly) > dead) {
                    balls_[0].pos -= camFwd * (ly * s);
                }
                clampCueBall();
            }
        }

        void handleGamepadButtonDown(Uint8 button) {
            if (screen_ == GameScreen::Intro) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    setScreen(GameScreen::Start);
                }
                return;
            }

            if (screen_ == GameScreen::Scores) {
                if (!enteringName_ && (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_START)) {
                    setScreen(GameScreen::Intro);
                }
                return;
            }

            if (screen_ == GameScreen::Start) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    resetGame();
                    setScreen(GameScreen::Game);
                } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
                    setScreen(GameScreen::Scores);
                } else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    exit();
                }
                return;
            }

            if (screen_ != GameScreen::Game) {
                return;
            }

            if (button == SDL_GAMEPAD_BUTTON_BACK) {
                exit();
                return;
            }

            if ((button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST) && phase_ == GamePhase::Aiming) {
                phase_ = GamePhase::Charging;
                chargeAmount_ = 0.0f;
                ctrlChargeButton_ = static_cast<int>(button);
            } else if ((button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST) && phase_ == GamePhase::Placing && canPlaceCueBall()) {
                phase_ = GamePhase::Aiming;
            }
        }

        void handleGamepadButtonUp(Uint8 button) {
            if (screen_ == GameScreen::Game && phase_ == GamePhase::Charging && static_cast<int>(button) == ctrlChargeButton_) {
                shootCueBall();
                ctrlChargeButton_ = -1;
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

        void closeGamepad() {
            if (gamepad_ != nullptr) {
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
            }
            gamepadId_ = 0;
        }

        void resetGame() {
            rackBalls();
            sinkAnims_.clear();
            phase_ = GamePhase::Aiming;
            cueAngle_ = 0.0f;
            chargeAmount_ = 0.0f;
            shotCount_ = 0;
            pointerDown_ = false;
            pointerCharging_ = false;
            activePointerId_ = std::numeric_limits<int64_t>::min();
            mouseCamDragging_ = false;
            touchPinchActive_ = false;
            touchPinchDistance_ = 0.0f;
            touchPoints_.clear();
            lastTime_ = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        }

        void rackBalls() {
            balls_[0] = {};
            balls_[0].pos = glm::vec2{-kTableHalfW * 0.5f, 0.0f};
            balls_[0].active = true;
            balls_[0].number = 0;

            const int order[15] = {1, 2, 3, 8, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
            const float sx = kTableHalfW * 0.3f;
            const float sp = kBallRadius * 2.1f;
            int idx = 0;

            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col <= row; ++col) {
                    if (idx >= 15) {
                        break;
                    }
                    const int bn = order[idx++];
                    balls_[bn] = {};
                    balls_[bn].pos = glm::vec2{sx + row * sp * 0.866f, (col - row * 0.5f) * sp};
                    balls_[bn].active = true;
                    balls_[bn].number = bn;
                    balls_[bn].spinAngle = randFloat(0.0f, 2.0f * kPi);
                }
            }
        }

        void handleAiming(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT]) {
                cueAngle_ -= 1.5f * dt;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                cueAngle_ += 1.5f * dt;
            }
        }

        void handleCharging(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT]) {
                cueAngle_ -= 1.5f * dt;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                cueAngle_ += 1.5f * dt;
            }
            chargeAmount_ += kMaxPower * 0.8f * dt;
            if (chargeAmount_ > kMaxPower) {
                chargeAmount_ = kMaxPower;
            }
        }

        void handlePlacing(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            const float s = 3.0f * dt;
            const glm::vec2 camRight{std::cos(camAngle_), -std::sin(camAngle_)};
            const glm::vec2 camFwd{-std::sin(camAngle_), -std::cos(camAngle_)};

            if (keys[SDL_SCANCODE_LEFT]) {
                balls_[0].pos -= camRight * s;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                balls_[0].pos += camRight * s;
            }
            if (keys[SDL_SCANCODE_UP]) {
                balls_[0].pos += camFwd * s;
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                balls_[0].pos -= camFwd * s;
            }

            clampCueBall();
        }

        void clampCueBall() {
            const float m = kBallRadius + 0.1f;
            balls_[0].pos.x = glm::clamp(balls_[0].pos.x, -kTableHalfW + m, kTableHalfW - m);
            balls_[0].pos.y = glm::clamp(balls_[0].pos.y, -kTableHalfH + m, kTableHalfH - m);
        }

        void updatePhysics(float dt) {
            float maxSpeed = 0.0f;
            for (const auto &ball : balls_) {
                if (ball.active && !ball.pocketed) {
                    maxSpeed = std::max(maxSpeed, glm::length(ball.vel));
                }
            }

            const float maxMovePerStep = kBallRadius * 0.75f;
            const int steps = std::max(8, static_cast<int>(std::ceil(maxSpeed * dt * 60.0f / maxMovePerStep)));
            const float sub = dt / static_cast<float>(steps);
            const float stepFriction = std::pow(kFriction, 4.0f / static_cast<float>(steps));

            for (int s = 0; s < steps; ++s) {
                for (auto &ball : balls_) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    ball.pos += ball.vel * sub * 60.0f;
                }

                for (int i = 0; i < kNumBalls; ++i) {
                    if (!balls_[i].active || balls_[i].pocketed) {
                        continue;
                    }
                    for (int j = i + 1; j < kNumBalls; ++j) {
                        if (!balls_[j].active || balls_[j].pocketed) {
                            continue;
                        }
                        resolveBall(balls_[i], balls_[j]);
                    }
                }

                for (auto &ball : balls_) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    resolveWall(ball);
                }

                for (auto &ball : balls_) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    checkPocket(ball);
                }

                for (auto &ball : balls_) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    ball.vel *= stepFriction;
                    if (glm::length(ball.vel) < kMinSpeed) {
                        ball.vel = glm::vec2{0.0f};
                    }
                }
            }
        }

        void resolveBall(PoolBall &a, PoolBall &b) {
            const glm::vec2 d = b.pos - a.pos;
            const float dist = glm::length(d);
            const float minD = kBallRadius * 2.0f;
            if (dist >= minD || dist <= 0.0001f) {
                return;
            }

            const glm::vec2 n = d / dist;
            const float overlap = minD - dist;
            a.pos -= n * (overlap * 0.5f);
            b.pos += n * (overlap * 0.5f);

            const float rv = glm::dot(a.vel - b.vel, n);
            if (rv > 0.0f) {
                a.vel -= n * rv;
                b.vel += n * rv;
            }
        }

        void resolveWall(PoolBall &ball) {
            const float l = -kTableHalfW + kBallRadius;
            const float r = kTableHalfW - kBallRadius;
            const float t = -kTableHalfH + kBallRadius;
            const float b = kTableHalfH - kBallRadius;

            if (ball.pos.x < l) {
                ball.pos.x = l;
                ball.vel.x = -ball.vel.x * 0.8f;
            }
            if (ball.pos.x > r) {
                ball.pos.x = r;
                ball.vel.x = -ball.vel.x * 0.8f;
            }
            if (ball.pos.y < t) {
                ball.pos.y = t;
                ball.vel.y = -ball.vel.y * 0.8f;
            }
            if (ball.pos.y > b) {
                ball.pos.y = b;
                ball.vel.y = -ball.vel.y * 0.8f;
            }
        }

        void checkPocket(PoolBall &ball) {
            for (const auto &pocket : kPockets) {
                if (glm::length(ball.pos - pocket) < kPocketR) {
                    const int idx = static_cast<int>(&ball - &balls_[0]);
                    sinkAnims_.push_back(SinkAnim{pocket,
                                                  kBallColors[static_cast<std::size_t>(idx)],
                                                  ball.spinAngle,
                                                  0.0f,
                                                  idx});
                    ball.pocketed = true;
                    ball.vel = glm::vec2{0.0f};
                    return;
                }
            }
        }

        [[nodiscard]] bool anyBallMoving() const {
            for (const auto &ball : balls_) {
                if (ball.active && !ball.pocketed && ball.isMoving()) {
                    return true;
                }
            }
            return !sinkAnims_.empty();
        }

        [[nodiscard]] bool allObjectBallsPocketed() const {
            for (int i = 1; i < kNumBalls; ++i) {
                if (balls_[i].active && !balls_[i].pocketed) {
                    return false;
                }
            }
            return true;
        }

        void checkGameOver() {
            if (!allObjectBallsPocketed()) {
                return;
            }
            finalScore_ = shotCount_;
            playerName_.clear();
            enteringName_ = highScores_.qualifies(finalScore_);
            if (enteringName_) {
                SDL_StartTextInput(window.get());
            }
            setScreen(GameScreen::Scores);
        }

        void commitScoreEntry() {
            if (playerName_.empty()) {
                return;
            }
            highScores_.addScore(playerName_, finalScore_);
            highScores_.write();
            enteringName_ = false;
            SDL_StopTextInput(window.get());
        }

        [[nodiscard]] glm::vec3 getCameraPosition() const {
            const float orbitDist = camZoom_ * kCamBaseTotalScale;
            const float horiz = orbitDist * std::cos(camPitch_);
            const float y = orbitDist * std::sin(camPitch_);
            return glm::vec3{std::sin(camAngle_) * horiz, y, std::cos(camAngle_) * horiz};
        }

        bool screenPointToTable(int px, int py, glm::vec2 &out) const {
            if (fallbackWidth_ <= 0 || fallbackHeight_ <= 0) {
                return false;
            }

            const float aspect = static_cast<float>(fallbackWidth_) / static_cast<float>(fallbackHeight_);
            const glm::vec3 camPos = getCameraPosition();
            const glm::mat4 view = glm::lookAt(camPos, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            const float nx = (2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(fallbackWidth_)) - 1.0f;
            const float ny = 1.0f - (2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(fallbackHeight_));
            const glm::vec4 nearClip{nx, ny, 0.0f, 1.0f};
            const glm::vec4 farClip{nx, ny, 1.0f, 1.0f};
            const glm::mat4 invVP = glm::inverse(proj * view);

            glm::vec4 nearWorld = invVP * nearClip;
            glm::vec4 farWorld = invVP * farClip;
            if (std::fabs(nearWorld.w) < 1e-6f || std::fabs(farWorld.w) < 1e-6f) {
                return false;
            }

            nearWorld /= nearWorld.w;
            farWorld /= farWorld.w;

            const glm::vec3 ro{nearWorld.x, nearWorld.y, nearWorld.z};
            const glm::vec3 rf{farWorld.x, farWorld.y, farWorld.z};
            const glm::vec3 rd = glm::normalize(rf - ro);
            if (std::fabs(rd.y) < 1e-6f) {
                return false;
            }

            const float t = -ro.y / rd.y;
            if (t < 0.0f) {
                return false;
            }

            const glm::vec3 hit = ro + rd * t;
            out = glm::vec2{hit.x, hit.z};
            return true;
        }

        void updateCueFromPointer(int px, int py) {
            if (screen_ != GameScreen::Game || !balls_[0].active || balls_[0].pocketed) {
                return;
            }

            glm::vec2 tablePos{0.0f};
            if (!screenPointToTable(px, py, tablePos)) {
                return;
            }

            const glm::vec2 d = tablePos - balls_[0].pos;
            if (glm::dot(d, d) < 0.00001f) {
                return;
            }

            const float worldAngle = std::atan2(d.y, d.x);
            cueAngle_ = worldAngle - camAngle_;
        }

        void onMouseRelativeMove(int dx, int dy) {
            if (screen_ != GameScreen::Game) {
                return;
            }
            if (phase_ == GamePhase::Aiming || phase_ == GamePhase::Charging) {
                cueAngle_ += static_cast<float>(dx) * 0.012f;
                return;
            }
            if (phase_ == GamePhase::Placing) {
                constexpr float kMoveScale = 0.02f;
                const glm::vec2 camRight{std::cos(camAngle_), -std::sin(camAngle_)};
                const glm::vec2 camFwd{-std::sin(camAngle_), -std::cos(camAngle_)};
                balls_[0].pos += camRight * (static_cast<float>(dx) * kMoveScale);
                balls_[0].pos -= camFwd * (static_cast<float>(dy) * kMoveScale);
                clampCueBall();
            }
        }

        void placeCueBallFromPointer(int px, int py) {
            if (phase_ != GamePhase::Placing || !balls_[0].active || balls_[0].pocketed) {
                return;
            }

            glm::vec2 tablePos{0.0f};
            if (!screenPointToTable(px, py, tablePos)) {
                return;
            }

            balls_[0].pos = tablePos;
            clampCueBall();
        }

        [[nodiscard]] bool canPlaceCueBall() const {
            for (int i = 1; i < kNumBalls; ++i) {
                if (!balls_[i].active || balls_[i].pocketed) {
                    continue;
                }
                if (glm::length(balls_[0].pos - balls_[i].pos) < kBallRadius * 2.5f) {
                    return false;
                }
            }
            return true;
        }

        void shootCueBall() {
            const float worldCueAngle = cueAngle_ + camAngle_;
            const glm::vec2 dir{std::cos(worldCueAngle), std::sin(worldCueAngle)};
            balls_[0].vel = dir * chargeAmount_;
            phase_ = GamePhase::Rolling;
            ++shotCount_;
            chargeAmount_ = 0.0f;
            pointerCharging_ = false;
            pointerDown_ = false;
            activePointerId_ = std::numeric_limits<int64_t>::min();
        }

        SDL_Rect makeTextRect(const std::string &txt, int x, int y, int pad) {
            int tw = 0;
            int th = 0;
            (void)getTextDimensions(txt, tw, th);
            SDL_Rect r{};
            r.x = x - pad;
            r.y = y - pad;
            r.w = tw + pad * 2;
            r.h = th + pad * 2;
            return r;
        }

        SDL_Rect makeNormRect(float cxN, float cyN, float wN, float hN) const {
            SDL_Rect r{};
            r.w = std::max(1, static_cast<int>(fallbackWidth_ * wN));
            r.h = std::max(1, static_cast<int>(fallbackHeight_ * hN));
            const int cx = static_cast<int>(fallbackWidth_ * cxN);
            const int cy = static_cast<int>(fallbackHeight_ * cyN);
            r.x = cx - r.w / 2;
            r.y = cy - r.h / 2;
            return r;
        }

        void updateStartClickTargets() {
            startPlayRect_ = makeNormRect(0.50f, 0.78f, 0.28f, 0.11f);
        }

        static bool pointInRect(int x, int y, const SDL_Rect &r) {
            return x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h);
        }

        void onPointerDown(int px, int py, int64_t pointerId) {
            if (screen_ == GameScreen::Intro) {
                setScreen(GameScreen::Start);
                return;
            }

            if (screen_ == GameScreen::Start) {
                updateStartClickTargets();
                if (pointInRect(px, py, startPlayRect_)) {
                    resetGame();
                    setScreen(GameScreen::Game);
                }
                return;
            }

            if (screen_ == GameScreen::Scores) {
                if (!enteringName_) {
                    if (pointInRect(px, py, scoresReturnRect_)) {
                        setScreen(GameScreen::Intro);
                    }
                    return;
                }
                if (pointInRect(px, py, scoresConfirmRect_) && !playerName_.empty()) {
                    commitScoreEntry();
                    return;
                }
                if (pointInRect(px, py, scoresDeleteRect_) && !playerName_.empty()) {
                    playerName_.pop_back();
                }
                return;
            }

            if (screen_ != GameScreen::Game) {
                return;
            }
            if (pointerId == -1 && !mouseCaptured_) {
                return;
            }
            if (activePointerId_ != std::numeric_limits<int64_t>::min() && activePointerId_ != pointerId) {
                return;
            }

            pointerDown_ = true;
            activePointerId_ = pointerId;
            const bool isCapturedMousePointer = (pointerId == -1 && mouseCaptured_);
            if (phase_ == GamePhase::Aiming) {
                // Keep the current cue direction when mouse press starts charging.
                // Relative mouse motion updates the cue after the player moves.
                if (!isCapturedMousePointer) {
                    updateCueFromPointer(px, py);
                }
                phase_ = GamePhase::Charging;
                chargeAmount_ = 0.0f;
                pointerCharging_ = true;
            } else if (phase_ == GamePhase::Charging) {
                if (!isCapturedMousePointer) {
                    updateCueFromPointer(px, py);
                }
                pointerCharging_ = true;
            } else if (phase_ == GamePhase::Placing) {
                if (!(pointerId == -1 && mouseCaptured_)) {
                    placeCueBallFromPointer(px, py);
                }
            }
        }

        void onPointerMove(int px, int py, int64_t pointerId) {
            if (screen_ != GameScreen::Game) {
                return;
            }
            if (activePointerId_ != std::numeric_limits<int64_t>::min() && activePointerId_ != pointerId) {
                return;
            }

            if (phase_ == GamePhase::Aiming || phase_ == GamePhase::Charging) {
                updateCueFromPointer(px, py);
            } else if (phase_ == GamePhase::Placing && (pointerId != -1 || !mouseCaptured_)) {
                placeCueBallFromPointer(px, py);
            }
        }

        void onPointerUp(int px, int py, int64_t pointerId) {
            if (screen_ == GameScreen::Start || screen_ == GameScreen::Scores) {
                onPointerDown(px, py, pointerId);
                return;
            }

            if (screen_ != GameScreen::Game) {
                return;
            }
            if (activePointerId_ != pointerId) {
                return;
            }

            pointerDown_ = false;
            if (phase_ == GamePhase::Charging && pointerCharging_) {
                shootCueBall();
                return;
            }

            if (phase_ == GamePhase::Placing) {
                if (!(pointerId == -1 && mouseCaptured_)) {
                    placeCueBallFromPointer(px, py);
                }
                if (canPlaceCueBall()) {
                    phase_ = GamePhase::Aiming;
                }
            }

            pointerCharging_ = false;
            activePointerId_ = std::numeric_limits<int64_t>::min();
        }

        std::string assetRoot_;
        HighScores highScores_;

        mxvk::VK_Sprite *backgroundSprite_ = nullptr;
        mxvk::VK_Sprite *startSprite_ = nullptr;
        mxvk::VK_Sprite *introSprite_ = nullptr;
        mxvk::VK_Sprite *scoresSprite_ = nullptr;

        mxvk::VKAbstractModel feltModel_{};
        mxvk::VKAbstractModel woodModel_{};
        mxvk::VKAbstractModel pocketModel_{};
        std::array<mxvk::VKAbstractModel, kNumBalls> ballModels_{};
        mxvk::VKAbstractModel cueStickModel_{};
        mxvk::VKAbstractModel cueAimModel_{};

        std::array<PoolBall, kNumBalls> balls_{};
        std::vector<SinkAnim> sinkAnims_{};

        GameScreen screen_ = GameScreen::Intro;
        GamePhase phase_ = GamePhase::Aiming;

        float cueAngle_ = 0.0f;
        float chargeAmount_ = 0.0f;
        int shotCount_ = 0;
        float lastTime_ = 0.0f;
        float camAngle_ = 0.4f;
        float camPitch_ = 0.744604f;
        float camZoom_ = 13.0f;

        SDL_Gamepad *gamepad_ = nullptr;
        SDL_JoystickID gamepadId_ = 0;
        int ctrlChargeButton_ = -1;

        bool enteringName_ = false;
        std::string playerName_;
        int finalScore_ = 0;
        int scoreFontSize_ = 0;

        bool pointerDown_ = false;
        bool pointerCharging_ = false;
        int64_t activePointerId_ = std::numeric_limits<int64_t>::min();
        bool consumeNextMouseLeftUp_ = false;
        bool mouseCaptured_ = false;
        bool mouseCamDragging_ = false;
        int mouseCamLastX_ = 0;
        int mouseCamLastY_ = 0;
        bool touchPinchActive_ = false;
        float touchPinchDistance_ = 0.0f;
        std::unordered_map<int64_t, SDL_FPoint> touchPoints_;

        Uint64 startTicks_ = 0U;

        int fallbackWidth_ = 1280;
        int fallbackHeight_ = 720;

        SDL_Rect startPlayRect_{0, 0, 0, 0};
        SDL_Rect scoresReturnRect_{0, 0, 0, 0};
        SDL_Rect scoresConfirmRect_{0, 0, 0, 0};
        SDL_Rect scoresDeleteRect_{0, 0, 0, 0};
    };

} // namespace example

int main(int argc, char **argv) {    
    try {
        Arguments args = proc_args(argc, argv);
        std::string assets = args.path.empty() ? POOL_DEMO_ASSET_DIR : args.path;
        demo::PoolWindow window(args.width, args.height, args.fullscreen, assets);
        window.loop();
    } catch (const mxvk::Exception &e) {
        SDL_Log("mxvk: Exception: %s", e.text().c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}