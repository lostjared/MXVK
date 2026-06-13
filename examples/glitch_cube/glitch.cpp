#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <string>

namespace example {

    class GlitchCubeWindow : public mxvk::VK_Window {
      public:
        GlitchCubeWindow(const std::string &filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(GLITCH_CUBE_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/cube.mxmod.z") : filename;
            const std::string textureManifestPath = assetRoot + "/data/cube.tex";
            const std::string textureBasePath = assetRoot + "/data";
            const std::string modelVertPath = std::string(GLITCH_CUBE_SHADER_DIR) + "/model.vert.spv";
            const std::string modelFragPath = std::string(GLITCH_CUBE_SHADER_DIR) + "/model.frag.spv";
            setFont(assetRoot + "/data/font.ttf", 22);
            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setShaders(this, modelVertPath, modelFragPath);
        }

        ~GlitchCubeWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                switch (e.key.key) {
                case SDLK_ESCAPE:
                    exit();
                    break;
                case SDLK_SPACE:
                    rotXFactor = (rotXFactor > 0.0f) ? 0.0f : 1.0f;
                    break;
                case SDLK_PAGEUP:
                    cubeScale += 0.1f;
                    if (cubeScale > 4.0f) {
                        cubeScale = 4.0f;
                    }
                    break;
                case SDLK_PAGEDOWN:
                    cubeScale -= 0.1f;
                    if (cubeScale < 0.2f) {
                        cubeScale = 0.2f;
                    }
                    break;
                default:
                    break;
                }
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
                pitchDegrees = std::clamp(pitchDegrees, -80.0f, 80.0f);

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.45f;
                cameraDistance = std::clamp(cameraDistance, 1.8f, 12.0f);
            }
        }

        void proc() override {
            printText("Drag left mouse to orbit the cube", 25, 25, {255, 255, 255, 255});
            printText("Mouse wheel zooms the camera", 25, 60, {255, 255, 255, 255});
            printText("Space toggles the rotation axis", 25, 95, {255, 255, 255, 255});
            printText("Page Up / Page Down adjust cube scale", 25, 130, {255, 255, 255, 255});
            printText(std::format("Current scale: {:.1f}", cubeScale), 25, 165, {230, 245, 255, 255});
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const VkExtent2D extent = getSwapchainExtent();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            const float rotationAngle = elapsedSeconds * glm::radians(50.0f);

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(rotXFactor, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale() * cubeScale));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());

            const float yawRadians = glm::radians(yawDegrees);
            const float pitchRadians = glm::radians(pitchDegrees);
            const glm::vec3 cameraPos{
                cameraDistance * std::cos(pitchRadians) * std::sin(yawRadians),
                cameraDistance * std::sin(pitchRadians),
                cameraDistance * std::cos(pitchRadians) * std::cos(yawRadians)};

            ubo.view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        float rotXFactor = 1.0f;
        float cubeScale = 1.0f;
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 12.0f;
        float cameraDistance = 5.0f;
        float mouseSensitivity = 0.35f;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::GlitchCubeWindow window(args.filename, args.path, "MXVK Glitch Cube", args.width, args.height, args.fullscreen);
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
