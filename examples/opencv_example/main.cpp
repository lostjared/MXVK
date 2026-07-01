#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <opencv2/videoio.hpp>
#include <string>

#ifndef opencv_example_ASSET_DIR
#define opencv_example_ASSET_DIR "."
#endif

#ifndef opencv_example_SHADER_DIR
#define opencv_example_SHADER_DIR "."
#endif

namespace example {
    class ExampleWindow : public mxvk::VK_Window {
        std::string current_path = ".";
        std::string shader_path;
        std::string input_filename;
        double fps = 60.0f;
        int camera_index = 0;
        bool using_file = false;
        mxvk::VK_Capture capture{};
        mxvk::VK_Sprite *camera_sprite = nullptr;
        int fallback_width = 1280;
        int fallback_height = 720;
        double current_fps = 0.0;
        uint32_t fps_frame_count = 0;
        std::chrono::steady_clock::time_point fps_sample_time{std::chrono::steady_clock::now()};
        std::string fps_text = "FPS: --";

        [[nodiscard]] bool openCaptureSource() {
            if (using_file) {
                return capture.open(input_filename);
            }
            return capture.open(camera_index);
        }

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

        [[nodiscard]] bool createOrRefreshCameraSprite() {
            int frame_width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
            int frame_height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
            if (frame_width <= 0 || frame_height <= 0) {
                frame_width = fallback_width;
                frame_height = fallback_height;
            }

            const std::string vertex_shader = shader_path + "/vertex.vert.spv";
            const std::string fragment_shader = shader_path + "/fragment.frag.spv";

            if (camera_sprite == nullptr) {
                camera_sprite = createSprite(frame_width, frame_height, vertex_shader, fragment_shader);
                return camera_sprite != nullptr;
            }

            camera_sprite->createEmptySprite(frame_width, frame_height, vertex_shader, fragment_shader);
            return true;
        }

        [[nodiscard]] bool uploadFrameToSprite(cv::Mat &frame) {
            if (camera_sprite == nullptr || frame.empty()) {
                return false;
            }

            cv::Mat rgba;
            if (frame.channels() == 4) {
                cv::cvtColor(frame, rgba, cv::COLOR_BGRA2RGBA);
            } else if (frame.channels() == 3) {
                cv::cvtColor(frame, rgba, cv::COLOR_BGR2RGBA);
            } else if (frame.channels() == 1) {
                cv::cvtColor(frame, rgba, cv::COLOR_GRAY2RGBA);
            } else {
                return false;
            }

            camera_sprite->updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
            return true;
        }

        void initializeCameraRendering() {
            if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
                createDevice();
            }

            if (!createOrRefreshCameraSprite()) {
                throw mxvk::Exception("opencv_example: failed to create capture sprite");
            }

            if (camera_sprite != nullptr && !capture.readToSprite(*camera_sprite)) {
                std::cerr << "opencv_example: failed to upload initial camera frame\n";
            }
        }

        void updateFpsOverlay() {
            ++fps_frame_count;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fps_sample_time).count();
            if (elapsed >= 0.25) {
                current_fps = static_cast<double>(fps_frame_count) / elapsed;
                fps_frame_count = 0;
                fps_sample_time = now;
                fps_text = std::format("FPS: {:.1f}", current_fps);
            }

            printText(fps_text, 15, 15, SDL_Color{255, 255, 255, 255});
        }

      public:
        ExampleWindow(const Arguments &args, const std::string &text) : mxvk::VK_Window(text, args.width, args.height, args.fullscreen, MXVK_VALIDATION, args.enable_vsync) {
            current_path = args.path.empty() ? std::string(opencv_example_ASSET_DIR) : args.path;
            shader_path = args.shaderPath.empty() ? std::string(opencv_example_SHADER_DIR) : args.shaderPath;
            setFont(current_path + "/data/font.ttf", 20);
            input_filename = args.filename;
            camera_index = args.camera_index;
            using_file = !input_filename.empty();
            if (!openCaptureSource()) {
                if (using_file) {
                    throw mxvk::Exception(std::format("opencv_example: failed to open video file '{}'", input_filename));
                }
                throw mxvk::Exception(std::format("opencv_example: failed to open camera index {}", camera_index));
            }
            fallback_width = args.width;
            fallback_height = args.height;
            if (!using_file) {
                capture.set(cv::CAP_PROP_FRAME_WIDTH, fallback_width);
                capture.set(cv::CAP_PROP_FRAME_HEIGHT, fallback_height);
                fallback_width = capture.get(cv::CAP_PROP_FRAME_WIDTH);
                fallback_height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
                fallback_height = args.height;
                fps = configureCameraFps();
            } else {
                fps = capture.get(cv::CAP_PROP_FPS);
            }
            std::cout << "mxvk_cv: Capture opened at: " << fallback_width << "x" << fallback_height << " @ " << fps << " fps\n";
            initializeCameraRendering();
        }

        ~ExampleWindow() override {
            capture.close();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void onSwapchainRecreated() override {
            initializeCameraRendering();
        }

        void proc() override {
            if (camera_sprite == nullptr || !capture.readToSprite(*camera_sprite)) {
                if (using_file) {
                    capture.close();
                    if (openCaptureSource()) {
                        if (camera_sprite != nullptr && !capture.readToSprite(*camera_sprite)) {
                            std::cerr << "opencv_example: failed to upload restarted stream frame\n";
                        }
                    }
                } else {
                    fps = configureCameraFps();
                }
                return;
            }

            int target_w = fallback_width;
            int target_h = fallback_height;
            if (swapchain_extent.width > 0U && swapchain_extent.height > 0U) {
                target_w = static_cast<int>(swapchain_extent.width);
                target_h = static_cast<int>(swapchain_extent.height);
            }

            if (camera_sprite) {
                camera_sprite->drawSpriteRect(0, 0, target_w, target_h);
            }
            updateFpsOverlay();
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args, "OpenCV Example");
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
    }
    return EXIT_SUCCESS;
}
