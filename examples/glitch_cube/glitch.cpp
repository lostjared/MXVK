#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <chrono>
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
              assetRoot_(path.empty() ? std::string(GLITCH_CUBE_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot_ + "/data/cube.mxmod.z") : filename;
            const std::string textureManifestPath = assetRoot_ + "/data/cube.tex";
            const std::string textureBasePath = assetRoot_ + "/data";
            const std::string modelVertPath = std::string(GLITCH_CUBE_SHADER_DIR) + "/model.vert.spv";
            const std::string modelFragPath = std::string(GLITCH_CUBE_SHADER_DIR) + "/model.frag.spv";
            setFont(assetRoot_ + "/data/font.ttf", 22);
            model_.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model_.setShaders(this, modelVertPath, modelFragPath);
        }

        ~GlitchCubeWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model_.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type != SDL_EVENT_KEY_DOWN) {
                return;
            }

            switch (e.key.key) {
            case SDLK_ESCAPE:
                exit();
                break;
            case SDLK_SPACE:
                rotXFactor_ = (rotXFactor_ > 0.0f) ? 0.0f : 1.0f;
                break;
            case SDLK_PAGEUP:
                cubeScale_ += 0.1f;
                if (cubeScale_ > 4.0f) {
                    cubeScale_ = 4.0f;
                }
                break;
            case SDLK_PAGEDOWN:
                cubeScale_ -= 0.1f;
                if (cubeScale_ < 0.2f) {
                    cubeScale_ = 0.2f;
                }
                break;
            default:
                break;
            }
        }

        void proc() override {
            printText("Press Arrow keys / Page Up Down to move light source Enter to reset", 25, 25, {255, 255, 255, 255});
            printText("Press Space to rotate cube", 25, 60, {255, 255, 255, 255});
            printText(std::format("Current scale: {:.1f}", cubeScale_), 25, 95, {230, 245, 255, 255});
        }

        void onSwapchainRecreated() override {
            model_.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
            const VkExtent2D extent = getSwapchainExtent();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            // Legacy glitch_cube rotation cadence was +50 degrees per second.
            const float rotationAngle = elapsedSeconds * glm::radians(50.0f);

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(rotXFactor_, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model_.modelRenderScale() * cubeScale_));
            ubo.model = glm::translate(ubo.model, model_.modelCenterOffset());

            ubo.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model_.updateUBO(imageIndex, ubo);
            model_.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot_;
        mxvk::VKAbstractModel model_{};
        float rotXFactor_ = 1.0f;
        float cubeScale_ = 1.0f;
        std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
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