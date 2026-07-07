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
        DarkWindow(const std::string &filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              assetRoot((path.empty() || path == ".") ? std::string(DARK_ASSET_DIR) : path) {
            fallbackWidth = width;
            fallbackHeight = height;

            const std::string shaderRoot = assetRoot + "/data";
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/pyramid.obj") : filename;
            const std::string beamModelPath = assetRoot + "/data/beam.obj";
            const std::string vertPath = shaderRoot + "/dark.vert.spv";
            const std::string fragPath = shaderRoot + "/dark.frag.spv";
            const std::string beamVertPath = shaderRoot + "/beam3d.vert.spv";
            const std::string beamFragPath = shaderRoot + "/beam.frag.spv";

            setFont(assetRoot + "/data/font.ttf", 48);

            beamModel.load(this, beamModelPath, "", "", 1.0f);
            beamModel.setAlphaBlending(true);
            beamModel.setShaders(this, beamVertPath, beamFragPath);

            model.load(this, modelPath, "", "", 1.0f);
            model.setAlphaBlending(true);
            model.setShaders(this, vertPath, fragPath);
        }

        ~DarkWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            beamModel.cleanup(this);
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = true;
                lastMouseX = static_cast<int>(e.button.x);
                lastMouseY = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseDragging) {
                const int x = static_cast<int>(e.motion.x);
                const int y = static_cast<int>(e.motion.y);
                const int deltaX = x - lastMouseX;
                const int deltaY = y - lastMouseY;

                yawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                pitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                pitchDegrees = std::clamp(pitchDegrees, -55.0f, 55.0f);

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.35f;
                cameraDistance = std::clamp(cameraDistance, 2.0f, 10.0f);
                return;
            }
        }

        void onSwapchainRecreated() override {
            beamModel.resize(this);
            model.resize(this);
        }

        void proc() override {
            const VkExtent2D extent = getSwapchainExtent();
            const int width = extent.width > 0U ? static_cast<int>(extent.width) : fallbackWidth;

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

            mxvk::UniformBufferObject beamUbo{};
            beamUbo.model = glm::mat4(1.0f);
            beamUbo.view = ubo.view;
            beamUbo.proj = ubo.proj;
            beamUbo.fx = glm::vec4(elapsedSeconds, autoSpinRadians, 1.0f, 1.0f);

            beamModel.updateUBO(imageIndex, beamUbo);
            beamModel.render(cmd, imageIndex, false);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel beamModel{};
        mxvk::VKAbstractModel model{};
        bool mouseDragging = false;
        bool autoSpinEnabled = true;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float cameraDistance = 5.35f;
        float mouseSensitivity = 0.35f;
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
        example::DarkWindow window(args.filename, args.path, "MXVK Dark Crystal Pyramid", args.width, args.height, args.fullscreen, args.enable_vsync);
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
