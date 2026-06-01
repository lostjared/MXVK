#include <cstdlib>

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
                ModelWindow(const std::string &filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
                            assetRoot_(path.empty() ? std::string(tux_example_ASSET_DIR) : path),
                            fallbackWidth_(width),
                            fallbackHeight_(height) {
                        const std::string modelPath = filename.empty() ? (assetRoot_ + "/data/tux.obj") : filename;
                    const std::string vertPath = std::string(tux_example_SHADER_DIR) + "/model.vert.spv";
                    const std::string fragPath = std::string(tux_example_SHADER_DIR) + "/model.frag.spv";
                    const std::string backgroundVertPath = std::string(tux_example_SHADER_DIR) + "/background.vert.spv";
                    const std::string backgroundFragPath = std::string(tux_example_SHADER_DIR) + "/background.frag.spv";
                    const std::string backgroundPath = assetRoot_ + "/data/ant-bg.png";

                    
                    setFont(assetRoot_ + "/data/font.ttf", 24);
                    background_ = createSprite(backgroundPath, backgroundVertPath, backgroundFragPath);
                    model_.load(this, modelPath, "", assetRoot_ + "/data", 1.0f);
                    model_.setShaders(this, vertPath, fragPath);

        }

        ~ModelWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model_.cleanup(this);
        }

        void event([[maybe_unused]] mxvk::VK_Window *window, SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc([[maybe_unused]] mxvk::VK_Window *window) override {
            if (background_ == nullptr) {
                return;
            }

            int targetWidth = fallbackWidth_;
            int targetHeight = fallbackHeight_;
            if (swapchain_extent.width > 0U && swapchain_extent.height > 0U) {
                targetWidth = static_cast<int>(swapchain_extent.width);
                targetHeight = static_cast<int>(swapchain_extent.height);
            }

            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_).count();
            background_->setShaderParams(elapsedSeconds, 0.0f, 0.0f, 0.0f);
            background_->drawSpriteRect(0, 0, targetWidth, targetHeight);

            printText("Tux Example", 15, 15, {255, 255, 255, 255});
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
            ubo.model = glm::rotate(glm::mat4(1.0f), elapsedSeconds * 0.65f, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model_.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model_.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model_.updateUBO(imageIndex, ubo);
            model_.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot_;
                mxvk::VK_Sprite *background_ = nullptr;
        mxvk::VKAbstractModel model_{};
                int fallbackWidth_ = 1280;
                int fallbackHeight_ = 720;
        std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::ModelWindow window(args.filename, args.path, "MXVK Tux Example", args.width, args.height, args.fullscreen);
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
