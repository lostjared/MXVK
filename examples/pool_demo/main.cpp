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

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#ifndef POOL_DEMO_ASSET_DIR
#define POOL_DEMO_ASSET_DIR "."
#endif

namespace {

    constexpr float TABLE_W = 12.0f;
    constexpr float TABLE_H = 6.0f;
    constexpr float TABLE_HALF_W = TABLE_W / 2.0f;
    constexpr float TABLE_HALF_H = TABLE_H / 2.0f;
    constexpr float POCKET_R = 0.25f;
    constexpr float BALL_RADIUS = 0.18f;
    constexpr float CUE_LENGTH = 2.5f;
    constexpr float CUE_THICKNESS = 0.03f;
    constexpr float AIM_LENGTH = 2.0f;
    constexpr float AIM_THICKNESS_Y = 0.008f;
    constexpr float AIM_THICKNESS_Z = 0.015f;
    constexpr int NUM_BALLS = 16;
    constexpr float FRICTION = 0.9988f;
    constexpr float MIN_SPEED = 0.001f;
    constexpr float MAX_POWER = 1.5f;
    constexpr float CAM_BASE_TOTAL_SCALE = 1.359411f;
    constexpr float SINK_DURATION = 0.45f;
    constexpr float PI = 3.14159265358979323846f;

    constexpr float POCKET_INSET = 0.25f;
    const std::array<glm::vec2, 6> POCKETS = {
        glm::vec2{-TABLE_HALF_W + POCKET_INSET, TABLE_HALF_H - POCKET_INSET},
        glm::vec2{0.0f, TABLE_HALF_H - 0.15f},
        glm::vec2{TABLE_HALF_W - POCKET_INSET, TABLE_HALF_H - POCKET_INSET},
        glm::vec2{-TABLE_HALF_W + POCKET_INSET, -TABLE_HALF_H + POCKET_INSET},
        glm::vec2{0.0f, -TABLE_HALF_H + 0.15f},
        glm::vec2{TABLE_HALF_W - POCKET_INSET, -TABLE_HALF_H + POCKET_INSET},
    };

    const std::array<glm::vec3, NUM_BALLS> BALL_COLORS = {
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

    bool hasPoolAssets(const std::filesystem::path &root) {
        const std::filesystem::path data = root / "data";
        return std::filesystem::exists(data / "pooltable_felt.mxmod.z") &&
               std::filesystem::exists(data / "pooltable_wood.mxmod.z") &&
               std::filesystem::exists(data / "pooltable_pocket.mxmod.z") &&
               std::filesystem::exists(data / "table.png");
    }

    std::string resolveAssetRoot(const std::string &userPath) {
        std::vector<std::filesystem::path> candidates;
        if (!userPath.empty() && userPath != "." && userPath != "./") {
            candidates.emplace_back(userPath);
        }

        const char *basePath = SDL_GetBasePath();
        if (basePath != nullptr && basePath[0] != '\0') {
            candidates.emplace_back(std::filesystem::path(basePath).lexically_normal());
        }

        candidates.emplace_back(std::filesystem::path(POOL_DEMO_ASSET_DIR).lexically_normal());

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            candidates.emplace_back(cwd);
        }

        for (const auto &candidate : candidates) {
            if (hasPoolAssets(candidate)) {
                return candidate.lexically_normal().string();
            }
        }

        if (!candidates.empty()) {
            return candidates.front().lexically_normal().string();
        }

        return std::string(POOL_DEMO_ASSET_DIR);
    }

    enum class GameScreen {
        Intro,
        Scores,
        Game,
        Start
    };
    enum class GamePhase {
        Aiming,
        Charging,
        Rolling,
        Placing,
        GameOver
    };

    struct PoolBall {
        glm::vec2 pos{0.0f};
        glm::vec2 vel{0.0f};
        bool active = true;
        bool pocketed = false;
        int number = 0;
        float spinAngle = 0.0f;

        bool isMoving() const {
            return glm::length(vel) > MIN_SPEED;
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
            : filePath(std::move(filePath)) {
            read();
        }

        ~HighScores() {
            write();
        }

        void addScore(const std::string &name, int shots) {
            entries.push_back({name, shots});
            sort();
            if (entries.size() > 10) {
                entries.resize(10);
            }
        }

        [[nodiscard]] bool qualifies(int shots) const {
            if (entries.size() < 10) {
                return true;
            }
            return shots < entries.back().shots;
        }

        [[nodiscard]] const std::vector<Score> &list() const {
            return entries;
        }

        void write() const {
            std::ofstream out(filePath);
            if (!out.is_open()) {
                return;
            }
            for (const auto &entry : entries) {
                out << entry.name << ':' << entry.shots << '\n';
            }
        }

      private:
        void sort() {
            std::sort(entries.begin(), entries.end(), [](const Score &a, const Score &b) {
                return a.shots < b.shots;
            });
        }

        void initDefaults() {
            entries.clear();
            for (int i = 1; i <= 10; ++i) {
                entries.push_back({"Anonymous", i * 20});
            }
        }

        void read() {
            std::ifstream in(filePath);
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
                entries.push_back({name, shots});
                ++count;
            }

            if (entries.empty()) {
                initDefaults();
            }
            sort();
        }

        std::string filePath;
        std::vector<Score> entries;
    };

} // namespace

namespace demo {

    class PoolWindow final : public mxvk::VK_Window {
      public:
        PoolWindow(int width, int height, bool fullscreen, bool enable_vsync, std::string asset_root)
            : mxvk::VK_Window("3D Pool / MXVK", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              assetRoot(std::move(asset_root)),
              highScores((std::filesystem::path(assetRoot) / "pool_scores.dat").string()),
              fallbackWidth(width),
              fallbackHeight(height) {
            setFont(assetRoot + "/font.ttf", 24);
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
            const int renderW = (extent.width > 0U) ? static_cast<int>(extent.width) : fallbackWidth;
            const int renderH = (extent.height > 0U) ? static_cast<int>(extent.height) : fallbackHeight;
            fallbackWidth = renderW;
            fallbackHeight = renderH;

            switch (screen) {
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
                if (gamepad != nullptr && e.gdevice.which == gamepadId) {
                    closeGamepad();
                    tryOpenFirstGamepad();
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    if (screen == GameScreen::Game && mouseCaptured) {
                        setMouseCapture(false);
                        return;
                    }
                    if (screen == GameScreen::Scores) {
                        setScreen(GameScreen::Intro);
                    } else {
                        exit();
                    }
                    return;
                }

                if (screen == GameScreen::Scores) {
                    if (enteringName) {
                        if (e.key.key == SDLK_RETURN && !playerName.empty()) {
                            commitScoreEntry();
                        } else if (e.key.key == SDLK_BACKSPACE && !playerName.empty()) {
                            playerName.pop_back();
                        }
                    } else if (e.key.key == SDLK_RETURN) {
                        setScreen(GameScreen::Intro);
                    }
                    return;
                }

                if (screen == GameScreen::Start) {
                    if (e.key.key == SDLK_RETURN) {
                        resetGame();
                        setScreen(GameScreen::Game);
                    } else if (e.key.key == SDLK_SPACE) {
                        setScreen(GameScreen::Scores);
                    }
                    return;
                }

                if (screen == GameScreen::Game) {
                    if (e.key.key == SDLK_R) {
                        resetGame();
                    } else if (e.key.key == SDLK_SPACE && phase == GamePhase::Aiming) {
                        phase = GamePhase::Charging;
                        chargeAmount = 0.0f;
                    } else if (e.key.key == SDLK_RETURN && phase == GamePhase::Placing && canPlaceCueBall()) {
                        phase = GamePhase::Aiming;
                    }
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_UP && screen == GameScreen::Game && e.key.key == SDLK_SPACE && phase == GamePhase::Charging) {
                shootCueBall();
                return;
            }

            if (e.type == SDL_EVENT_TEXT_INPUT && screen == GameScreen::Scores && enteringName) {
                if (playerName.size() < 15U) {
                    playerName += e.text.text;
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL && screen == GameScreen::Game) {
                const float dy = (e.wheel.y != 0.0f) ? e.wheel.y : e.wheel.integer_y;
                camZoom -= dy * 1.2f;
                camZoom = glm::clamp(camZoom, 5.0f, 25.0f);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT && screen == GameScreen::Game) {
                if (!mouseCaptured) {
                    setMouseCapture(true);
                }
                mouseCamDragging = true;
                mouseCamLastX = static_cast<int>(e.button.x);
                mouseCamLastY = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (screen == GameScreen::Game && !mouseCaptured) {
                    setMouseCapture(true);
                    consumeNextMouseLeftUp = true;
                    return;
                }
                onPointerDown(static_cast<int>(e.button.x), static_cast<int>(e.button.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                mouseCamDragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                if (consumeNextMouseLeftUp) {
                    consumeNextMouseLeftUp = false;
                    return;
                }
                if (screen == GameScreen::Game && phase == GamePhase::Charging && pointerCharging) {
                    shootCueBall();
                    return;
                }
                onPointerUp(static_cast<int>(e.button.x), static_cast<int>(e.button.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (screen == GameScreen::Game && mouseCaptured) {
                    if (mouseCamDragging) {
                        camAngle += static_cast<float>(e.motion.xrel) * 0.01f;
                        camPitch -= static_cast<float>(e.motion.yrel) * 0.006f;
                        camPitch = glm::clamp(camPitch, 0.30f, 1.25f);
                        return;
                    }
                    onMouseRelativeMove(e.motion.xrel, e.motion.yrel);
                    return;
                }
                if (mouseCamDragging && screen == GameScreen::Game) {
                    const int nx = static_cast<int>(e.motion.x);
                    const int ny = static_cast<int>(e.motion.y);
                    const int dx = nx - mouseCamLastX;
                    const int dy = ny - mouseCamLastY;
                    camAngle += static_cast<float>(dx) * 0.01f;
                    camPitch -= static_cast<float>(dy) * 0.006f;
                    camPitch = glm::clamp(camPitch, 0.30f, 1.25f);
                    mouseCamLastX = nx;
                    mouseCamLastY = ny;
                    return;
                }
                onPointerMove(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y), -1);
                return;
            }

            if (e.type == SDL_EVENT_FINGER_DOWN) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight));
                touchPoints[static_cast<int64_t>(e.tfinger.fingerID)] = SDL_FPoint{static_cast<float>(px), static_cast<float>(py)};
                if (screen == GameScreen::Game && touchPoints.size() == 2U) {
                    auto it = touchPoints.begin();
                    const SDL_FPoint a = it->second;
                    ++it;
                    const SDL_FPoint b = it->second;
                    const float dx = a.x - b.x;
                    const float dy = a.y - b.y;
                    touchPinchDistance = std::sqrt(dx * dx + dy * dy);
                    touchPinchActive = true;
                    if (phase == GamePhase::Charging) {
                        phase = GamePhase::Aiming;
                        chargeAmount = 0.0f;
                    }
                    pointerCharging = false;
                    pointerDown = false;
                    activePointerId = std::numeric_limits<int64_t>::min();
                    return;
                }
                onPointerDown(px, py, static_cast<int64_t>(e.tfinger.fingerID));
                return;
            }

            if (e.type == SDL_EVENT_FINGER_MOTION) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight));
                touchPoints[static_cast<int64_t>(e.tfinger.fingerID)] = SDL_FPoint{static_cast<float>(px), static_cast<float>(py)};
                if (screen == GameScreen::Game && touchPinchActive && touchPoints.size() >= 2U) {
                    auto it = touchPoints.begin();
                    const SDL_FPoint a = it->second;
                    ++it;
                    const SDL_FPoint b = it->second;
                    const float dx = a.x - b.x;
                    const float dy = a.y - b.y;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float delta = dist - touchPinchDistance;
                    touchPinchDistance = dist;
                    camZoom -= delta * 0.02f;
                    camZoom = glm::clamp(camZoom, 5.0f, 25.0f);
                    return;
                }
                onPointerMove(px, py, static_cast<int64_t>(e.tfinger.fingerID));
                return;
            }

            if (e.type == SDL_EVENT_FINGER_UP) {
                const int px = static_cast<int>(e.tfinger.x * static_cast<float>(fallbackWidth));
                const int py = static_cast<int>(e.tfinger.y * static_cast<float>(fallbackHeight));
                const bool wasPinching = touchPinchActive;
                touchPoints.erase(static_cast<int64_t>(e.tfinger.fingerID));
                if (touchPinchActive && touchPoints.size() < 2U) {
                    touchPinchActive = false;
                    touchPinchDistance = 0.0f;
                }
                if (wasPinching) {
                    pointerCharging = false;
                    pointerDown = false;
                    activePointerId = std::numeric_limits<int64_t>::min();
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
            feltModel.resize(this);
            woodModel.resize(this);
            pocketModel.resize(this);
            for (auto &ballModel : ballModels) {
                ballModel.resize(this);
            }
            cueStickModel.resize(this);
            cueAimModel.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (screen != GameScreen::Game) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            if (backgroundSprite != nullptr) {
                backgroundSprite->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                backgroundSprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
                backgroundSprite->renderSprites(cmd,
                                                backgroundSprite->getPipelineLayout(),
                                                extent.width,
                                                extent.height);
                backgroundSprite->clearQueue();
            }

            const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            const float time = static_cast<float>(SDL_GetTicks()) / 1000.0f;

            const glm::vec3 camPos = getCameraPosition();
            const glm::mat4 view = glm::lookAt(camPos, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            drawModel(feltModel, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, time);
            drawModel(woodModel, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{0.4f, 0.22f, 0.05f, 1.0f}, time);
            drawModel(pocketModel, imageIndex, cmd, view, proj,
                      composeTransform(glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec4{0.08f, 0.08f, 0.08f, 1.0f}, time);

            for (int i = 0; i < NUM_BALLS; ++i) {
                if (!balls[i].active || balls[i].pocketed) {
                    continue;
                }
                glm::mat4 m{1.0f};
                m = glm::translate(m, glm::vec3{balls[i].pos.x, BALL_RADIUS, balls[i].pos.y});
                m = glm::rotate(m, balls[i].spinAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                m = glm::scale(m, glm::vec3{BALL_RADIUS});
                drawModel(ballModels[static_cast<std::size_t>(i)],
                          imageIndex,
                          cmd,
                          view,
                          proj,
                          m,
                          glm::vec4{BALL_COLORS[static_cast<std::size_t>(i)], 1.0f},
                          time);
            }

            for (const auto &anim : sinkAnims) {
                const float t = anim.timer / SINK_DURATION;
                const float y = glm::mix(BALL_RADIUS, -BALL_RADIUS * 3.5f, t);
                const float sc = BALL_RADIUS * (1.0f - t * 0.75f);
                glm::mat4 m{1.0f};
                m = glm::translate(m, glm::vec3{anim.pocketPos.x, y, anim.pocketPos.y});
                m = glm::rotate(m, anim.spinAngle + t * 6.0f, glm::vec3{0.0f, 1.0f, 0.0f});
                m = glm::scale(m, glm::vec3{sc});
                const std::size_t sinkIndex = static_cast<std::size_t>(glm::clamp(anim.ballIndex, 0, NUM_BALLS - 1));
                drawModel(ballModels[sinkIndex], imageIndex, cmd, view, proj, m, glm::vec4{anim.color, 1.0f}, time);
            }

            if ((phase == GamePhase::Aiming || phase == GamePhase::Charging) && balls[0].active) {
                const float offset = (phase == GamePhase::Charging) ? (chargeAmount / MAX_POWER) : 0.0f;
                const float stickDist = BALL_RADIUS + 0.3f + offset;
                const float worldCueAngle = cueAngle + camAngle;
                const glm::vec2 dir{std::cos(worldCueAngle), std::sin(worldCueAngle)};

                const glm::vec2 stickCenter = balls[0].pos - dir * (stickDist + CUE_LENGTH * 0.5f);
                glm::mat4 cueTransform{1.0f};
                cueTransform = glm::translate(cueTransform, glm::vec3{stickCenter.x, BALL_RADIUS + 0.05f, stickCenter.y});
                cueTransform = glm::rotate(cueTransform, -worldCueAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                cueTransform = glm::scale(cueTransform, glm::vec3{CUE_LENGTH * 0.5f, CUE_THICKNESS, CUE_THICKNESS});

                const float pct = chargeAmount / MAX_POWER;
                const glm::vec4 cueColor = (phase == GamePhase::Charging)
                                               ? glm::vec4{0.55f + pct * 0.45f, 0.27f * (1.0f - pct), 0.07f * (1.0f - pct), 1.0f}
                                               : glm::vec4{0.55f, 0.27f, 0.07f, 1.0f};
                drawModel(cueStickModel, imageIndex, cmd, view, proj, cueTransform, cueColor, time);

                const glm::vec2 aimCenter = balls[0].pos + dir * (BALL_RADIUS + AIM_LENGTH * 0.5f);
                glm::mat4 aimTransform{1.0f};
                aimTransform = glm::translate(aimTransform, glm::vec3{aimCenter.x, BALL_RADIUS + 0.06f, aimCenter.y});
                aimTransform = glm::rotate(aimTransform, -worldCueAngle, glm::vec3{0.0f, 1.0f, 0.0f});
                aimTransform = glm::scale(aimTransform, glm::vec3{AIM_LENGTH * 0.5f, AIM_THICKNESS_Y, AIM_THICKNESS_Z});
                drawModel(cueAimModel, imageIndex, cmd, view, proj, aimTransform, glm::vec4{1.0f, 1.0f, 0.0f, 1.0f}, time);
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
            backgroundSprite = createSprite(assetRoot + "/data/background.png", assetRoot + "/data/sprite_vert.spv", assetRoot + "/data/sprite_frag.spv");
            startSprite = createSprite(assetRoot + "/data/start.png", assetRoot + "/data/sprite_vert.spv", assetRoot + "/data/sprite_frag.spv");
            scoresSprite = createSprite(assetRoot + "/data/scores.png", assetRoot + "/data/sprite_vert.spv", assetRoot + "/data/sprite_frag.spv");
            introSprite = createSprite(assetRoot + "/data/logo.png", assetRoot + "/data/sprite_vert.spv", assetRoot + "/data/bend_dir.spv");
        }

        void initModels() {
            loadModel(feltModel, assetRoot + "/data/pooltable_felt.mxmod.z");
            loadModel(woodModel, assetRoot + "/data/pooltable_wood.mxmod.z");
            loadModel(pocketModel, assetRoot + "/data/pooltable_pocket.mxmod.z");

            for (auto &ballModel : ballModels) {
                loadModel(ballModel, assetRoot + "/data/geosphere.mxmod.z");
            }
            loadModel(cueStickModel, assetRoot + "/data/cube.mxmod.z", 2.0f);
            loadModel(cueAimModel, assetRoot + "/data/cube.mxmod.z", 2.0f);

            applyPrimaryTexture(feltModel, assetRoot + "/data/table.png");
        }

        void cleanupModels() {
            feltModel.cleanup(this);
            woodModel.cleanup(this);
            pocketModel.cleanup(this);
            for (auto &ballModel : ballModels) {
                ballModel.cleanup(this);
            }
            cueStickModel.cleanup(this);
            cueAimModel.cleanup(this);
        }

        void loadModel(mxvk::VKAbstractModel &model, const std::string &path, float scale = 1.0f) {
            model.load(this, path, "", assetRoot + "/data", scale);
            model.setShaders(this,
                             assetRoot + "/data/model.vert.spv",
                             assetRoot + "/data/model.frag.spv");
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
            [[maybe_unused]] const bool texture_updated = model.updatePrimaryTexture(rgba->pixels, rgba->w, rgba->h, pitch);
            SDL_DestroySurface(rgba);
        }

        void setScreen(GameScreen next) {
            if (screen == GameScreen::Scores && next != GameScreen::Scores) {
                SDL_StopTextInput(window.get());
                setFont(assetRoot + "/font.ttf", 24);
                scoreFontSize = 0;
            }
            if (screen != GameScreen::Scores && next == GameScreen::Scores && enteringName) {
                SDL_StartTextInput(window.get());
            }
            screen = next;
            if (screen == GameScreen::Game) {
                setMouseCapture(true);
            } else {
                setMouseCapture(false);
            }
        }

        void setMouseCapture(bool enabled) {
            if (mouseCaptured == enabled) {
                return;
            }
            mouseCaptured = enabled;
            [[maybe_unused]] const bool mouse_grabbed = SDL_SetWindowMouseGrab(window.get(), enabled);
            [[maybe_unused]] const bool relative_mouse_mode = SDL_SetWindowRelativeMouseMode(window.get(), enabled);
            if (enabled) {
                SDL_HideCursor();
            } else {
                SDL_ShowCursor();
            }
            mouseCamDragging = false;
        }

        void procIntro(int width, int height) {
            if (startTicks == 0U) {
                startTicks = SDL_GetTicks();
            }

            const Uint64 elapsed = SDL_GetTicks() - startTicks;
            constexpr Uint64 TOTAL = 5000;
            constexpr Uint64 FADE_DUR = 1500;

            float fadeOut = 1.0f;
            if (elapsed > (TOTAL - FADE_DUR)) {
                fadeOut = 1.0f - static_cast<float>(elapsed - (TOTAL - FADE_DUR)) / static_cast<float>(FADE_DUR);
            }

            if (startSprite != nullptr) {
                startSprite->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                startSprite->drawSpriteRect(0, 0, width, height);
            }
            if (introSprite != nullptr) {
                introSprite->setShaderParams(fadeOut, 1.0f, 1.0f, static_cast<float>(SDL_GetTicks()) / 1000.0f);
                introSprite->drawSpriteRect(0, 0, width, height);
            }

            if (elapsed >= TOTAL) {
                startTicks = 0U;
                setScreen(GameScreen::Start);
            }
        }

        void procStart(int width, int height) {
            if (startSprite != nullptr) {
                startSprite->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                startSprite->drawSpriteRect(0, 0, width, height);
            }

            const char *hint = "ENTER / A - Play";
            int tw = 0;
            int th = 0;
            [[maybe_unused]] const bool hint_dims = getTextDimensions(hint, tw, th);
            const int x = width / 2 - tw / 2;
            const int y = height - (th * 3) + 20;
            printText(hint, x, y, SDL_Color{220, 220, 100, 255});
            updateStartClickTargets();
        }

        void procScores(int width, int height) {
            if (scoresSprite != nullptr) {
                scoresSprite->setShaderParams(1.0f, 1.0f, 1.0f, 1.0f);
                scoresSprite->drawSpriteRect(0, 0, width, height);
            }

            const int feltL = static_cast<int>(width * 0.125f);
            const int feltT = static_cast<int>(height * 0.19f);
            const int feltB = static_cast<int>(height * 0.87f);
            const int feltH = feltB - feltT;
            const int feltCX = static_cast<int>(width * 0.48f);
            const int fs = std::max(10, feltH / 15);

            if (fs != scoreFontSize) {
                setFont(assetRoot + "/font.ttf", fs);
                scoreFontSize = fs;
            }

            const int lineH = fs + fs / 3;
            const auto &list = highScores.list();
            for (std::size_t i = 0; i < list.size() && i < 10U; ++i) {
                std::ostringstream ss;
                ss << (i + 1U) << ". " << list[i].name << "  " << list[i].shots << " shots";
                SDL_Color color{255, 255, 255, 255};
                printText(ss.str(), feltL + fs / 2, feltT + static_cast<int>(i) * lineH, color);
            }

            if (enteringName) {
                const int entryY = feltT + 10 * lineH + lineH / 2;
                std::ostringstream ys;
                ys << "Your score: " << finalScore << " shots";

                int tw = 0;
                int th = 0;
                [[maybe_unused]] const bool score_dims = getTextDimensions(ys.str(), tw, th);
                printText(ys.str(), feltCX - tw / 2, entryY, SDL_Color{255, 255, 0, 255});

                const std::string dn = playerName + "_";
                [[maybe_unused]] const bool name_dims = getTextDimensions(dn, tw, th);
                printText(dn, feltCX - tw / 2, entryY + lineH, SDL_Color{0, 255, 255, 255});

                const std::string confirm = "ENTER to confirm";
                const std::string del = "BACKSPACE to delete";
                int cw = 0;
                int ch = 0;
                [[maybe_unused]] const bool confirm_dims = getTextDimensions(confirm, cw, ch);
                printText(confirm, feltCX - cw - fs / 3, entryY + lineH * 2, SDL_Color{200, 200, 200, 255});
                int dw = 0;
                int dh = 0;
                [[maybe_unused]] const bool delete_dims = getTextDimensions(del, dw, dh);
                printText(del, feltCX + fs / 3, entryY + lineH * 2, SDL_Color{200, 200, 200, 255});

                scoresConfirmRect = makeTextRect(confirm, feltCX - cw - fs / 3, entryY + lineH * 2, 10);
                scoresDeleteRect = makeTextRect(del, feltCX + fs / 3, entryY + lineH * 2, 10);
                scoresReturnRect = SDL_Rect{0, 0, 0, 0};
            } else {
                const std::string ret = "Press ENTER to return to intro";
                int tw = 0;
                int th = 0;
                [[maybe_unused]] const bool return_dims = getTextDimensions(ret, tw, th);
                const int x = feltCX - tw / 2;
                const int y = feltB - lineH;
                printText(ret, x, y, SDL_Color{255, 255, 0, 255});
                scoresReturnRect = makeTextRect(ret, x, y, 12);
                scoresConfirmRect = SDL_Rect{0, 0, 0, 0};
                scoresDeleteRect = SDL_Rect{0, 0, 0, 0};
            }
        }

        void procGame(int width, int height) {
            const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            float dt = now - lastTime;
            if (dt > 0.05f) {
                dt = 0.05f;
            }
            lastTime = now;

            handleGameState(dt);
            handleCameraAndInput(dt);

            printText("Shots: " + std::to_string(shotCount), 15, 45, SDL_Color{255, 255, 0, 255});
            int rem = 0;
            for (int i = 1; i < NUM_BALLS; ++i) {
                if (balls[i].active && !balls[i].pocketed) {
                    ++rem;
                }
            }
            printText("Balls: " + std::to_string(rem), 15, 75, SDL_Color{200, 200, 200, 255});

            if (phase == GamePhase::Aiming) {
                printText("Mouse: move aim + hold/release | Right-drag: rotate table | Wheel/Pinch: zoom", 15, height - 40,
                          SDL_Color{180, 180, 180, 255});
            } else if (phase == GamePhase::Charging) {
                const int pct = static_cast<int>(chargeAmount / MAX_POWER * 100.0f);
                printText("Power: " + std::to_string(pct) + "%", 15, 105,
                          SDL_Color{255, static_cast<Uint8>(std::max(0, 255 - pct * 2)), 0, 255});
            } else if (phase == GamePhase::Placing) {
                printText("Mouse move: place cue by camera direction | Click/Enter/A/B: place", 15, height - 40,
                          SDL_Color{255, 100, 100, 255});
            }
        }

        void handleGameState(float dt) {
            switch (phase) {
            case GamePhase::Aiming:
                handleAiming(dt);
                break;
            case GamePhase::Charging:
                handleCharging(dt);
                break;
            case GamePhase::Rolling:
                updatePhysics(dt);
                if (!anyBallMoving()) {
                    if (balls[0].pocketed) {
                        phase = GamePhase::Placing;
                        balls[0].active = true;
                        balls[0].pocketed = false;
                        balls[0].pos = glm::vec2{-TABLE_HALF_W * 0.5f, 0.0f};
                        balls[0].vel = glm::vec2{0.0f};
                    } else {
                        phase = GamePhase::Aiming;
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

            for (auto &ball : balls) {
                if (ball.active && ball.isMoving()) {
                    ball.spinAngle += glm::length(ball.vel) * 5.0f * dt;
                }
            }

            for (auto &anim : sinkAnims) {
                anim.timer += dt;
            }
            sinkAnims.erase(
                std::remove_if(sinkAnims.begin(), sinkAnims.end(), [](const SinkAnim &anim) {
                    return anim.timer >= SINK_DURATION;
                }),
                sinkAnims.end());
        }

        void handleCameraAndInput(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_A]) {
                camAngle -= 1.2f * dt;
            }
            if (keys[SDL_SCANCODE_S]) {
                camAngle += 1.2f * dt;
            }
            if (keys[SDL_SCANCODE_W]) {
                camZoom -= 5.0f * dt;
            }
            if (keys[SDL_SCANCODE_E]) {
                camZoom += 5.0f * dt;
            }
            camZoom = glm::clamp(camZoom, 5.0f, 25.0f);

            if (gamepad == nullptr) {
                return;
            }

            constexpr float dead = 0.15f;
            const auto readAxis = [this](SDL_GamepadAxis axis) {
                return static_cast<float>(SDL_GetGamepadAxis(gamepad, axis)) / 32767.0f;
            };

            const float rx = readAxis(SDL_GAMEPAD_AXIS_RIGHTX);
            const float ry = readAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            if (std::fabs(rx) > dead) {
                camAngle += rx * 1.5f * dt;
            }
            if (std::fabs(ry) > dead) {
                camZoom += ry * 5.0f * dt;
            }
            camZoom = glm::clamp(camZoom, 5.0f, 25.0f);

            const float lx = readAxis(SDL_GAMEPAD_AXIS_LEFTX);
            const float ly = readAxis(SDL_GAMEPAD_AXIS_LEFTY);
            if (phase == GamePhase::Aiming || phase == GamePhase::Charging) {
                if (std::fabs(lx) > dead) {
                    cueAngle += lx * 1.5f * dt;
                }
            } else if (phase == GamePhase::Placing) {
                const float s = 3.0f * dt;
                const glm::vec2 camRight{std::cos(camAngle), -std::sin(camAngle)};
                const glm::vec2 camFwd{-std::sin(camAngle), -std::cos(camAngle)};
                if (std::fabs(lx) > dead) {
                    balls[0].pos += camRight * (lx * s);
                }
                if (std::fabs(ly) > dead) {
                    balls[0].pos -= camFwd * (ly * s);
                }
                clampCueBall();
            }
        }

        void handleGamepadButtonDown(Uint8 button) {
            if (screen == GameScreen::Intro) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    setScreen(GameScreen::Start);
                }
                return;
            }

            if (screen == GameScreen::Scores) {
                if (!enteringName && (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_START)) {
                    setScreen(GameScreen::Intro);
                }
                return;
            }

            if (screen == GameScreen::Start) {
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

            if (screen != GameScreen::Game) {
                return;
            }

            if (button == SDL_GAMEPAD_BUTTON_BACK) {
                exit();
                return;
            }

            if ((button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST) && phase == GamePhase::Aiming) {
                phase = GamePhase::Charging;
                chargeAmount = 0.0f;
                ctrlChargeButton = static_cast<int>(button);
            } else if ((button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST) && phase == GamePhase::Placing && canPlaceCueBall()) {
                phase = GamePhase::Aiming;
            }
        }

        void handleGamepadButtonUp(Uint8 button) {
            if (screen == GameScreen::Game && phase == GamePhase::Charging && static_cast<int>(button) == ctrlChargeButton) {
                shootCueBall();
                ctrlChargeButton = -1;
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

        void resetGame() {
            rackBalls();
            sinkAnims.clear();
            phase = GamePhase::Aiming;
            cueAngle = 0.0f;
            chargeAmount = 0.0f;
            shotCount = 0;
            pointerDown = false;
            pointerCharging = false;
            activePointerId = std::numeric_limits<int64_t>::min();
            mouseCamDragging = false;
            touchPinchActive = false;
            touchPinchDistance = 0.0f;
            touchPoints.clear();
            lastTime = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        }

        void rackBalls() {
            balls[0] = {};
            balls[0].pos = glm::vec2{-TABLE_HALF_W * 0.5f, 0.0f};
            balls[0].active = true;
            balls[0].number = 0;

            const int order[15] = {1, 2, 3, 8, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
            const float sx = TABLE_HALF_W * 0.3f;
            const float sp = BALL_RADIUS * 2.1f;
            int idx = 0;

            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col <= row; ++col) {
                    if (idx >= 15) {
                        break;
                    }
                    const int bn = order[idx++];
                    balls[bn] = {};
                    balls[bn].pos = glm::vec2{sx + row * sp * 0.866f, (col - row * 0.5f) * sp};
                    balls[bn].active = true;
                    balls[bn].number = bn;
                    balls[bn].spinAngle = randFloat(0.0f, 2.0f * PI);
                }
            }
        }

        void handleAiming(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT]) {
                cueAngle -= 1.5f * dt;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                cueAngle += 1.5f * dt;
            }
        }

        void handleCharging(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_LEFT]) {
                cueAngle -= 1.5f * dt;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                cueAngle += 1.5f * dt;
            }
            chargeAmount += MAX_POWER * 0.8f * dt;
            if (chargeAmount > MAX_POWER) {
                chargeAmount = MAX_POWER;
            }
        }

        void handlePlacing(float dt) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            const float s = 3.0f * dt;
            const glm::vec2 camRight{std::cos(camAngle), -std::sin(camAngle)};
            const glm::vec2 camFwd{-std::sin(camAngle), -std::cos(camAngle)};

            if (keys[SDL_SCANCODE_LEFT]) {
                balls[0].pos -= camRight * s;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                balls[0].pos += camRight * s;
            }
            if (keys[SDL_SCANCODE_UP]) {
                balls[0].pos += camFwd * s;
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                balls[0].pos -= camFwd * s;
            }

            clampCueBall();
        }

        void clampCueBall() {
            const float m = BALL_RADIUS + 0.1f;
            balls[0].pos.x = glm::clamp(balls[0].pos.x, -TABLE_HALF_W + m, TABLE_HALF_W - m);
            balls[0].pos.y = glm::clamp(balls[0].pos.y, -TABLE_HALF_H + m, TABLE_HALF_H - m);
        }

        void updatePhysics(float dt) {
            float maxSpeed = 0.0f;
            for (const auto &ball : balls) {
                if (ball.active && !ball.pocketed) {
                    maxSpeed = std::max(maxSpeed, glm::length(ball.vel));
                }
            }

            const float maxMovePerStep = BALL_RADIUS * 0.75f;
            const int steps = std::max(8, static_cast<int>(std::ceil(maxSpeed * dt * 60.0f / maxMovePerStep)));
            const float sub = dt / static_cast<float>(steps);
            const float stepFriction = std::pow(FRICTION, 4.0f / static_cast<float>(steps));

            for (int s = 0; s < steps; ++s) {
                for (auto &ball : balls) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    ball.pos += ball.vel * sub * 60.0f;
                }

                for (int i = 0; i < NUM_BALLS; ++i) {
                    if (!balls[i].active || balls[i].pocketed) {
                        continue;
                    }
                    for (int j = i + 1; j < NUM_BALLS; ++j) {
                        if (!balls[j].active || balls[j].pocketed) {
                            continue;
                        }
                        resolveBall(balls[i], balls[j]);
                    }
                }

                for (auto &ball : balls) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    resolveWall(ball);
                }

                for (auto &ball : balls) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    checkPocket(ball);
                }

                for (auto &ball : balls) {
                    if (!ball.active || ball.pocketed) {
                        continue;
                    }
                    ball.vel *= stepFriction;
                    if (glm::length(ball.vel) < MIN_SPEED) {
                        ball.vel = glm::vec2{0.0f};
                    }
                }
            }
        }

        void resolveBall(PoolBall &a, PoolBall &b) {
            const glm::vec2 d = b.pos - a.pos;
            const float dist = glm::length(d);
            const float minD = BALL_RADIUS * 2.0f;
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
            const float l = -TABLE_HALF_W + BALL_RADIUS;
            const float r = TABLE_HALF_W - BALL_RADIUS;
            const float t = -TABLE_HALF_H + BALL_RADIUS;
            const float b = TABLE_HALF_H - BALL_RADIUS;

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
            for (const auto &pocket : POCKETS) {
                if (glm::length(ball.pos - pocket) < POCKET_R) {
                    const int idx = static_cast<int>(&ball - &balls[0]);
                    sinkAnims.push_back(SinkAnim{pocket,
                                                 BALL_COLORS[static_cast<std::size_t>(idx)],
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
            for (const auto &ball : balls) {
                if (ball.active && !ball.pocketed && ball.isMoving()) {
                    return true;
                }
            }
            return !sinkAnims.empty();
        }

        [[nodiscard]] bool allObjectBallsPocketed() const {
            for (int i = 1; i < NUM_BALLS; ++i) {
                if (balls[i].active && !balls[i].pocketed) {
                    return false;
                }
            }
            return true;
        }

        void checkGameOver() {
            if (!allObjectBallsPocketed()) {
                return;
            }
            finalScore = shotCount;
            playerName.clear();
            enteringName = highScores.qualifies(finalScore);
            if (enteringName) {
                SDL_StartTextInput(window.get());
            }
            setScreen(GameScreen::Scores);
        }

        void commitScoreEntry() {
            if (playerName.empty()) {
                return;
            }
            highScores.addScore(playerName, finalScore);
            highScores.write();
            enteringName = false;
            SDL_StopTextInput(window.get());
        }

        [[nodiscard]] glm::vec3 getCameraPosition() const {
            const float orbitDist = camZoom * CAM_BASE_TOTAL_SCALE;
            const float horiz = orbitDist * std::cos(camPitch);
            const float y = orbitDist * std::sin(camPitch);
            return glm::vec3{std::sin(camAngle) * horiz, y, std::cos(camAngle) * horiz};
        }

        bool screenPointToTable(int px, int py, glm::vec2 &out) const {
            if (fallbackWidth <= 0 || fallbackHeight <= 0) {
                return false;
            }

            const float aspect = static_cast<float>(fallbackWidth) / static_cast<float>(fallbackHeight);
            const glm::vec3 camPos = getCameraPosition();
            const glm::mat4 view = glm::lookAt(camPos, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            const float nx = (2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(fallbackWidth)) - 1.0f;
            const float ny = 1.0f - (2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(fallbackHeight));
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
            if (screen != GameScreen::Game || !balls[0].active || balls[0].pocketed) {
                return;
            }

            glm::vec2 tablePos{0.0f};
            if (!screenPointToTable(px, py, tablePos)) {
                return;
            }

            const glm::vec2 d = tablePos - balls[0].pos;
            if (glm::dot(d, d) < 0.00001f) {
                return;
            }

            const float worldAngle = std::atan2(d.y, d.x);
            cueAngle = worldAngle - camAngle;
        }

        void onMouseRelativeMove(int dx, int dy) {
            if (screen != GameScreen::Game) {
                return;
            }
            if (phase == GamePhase::Aiming || phase == GamePhase::Charging) {
                cueAngle += static_cast<float>(dx) * 0.012f;
                return;
            }
            if (phase == GamePhase::Placing) {
                constexpr float MOVE_SCALE = 0.02f;
                const glm::vec2 camRight{std::cos(camAngle), -std::sin(camAngle)};
                const glm::vec2 camFwd{-std::sin(camAngle), -std::cos(camAngle)};
                balls[0].pos += camRight * (static_cast<float>(dx) * MOVE_SCALE);
                balls[0].pos -= camFwd * (static_cast<float>(dy) * MOVE_SCALE);
                clampCueBall();
            }
        }

        void placeCueBallFromPointer(int px, int py) {
            if (phase != GamePhase::Placing || !balls[0].active || balls[0].pocketed) {
                return;
            }

            glm::vec2 tablePos{0.0f};
            if (!screenPointToTable(px, py, tablePos)) {
                return;
            }

            balls[0].pos = tablePos;
            clampCueBall();
        }

        [[nodiscard]] bool canPlaceCueBall() const {
            for (int i = 1; i < NUM_BALLS; ++i) {
                if (!balls[i].active || balls[i].pocketed) {
                    continue;
                }
                if (glm::length(balls[0].pos - balls[i].pos) < BALL_RADIUS * 2.5f) {
                    return false;
                }
            }
            return true;
        }

        void shootCueBall() {
            const float worldCueAngle = cueAngle + camAngle;
            const glm::vec2 dir{std::cos(worldCueAngle), std::sin(worldCueAngle)};
            balls[0].vel = dir * chargeAmount;
            phase = GamePhase::Rolling;
            ++shotCount;
            chargeAmount = 0.0f;
            pointerCharging = false;
            pointerDown = false;
            activePointerId = std::numeric_limits<int64_t>::min();
        }

        SDL_Rect makeTextRect(const std::string &txt, int x, int y, int pad) {
            int tw = 0;
            int th = 0;
            [[maybe_unused]] const bool text_dims = getTextDimensions(txt, tw, th);
            SDL_Rect r{};
            r.x = x - pad;
            r.y = y - pad;
            r.w = tw + pad * 2;
            r.h = th + pad * 2;
            return r;
        }

        SDL_Rect makeNormRect(float cxN, float cyN, float wN, float hN) const {
            SDL_Rect r{};
            r.w = std::max(1, static_cast<int>(fallbackWidth * wN));
            r.h = std::max(1, static_cast<int>(fallbackHeight * hN));
            const int cx = static_cast<int>(fallbackWidth * cxN);
            const int cy = static_cast<int>(fallbackHeight * cyN);
            r.x = cx - r.w / 2;
            r.y = cy - r.h / 2;
            return r;
        }

        void updateStartClickTargets() {
            startPlayRect = makeNormRect(0.50f, 0.78f, 0.28f, 0.11f);
        }

        static bool pointInRect(int x, int y, const SDL_Rect &r) {
            return x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h);
        }

        void onPointerDown(int px, int py, int64_t pointerId) {
            if (screen == GameScreen::Intro) {
                setScreen(GameScreen::Start);
                return;
            }

            if (screen == GameScreen::Start) {
                updateStartClickTargets();
                if (pointInRect(px, py, startPlayRect)) {
                    resetGame();
                    setScreen(GameScreen::Game);
                }
                return;
            }

            if (screen == GameScreen::Scores) {
                if (!enteringName) {
                    if (pointInRect(px, py, scoresReturnRect)) {
                        setScreen(GameScreen::Intro);
                    }
                    return;
                }
                if (pointInRect(px, py, scoresConfirmRect) && !playerName.empty()) {
                    commitScoreEntry();
                    return;
                }
                if (pointInRect(px, py, scoresDeleteRect) && !playerName.empty()) {
                    playerName.pop_back();
                }
                return;
            }

            if (screen != GameScreen::Game) {
                return;
            }
            if (pointerId == -1 && !mouseCaptured) {
                return;
            }
            if (activePointerId != std::numeric_limits<int64_t>::min() && activePointerId != pointerId) {
                return;
            }

            pointerDown = true;
            activePointerId = pointerId;
            const bool isCapturedMousePointer = (pointerId == -1 && mouseCaptured);
            if (phase == GamePhase::Aiming) {
                // Keep the current cue direction when mouse press starts charging.
                // Relative mouse motion updates the cue after the player moves.
                if (!isCapturedMousePointer) {
                    updateCueFromPointer(px, py);
                }
                phase = GamePhase::Charging;
                chargeAmount = 0.0f;
                pointerCharging = true;
            } else if (phase == GamePhase::Charging) {
                if (!isCapturedMousePointer) {
                    updateCueFromPointer(px, py);
                }
                pointerCharging = true;
            } else if (phase == GamePhase::Placing) {
                if (!(pointerId == -1 && mouseCaptured)) {
                    placeCueBallFromPointer(px, py);
                }
            }
        }

        void onPointerMove(int px, int py, int64_t pointerId) {
            if (screen != GameScreen::Game) {
                return;
            }
            if (activePointerId != std::numeric_limits<int64_t>::min() && activePointerId != pointerId) {
                return;
            }

            if (phase == GamePhase::Aiming || phase == GamePhase::Charging) {
                updateCueFromPointer(px, py);
            } else if (phase == GamePhase::Placing && (pointerId != -1 || !mouseCaptured)) {
                placeCueBallFromPointer(px, py);
            }
        }

        void onPointerUp(int px, int py, int64_t pointerId) {
            if (screen == GameScreen::Start || screen == GameScreen::Scores) {
                onPointerDown(px, py, pointerId);
                return;
            }

            if (screen != GameScreen::Game) {
                return;
            }
            if (activePointerId != pointerId) {
                return;
            }

            pointerDown = false;
            if (phase == GamePhase::Charging && pointerCharging) {
                shootCueBall();
                return;
            }

            if (phase == GamePhase::Placing) {
                if (!(pointerId == -1 && mouseCaptured)) {
                    placeCueBallFromPointer(px, py);
                }
                if (canPlaceCueBall()) {
                    phase = GamePhase::Aiming;
                }
            }

            pointerCharging = false;
            activePointerId = std::numeric_limits<int64_t>::min();
        }

        std::string assetRoot;
        HighScores highScores;

        mxvk::VK_Sprite *backgroundSprite = nullptr;
        mxvk::VK_Sprite *startSprite = nullptr;
        mxvk::VK_Sprite *introSprite = nullptr;
        mxvk::VK_Sprite *scoresSprite = nullptr;

        mxvk::VKAbstractModel feltModel{};
        mxvk::VKAbstractModel woodModel{};
        mxvk::VKAbstractModel pocketModel{};
        std::array<mxvk::VKAbstractModel, NUM_BALLS> ballModels{};
        mxvk::VKAbstractModel cueStickModel{};
        mxvk::VKAbstractModel cueAimModel{};

        std::array<PoolBall, NUM_BALLS> balls{};
        std::vector<SinkAnim> sinkAnims{};

        GameScreen screen = GameScreen::Intro;
        GamePhase phase = GamePhase::Aiming;

        float cueAngle = 0.0f;
        float chargeAmount = 0.0f;
        int shotCount = 0;
        float lastTime = 0.0f;
        float camAngle = 0.4f;
        float camPitch = 0.744604f;
        float camZoom = 13.0f;

        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepadId = 0;
        int ctrlChargeButton = -1;

        bool enteringName = false;
        std::string playerName;
        int finalScore = 0;
        int scoreFontSize = 0;

        bool pointerDown = false;
        bool pointerCharging = false;
        int64_t activePointerId = std::numeric_limits<int64_t>::min();
        bool consumeNextMouseLeftUp = false;
        bool mouseCaptured = false;
        bool mouseCamDragging = false;
        int mouseCamLastX = 0;
        int mouseCamLastY = 0;
        bool touchPinchActive = false;
        float touchPinchDistance = 0.0f;
        std::unordered_map<int64_t, SDL_FPoint> touchPoints;

        Uint64 startTicks = 0U;

        int fallbackWidth = 1280;
        int fallbackHeight = 720;

        SDL_Rect startPlayRect{0, 0, 0, 0};
        SDL_Rect scoresReturnRect{0, 0, 0, 0};
        SDL_Rect scoresConfirmRect{0, 0, 0, 0};
        SDL_Rect scoresDeleteRect{0, 0, 0, 0};
    };

} // namespace demo

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        std::string assets = resolveAssetRoot(args.path);
        SDL_Log("pool_demo: using asset root: %s", assets.c_str());
        demo::PoolWindow window(args.width, args.height, args.fullscreen, args.enable_vsync, assets);
        window.loop();
    } catch (const mxvk::Exception &e) {
        SDL_Log("mxvk: Exception: %s", e.text().c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
