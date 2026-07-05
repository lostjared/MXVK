#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "rain.hpp"

namespace example {
    namespace {
        constexpr int MATRIX_SURFACE_WIDTH = 1280;
        constexpr int MATRIX_SURFACE_HEIGHT = 720;
    }

    using Clock = std::chrono::steady_clock;

    class PlanetWindow;

    class MatrixRainBackdrop {
      public:
        MatrixRainBackdrop(PlanetWindow &window,
                           const std::string &assetRoot,
                           bool binaryGlyphMode,
                           int fontSize,
                           const std::string &fontPath,
                           const std::string &color);
        ~MatrixRainBackdrop();

        void onSwapchainAboutToRecreate();
        void onSwapchainRecreated(PlanetWindow &window);
        void updateAndRender(PlanetWindow &window, VkCommandBuffer cmd);

      private:
        std::unique_ptr<matrix::Rain> rain;
        Clock::time_point lastFrame{Clock::now()};
    };

    class PlanetWindow : public mxvk::VK_Window {
      public:
        PlanetWindow(const std::string &filename,
                     const std::string &path,
                     const std::string &title,
                     int width,
                     int height,
                     bool fullscreen,
                     bool enable_vsync,
                     bool binary,
                     int font_size,
                     const std::string &font_path,
                     const std::string &color)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              assetRoot(path.empty() ? std::string(PLANET_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/saturn.mxmod.z") : filename;
            const std::string textureManifestPath = assetRoot + "/data/saturn.tex";
            const std::string textureBasePath = assetRoot + "/data";
            const std::string vertPath = assetRoot + "/data/model.vert.spv";
            const std::string fragPath = assetRoot + "/data/model.frag.spv";

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setBackfaceCulling(false);
            model.setShaders(this, vertPath, fragPath);

            backdrop = std::make_unique<MatrixRainBackdrop>(*this, assetRoot, binary, font_size, font_path, color);
        }

        ~PlanetWindow() override {
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
                return;
            }
        }

        void onSwapchainAboutToRecreate() override {
            if (backdrop) {
                backdrop->onSwapchainAboutToRecreate();
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
            if (backdrop) {
                backdrop->onSwapchainRecreated(*this);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (backdrop) {
                backdrop->updateAndRender(*this, cmd);
            }

            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(-18.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, elapsedSeconds * 0.45f, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.15f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

        void renderBackgroundSprite(mxvk::VK_Sprite &sprite, VkCommandBuffer cmd) {
            renderStandaloneSprite(sprite, cmd);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        std::unique_ptr<MatrixRainBackdrop> backdrop{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float cameraDistance = 4.6f;
        float mouseSensitivity = 0.35f;
    };

    MatrixRainBackdrop::MatrixRainBackdrop(PlanetWindow &window,
                                           const std::string &assetRootPath,
                                           bool binaryGlyphMode,
                                           int fontSize,
                                           const std::string &fontPath,
                                           const std::string &color)
        : rain([&]() {
              matrix::RainConfig config = matrix::make_matrix_rain_config(assetRootPath, binaryGlyphMode);
              config.surface_width = MATRIX_SURFACE_WIDTH;
              config.surface_height = MATRIX_SURFACE_HEIGHT;
              if (binaryGlyphMode && fontPath.empty()) {
                  config.font_path = assetRootPath + "/data/binary.ttf";
              }
              config.font_size = std::max(1, fontSize);
              if (!fontPath.empty()) {
                  config.font_path = fontPath;
              }
              config.color = color;
              return std::make_unique<matrix::Rain>(window, std::move(config));
          }()) {
        lastFrame = Clock::now();
    }

    MatrixRainBackdrop::~MatrixRainBackdrop() {
    }

    void MatrixRainBackdrop::onSwapchainAboutToRecreate() {
    }

    void MatrixRainBackdrop::onSwapchainRecreated(PlanetWindow &window) {
        if (rain) {
            rain->resize(window);
        }
    }

    void MatrixRainBackdrop::updateAndRender(PlanetWindow &window, VkCommandBuffer cmd) {
        const auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);

        if (rain == nullptr) {
            return;
        }

        rain->resize(window);
        rain->update(dt);
        const VkExtent2D extent = window.getSwapchainExtent();
        rain->render(static_cast<int>(extent.width), static_cast<int>(extent.height));
        if (rain->sprite() != nullptr) {
            window.renderBackgroundSprite(*rain->sprite(), cmd);
            rain->sprite()->clearQueue();
        }
    }
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::PlanetWindow window(
            args.filename, args.path, "MXVK Planet Example", args.width, args.height, args.fullscreen, args.enable_vsync, args.binary,
            args.font_size, args.font_path, args.color);
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
