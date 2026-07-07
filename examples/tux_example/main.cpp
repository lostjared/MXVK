#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

namespace example {

    struct SnowFlake {
        float x;
        float y;
        float z;
        float intensity;
        float size;
        float speed;
        float angle;
        float rotation;
        float rotationSpeed;
        float rotationAngle;
        float rotationDirection;
        float rotationIntensity;
        float rotationSize;
        float rotationSpeedSize;
        float rotationSpeedIntensity;
    };

    class SnowEffect3D {
      public:
        static constexpr int MAX_SNOWFLAKES = 600;
        static constexpr float modelDepthClearance = 0.55f;

        void init(mxvk::VK_Window *window, const std::string &assetRoot, const std::string &fragmentShaderPath) {
            if (snowSprite != nullptr) {
                return;
            }

            snowSprite = window->createSprite3D(assetRoot + "/data/snowflake.png", "", fragmentShaderPath);
            snowSprite->setDepthTestEnabled(true);
            snowSprite->setDepthWriteEnabled(false);
            snowSprite->setAlphaDiscardThreshold(0.1f);

            snowflakes.reserve(MAX_SNOWFLAKES);
            for (int i = 0; i < MAX_SNOWFLAKES; ++i) {
                snowflakes.push_back(makeFlake());
            }
        }

        void update(float deltaSeconds) {
            windTime += deltaSeconds * 1.2f;
            const float windOffsetX = std::sin(windTime) * 0.12f;
            const float windOffsetZ = std::cos(windTime * 0.7f) * 0.04f;
            constexpr float twoPi = 6.28318530717958647692f;

            for (auto &flake : snowflakes) {
                flake.y -= flake.speed * deltaSeconds;
                flake.x += windOffsetX * deltaSeconds;
                flake.z += windOffsetZ * deltaSeconds;
                if (std::abs(flake.z) < modelDepthClearance) {
                    flake.z = (flake.z >= 0.0f) ? modelDepthClearance : -modelDepthClearance;
                }
                flake.rotation += (flake.rotationSpeed + std::sin(windTime + flake.rotationAngle) *
                                                             flake.rotationSpeedIntensity * 0.5f) *
                                  deltaSeconds;
                if (flake.rotation > twoPi) {
                    flake.rotation -= twoPi;
                } else if (flake.rotation < -twoPi) {
                    flake.rotation += twoPi;
                }

                if (flake.y < -3.8f) {
                    flake = makeFlake();
                    flake.y = 3.8f;
                }
            }
        }

        void drawLayer(VkCommandBuffer cmd, uint32_t imageIndex, const glm::mat4 &view, const glm::mat4 &proj, bool frontLayer) {
            if (snowSprite == nullptr) {
                return;
            }

            snowSprite->updateCamera(imageIndex, view, proj);
            for (const SnowFlake &flake : snowflakes) {
                const bool isFront = flake.z >= 0.0f;
                if (isFront != frontLayer) {
                    continue;
                }

                snowSprite->drawSprite(glm::vec3(flake.x, flake.y, flake.z),
                                       glm::vec2(flake.size, flake.size),
                                       glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                       flake.rotation);
            }

            snowSprite->render(cmd, imageIndex);
            snowSprite->clearQueue();
        }

      private:
        static float randomSignedCoordinate() {
            return static_cast<float>(std::rand() % 900 - 450) / 100.0f;
        }

        static float randomDepth() {
            const float depthRange = 2.5f - modelDepthClearance;
            const float depth = modelDepthClearance + static_cast<float>(std::rand() % 1000) * (depthRange / 999.0f);
            return (std::rand() % 2 == 0) ? -depth : depth;
        }

        static SnowFlake makeFlake() {
            SnowFlake flake{};
            flake.x = randomSignedCoordinate();
            flake.y = static_cast<float>(std::rand() % 760 - 380) / 100.0f;
            flake.z = randomDepth();
            flake.intensity = 1.0f;
            flake.size = 0.06f + static_cast<float>(std::rand() % 180) / 1600.0f;
            flake.speed = 0.25f + static_cast<float>(std::rand() % 100) / 600.0f;
            constexpr float pi = 3.14159265358979323846f;
            flake.angle = static_cast<float>(std::rand() % 360) * (pi / 180.0f);
            flake.rotation = static_cast<float>(std::rand() % 360) * (pi / 180.0f);
            flake.rotationAngle = static_cast<float>(std::rand() % 360) * (pi / 180.0f);
            flake.rotationDirection = (std::rand() % 2) ? 1.0f : -1.0f;
            flake.rotationIntensity = static_cast<float>(std::rand() % 100) / 100.0f;
            flake.rotationSpeed = flake.rotationDirection * (0.8f + flake.rotationIntensity * 2.0f) +
                                  ((static_cast<float>(std::rand() % 100) / 1000.0f) - 0.05f);
            flake.rotationSize = 0.01f + static_cast<float>(std::rand() % 100) / 4000.0f;
            flake.rotationSpeedSize = (static_cast<float>(std::rand() % 100) / 2000.0f) - 0.025f;
            flake.rotationSpeedIntensity = static_cast<float>(std::rand() % 100) / 100.0f;
            return flake;
        }

        std::vector<SnowFlake> snowflakes;
        mxvk::VK_Sprite3D *snowSprite = nullptr;
        float windTime = 0.0f;
    };

    class ModelWindow : public mxvk::VK_Window {
      public:
        ModelWindow(const std::string &filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              assetRoot((path.empty() || path == ".") ? std::string(tux_example_ASSET_DIR) : path) {
            const std::string shaderRoot = assetRoot + "/data";
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/tux.obj") : filename;
            const std::string vertPath = shaderRoot + "/model.vert.spv";
            const std::string fragPath = shaderRoot + "/model.frag.spv";
            const std::string backgroundVertPath = shaderRoot + "/background.vert.spv";
            const std::string backgroundFragPath = shaderRoot + "/background.frag.spv";
            const std::string snowflakeFragPath = shaderRoot + "/snowflake.frag.spv";
            const std::string backgroundPath = assetRoot + "/data/ant-bg.png";

            setFont(assetRoot + "/data/font.ttf", 24);
            background = createSprite(backgroundPath, backgroundVertPath, backgroundFragPath);
            snow.init(this, assetRoot, snowflakeFragPath);
            model.load(this, modelPath, "", assetRoot + "/data", 1.0f);
            model.setShaders(this, vertPath, fragPath);
        }

        ~ModelWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }
        }

        void proc() override {
            printText("Tux Example", 15, 15, {255, 255, 255, 255});
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const auto now = std::chrono::steady_clock::now();
            const float elapsedSeconds = std::chrono::duration<float>(now - start).count();
            const VkExtent2D extent = getSwapchainExtent();

            if (background != nullptr && sprite_pipeline != VK_NULL_HANDLE && sprite_pipeline_layout != VK_NULL_HANDLE) {
                background->setShaderParams(elapsedSeconds, 0.0f, 0.0f, 0.0f);
                background->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
                background->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
                // Prevent the generic overlay pass from drawing the same full-screen sprite on top.
                background->clearQueue();
            }

            const float deltaSeconds = std::chrono::duration<float>(now - lastSnowUpdate).count();
            lastSnowUpdate = now;
            snow.update(deltaSeconds);

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), elapsedSeconds * autoSpinSpeed, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);

            snow.drawLayer(cmd, imageIndex, ubo.view, ubo.proj, false);
            model.render(cmd, imageIndex, false);
            snow.drawLayer(cmd, imageIndex, ubo.view, ubo.proj, true);
        }

      private:
        std::string assetRoot;
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VKAbstractModel model{};
        SnowEffect3D snow{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastSnowUpdate{std::chrono::steady_clock::now()};
        float autoSpinSpeed = 0.65f;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::ModelWindow window(args.filename, args.path, "MXVK Tux Example", args.width, args.height, args.fullscreen, args.enable_vsync);
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
