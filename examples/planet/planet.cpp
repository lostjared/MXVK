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

    class PlanetWindow : public mxvk::VK_Window {
      public:
        PlanetWindow(const std::string &filename,
                     const std::string &path,
                     const std::string &title,
                     int width,
                     int height,
                     bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot_(path.empty() ? std::string(PLANET_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot_ + "/data/saturn.mxmod.z") : filename;
            const std::string textureManifestPath = assetRoot_ + "/data/saturn.tex";
            const std::string textureBasePath = assetRoot_ + "/data";
            const std::string vertPath = std::string(PLANET_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = std::string(PLANET_SHADER_DIR) + "/model.frag.spv";

            model_.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model_.setBackfaceCulling(false);
            model_.setShaders(this, vertPath, fragPath);
        }

        ~PlanetWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model_.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging_ = true;
                lastMouseX_ = static_cast<int>(e.button.x);
                lastMouseY_ = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging_ = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseDragging_) {
                const int x = static_cast<int>(e.motion.x);
                const int y = static_cast<int>(e.motion.y);
                const int deltaX = x - lastMouseX_;
                const int deltaY = y - lastMouseY_;

                yawDegrees_ += static_cast<float>(deltaX) * mouseSensitivity_;
                pitchDegrees_ += static_cast<float>(deltaY) * mouseSensitivity_;
                pitchDegrees_ = std::clamp(pitchDegrees_, -80.0f, 80.0f);

                lastMouseX_ = x;
                lastMouseY_ = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance_ -= delta * 0.45f;
                cameraDistance_ = std::clamp(cameraDistance_, 1.8f, 12.0f);
                return;
            }
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

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees_), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees_), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(-18.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, elapsedSeconds * 0.45f, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model_.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model_.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.15f, cameraDistance_), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model_.updateUBO(imageIndex, ubo);
            model_.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot_;
        mxvk::VKAbstractModel model_{};
        std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
        bool mouseDragging_ = false;
        int lastMouseX_ = 0;
        int lastMouseY_ = 0;
        float yawDegrees_ = 0.0f;
        float pitchDegrees_ = 0.0f;
        float cameraDistance_ = 4.6f;
        float mouseSensitivity_ = 0.35f;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::PlanetWindow window(args.filename, args.path, "MXVK Planet Example", args.width, args.height, args.fullscreen);
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
