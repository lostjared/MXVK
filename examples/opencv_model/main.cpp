#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <algorithm>
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
            : mxvk::VK_Window(title, args.width, args.height, args.fullscreen, MXVK_VALIDATION),
              assetRoot_(args.path.empty() ? std::string(opencv_model_ASSET_DIR) : args.path),
              shaderRoot_(args.shaderPath.empty() ? std::string(opencv_model_SHADER_DIR) : args.shaderPath),
              cameraIndex_(args.camera_index),
              fallbackWidth_(args.width),
              fallbackHeight_(args.height) {
            try {
                modelPath_ = resolveModelPath(args.filename, assetRoot_);
                if (modelPath_.empty()) {
                    throw mxvk::Exception("opencv_model: args.filename must point to a model file (.obj/.mxmod/.mxmod.z)");
                }
                setFont(assetRoot_ + "/data/font.ttf", 20);

                if (!capture_.open(cameraIndex_)) {
                    throw mxvk::Exception(std::format("opencv_model: failed to open camera index {}", cameraIndex_));
                }

                capture_.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(fallbackWidth_));
                capture_.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(fallbackHeight_));

                fallbackWidth_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
                fallbackHeight_ = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
                fps_ = capture_.get(cv::CAP_PROP_FPS);

                std::cout << "opencv_model: model='" << modelPath_ << "' capture="
                          << fallbackWidth_ << "x" << fallbackHeight_ << " @ " << fps_ << " fps\n";

                const std::string vertPath = shaderRoot_ + "/model.vert.spv";
                const std::string fragPath = shaderRoot_ + "/model.frag.spv";
                model_.load(this, modelPath_, "", "", 1.0f);
                model_.setShaders(this, vertPath, fragPath);

                if (!capture_.readToModelTexture(model_, true)) {
                    std::cerr << "opencv_model: failed to upload initial camera frame\n";
                }
            } catch (...) {
                capture_.close();
                if (device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(device);
                    model_.cleanup(this);
                }
                throw;
            }
        }

        ~OpenCVModelWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model_.cleanup(this);
            capture_.close();
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

                mouseYawDegrees_ += static_cast<float>(deltaX) * mouseSensitivity_;
                mousePitchDegrees_ += static_cast<float>(deltaY) * mouseSensitivity_;
                mousePitchDegrees_ = std::clamp(mousePitchDegrees_, -80.0f, 80.0f);

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

        void proc() override {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::clamp(
                std::chrono::duration<float>(now - lastUpdateTime_).count(),
                0.0f,
                0.1f);
            lastUpdateTime_ = now;
            updateRotationFromKeyboard(deltaSeconds);

            if (!capture_.readToModelTexture(model_, true)) {
                capture_.close();
                if (!capture_.open(cameraIndex_)) {
                    return;
                }
                if (!capture_.readToModelTexture(model_, true)) {
                    return;
                }
            }
            updateFpsOverlay();
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::mat4(1.0f);
            ubo.model = glm::rotate(ubo.model, glm::radians(mousePitchDegrees_), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(mouseYawDegrees_), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, pitchRadians_, glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, yawRadians_ + autoSpinRadians_, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model_.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model_.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.15f, cameraDistance_), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.32f, 0.18f + 0.12f * std::sin(elapsedSeconds * 1.2f), 0.0f);

            model_.updateUBO(imageIndex, ubo);
            model_.render(cmd, imageIndex, false);
        }

      private:
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
            constexpr float kTwoPi = 6.28318530718f;
            float wrapped = std::fmod(angleRadians, kTwoPi);
            if (wrapped < 0.0f) {
                wrapped += kTwoPi;
            }
            return wrapped;
        }

        void updateRotationFromKeyboard(float deltaSeconds) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            constexpr float kManualSpeed = glm::radians(120.0f);
            constexpr float kAutoSpinSpeed = 0.55f;

            bool usingArrowKeys = false;
            if (keys[SDL_SCANCODE_LEFT]) {
                yawRadians_ -= kManualSpeed * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                yawRadians_ += kManualSpeed * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_UP]) {
                pitchRadians_ -= kManualSpeed * deltaSeconds;
                usingArrowKeys = true;
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                pitchRadians_ += kManualSpeed * deltaSeconds;
                usingArrowKeys = true;
            }

            if (!usingArrowKeys) {
                autoSpinRadians_ += kAutoSpinSpeed * deltaSeconds;
            }

            yawRadians_ = wrapAngle(yawRadians_);
            pitchRadians_ = wrapAngle(pitchRadians_);
            autoSpinRadians_ = wrapAngle(autoSpinRadians_);
        }

        void updateFpsOverlay() {
            ++fpsFrameCount_;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsSampleTime_).count();
            if (elapsed >= 0.25) {
                fps_ = static_cast<double>(fpsFrameCount_) / elapsed;
                fpsFrameCount_ = 0;
                fpsSampleTime_ = now;
                fpsText_ = std::format("FPS: {:.1f}", fps_);
            }

            printText(fpsText_, 15, 15, SDL_Color{255, 255, 255, 255});
        }

        std::string assetRoot_{};
        std::string shaderRoot_{};
        std::string modelPath_{};
        int cameraIndex_ = 0;
        int fallbackWidth_ = 1280;
        int fallbackHeight_ = 720;
        float yawRadians_ = 0.0f;
        float pitchRadians_ = 0.0f;
        float autoSpinRadians_ = 0.0f;
        bool mouseDragging_ = false;
        int lastMouseX_ = 0;
        int lastMouseY_ = 0;
        float mouseYawDegrees_ = 0.0f;
        float mousePitchDegrees_ = 0.0f;
        float cameraDistance_ = 4.2f;
        float mouseSensitivity_ = 0.35f;
        double fps_ = 0.0;
        uint32_t fpsFrameCount_ = 0;
        std::chrono::steady_clock::time_point fpsSampleTime_{std::chrono::steady_clock::now()};
        std::string fpsText_ = "FPS: --";
        mxvk::VK_Capture capture_{};
        mxvk::VKAbstractModel model_{};
        std::chrono::steady_clock::time_point lastUpdateTime_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point startTime_{std::chrono::steady_clock::now()};
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
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
