#include <cstdlib>

#include <algorithm>
#include <chrono>
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

    class ModelWindow : public mxvk::VK_Window {
      public:
        ModelWindow(const std::string filename, bool usingDefaultModel, const std::string &path, const std::string &resource, const std::string &resource_path, const std::string &fragmentShaderPath, const std::string &title, int width, int height, bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(MODEL_EXAMPLE_ASSET_DIR) : path) {
            const std::string modelPath = filename;
            const std::string textureManifestPath = resource.empty() && usingDefaultModel ? assetRoot + "/data/texture_manifest.txt" : resource;
            const bool useDefaultTextureBase = resource_path.empty() && (usingDefaultModel || !resource.empty());
            const std::string textureBasePath = resource_path.empty() ? (useDefaultTextureBase ? assetRoot + "/data" : "") : resource_path;
            const std::string vertPath = std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = fragmentShaderPath.empty() ? (std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.frag.spv") : fragmentShaderPath;

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
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

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                if (e.key.repeat) {
                    return;
                }
                autoSpinEnabled = !autoSpinEnabled;
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

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.45f;
                cameraDistance = std::clamp(cameraDistance, 1.8f, 12.0f);
                return;
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
            const float elapsedSeconds = std::chrono::duration<float>(now - startTime).count();
            lastFrameTime = now;
            if (autoSpinEnabled) {
                autoSpinRadians += deltaSeconds * autoSpinSpeed;
            }

            const VkExtent2D extent = getSwapchainExtent();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees) + autoSpinRadians, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 1.0f);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        bool mouseDragging = false;
        bool autoSpinEnabled = true;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 12.0f;
        float cameraDistance = 4.2f;
        float mouseSensitivity = 0.35f;
        float autoSpinSpeed = 0.65f;
        float autoSpinRadians = 0.0f;
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const bool usingDefaultModel = args.filename.empty();
        std::string filename = args.filename;
        if (args.filename.empty()) {
            filename = args.path + "/data/pyramid.obj";
        }
        example::ModelWindow window(filename, usingDefaultModel, args.path, args.resource, args.resource_path, args.fragmentPath, "MXVK Model Example", args.width, args.height, args.fullscreen);
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
