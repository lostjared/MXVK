#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <string>

#ifndef opencv_model_ASSET_DIR
#define opencv_model_ASSET_DIR "."
#endif

#ifndef opencv_model_SHADER_DIR
#define opencv_model_SHADER_DIR "."
#endif

namespace example {

    class OpenCVModelWindow : public mxvk::VK_Window {
      public:
        OpenCVModelWindow(const Arguments &args, const std::string &title)
            : mxvk::VK_Window(title, args.width, args.height, args.fullscreen, MXVK_VALIDATION, args.enable_vsync),
              assetRoot(args.path.empty() ? std::string(opencv_model_ASSET_DIR) : args.path),
              shaderRoot(args.shaderPath.empty() ? std::string(opencv_model_SHADER_DIR) : args.shaderPath),
              cameraIndex(args.camera_index),
              fallbackWidth(args.width),
              fallbackHeight(args.height) {
            try {
                modelPath = resolveModelPath(args.filename, assetRoot);
                if (modelPath.empty()) {
                    throw mxvk::Exception("opencv_model: args.filename must point to a model file (.obj/.mxmod/.mxmod.z)");
                }
                setFont(assetRoot + "/data/font.ttf", 20);

                if (!capture.open(cameraIndex)) {
                    throw mxvk::Exception(std::format("opencv_model: failed to open camera index {}", cameraIndex));
                }

                capture.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(fallbackWidth));
                capture.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(fallbackHeight));
                fps = configureCameraFps();

                fallbackWidth = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
                fallbackHeight = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

                std::cout << "opencv_model: model='" << modelPath << "' capture="
                          << fallbackWidth << "x" << fallbackHeight << " @ " << fps << " fps\n";

                const std::string vertPath = shaderRoot + "/model.vert.spv";
                const std::string fragPath = shaderRoot + "/model.frag.spv";
                model.load(this, modelPath, "", "", 1.0f);
                model.setShaders(this, vertPath, fragPath);

                if (!capture.readToModelTexture(model, true)) {
                    std::cerr << "opencv_model: failed to upload initial camera frame\n";
                }
            } catch (...) {
                capture.close();
                if (device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(device);
                    model.cleanup(this);
                }
                throw;
            }
        }

        ~OpenCVModelWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
            capture.close();
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

                mouseYawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                mousePitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                mousePitchDegrees = std::clamp(mousePitchDegrees, -80.0f, 80.0f);

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

        void proc() override {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::clamp(
                std::chrono::duration<float>(now - lastUpdateTime).count(),
                0.0f,
                0.1f);
            lastUpdateTime = now;
            updateRotationFromKeyboard(deltaSeconds);

            if (!capture.readToModelTexture(model, true)) {
                capture.close();
                if (!capture.open(cameraIndex)) {
                    return;
                }
                capture.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(fallbackWidth));
                capture.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(fallbackHeight));
                fps = configureCameraFps();
                if (!capture.readToModelTexture(model, true)) {
                    return;
                }
            }
            updateFpsOverlay();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.model = glm::rotate(ubo.model, glm::radians(mousePitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(mouseYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, pitchRadians, glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, yawRadians + autoSpinRadians, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.15f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.32f, 0.18f + 0.12f * std::sin(elapsedSeconds * 1.2f), 0.0f);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

      private:
        [[nodiscard]] double configureCameraFps() {
            static constexpr std::array<double, 3> fpsChoices = {60.0, 30.0, 24.0};

            for (const double requestedFps : fpsChoices) {
                capture.set(cv::CAP_PROP_FPS, requestedFps);
                const double reportedFps = capture.get(cv::CAP_PROP_FPS);
                if (reportedFps > 0.0 && reportedFps + 0.5 >= requestedFps) {
                    return reportedFps;
                }
            }

            capture.set(cv::CAP_PROP_FPS, fpsChoices.back());
            const double reportedFps = capture.get(cv::CAP_PROP_FPS);
            return (reportedFps > 0.0) ? reportedFps : fpsChoices.back();
        }

        [[nodiscard]] static std::string resolveModelPath(const std::string &filename, const std::string &assetRoot) {
            if (filename.empty()) {
                return {};
            }

            namespace fs = std::filesystem;
            const fs::path requested(filename);
            std::error_code ec{};
            if (fs::exists(requested, ec)) {
                return fs::weakly_canonical(requested, ec).string();
            }
            ec.clear();

            if (requested.is_absolute()) {
                return filename;
            }

            const fs::path assetPath(assetRoot);
            const fs::path sourceRoot = assetPath.parent_path().parent_path();
            const std::array<fs::path, 3> candidates = {
                assetPath / requested,
                sourceRoot / requested,
                sourceRoot / "models" / requested.filename(),
            };

            for (const fs::path &candidate : candidates) {
                if (fs::exists(candidate, ec)) {
                    return fs::weakly_canonical(candidate, ec).string();
                }
                ec.clear();
            }

            return filename;
        }

        [[nodiscard]] static float wrapAngle(float angleRadians) {
            constexpr float TWO_PI = 6.28318530718f;
            float wrapped = std::fmod(angleRadians, TWO_PI);
            if (wrapped < 0.0f) {
                wrapped += TWO_PI;
            }
            return wrapped;
        }

        void updateRotationFromKeyboard(float deltaSeconds) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            constexpr float MANUAL_SPEED = glm::radians(120.0f);
            constexpr float AUTO_SPIN_SPEED = 0.55f;

            bool usingArrowKeys = false;
            if (keys[SDL_SCANCODE_LEFT]) {
                yawRadians -= MANUAL_SPEED * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                yawRadians += MANUAL_SPEED * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_UP]) {
                pitchRadians -= MANUAL_SPEED * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                pitchRadians += MANUAL_SPEED * deltaSeconds;
                usingArrowKeys = true;
            }

            if (!usingArrowKeys) {
                autoSpinRadians += AUTO_SPIN_SPEED * deltaSeconds;
            }

            yawRadians = wrapAngle(yawRadians);
            pitchRadians = wrapAngle(pitchRadians);
            autoSpinRadians = wrapAngle(autoSpinRadians);
        }

        void updateFpsOverlay() {
            ++fpsFrameCount;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsSampleTime).count();
            if (elapsed >= 0.25) {
                fps = static_cast<double>(fpsFrameCount) / elapsed;
                fpsFrameCount = 0;
                fpsSampleTime = now;
                fpsText = std::format("FPS: {:.1f}", fps);
            }

            printText(fpsText, 15, 15, SDL_Color{255, 255, 255, 255});
        }

        std::string assetRoot{};
        std::string shaderRoot{};
        std::string modelPath{};
        int cameraIndex = 0;
        int fallbackWidth = 1280;
        int fallbackHeight = 720;
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
        float autoSpinRadians = 0.0f;
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float mouseYawDegrees = 0.0f;
        float mousePitchDegrees = 0.0f;
        float cameraDistance = 4.2f;
        float mouseSensitivity = 0.35f;
        double fps = 0.0;
        uint32_t fpsFrameCount = 0;
        std::chrono::steady_clock::time_point fpsSampleTime{std::chrono::steady_clock::now()};
        std::string fpsText = "FPS: --";
        mxvk::VK_Capture capture{};
        mxvk::VKAbstractModel model{};
        std::chrono::steady_clock::time_point lastUpdateTime{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::now()};
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::OpenCVModelWindow window(args, "OpenCV Model Example");
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
