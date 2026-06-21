#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

namespace example {

    class DarkWindow : public mxvk::VK_Window {
      public:
        DarkWindow(const std::string &filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot((path.empty() || path == ".") ? std::string(DARK_ASSET_DIR) : path) {
            fallbackWidth = width;
            fallbackHeight = height;

            const std::string modelPath = filename.empty() ? (assetRoot + "/data/pyramid.obj") : filename;
            const std::string textureManifestPath = assetRoot + "/data/crystal.txt";
            const std::string textureBasePath = assetRoot + "/data";
            const std::string vertPath = std::string(DARK_SHADER_DIR) + "/dark.vert.spv";
            const std::string fragPath = std::string(DARK_SHADER_DIR) + "/dark.frag.spv";
            const std::string beamVertPath = std::string(MXVK_SPRITE_SHADER_DIR) + "/sprite.vert.spv";
            const std::string beamFragPath = std::string(DARK_SHADER_DIR) + "/beam.frag.spv";

            setFont(assetRoot + "/data/font.ttf", 48);

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setAlphaBlending(true);
            model.setShaders(this, vertPath, fragPath);

            beamSprite = createSprite(1, 1, beamVertPath, beamFragPath);
            beamSprite->setEffectsEnabled(true);
        }

        ~DarkWindow() override {
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

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                if (!e.key.repeat) {
                    autoSpinEnabled = !autoSpinEnabled;
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.35f;
                cameraDistance = std::clamp(cameraDistance, 2.0f, 10.0f);
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void proc() override {
            if (beamSprite == nullptr) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            const int width = extent.width > 0U ? static_cast<int>(extent.width) : fallbackWidth;
            const int height = extent.height > 0U ? static_cast<int>(extent.height) : fallbackHeight;

            beamSprite->setShaderParams(autoSpinRadians, elapsedSeconds, 1.0f, 0.0f);
            beamSprite->drawSpriteRect(0, 0, width, height);

            constexpr const char *titleText = "the Lunatic iS in my heaD";
            int textWidth = 0;
            int textHeight = 0;
            if (getTextDimensions(titleText, textWidth, textHeight)) {
                const int textX = std::max(0, (width - textWidth) / 2);
                printText(titleText, textX, 24, {255, 255, 255, 255});
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
            elapsedSeconds = std::chrono::duration<float>(now - startTime).count();
            lastFrameTime = now;

            if (autoSpinEnabled) {
                autoSpinRadians += deltaSeconds * 0.55f;
            }

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            const float yawRadians = glm::radians(yawDegrees);
            const float pitchRadians = glm::radians(pitchDegrees);
            const glm::vec3 cameraPos{
                cameraDistance * std::cos(pitchRadians) * std::sin(yawRadians),
                0.35f + cameraDistance * std::sin(pitchRadians),
                cameraDistance * std::cos(pitchRadians) * std::cos(yawRadians)};

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), autoSpinRadians, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale() * 1.18f));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.35f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(48.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.42f);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        mxvk::VK_Sprite *beamSprite = nullptr;
        bool autoSpinEnabled = true;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float cameraDistance = 5.35f;
        float autoSpinRadians = 0.0f;
        float elapsedSeconds = 0.0f;
        int fallbackWidth = 1280;
        int fallbackHeight = 720;
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::DarkWindow window(args.filename, args.path, "MXVK Dark Crystal Pyramid", args.width, args.height, args.fullscreen);
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
