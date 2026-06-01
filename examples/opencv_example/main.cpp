#include "mxvk/argz.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
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
        std::string shader_path_;
        std::string input_filename_;
        double fps = 60.0f;
        int camera_index_ = 0;
        bool using_file_ = false;
        mxvk::VK_Capture capture_{};
        mxvk::VK_Sprite *camera_sprite_ = nullptr;
        int fallback_width_ = 1280;
        int fallback_height_ = 720;

        [[nodiscard]] bool openCaptureSource() {
            if (using_file_) {
                return capture_.open(input_filename_);
            }
            return capture_.open(camera_index_);
        }

        [[nodiscard]] bool createOrRefreshCameraSprite() {
            int frame_width = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
            int frame_height = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
            if (frame_width <= 0 || frame_height <= 0) {
                frame_width = fallback_width_;
                frame_height = fallback_height_;
            }

            const std::string vertex_shader = shader_path_ + "/vertex.vert.spv";
            const std::string fragment_shader = shader_path_ + "/fragment.frag.spv";

            if (camera_sprite_ == nullptr) {
                camera_sprite_ = createSprite(frame_width, frame_height, vertex_shader, fragment_shader);
                return camera_sprite_ != nullptr;
            }

            camera_sprite_->createEmptySprite(frame_width, frame_height, vertex_shader, fragment_shader);
            return true;
        }

        [[nodiscard]] bool uploadFrameToSprite(cv::Mat &frame) {
            if (camera_sprite_ == nullptr || frame.empty()) {
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

            camera_sprite_->updateTexture(rgba.ptr(), rgba.cols, rgba.rows, static_cast<int>(rgba.step));
            return true;
        }

        void initializeCameraRendering() {
            if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
                createDevice();
            }

            if (!createOrRefreshCameraSprite()) {
                throw mxvk::Exception("opencv_example: failed to create capture sprite");
            }

            cv::Mat frame;
            if (capture_.read(frame)) {
                if (!uploadFrameToSprite(frame)) {
                    std::cerr << "opencv_example: failed to upload initial camera frame\n";
                }
            }
        }

      public:
        ExampleWindow(const Arguments &args, const std::string &text) : mxvk::VK_Window(text, args.width, args.height, args.fullscreen, MXVK_VALIDATION) {
            current_path = args.path.empty() ? std::string(opencv_example_ASSET_DIR) : args.path;
            shader_path_ = args.shaderPath.empty() ? std::string(opencv_example_SHADER_DIR) : args.shaderPath;
            input_filename_ = args.filename;
            camera_index_ = args.camera_index;
            using_file_ = !input_filename_.empty();
            if (!openCaptureSource()) {
                if (using_file_) {
                    throw mxvk::Exception(std::format("opencv_example: failed to open video file '{}'", input_filename_));
                }
                throw mxvk::Exception(std::format("opencv_example: failed to open camera index {}", camera_index_));
            }
            fallback_width_ = args.width;
            fallback_height_ = args.height;
            if(!using_file_) {
                capture_.set(cv::CAP_PROP_FRAME_WIDTH, fallback_width_);
                capture_.set(cv::CAP_PROP_FRAME_HEIGHT, fallback_height_);
                fallback_width_ = capture_.get(cv::CAP_PROP_FRAME_WIDTH);
                fallback_height_ = capture_.get(cv::CAP_PROP_FRAME_HEIGHT);
                fallback_height_ = args.height;
                capture_.set(cv::CAP_PROP_FPS, fps);
            }
            fps = capture_.get(cv::CAP_PROP_FPS);
            std::cout << "mxvk_cv: Capture opened at: " << fallback_width_ << "x" << fallback_height_ <<  " @ " << fps << " fps\n";
            initializeCameraRendering();
        }

        ~ExampleWindow() override {
            capture_.close();
        }

        void event([[maybe_unused]] mxvk::VK_Window *window, SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void onSwapchainRecreated() override {
            initializeCameraRendering();
        }

        void proc([[maybe_unused]] mxvk::VK_Window *window) override {
            cv::Mat frame;
            if (!capture_.read(frame)) {
                if (using_file_) {
                    capture_.close();
                    if (openCaptureSource()) {
                        if (capture_.read(frame)) {
                            if (!uploadFrameToSprite(frame)) {
                                std::cerr << "opencv_example: failed to upload restarted stream frame\n";
                            }
                        }
                    }
                }
                return;
            }

            if (!uploadFrameToSprite(frame)) {
                return;
            }

            int target_w = fallback_width_;
            int target_h = fallback_height_;
            if (swapchain_extent.width > 0U && swapchain_extent.height > 0U) {
                target_w = static_cast<int>(swapchain_extent.width);
                target_h = static_cast<int>(swapchain_extent.height);
            }

            if (camera_sprite_) {
                camera_sprite_->drawSpriteRect(0, 0, target_w, target_h);
            }
        }
    };
}

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args, "OpenCV Example");
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
    }
    return EXIT_SUCCESS;
}
