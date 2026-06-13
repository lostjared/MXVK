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
              assetRoot(path.empty() ? std::string(tux_example_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/tux.obj") : filename;
            const std::string vertPath = std::string(tux_example_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = std::string(tux_example_SHADER_DIR) + "/model.frag.spv";
            const std::string backgroundVertPath = std::string(tux_example_SHADER_DIR) + "/background.vert.spv";
            const std::string backgroundFragPath = std::string(tux_example_SHADER_DIR) + "/background.frag.spv";
            const std::string backgroundPath = assetRoot + "/data/ant-bg.png";

            setFont(assetRoot + "/data/font.ttf", 24);
            background = createSprite(backgroundPath, backgroundVertPath, backgroundFragPath);
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
            }
        }

        void proc() override {
            printText("Tux Example", 15, 15, {255, 255, 255, 255});
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const VkExtent2D extent = getSwapchainExtent();

            if (background != nullptr && sprite_pipeline != VK_NULL_HANDLE && sprite_pipeline_layout != VK_NULL_HANDLE) {
                background->setShaderParams(elapsedSeconds, 0.0f, 0.0f, 0.0f);
                background->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
                background->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
                // Prevent the generic overlay pass from drawing the same full-screen sprite on top.
                background->clearQueue();
            }

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), elapsedSeconds * 0.65f, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.2f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VKAbstractModel model{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
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
