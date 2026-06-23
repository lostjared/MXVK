#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

#include "rain.hpp"

namespace example {

    class ModelWindow : public mxvk::VK_Window {
      public:
        ModelWindow(const std::string filename,
                    bool usingDefaultModel,
                    const std::string &path,
                    const std::string &resource,
                    const std::string &resource_path,
                    const std::string &fragmentShaderPath,
                    const std::string &title,
                    int width,
                    int height,
                    bool fullscreen,
                    bool binaryTextureMode,
                    int fontSize,
                    const std::string &fontPath,
                    const std::string &color)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(MODEL_EXAMPLE_ASSET_DIR) : path),
              binaryTextureMode(binaryTextureMode) {
            const std::string modelPath = filename;
            const std::string textureManifestPath = resource.empty() && usingDefaultModel ? assetRoot + "/data/texture_manifest.txt" : resource;
            const bool useDefaultTextureBase = resource_path.empty() && (usingDefaultModel || !resource.empty());
            const std::string textureBasePath = resource_path.empty() ? (useDefaultTextureBase ? assetRoot + "/data" : "") : resource_path;
            const std::string vertPath = std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = fragmentShaderPath.empty() ? (std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.frag.spv") : fragmentShaderPath;

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setShaders(this, vertPath, fragPath);
            model.setBackfaceCulling(!binaryTextureMode);
            if (binaryTextureMode) {
                matrix::RainConfig rainConfig = matrix::make_matrix_rain_config(assetRoot, false);
                rainConfig.surface_width = 2048;
                rainConfig.surface_height = 2048;
                rainConfig.font_size = std::max(1, fontSize);
                if (!fontPath.empty()) {
                    rainConfig.font_path = fontPath;
                }
                rainConfig.color = color;
                binaryMatrixTexture = std::make_unique<matrix::Rain>(std::move(rainConfig));
            }
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

            if (binaryTextureMode && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                if (e.key.repeat) {
                    return;
                }
                skyboxMode = !skyboxMode;
                if (skyboxMode) {
                    resetSkyboxCamera();
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
                if (e.key.key == SDLK_W) {
                    skyboxLookUpKey = pressed;
                } else if (e.key.key == SDLK_S) {
                    skyboxLookDownKey = pressed;
                } else if (e.key.key == SDLK_A) {
                    skyboxLookLeftKey = pressed;
                } else if (e.key.key == SDLK_D) {
                    skyboxLookRightKey = pressed;
                }
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

                if (skyboxMode) {
                    skyboxYawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                    skyboxPitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                    skyboxPitchDegrees = std::clamp(skyboxPitchDegrees, -85.0f, 85.0f);
                } else {
                    yawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                    pitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                    pitchDegrees = std::clamp(pitchDegrees, -80.0f, 80.0f);
                }

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL && !skyboxMode) {
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

            if (skyboxMode) {
                updateSkyboxCamera(deltaSeconds);
            }

            const VkExtent2D extent = getSwapchainExtent();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            if (skyboxMode) {
                const glm::vec3 front = skyboxForwardVector();
                const glm::vec3 target = skyboxCameraPosition + front;
                ubo.model = glm::scale(glm::mat4(1.0f), glm::vec3(model.modelRenderScale() * skyboxScaleMultiplier));
                ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
                ubo.view = glm::lookAt(skyboxCameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.proj = glm::perspective(glm::radians(70.0f), aspect, 0.02f, 100.0f);
            } else {
                ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
                ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees) + autoSpinRadians, glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
                ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
                ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            }
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.37f);

            model.updateUBO(imageIndex, ubo);
            if (binaryTextureMode && binaryMatrixTexture != nullptr) {
                binaryMatrixTexture->update(deltaSeconds);
                const int textureWidth = binaryMatrixTexture->width();
                const int textureHeight = binaryMatrixTexture->height();
                const int texturePitch = binaryMatrixTexture->pitch();
                const void *texturePixels = flippedMatrixTexturePixels(textureWidth, textureHeight, texturePitch);
                [[maybe_unused]] const bool textureUpdated = model.updatePrimaryTexture(
                    texturePixels, textureWidth, textureHeight, textureWidth * 4);
            }
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        bool binaryTextureMode = false;
        std::unique_ptr<matrix::Rain> binaryMatrixTexture{};
        bool mouseDragging = false;
        bool skyboxMode = false;
        bool skyboxLookUpKey = false;
        bool skyboxLookDownKey = false;
        bool skyboxLookLeftKey = false;
        bool skyboxLookRightKey = false;
        bool autoSpinEnabled = true;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 12.0f;
        float cameraDistance = 4.2f;
        glm::vec3 skyboxCameraPosition{0.0f, 0.0f, 0.0f};
        float skyboxYawDegrees = 180.0f;
        float skyboxPitchDegrees = 0.0f;
        float skyboxScaleMultiplier = 1.6f;
        float mouseSensitivity = 0.35f;
        float autoSpinSpeed = 0.65f;
        float autoSpinRadians = 0.0f;
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
        std::vector<Uint8> flippedMatrixTexture;

        [[nodiscard]] const void *flippedMatrixTexturePixels(int width, int height, int pitch) {
            const auto *pixels = static_cast<const Uint8 *>(binaryMatrixTexture != nullptr ? binaryMatrixTexture->pixels() : nullptr);
            if (pixels == nullptr || width <= 0 || height <= 0 || pitch <= 0) {
                return nullptr;
            }

            const int rowBytes = width * 4;
            flippedMatrixTexture.resize(static_cast<std::size_t>(rowBytes * height));
            for (int y = 0; y < height; ++y) {
                const Uint8 *src = pixels + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(pitch);
                Uint8 *dst = flippedMatrixTexture.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rowBytes);
                std::memcpy(dst, src, static_cast<std::size_t>(rowBytes));
            }
            return flippedMatrixTexture.data();
        }

        void resetSkyboxCamera() {
            skyboxCameraPosition = glm::vec3(0.0f);
            skyboxYawDegrees = 180.0f;
            skyboxPitchDegrees = 0.0f;
        }

        [[nodiscard]] glm::vec3 skyboxForwardVector() const {
            const float yawRadians = glm::radians(skyboxYawDegrees);
            const float pitchRadians = glm::radians(skyboxPitchDegrees);
            return glm::normalize(glm::vec3{
                std::sin(yawRadians) * std::cos(pitchRadians),
                std::sin(pitchRadians),
                -std::cos(yawRadians) * std::cos(pitchRadians)});
        }

        [[nodiscard]] glm::vec3 skyboxRightVector() const {
            const glm::vec3 forward = skyboxForwardVector();
            return glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        }

        void updateSkyboxCamera(float deltaSeconds) {
            const float lookSpeed = 85.0f;
            if (skyboxLookLeftKey) {
                skyboxYawDegrees -= lookSpeed * deltaSeconds;
            }
            if (skyboxLookRightKey) {
                skyboxYawDegrees += lookSpeed * deltaSeconds;
            }
            if (skyboxLookUpKey) {
                skyboxPitchDegrees += lookSpeed * deltaSeconds;
            }
            if (skyboxLookDownKey) {
                skyboxPitchDegrees -= lookSpeed * deltaSeconds;
            }
            skyboxPitchDegrees = std::clamp(skyboxPitchDegrees, -85.0f, 85.0f);
        }
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
        example::ModelWindow window(
            filename,
            usingDefaultModel,
            args.path,
            args.resource,
            args.resource_path,
            args.fragmentPath,
            "MXVK Model Example",
            args.width,
            args.height,
            args.fullscreen,
            args.binary,
            args.font_size,
            args.font_path,
            args.color);
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
