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
        ModelWindow(const std::string filename, const std::string &path, const std::string &title, int width, int height, bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot_(path.empty() ? std::string(MODEL_EXAMPLE_ASSET_DIR) : path) {
            const std::string modelPath = filename;
            const std::string textureManifestPath = assetRoot_ + "/data/texture_manifest.txt";
            const std::string textureBasePath = assetRoot_ + "/data";
            const std::string vertPath = std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.frag.spv";

            model_.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model_.setShaders(this, vertPath, fragPath);
        }

        ~ModelWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model_.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
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
        mxvk::VKAbstractModel model_{};
        std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        std::string filename = args.filename;
        if (args.filename.empty()) {
            filename = args.path + "/data/pyramid.obj";
        }
        example::ModelWindow window(filename, args.path, "MXVK Model Example", args.width, args.height, args.fullscreen);
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
