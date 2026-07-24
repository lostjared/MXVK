#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_stopwatch.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#ifndef VIEWER_ASSET_DIR
#define VIEWER_ASSET_DIR "."
#endif

#ifndef VIEWER_SHADER_DIR
#define VIEWER_SHADER_DIR "."
#endif

namespace viewer {

    class ModelViewerWindow : public mxvk::VK_Window {
      public:
        explicit ModelViewerWindow(const Arguments &args)
            : mxvk::VK_Window("MXModel Viewer - [ Vulkan ]",
                              args.width,
                              args.height,
                              args.fullscreen,
                              MXVK_VALIDATION,
                              args.enable_vsync),
              assetRoot((args.path.empty() || args.path == ".") ? std::string(VIEWER_ASSET_DIR) : args.path),
              shaderRoot(args.shaderPath.empty() ? assetRoot + "/data" : args.shaderPath),
              benchmarkEnabled(args.benchmark) {
            setClearColor(0.3f, 0.3f, 0.3f, 1.0f);
            setFont(resolveFontPath(), 18);

            modelPath = resolveModelPath(args.filename.empty() ? defaultModelName : args.filename);
            textureManifestPath = resolveOptionalPath(args.texture);
            textureBasePath = args.resource_path.empty() ? resolveTextureBasePath(textureManifestPath) : resolveOptionalPath(args.resource_path);

            std::cout << "viewer: model='" << modelPath << "'\n";
            if (!textureManifestPath.empty()) {
                std::cout << "viewer: texture manifest='" << textureManifestPath << "' base='" << textureBasePath << "'\n";
            }

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setShaders(this, shaderRoot + "/model.vert.spv", shaderRoot + "/model.frag.spv");
            const std::string modelFormat =
                std::filesystem::path(modelPath).extension() == ".obj" ? "OBJ" : "model";
            benchmarkName = std::format(
                "{} geometry draw (Vulkan backend, {} frames)",
                modelFormat,
                BENCHMARK_FRAME_COUNT);
        }

        ~ModelViewerWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                    return;
                }
                if (e.key.repeat) {
                    return;
                }

                switch (e.key.key) {
                case SDLK_W:
                    wireframe = !wireframe;
                    std::cout << "viewer: wireframe " << (wireframe ? "on" : "off") << '\n';
                    return;
                case SDLK_P:
                case SDLK_R:
                    autoRotate = !autoRotate;
                    std::cout << "viewer: auto-rotate " << (autoRotate ? "on" : "off") << '\n';
                    return;
                case SDLK_H:
                case SDLK_SPACE:
                    showHelp = !showHelp;
                    return;
                case SDLK_HOME:
                    resetView();
                    return;
                case SDLK_EQUALS:
                case SDLK_PLUS:
                    adjustCameraDistance(-0.5f);
                    return;
                case SDLK_MINUS:
                    adjustCameraDistance(0.5f);
                    return;
                default:
                    break;
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
                rotationYDegrees += static_cast<float>(x - lastMouseX) * mouseSensitivity;
                rotationXDegrees += static_cast<float>(y - lastMouseY) * mouseSensitivity;
                rotationXDegrees = std::clamp(rotationXDegrees, -89.0f, 89.0f);
                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                adjustCameraDistance(-delta * 0.45f);
                return;
            }
        }

        void proc() override {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            if (keys == nullptr) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::clamp(std::chrono::duration<float>(now - lastUpdateTime).count(), 0.0f, 0.1f);
            lastUpdateTime = now;

            constexpr float ROTATE_SPEED = 120.0f;
            if (keys[SDL_SCANCODE_LEFT]) {
                rotationYDegrees -= ROTATE_SPEED * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                rotationYDegrees += ROTATE_SPEED * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_UP]) {
                rotationXDegrees -= ROTATE_SPEED * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_DOWN]) {
                rotationXDegrees += ROTATE_SPEED * deltaSeconds;
            }
            if (keys[SDL_SCANCODE_A]) {
                adjustCameraDistance(-2.5f * deltaSeconds);
            }
            if (keys[SDL_SCANCODE_S]) {
                adjustCameraDistance(2.5f * deltaSeconds);
            }
            rotationXDegrees = std::clamp(rotationXDegrees, -89.0f, 89.0f);

            if (autoRotate) {
                autoRotationRadians = wrapRadians(autoRotationRadians + 0.55f * deltaSeconds);
            }

            if (!benchmarkEnabled) {
                updateOverlay();
            }
        }

        void render() override {
            if (benchmarkEnabled && benchmarkStopwatch == nullptr) {
                benchmarkStopwatch =
                    std::make_unique<StopWatch<HighResolutionClockPolicy>>(benchmarkName);
            }

            mxvk::VK_Window::render();

            if (benchmarkStopwatch == nullptr) {
                return;
            }
            ++benchmarkFrameCount;
            if (benchmarkFrameCount == BENCHMARK_FRAME_COUNT) {
                if (device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(device);
                }
                benchmarkStopwatch->Stop();
                benchmarkStopwatch.reset();
                exit();
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;
            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime).count();

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(rotationXDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(rotationYDegrees) + autoRotationRadians, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, wireframe ? 1.0f : 0.0f, 0.0f, 0.0f);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, wireframe);
        }

      private:
        [[nodiscard]] std::string resolveFontPath() const {
            const std::array<std::filesystem::path, 4> candidates = {
                std::filesystem::path(assetRoot) / "data/default.ttf",
                std::filesystem::path(assetRoot) / "data/font.ttf",
                std::filesystem::path(VIEWER_ASSET_DIR) / "data/default.ttf",
                std::filesystem::path(VIEWER_ASSET_DIR) / "data/font.ttf",
            };

            for (const std::filesystem::path &candidate : candidates) {
                if (std::filesystem::exists(candidate)) {
                    return candidate.string();
                }
            }

            return candidates.front().string();
        }

        [[nodiscard]] std::string resolveModelPath(const std::string &name) const {
            namespace fs = std::filesystem;
            const fs::path requested(name);
            std::error_code ec{};

            if (fs::exists(requested, ec)) {
                return fs::weakly_canonical(requested, ec).string();
            }
            ec.clear();

            const fs::path assetPath(assetRoot);
            const fs::path sourceRoot = fs::path(VIEWER_SOURCE_DIR).parent_path().parent_path();
            const fs::path runtimeRoot = fs::path(VIEWER_ASSET_DIR).parent_path().parent_path();
            const fs::path fileName = requested.filename();
            const fs::path withoutExtension = fileName.stem();

            const fs::path candidates[] = {
                assetPath / requested,
                assetPath / "data" / requested,
                sourceRoot / requested,
                sourceRoot / "models" / requested,
                sourceRoot / "models" / fileName,
                sourceRoot / "models" / (withoutExtension.string() + ".mxmod.z"),
                runtimeRoot / requested,
                runtimeRoot / "models" / requested,
                runtimeRoot / "models" / fileName,
                runtimeRoot / "models" / (withoutExtension.string() + ".mxmod.z"),
            };

            for (const fs::path &candidate : candidates) {
                if (fs::exists(candidate, ec)) {
                    return fs::weakly_canonical(candidate, ec).string();
                }
                ec.clear();
            }

            throw mxvk::Exception("viewer: failed to locate model: " + name);
        }

        [[nodiscard]] std::string resolveOptionalPath(const std::string &name) const {
            if (name.empty()) {
                return {};
            }

            namespace fs = std::filesystem;
            const fs::path requested(name);
            std::error_code ec{};
            if (fs::exists(requested, ec)) {
                return fs::weakly_canonical(requested, ec).string();
            }
            ec.clear();

            const fs::path assetPath(assetRoot);
            const fs::path sourceRoot = fs::path(VIEWER_SOURCE_DIR).parent_path().parent_path();
            const fs::path candidates[] = {
                assetPath / requested,
                assetPath / "data" / requested,
                sourceRoot / requested,
                sourceRoot / "models" / requested,
            };

            for (const fs::path &candidate : candidates) {
                if (fs::exists(candidate, ec)) {
                    return fs::weakly_canonical(candidate, ec).string();
                }
                ec.clear();
            }

            return name;
        }

        [[nodiscard]] static std::string resolveTextureBasePath(const std::string &manifestPath) {
            if (manifestPath.empty()) {
                return {};
            }
            return std::filesystem::path(manifestPath).parent_path().string();
        }

        void adjustCameraDistance(float delta) {
            cameraDistance = std::clamp(cameraDistance + delta, 0.4f, 100.0f);
        }

        void resetView() {
            rotationXDegrees = 0.0f;
            rotationYDegrees = 0.0f;
            autoRotationRadians = 0.0f;
            cameraDistance = 5.0f;
        }

        [[nodiscard]] static float wrapRadians(float radians) {
            constexpr float TWO_PI = 6.28318530718f;
            float wrapped = std::fmod(radians, TWO_PI);
            if (wrapped < 0.0f) {
                wrapped += TWO_PI;
            }
            return wrapped;
        }

        void updateOverlay() {
            ++frameCount;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fpsSampleTime).count();
            if (elapsed >= 0.25) {
                fpsText = std::format("FPS: {:.1f}", static_cast<double>(frameCount) / elapsed);
                frameCount = 0;
                fpsSampleTime = now;
            }

            printText(fpsText, 14, 12, SDL_Color{235, 240, 255, 255});
            printText(std::format("Model: {}", std::filesystem::path(modelPath).filename().string()), 14, 36, SDL_Color{210, 220, 235, 255});
            printText(std::format("Mode: {}  Auto: {}  Distance: {:.1f}",
                                  wireframe ? "wire" : "fill",
                                  autoRotate ? "on" : "off",
                                  cameraDistance),
                      14,
                      60,
                      SDL_Color{210, 220, 235, 255});

            if (showHelp) {
                printText("Drag/arrows rotate  Wheel/+/-/A/S zoom  W wire  R/P auto-rotate  H/Space help  Home reset  Esc quit",
                          14,
                          static_cast<int>(getSwapchainExtent().height) - 30,
                          SDL_Color{185, 198, 215, 255});
            }
        }

        static constexpr const char *defaultModelName = "cube.mxmod.z";
        static constexpr std::size_t BENCHMARK_FRAME_COUNT = 60 * 10;

        std::string assetRoot{};
        std::string shaderRoot{};
        std::string modelPath{};
        std::string textureManifestPath{};
        std::string textureBasePath{};
        mxvk::VKAbstractModel model{};
        bool wireframe = false;
        bool autoRotate = true;
        bool showHelp = true;
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float mouseSensitivity = 0.5f;
        float rotationXDegrees = 0.0f;
        float rotationYDegrees = 0.0f;
        float autoRotationRadians = 0.0f;
        float cameraDistance = 5.0f;
        uint32_t frameCount = 0;
        std::string fpsText = "FPS: --";
        std::chrono::steady_clock::time_point fpsSampleTime{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point lastUpdateTime{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::now()};
        bool benchmarkEnabled = false;
        std::size_t benchmarkFrameCount = 0;
        std::string benchmarkName;
        std::unique_ptr<StopWatch<HighResolutionClockPolicy>> benchmarkStopwatch;
    };

} // namespace viewer

int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    try {
        Arguments args = proc_args(argc, argv);
        if (args.benchmark && !args.resolutionSpecified) {
            args.width = 320;
            args.height = 180;
            std::cout << "viewer: benchmark resolution defaults to 320x180; use -r or --resolution to override\n";
        }
        viewer::ModelViewerWindow window(args);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << std::format("viewer: Exception: {}\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
