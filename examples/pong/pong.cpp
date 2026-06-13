#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#ifndef pong_ASSET_DIR
#define pong_ASSET_DIR "."
#endif

#ifndef pong_SHADER_DIR
#define pong_SHADER_DIR "."
#endif

namespace {

    struct PongUniformBufferObject {
        alignas(16) glm::mat4 model{1.0f};
        alignas(16) glm::mat4 view{1.0f};
        alignas(16) glm::mat4 proj{1.0f};
        alignas(16) glm::vec4 params{0.0f, 0.0f, 0.0f, 0.0f};
        alignas(16) glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct StarVertex {
        float pos[3];
        float size;
        float color[4];
    };

    struct Star {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float magnitude = 0.0f;
        float temperature = 0.0f;
        float twinkle = 0.0f;
        float size = 0.0f;
        int starType = 0;
        bool isConstellation = false;
    };

    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float life = 0.0f;
        glm::vec4 color{1.0f};
    };

    struct ParticleVertex {
        glm::vec3 position{0.0f};
        glm::vec4 color{1.0f};
    };

    class Paddle {
      public:
        glm::vec3 position;
        glm::vec3 size;
        float rotationAngle = 0.0f;
        float rotationSpeed = 0.0f;
        bool isRotating = false;

        Paddle(const glm::vec3 &pos, const glm::vec3 &sz)
            : position(pos), size(sz) {}

        void update(float deltaTime) {
            if (!isRotating) {
                return;
            }
            rotationAngle += rotationSpeed * deltaTime;
            if (rotationAngle >= 360.0f) {
                rotationAngle = 0.0f;
                isRotating = false;
            }
        }

        void startRotation(float speed) {
            if (!isRotating) {
                rotationSpeed = speed;
                isRotating = true;
            }
        }

        [[nodiscard]] glm::mat4 modelMatrix() const {
            glm::mat4 model(1.0f);
            model = glm::translate(model, position);
            model = glm::rotate(model, glm::radians(rotationAngle), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, size);
            return model;
        }
    };

    class Ball {
      public:
        glm::vec3 position;
        glm::vec3 velocity;
        float radius = 0.05f;
        float speed = 1.0f;
        bool hitPaddle1 = false;
        bool hitPaddle2 = false;
        bool hitWall = false;
        glm::vec3 lastImpactPos{0.0f};

        explicit Ball(const glm::vec3 &pos, const glm::vec3 &vel, float r)
            : position(pos), velocity(vel), radius(r), speed(glm::length(vel)) {}

        void resetBall() {
            position = glm::vec3(0.0f, 0.0f, 0.0f);
            const float angle = glm::radians(static_cast<float>(std::rand() % 120 - 60));
            speed = 1.0f;

            float vx = std::cos(angle);
            float vy = std::sin(angle);

            if (std::abs(vx) < 0.5f) {
                vx = (vx < 0.0f) ? -0.5f : 0.5f;
            }

            vx *= (std::rand() % 2 == 0) ? 1.0f : -1.0f;
            velocity = glm::normalize(glm::vec3(vx, vy, 0.0f)) * speed;
        }

        [[nodiscard]] glm::mat4 modelMatrix() const {
            glm::mat4 model(1.0f);
            model = glm::translate(model, position);
            model = glm::scale(model, glm::vec3(radius));
            return model;
        }

        void update(float deltaTime, Paddle &paddle1, Paddle &paddle2, int &score1, int &score2) {
            hitPaddle1 = false;
            hitPaddle2 = false;
            hitWall = false;

            position += velocity * deltaTime;

            if (position.y + radius > 1.0f) {
                position.y = 1.0f - radius;
                velocity.y = -velocity.y;
                hitWall = true;
                lastImpactPos = glm::vec3(position.x, 1.0f, 0.0f);
            } else if (position.y - radius < -1.0f) {
                position.y = -1.0f + radius;
                velocity.y = -velocity.y;
                hitWall = true;
                lastImpactPos = glm::vec3(position.x, -1.0f, 0.0f);
            }

            handlePaddleCollision(paddle1);
            handlePaddleCollision(paddle2);

            if (position.x - radius < -1.8f) {
                ++score2;
                resetBall();
                return;
            }
            if (position.x + radius > 1.8f) {
                ++score1;
                resetBall();
            }
        }

      private:
        static float clampf(float value, float minv, float maxv) {
            return std::max(minv, std::min(value, maxv));
        }

        void handlePaddleCollision(Paddle &paddle) {
            const float paddleLeft = paddle.position.x - paddle.size.x / 2.0f;
            const float paddleRight = paddle.position.x + paddle.size.x / 2.0f;
            const float paddleTop = paddle.position.y + paddle.size.y / 2.0f;
            const float paddleBottom = paddle.position.y - paddle.size.y / 2.0f;

            const float closestX = clampf(position.x, paddleLeft, paddleRight);
            const float closestY = clampf(position.y, paddleBottom, paddleTop);

            const float distanceX = position.x - closestX;
            const float distanceY = position.y - closestY;
            const float distanceSquared = (distanceX * distanceX) + (distanceY * distanceY);

            if (distanceSquared >= (radius * radius)) {
                return;
            }

            float distance = std::sqrt(distanceSquared);
            if (distance <= 0.0001f) {
                distance = 0.0001f;
            }

            const float nx = distanceX / distance;
            const float ny = distanceY / distance;
            const glm::vec3 normal(nx, ny, 0.0f);

            velocity = glm::reflect(velocity, normal);
            position += normal * (radius - distance);

            const float impactY = position.y - paddle.position.y;
            velocity.y += impactY * 5.0f;

            const float maxVerticalComponent = speed * 0.75f;
            if (std::abs(velocity.y) > maxVerticalComponent) {
                velocity.y = (velocity.y > 0.0f) ? maxVerticalComponent : -maxVerticalComponent;
            }

            velocity = glm::normalize(velocity) * speed;
            paddle.startRotation(360.0f);

            if (paddle.position.x < 0.0f) {
                hitPaddle1 = true;
                lastImpactPos = glm::vec3(paddleRight, position.y, 0.0f);
            } else {
                hitPaddle2 = true;
                lastImpactPos = glm::vec3(paddleLeft, position.y, 0.0f);
            }
        }
    };

    class PongWindow final : public mxvk::VK_Window {
      public:
        PongWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ MXVK Pong ]-", width, height, fullscreen, MXVK_VALIDATION),
              assetRoot((path.empty() || path == ".") ? std::string(pong_ASSET_DIR) : path),
              dataRoot(assetRoot + "/data"),
              paddle1(glm::vec3(-1.5f, 0.0f, 0.0f), glm::vec3(0.1f, 0.4f, 0.1f)),
              paddle2(glm::vec3(1.5f, 0.0f, 0.0f), glm::vec3(0.1f, 0.4f, 0.1f)),
              ball(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.3f, 0.0f), 0.05f),
              fallbackWidth(width),
              fallbackHeight(height) {
            std::srand(static_cast<unsigned>(std::time(nullptr)));
            setFont(dataRoot + "/font.ttf", 24);
            initModels();
            createParticleBuffer();
            initStarfield(30000);
            ball.resetBall();
            tryOpenFirstGamepad();
        }

        ~PongWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            closeGamepad();
            cleanupStarSwapchainResources();
            cleanupParticleResources();
            cleanupStarResources();
            paddleModel1.cleanup(this);
            paddleModel2.cleanup(this);
            ballModel.cleanup(this);
        }

        void onSwapchainAboutToRecreate() override {
            cleanupStarSwapchainResources();
        }

        void onSwapchainRecreated() override {
            paddleModel1.resize(this);
            paddleModel2.resize(this);
            ballModel.resize(this);
            createStarSwapchainResources();
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
                switch (e.key.key) {
                case SDLK_SPACE:
                    wireframe = !wireframe;
                    break;
                case SDLK_RETURN:
                    cameraZ = 5.0f;
                    gridRotation = 0.0f;
                    gridYRotation = 0.0f;
                    break;
                case SDLK_ESCAPE:
                    exit();
                    break;
                case SDLK_R:
                    resetGame();
                    break;
                default:
                    break;
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                    mouseDragging = true;
                    lastMouseX = static_cast<int>(e.button.x);
                    lastMouseY = static_cast<int>(e.button.y);
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
                    mouseDragging = false;
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (mouseDragging) {
                    const int deltaX = static_cast<int>(e.motion.x) - lastMouseX;
                    const int deltaY = static_cast<int>(e.motion.y) - lastMouseY;

                    gridYRotation += static_cast<float>(deltaX) * mouseSensitivity;
                    gridRotation += static_cast<float>(deltaY) * mouseSensitivity;

                    gridRotation = std::clamp(gridRotation, -89.0f, 89.0f);

                    lastMouseX = static_cast<int>(e.motion.x);
                    lastMouseY = static_cast<int>(e.motion.y);
                } else {
                    const int renderH = std::max(1, fallbackHeight);
                    const float normalizedY = (static_cast<float>(e.motion.y) / static_cast<float>(renderH)) * 2.0f - 1.0f;
                    paddle1.position.y = -normalizedY;
                    clampPaddle(paddle1);
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraZ -= delta * 0.5f;
                cameraZ = std::clamp(cameraZ, 1.0f, 20.0f);
                return;
            }

            if (e.type == SDL_EVENT_FINGER_MOTION) {
                const float normalizedY = e.tfinger.y * 2.0f - 1.0f;
                paddle1.position.y = -normalizedY;
                clampPaddle(paddle1);
            }
        }

        void proc() override {
            const auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
            lastFrameTime = currentTime;

            if (deltaTime > 0.1f) {
                deltaTime = 0.1f;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width > 0U) {
                fallbackWidth = static_cast<int>(extent.width);
            }
            if (extent.height > 0U) {
                fallbackHeight = static_cast<int>(extent.height);
            }

            updateFromKeyboard(deltaTime);
            updateFromGamepad(deltaTime);

            updateAI(deltaTime);

            paddle1.update(deltaTime);
            paddle2.update(deltaTime);
            ball.update(deltaTime, paddle1, paddle2, score1, score2);

            if (ball.hitPaddle1) {
                spawnBurst(ball.lastImpactPos, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));
            }
            if (ball.hitPaddle2) {
                spawnBurst(ball.lastImpactPos, glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));
            }

            updateParticles(deltaTime);
            printHud(deltaTime);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (!ensureRenderResources()) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            drawStarfield(cmd, imageIndex, extent);

            const glm::mat4 view = buildViewMatrix();
            glm::mat4 proj = glm::perspective(
                glm::radians(35.0f),
                static_cast<float>(extent.width) / static_cast<float>(extent.height),
                0.1f,
                100.0f);
            proj[1][1] *= -1.0f;

            drawModel(cmd, imageIndex, paddleModel1, paddle1.modelMatrix(), glm::vec3(0.3f, 0.6f, 1.0f), view, proj);
            drawModel(cmd, imageIndex, paddleModel2, paddle2.modelMatrix(), glm::vec3(1.0f, 0.3f, 0.3f), view, proj);
            drawModel(cmd, imageIndex, ballModel, ball.modelMatrix(), glm::vec3(1.0f, 1.0f, 1.0f), view, proj);

            drawParticles(cmd, imageIndex, extent, view, proj);
        }

      private:
        static constexpr int maxParticles = 500;

        std::string assetRoot;
        std::string dataRoot;

        Paddle paddle1;
        Paddle paddle2;
        Ball ball;

        mxvk::VKAbstractModel paddleModel1{};
        mxvk::VKAbstractModel paddleModel2{};
        mxvk::VKAbstractModel ballModel{};

        int score1 = 0;
        int score2 = 0;

        float gridRotation = 0.0f;
        float gridYRotation = 0.0f;
        float rotationSpeed = 50.0f;
        float cameraZ = 5.0f;
        float mouseSensitivity = 0.5f;

        bool wireframe = false;
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;

        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepadId = 0;
        static constexpr float controllerDeadzone = 8000.0f;
        static constexpr float controllerMax = 32767.0f;

        int fallbackWidth = 1280;
        int fallbackHeight = 720;

        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
        float fpsAccumulator = 0.0f;
        int fpsFrameCounter = 0;
        float fpsValue = 0.0f;

        std::vector<Particle> particles{};
        uint32_t activeParticleCount = 0;
        void *mappedParticleData = nullptr;
        VkBuffer particleBuffer = VK_NULL_HANDLE;
        VkDeviceMemory particleBufferMemory = VK_NULL_HANDLE;
        VkPipeline particlePipeline = VK_NULL_HANDLE;
        VkPipelineLayout particlePipelineLayout = VK_NULL_HANDLE;
        VkDescriptorPool particleDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> particleDescriptorSets{};
        std::vector<VkBuffer> particleUniformBuffers{};
        std::vector<VkDeviceMemory> particleUniformBufferMemories{};
        std::vector<void *> particleUniformBufferMapped{};

        std::vector<Star> stars{};
        int numStars = 0;
        bool starfieldInitialized = false;
        Uint32 lastStarUpdateTime = 0;
        float atmosphericTwinkle = 1.0f;
        float lightPollution = 0.1f;

        VkImage starTexture = VK_NULL_HANDLE;
        VkDeviceMemory starTextureMemory = VK_NULL_HANDLE;
        VkImageView starTextureView = VK_NULL_HANDLE;
        VkSampler starSampler = VK_NULL_HANDLE;

        VkBuffer starVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory starVertexBufferMemory = VK_NULL_HANDLE;
        void *starVertexBufferMapped = nullptr;

        VkDescriptorSetLayout starDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool starDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> starDescriptorSets{};

        std::vector<VkBuffer> starUniformBuffers{};
        std::vector<VkDeviceMemory> starUniformBufferMemories{};
        std::vector<void *> starUniformBufferMapped{};

        VkPipeline starPipeline = VK_NULL_HANDLE;
        VkPipelineLayout starPipelineLayout = VK_NULL_HANDLE;

        void initModels() {
            const std::string modelPath = dataRoot + "/cube.mxmod";
            const std::string shaderVert = std::string(pong_SHADER_DIR) + "/pong_model.vert.spv";
            const std::string shaderFrag = std::string(pong_SHADER_DIR) + "/pong_model.frag.spv";
            const std::string paddleManifest = dataRoot + "/paddle_texture_manifest.txt";
            const std::string ballManifest = dataRoot + "/ball_texture_manifest.txt";

            paddleModel1.load(this, modelPath, paddleManifest, dataRoot, 1.0f);
            paddleModel1.setShaders(this, shaderVert, shaderFrag);

            paddleModel2.load(this, modelPath, paddleManifest, dataRoot, 1.0f);
            paddleModel2.setShaders(this, shaderVert, shaderFrag);

            ballModel.load(this, modelPath, ballManifest, dataRoot, 1.0f);
            ballModel.setShaders(this, shaderVert, shaderFrag);
        }

        [[nodiscard]] glm::mat4 buildViewMatrix() const {
            glm::mat4 view(1.0f);
            view = glm::translate(view, glm::vec3(0.0f, 0.0f, -cameraZ));
            view = glm::rotate(view, glm::radians(gridRotation), glm::vec3(1.0f, 0.0f, 0.0f));
            view = glm::rotate(view, glm::radians(gridYRotation), glm::vec3(0.0f, 1.0f, 0.0f));
            return view;
        }

        void drawModel(VkCommandBuffer cmd,
                       uint32_t imageIndex,
                       mxvk::VKAbstractModel &model,
                       const glm::mat4 &world,
                       const glm::vec3 &color,
                       const glm::mat4 &view,
                       const glm::mat4 &proj) {
            mxvk::UniformBufferObject ubo{};
            ubo.model = world;
            ubo.view = view;
            ubo.proj = proj;
            ubo.fx = glm::vec4(color, 1.0f);
            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, wireframe);
        }

        void resetGame() {
            score1 = 0;
            score2 = 0;
            paddle1.position.y = 0.0f;
            paddle2.position.y = 0.0f;
            ball.resetBall();
        }

        static void clampPaddle(Paddle &paddle) {
            const float halfPaddleHeight = paddle.size.y / 2.0f;
            paddle.position.y = std::clamp(paddle.position.y, -1.0f + halfPaddleHeight, 1.0f - halfPaddleHeight);
        }

        [[nodiscard]] static float normalizeAxisWithDeadzone(float axisValue) {
            const float magnitude = std::abs(axisValue);
            if (magnitude <= controllerDeadzone) {
                return 0.0f;
            }

            const float normalized = (magnitude - controllerDeadzone) / (controllerMax - controllerDeadzone);
            return std::copysign(std::clamp(normalized, 0.0f, 1.0f), axisValue);
        }

        void updateFromKeyboard(float deltaTime) {
            const bool *keyState = SDL_GetKeyboardState(nullptr);
            if (keyState == nullptr) {
                return;
            }

            if (keyState[SDL_SCANCODE_A]) {
                gridRotation -= rotationSpeed * deltaTime;
            }
            if (keyState[SDL_SCANCODE_D]) {
                gridRotation += rotationSpeed * deltaTime;
            }
            if (keyState[SDL_SCANCODE_S]) {
                gridYRotation -= rotationSpeed * deltaTime;
            }
            if (keyState[SDL_SCANCODE_W]) {
                gridYRotation += rotationSpeed * deltaTime;
            }
            if (keyState[SDL_SCANCODE_Q]) {
                gridRotation = 0.0f;
                gridYRotation = 0.0f;
            }

            constexpr float speed = 2.0f;
            if (keyState[SDL_SCANCODE_UP]) {
                paddle1.position.y += speed * deltaTime;
            }
            if (keyState[SDL_SCANCODE_DOWN]) {
                paddle1.position.y -= speed * deltaTime;
            }
            clampPaddle(paddle1);
        }

        void updateFromGamepad(float deltaTime) {
            if (gamepad == nullptr) {
                return;
            }

            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) {
                exit();
                return;
            }

            constexpr float paddleMoveSpeed = 2.0f;

            const float leftY = normalizeAxisWithDeadzone(static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)));
            paddle1.position.y -= leftY * paddleMoveSpeed * deltaTime;
            clampPaddle(paddle1);

            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
                paddle1.position.y += 2.0f * deltaTime;
            }
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
                paddle1.position.y -= 2.0f * deltaTime;
            }
            clampPaddle(paddle1);

            const float rightX = normalizeAxisWithDeadzone(static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX)));
            const float rightY = normalizeAxisWithDeadzone(static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)));
            gridRotation += rightX * rotationSpeed * deltaTime;
            gridYRotation -= rightY * rotationSpeed * deltaTime;

            const float leftTrigger = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
            const float rightTrigger = static_cast<float>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
            if (leftTrigger > controllerDeadzone) {
                cameraZ += (leftTrigger / controllerMax) * 3.0f * deltaTime;
            }
            if (rightTrigger > controllerDeadzone) {
                cameraZ -= (rightTrigger / controllerMax) * 3.0f * deltaTime;
            }

            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                cameraZ += 3.0f * deltaTime;
            }
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                cameraZ -= 3.0f * deltaTime;
            }

            cameraZ = std::clamp(cameraZ, 1.0f, 20.0f);
        }

        void updateAI(float deltaTime) {
            constexpr float paddleSpeed = 0.9f;
            if (ball.position.y > paddle2.position.y + paddle2.size.y / 4.0f) {
                paddle2.position.y += paddleSpeed * deltaTime;
            }
            if (ball.position.y < paddle2.position.y - paddle2.size.y / 4.0f) {
                paddle2.position.y -= paddleSpeed * deltaTime;
            }
            clampPaddle(paddle2);
        }

        void printHud(float deltaTime) {
            fpsAccumulator += deltaTime;
            ++fpsFrameCounter;
            if (fpsAccumulator >= 0.2f) {
                fpsValue = static_cast<float>(fpsFrameCounter) / fpsAccumulator;
                fpsAccumulator = 0.0f;
                fpsFrameCounter = 0;
            }

            const SDL_Color white{255, 255, 255, 255};
            const SDL_Color yellow{255, 255, 0, 255};

            printText("Vulkan Pong", 50, 50, white);
            printText(std::format("Player 1: {} : Player 2: {}", score1, score2), 50, 80, yellow);

            std::ostringstream fpsStream;
            fpsStream << std::fixed << std::setprecision(1) << "FPS: " << fpsValue;
            const std::string polygonMode = wireframe ? "WIREFRAME" : "SOLID";
            const std::string controllerStatus = (gamepad != nullptr) ? "Controller: Connected" : "Controller: None";

            printText(fpsStream.str() + " | Mode: " + polygonMode, 50, 110, white);
            printText(controllerStatus, 50, 140, white);
        }

        void spawnBurst(const glm::vec3 &impactPos, const glm::vec3 &normal, const glm::vec4 &paddleColor) {
            for (int i = 0; i < 35 && particles.size() < static_cast<size_t>(maxParticles); ++i) {
                Particle p;
                p.position = impactPos;
                p.velocity = normal * static_cast<float>((std::rand() % 50) / 10.0f + 0.5f) +
                             glm::vec3(0.0f,
                                       static_cast<float>((std::rand() % 60) - 30) / 30.0f,
                                       static_cast<float>((std::rand() % 40) - 20) / 40.0f);
                p.life = 0.6f;
                p.color = paddleColor;
                particles.push_back(p);
            }
        }

        void updateParticles(float deltaTime) {
            if (mappedParticleData == nullptr) {
                activeParticleCount = 0;
                return;
            }

            const glm::vec3 gravity(0.0f, -2.0f, 0.0f);
            activeParticleCount = 0;
            auto *particleBufferData = static_cast<ParticleVertex *>(mappedParticleData);

            particles.erase(
                std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.life <= 0.0f; }),
                particles.end());

            for (auto &p : particles) {
                if (p.life <= 0.0f || activeParticleCount >= static_cast<uint32_t>(maxParticles)) {
                    continue;
                }

                p.velocity += gravity * deltaTime;
                p.position += p.velocity * deltaTime;
                p.life -= deltaTime * 1.5f;
                p.color.a = p.life;

                particleBufferData[activeParticleCount].position = p.position;
                particleBufferData[activeParticleCount].color = p.color;
                ++activeParticleCount;
            }
        }

        void createParticleBuffer() {
            const VkDeviceSize bufferSize = sizeof(ParticleVertex) * maxParticles;
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                particleBuffer,
                particleBufferMemory);

            VK_CHECK_RESULT(vkMapMemory(device, particleBufferMemory, 0, bufferSize, 0, &mappedParticleData));
        }

        void cleanupParticleResources() {
            cleanupParticleSwapchainResources();
            if (mappedParticleData != nullptr && particleBufferMemory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, particleBufferMemory);
                mappedParticleData = nullptr;
            }
            if (particleBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, particleBuffer, nullptr);
                particleBuffer = VK_NULL_HANDLE;
            }
            if (particleBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, particleBufferMemory, nullptr);
                particleBufferMemory = VK_NULL_HANDLE;
            }
        }

        void destroyParticleUniformBuffers() {
            for (size_t i = 0; i < particleUniformBuffers.size(); ++i) {
                if (particleUniformBufferMapped[i] != nullptr && particleUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkUnmapMemory(device, particleUniformBufferMemories[i]);
                    particleUniformBufferMapped[i] = nullptr;
                }
                if (particleUniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, particleUniformBuffers[i], nullptr);
                    particleUniformBuffers[i] = VK_NULL_HANDLE;
                }
                if (particleUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, particleUniformBufferMemories[i], nullptr);
                    particleUniformBufferMemories[i] = VK_NULL_HANDLE;
                }
            }
            particleUniformBuffers.clear();
            particleUniformBufferMemories.clear();
            particleUniformBufferMapped.clear();
        }

        void cleanupParticleSwapchainResources() {
            cleanupParticlePipeline();
            if (particleDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, particleDescriptorPool, nullptr);
                particleDescriptorPool = VK_NULL_HANDLE;
            }
            particleDescriptorSets.clear();
            destroyParticleUniformBuffers();
        }

        void createParticleUniformBuffers() {
            const size_t imageCount = getSwapchainImageCount();
            const VkDeviceSize bufferSize = sizeof(PongUniformBufferObject);

            particleUniformBuffers.resize(imageCount, VK_NULL_HANDLE);
            particleUniformBufferMemories.resize(imageCount, VK_NULL_HANDLE);
            particleUniformBufferMapped.resize(imageCount, nullptr);

            for (size_t i = 0; i < imageCount; ++i) {
                createBuffer(
                    bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    particleUniformBuffers[i],
                    particleUniformBufferMemories[i]);
                VK_CHECK_RESULT(vkMapMemory(device, particleUniformBufferMemories[i], 0, bufferSize, 0, &particleUniformBufferMapped[i]));
            }
        }

        void createParticleDescriptorPool() {
            const uint32_t imageCount = static_cast<uint32_t>(getSwapchainImageCount());

            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = imageCount;
            poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = imageCount;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = imageCount;

            VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &particleDescriptorPool));
        }

        void createParticleDescriptorSets() {
            const size_t imageCount = getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(imageCount, starDescriptorSetLayout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = particleDescriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
            allocInfo.pSetLayouts = layouts.data();

            particleDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, particleDescriptorSets.data()));

            for (size_t i = 0; i < imageCount; ++i) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = starTextureView;
                imageInfo.sampler = starSampler;

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = particleUniformBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(PongUniformBufferObject);

                std::array<VkWriteDescriptorSet, 2> writes{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = particleDescriptorSets[i];
                writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].descriptorCount = 1;
                writes[0].pImageInfo = &imageInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = particleDescriptorSets[i];
                writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[1].descriptorCount = 1;
                writes[1].pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void updateParticleUniform(uint32_t imageIndex,
                                   const VkExtent2D &extent,
                                   const glm::mat4 &view,
                                   const glm::mat4 &proj,
                                   float timeSeconds) {
            if (imageIndex >= particleUniformBufferMapped.size() || particleUniformBufferMapped[imageIndex] == nullptr) {
                return;
            }

            PongUniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.view = view;
            ubo.proj = proj;
            ubo.params = glm::vec4(timeSeconds, 0.0f, 0.0f, 0.0f);
            ubo.color = glm::vec4(1.0f);

            (void)extent;
            std::memcpy(particleUniformBufferMapped[imageIndex], &ubo, sizeof(ubo));
        }

        void cleanupParticlePipeline() {
            if (particlePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, particlePipeline, nullptr);
                particlePipeline = VK_NULL_HANDLE;
            }
            if (particlePipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, particlePipelineLayout, nullptr);
                particlePipelineLayout = VK_NULL_HANDLE;
            }
        }

        void drawParticles(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &proj) {
            if (particlePipeline == VK_NULL_HANDLE || activeParticleCount == 0 || imageIndex >= particleDescriptorSets.size()) {
                return;
            }

            updateParticleUniform(imageIndex, extent, view, proj, SDL_GetTicks() * 0.001f);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);

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

            VkBuffer vertexBuffers[] = {particleBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                particlePipelineLayout,
                0,
                1,
                &particleDescriptorSets[imageIndex],
                0,
                nullptr);

            vkCmdDraw(cmd, activeParticleCount, 1, 0, 0);
        }

        float randomFloat(float minv, float maxv) {
            static std::random_device rd;
            static std::default_random_engine eng(rd());
            std::uniform_real_distribution<float> dist(minv, maxv);
            return dist(eng);
        }

        glm::vec3 getStarColor(float temperature) const {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;

            if (temperature < 3700.0f) {
                r = 1.0f;
                g = temperature / 3700.0f * 0.6f;
                b = 0.0f;
            } else if (temperature < 5200.0f) {
                r = 1.0f;
                g = 0.6f + (temperature - 3700.0f) / 1500.0f * 0.4f;
                b = (temperature - 3700.0f) / 1500.0f * 0.3f;
            } else if (temperature < 6000.0f) {
                r = 1.0f;
                g = 1.0f;
                b = (temperature - 5200.0f) / 800.0f * 0.7f;
            } else if (temperature < 7500.0f) {
                r = 1.0f;
                g = 1.0f;
                b = 0.7f + (temperature - 6000.0f) / 1500.0f * 0.3f;
            } else {
                r = 0.7f - (temperature - 7500.0f) / 10000.0f * 0.4f;
                g = 0.8f + (temperature - 7500.0f) / 10000.0f * 0.2f;
                b = 1.0f;
            }

            return glm::vec3(r, g, b);
        }

        float magnitudeToSize(float magnitude) const {
            return glm::clamp(15.0f - magnitude * 2.0f, 1.0f, 25.0f);
        }

        float magnitudeToAlpha(float magnitude) const {
            const float alpha = (6.5f - magnitude) / 6.5f;
            return glm::clamp(alpha - lightPollution, 0.0f, 1.0f);
        }

        void initStarfield(int numStarsParam) {
            if (starfieldInitialized) {
                return;
            }

            numStars = numStarsParam;
            stars.resize(static_cast<size_t>(numStars));

            constexpr float pi = 3.14159265358979323846f;
            for (int i = 0; i < numStars; ++i) {
                auto &star = stars[static_cast<size_t>(i)];

                const float theta = randomFloat(0.0f, 2.0f * pi);
                const float phi = std::acos(randomFloat(-1.0f, 1.0f));
                const float radius = randomFloat(50.0f, 200.0f);

                star.x = radius * std::sin(phi) * std::cos(theta);
                star.y = radius * std::sin(phi) * std::sin(theta);
                star.z = radius * std::cos(phi);

                star.vx = randomFloat(-0.001f, 0.001f);
                star.vy = randomFloat(-0.001f, 0.001f);
                star.vz = randomFloat(-0.001f, 0.001f);

                const float r = randomFloat(0.0f, 1.0f);
                if (r < 0.05f) {
                    star.magnitude = randomFloat(-1.0f, 2.0f);
                    star.starType = 1;
                } else if (r < 0.3f) {
                    star.magnitude = randomFloat(2.0f, 4.0f);
                    star.starType = 0;
                } else {
                    star.magnitude = randomFloat(4.0f, 6.5f);
                    star.starType = 2;
                }

                if (star.starType == 1) {
                    star.temperature = randomFloat(3000.0f, 5000.0f);
                } else if (star.starType == 0) {
                    star.temperature = randomFloat(4000.0f, 8000.0f);
                } else {
                    star.temperature = randomFloat(2500.0f, 4000.0f);
                }

                star.twinkle = randomFloat(0.5f, 3.0f);
                star.size = magnitudeToSize(star.magnitude);
                star.isConstellation = (star.magnitude < 3.0f) && (randomFloat(0.0f, 1.0f) < 0.3f);
            }

            createStarTexture();
            createStarVertexBuffer();
            createStarSwapchainResources();

            lastStarUpdateTime = SDL_GetTicks();
            starfieldInitialized = true;
        }

        void cleanupStarResources() {
            if (starVertexBufferMapped != nullptr && starVertexBufferMemory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, starVertexBufferMemory);
                starVertexBufferMapped = nullptr;
            }
            if (starVertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, starVertexBuffer, nullptr);
                starVertexBuffer = VK_NULL_HANDLE;
            }
            if (starVertexBufferMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, starVertexBufferMemory, nullptr);
                starVertexBufferMemory = VK_NULL_HANDLE;
            }

            if (starSampler != VK_NULL_HANDLE) {
                vkDestroySampler(device, starSampler, nullptr);
                starSampler = VK_NULL_HANDLE;
            }
            if (starTextureView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, starTextureView, nullptr);
                starTextureView = VK_NULL_HANDLE;
            }
            if (starTexture != VK_NULL_HANDLE) {
                vkDestroyImage(device, starTexture, nullptr);
                starTexture = VK_NULL_HANDLE;
            }
            if (starTextureMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, starTextureMemory, nullptr);
                starTextureMemory = VK_NULL_HANDLE;
            }
        }

        void cleanupStarSwapchainResources() {
            cleanupParticleSwapchainResources();

            if (starPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, starPipeline, nullptr);
                starPipeline = VK_NULL_HANDLE;
            }
            if (starPipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(device, starPipelineLayout, nullptr);
                starPipelineLayout = VK_NULL_HANDLE;
            }
            if (starDescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, starDescriptorPool, nullptr);
                starDescriptorPool = VK_NULL_HANDLE;
            }
            if (starDescriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, starDescriptorSetLayout, nullptr);
                starDescriptorSetLayout = VK_NULL_HANDLE;
            }

            destroyStarUniformBuffers();
            starDescriptorSets.clear();
        }

        void createStarSwapchainResources() {
            if (!starfieldInitialized && stars.empty()) {
                return;
            }
            createStarDescriptorSetLayout();
            createStarUniformBuffers();
            createStarDescriptorPool();
            createStarDescriptorSets();
            createStarPipeline();
            createParticleUniformBuffers();
            createParticleDescriptorPool();
            createParticleDescriptorSets();
            createParticlePipeline();
        }

        void createStarTexture() {
            SDL_Surface *starImg = mxvk::LoadPNG((dataRoot + "/star.png").c_str());
            if (starImg == nullptr) {
                throw mxvk::Exception("Failed to load star.png texture");
            }

            const VkDeviceSize imageSize = static_cast<VkDeviceSize>(starImg->w) * static_cast<VkDeviceSize>(starImg->h) * 4U;

            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
            createBuffer(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer,
                stagingBufferMemory);

            void *data = nullptr;
            VK_CHECK_RESULT(vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data));
            std::memcpy(data, starImg->pixels, static_cast<size_t>(imageSize));
            vkUnmapMemory(device, stagingBufferMemory);

            createImage(
                static_cast<uint32_t>(starImg->w),
                static_cast<uint32_t>(starImg->h),
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                starTexture,
                starTextureMemory);

            transitionImageLayout(starTexture, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copyBufferToImage(stagingBuffer, starTexture, static_cast<uint32_t>(starImg->w), static_cast<uint32_t>(starImg->h));
            transitionImageLayout(starTexture, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingBufferMemory, nullptr);

            starTextureView = createImageView(starTexture, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &starSampler));

            SDL_DestroySurface(starImg);
        }

        void createStarVertexBuffer() {
            const VkDeviceSize bufferSize = sizeof(StarVertex) * static_cast<VkDeviceSize>(numStars);
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                starVertexBuffer,
                starVertexBufferMemory);

            VK_CHECK_RESULT(vkMapMemory(device, starVertexBufferMemory, 0, bufferSize, 0, &starVertexBufferMapped));
        }

        void createStarDescriptorSetLayout() {
            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 0;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = 1;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            std::array<VkDescriptorSetLayoutBinding, 2> bindings = {samplerBinding, uboBinding};
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &starDescriptorSetLayout));
        }

        void createStarUniformBuffers() {
            const size_t imageCount = getSwapchainImageCount();
            const VkDeviceSize bufferSize = sizeof(PongUniformBufferObject);

            starUniformBuffers.resize(imageCount, VK_NULL_HANDLE);
            starUniformBufferMemories.resize(imageCount, VK_NULL_HANDLE);
            starUniformBufferMapped.resize(imageCount, nullptr);

            for (size_t i = 0; i < imageCount; ++i) {
                createBuffer(
                    bufferSize,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    starUniformBuffers[i],
                    starUniformBufferMemories[i]);
                VK_CHECK_RESULT(vkMapMemory(device, starUniformBufferMemories[i], 0, bufferSize, 0, &starUniformBufferMapped[i]));
            }
        }

        void destroyStarUniformBuffers() {
            for (size_t i = 0; i < starUniformBuffers.size(); ++i) {
                if (starUniformBufferMapped[i] != nullptr && starUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkUnmapMemory(device, starUniformBufferMemories[i]);
                    starUniformBufferMapped[i] = nullptr;
                }
                if (starUniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, starUniformBuffers[i], nullptr);
                    starUniformBuffers[i] = VK_NULL_HANDLE;
                }
                if (starUniformBufferMemories[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, starUniformBufferMemories[i], nullptr);
                    starUniformBufferMemories[i] = VK_NULL_HANDLE;
                }
            }
            starUniformBuffers.clear();
            starUniformBufferMemories.clear();
            starUniformBufferMapped.clear();
        }

        void createStarDescriptorPool() {
            const uint32_t imageCount = static_cast<uint32_t>(getSwapchainImageCount());

            std::array<VkDescriptorPoolSize, 2> poolSizes{};
            poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSizes[0].descriptorCount = imageCount;
            poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            poolSizes[1].descriptorCount = imageCount;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = imageCount;

            VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &starDescriptorPool));
        }

        void createStarDescriptorSets() {
            const size_t imageCount = getSwapchainImageCount();
            std::vector<VkDescriptorSetLayout> layouts(imageCount, starDescriptorSetLayout);

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = starDescriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
            allocInfo.pSetLayouts = layouts.data();

            starDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, starDescriptorSets.data()));

            for (size_t i = 0; i < imageCount; ++i) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = starTextureView;
                imageInfo.sampler = starSampler;

                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = starUniformBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(PongUniformBufferObject);

                std::array<VkWriteDescriptorSet, 2> writes{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[0].dstSet = starDescriptorSets[i];
                writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[0].descriptorCount = 1;
                writes[0].pImageInfo = &imageInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[1].dstSet = starDescriptorSets[i];
                writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writes[1].descriptorCount = 1;
                writes[1].pBufferInfo = &bufferInfo;

                vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }

        void createStarPipeline() {
            const std::vector<char> vertShaderCode = loadSpv(dataRoot + "/star_vert.spv");
            const std::vector<char> fragShaderCode = loadSpv(dataRoot + "/star_frag.spv");

            VkShaderModule vertShaderModule = createShaderModule(device, vertShaderCode);
            VkShaderModule fragShaderModule = VK_NULL_HANDLE;

            try {
                fragShaderModule = createShaderModule(device, fragShaderCode);

                VkPipelineShaderStageCreateInfo vertStage{};
                vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vertStage.module = vertShaderModule;
                vertStage.pName = "main";

                VkPipelineShaderStageCreateInfo fragStage{};
                fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragStage.module = fragShaderModule;
                fragStage.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(StarVertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 3> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(StarVertex, pos);

                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32_SFLOAT;
                attributes[1].offset = offsetof(StarVertex, size);

                attributes[2].binding = 0;
                attributes[2].location = 2;
                attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[2].offset = offsetof(StarVertex, color);

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &bindingDescription;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                inputAssembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                std::array<VkDynamicState, 2> dynamicStates = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamicInfo{};
                dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicInfo.pDynamicStates = dynamicStates.data();

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

                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = VK_FALSE;
                depthStencil.depthWriteEnable = VK_FALSE;
                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencil.depthBoundsTestEnable = VK_FALSE;
                depthStencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;

                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = 1;
                layoutInfo.pSetLayouts = &starDescriptorSetLayout;
                layoutInfo.pushConstantRangeCount = 0;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &starPipelineLayout));

                VkFormat colorFormat = getSwapchainFormat();
                VkFormat depthFormat = getDepthFormat();

                VkPipelineRenderingCreateInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                renderingInfo.depthAttachmentFormat = depthFormat;
                renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicInfo;
                pipelineInfo.layout = starPipelineLayout;
                pipelineInfo.renderPass = VK_NULL_HANDLE;
                pipelineInfo.subpass = 0;
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
                pipelineInfo.basePipelineIndex = -1;

                VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &starPipeline));
            } catch (...) {
                if (fragShaderModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, fragShaderModule, nullptr);
                }
                vkDestroyShaderModule(device, vertShaderModule, nullptr);
                throw;
            }

            if (fragShaderModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, fragShaderModule, nullptr);
            }
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
        }

        void updateStarfield(float deltaTime) {
            if (!starfieldInitialized || starVertexBufferMapped == nullptr) {
                return;
            }
            if (deltaTime > 0.1f) {
                deltaTime = 0.1f;
            }

            const float time = SDL_GetTicks() * 0.001f;
            auto *vertices = static_cast<StarVertex *>(starVertexBufferMapped);

            for (int i = 0; i < numStars; ++i) {
                auto &star = stars[static_cast<size_t>(i)];

                star.x += star.vx * deltaTime;
                star.y += star.vy * deltaTime;
                star.z += star.vz * deltaTime;

                vertices[i].pos[0] = star.x;
                vertices[i].pos[1] = star.y;
                vertices[i].pos[2] = star.z;

                float twinkleFactor = 1.0f;
                if (atmosphericTwinkle > 0.0f) {
                    twinkleFactor = 0.7f + 0.3f * std::sin(time * star.twinkle) * atmosphericTwinkle;
                }

                float size = star.size * twinkleFactor;
                if (star.isConstellation) {
                    size *= 1.2f;
                }
                vertices[i].size = size;

                const glm::vec3 starColor = getStarColor(star.temperature);
                const float alpha = magnitudeToAlpha(star.magnitude) * twinkleFactor;

                vertices[i].color[0] = starColor.r;
                vertices[i].color[1] = starColor.g;
                vertices[i].color[2] = starColor.b;
                vertices[i].color[3] = alpha;
            }
        }

        void updateStarUniform(uint32_t imageIndex,
                               const VkExtent2D &extent,
                               const glm::mat4 &view,
                               const glm::mat4 &proj,
                               float timeSeconds) {
            if (imageIndex >= starUniformBufferMapped.size() || starUniformBufferMapped[imageIndex] == nullptr) {
                return;
            }

            PongUniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.view = view;
            ubo.proj = proj;
            ubo.params = glm::vec4(timeSeconds, 0.0f, 0.0f, 0.0f);
            ubo.color = glm::vec4(1.0f);

            (void)extent;
            std::memcpy(starUniformBufferMapped[imageIndex], &ubo, sizeof(ubo));
        }

        void drawStarfield(VkCommandBuffer cmd, uint32_t imageIndex, const VkExtent2D &extent) {
            if (!starfieldInitialized || starPipeline == VK_NULL_HANDLE || imageIndex >= starDescriptorSets.size()) {
                return;
            }

            const Uint32 currentTime = SDL_GetTicks();
            const float deltaTime = static_cast<float>(currentTime - lastStarUpdateTime) / 1000.0f;
            lastStarUpdateTime = currentTime;
            updateStarfield(deltaTime);

            const glm::mat4 view = buildViewMatrix();
            glm::mat4 proj = glm::perspective(
                glm::radians(60.0f),
                static_cast<float>(extent.width) / static_cast<float>(extent.height),
                0.1f,
                1000.0f);
            proj[1][1] *= -1.0f;

            updateStarUniform(imageIndex, extent, view, proj, SDL_GetTicks() * 0.001f);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, starPipeline);

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

            VkBuffer vertexBuffers[] = {starVertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                starPipelineLayout,
                0,
                1,
                &starDescriptorSets[imageIndex],
                0,
                nullptr);

            vkCmdDraw(cmd, static_cast<uint32_t>(numStars), 1, 0, 0);
        }

        void createParticlePipeline() {
            const std::vector<char> vertShaderCode = loadSpv(dataRoot + "/particle_vert.spv");
            const std::vector<char> fragShaderCode = loadSpv(dataRoot + "/particle_frag.spv");

            VkShaderModule vertShaderModule = createShaderModule(device, vertShaderCode);
            VkShaderModule fragShaderModule = VK_NULL_HANDLE;

            try {
                fragShaderModule = createShaderModule(device, fragShaderCode);

                VkPipelineShaderStageCreateInfo vertStage{};
                vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
                vertStage.module = vertShaderModule;
                vertStage.pName = "main";

                VkPipelineShaderStageCreateInfo fragStage{};
                fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragStage.module = fragShaderModule;
                fragStage.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(ParticleVertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0].binding = 0;
                attributes[0].location = 0;
                attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attributes[0].offset = offsetof(ParticleVertex, position);

                attributes[1].binding = 0;
                attributes[1].location = 1;
                attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                attributes[1].offset = offsetof(ParticleVertex, color);

                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &bindingDescription;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                inputAssembly.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                std::array<VkDynamicState, 2> dynamicStates = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                };
                VkPipelineDynamicStateCreateInfo dynamicInfo{};
                dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamicInfo.pDynamicStates = dynamicStates.data();

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

                VkPipelineDepthStencilStateCreateInfo depthStencil{};
                depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencil.depthTestEnable = VK_FALSE;
                depthStencil.depthWriteEnable = VK_FALSE;
                depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencil.depthBoundsTestEnable = VK_FALSE;
                depthStencil.stencilTestEnable = VK_FALSE;

                VkPipelineColorBlendAttachmentState colorBlendAttachment{};
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo colorBlending{};
                colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlending.logicOpEnable = VK_FALSE;
                colorBlending.attachmentCount = 1;
                colorBlending.pAttachments = &colorBlendAttachment;

                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = 1;
                layoutInfo.pSetLayouts = &starDescriptorSetLayout;
                layoutInfo.pushConstantRangeCount = 0;

                VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &particlePipelineLayout));

                VkFormat colorFormat = getSwapchainFormat();
                VkFormat depthFormat = getDepthFormat();

                VkPipelineRenderingCreateInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &colorFormat;
                renderingInfo.depthAttachmentFormat = depthFormat;
                renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.pNext = &renderingInfo;
                pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
                pipelineInfo.pStages = shaderStages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.pDynamicState = &dynamicInfo;
                pipelineInfo.layout = particlePipelineLayout;
                pipelineInfo.renderPass = VK_NULL_HANDLE;
                pipelineInfo.subpass = 0;
                pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
                pipelineInfo.basePipelineIndex = -1;

                VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &particlePipeline));
            } catch (...) {
                if (fragShaderModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(device, fragShaderModule, nullptr);
                }
                vkDestroyShaderModule(device, vertShaderModule, nullptr);
                throw;
            }

            if (fragShaderModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, fragShaderModule, nullptr);
            }
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
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

        void createBuffer(VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer &buffer,
                          VkDeviceMemory &bufferMemory) const {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

            VkMemoryRequirements memRequirements{};
            vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory));
            VK_CHECK_RESULT(vkBindBufferMemory(device, buffer, bufferMemory, 0));
        }

        [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
                if ((typeFilter & (1U << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw mxvk::Exception("Failed to find suitable memory type");
        }

        [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = command_pool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
            VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VK_CHECK_RESULT(vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE));
            VK_CHECK_RESULT(vkQueueWaitIdle(graphics_queue));

            vkFreeCommandBuffers(device, command_pool, 1, &commandBuffer);
        }

        void createImage(uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage &image,
                         VkDeviceMemory &memory) const {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = format;
            imageInfo.tiling = tiling;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &image));

            VkMemoryRequirements memRequirements{};
            vkGetImageMemoryRequirements(device, image, &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

            VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &memory));
            VK_CHECK_RESULT(vkBindImageMemory(device, image, memory, 0));
        }

        [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = aspect;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
            return imageView;
        }

        void transitionImageLayout(VkImage image,
                                   VkFormat,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout) const {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } else {
                throw mxvk::Exception("Unsupported image layout transition");
            }

            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                destinationStage,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);

            endSingleTimeCommands(commandBuffer);
        }

        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};

            vkCmdCopyBufferToImage(
                commandBuffer,
                buffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region);

            endSingleTimeCommands(commandBuffer);
        }
    };

} // namespace

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        PongWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << std::format("std::exception: {}\n", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
