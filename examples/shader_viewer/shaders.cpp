#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>
#include <vector>

#ifndef shader_viewer_ASSET_DIR
#define shader_viewer_ASSET_DIR "."
#endif

#ifndef shader_viewer_SHADER_DIR
#define shader_viewer_SHADER_DIR "."
#endif

namespace example {
    [[nodiscard]] std::string trimLine(const std::string &text) {
        auto begin = text.begin();
        while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
            ++begin;
        }

        auto end = text.end();
        while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
            --end;
        }

        return std::string(begin, end);
    }

    [[nodiscard]] std::string joinPath(const std::string &base, const std::string &file) {
        const std::filesystem::path file_path(file);
        if (base.empty() || file_path.is_absolute()) {
            return file_path.string();
        }
        return (std::filesystem::path(base) / file_path).string();
    }

    [[nodiscard]] std::string resolveShaderEntry(const std::string &shader_path, const std::string &entry) {
        const std::filesystem::path entry_path(entry);
        if (entry_path.extension() == ".spv") {
            return joinPath(shader_path, entry);
        }

        std::filesystem::path spv_entry = entry_path.parent_path() / "spv" / entry_path.stem();
        spv_entry.replace_extension(".spv");
        const std::filesystem::path spv_path = std::filesystem::path(shader_path) / spv_entry;
        if (std::filesystem::exists(spv_path)) {
            return spv_path.string();
        }

        std::filesystem::path sibling_entry = entry_path;
        sibling_entry.replace_extension(".spv");
        const std::filesystem::path sibling_spv_path = std::filesystem::path(shader_path) / sibling_entry;
        if (std::filesystem::exists(sibling_spv_path)) {
            return sibling_spv_path.string();
        }

        return joinPath(shader_path, entry);
    }

    class ExampleWindow : public mxvk::VK_Window {
        std::string current_path = ".";
        std::string shader_path;
        std::string input_filename;
        std::vector<std::string> shader_files;
        int current_shader_index = 0;
        double fps = 60.0f;
        double requested_fps = 0.0;
        int camera_index = 0;
        bool using_file = false;
        bool shader_list_requested = false;
        mxvk::VK_Capture capture{};
        mxvk::VK_Sprite *camera_sprite = nullptr;
        std::thread capture_thread{};
        std::atomic_bool capture_thread_active{false};
        std::mutex capture_mutex{};
        cv::Mat latest_capture_frame{};
        bool latest_capture_frame_available = false;
        int fallback_width = 1280;
        int fallback_height = 720;
        double current_fps = 0.0;
        double current_capture_fps = 0.0;
        uint32_t fps_frame_count = 0;
        uint32_t capture_fps_frame_count = 0;
        std::chrono::steady_clock::time_point fps_sample_time{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point capture_fps_sample_time{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point shader_start_time{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point previous_shader_frame_time{shader_start_time};
        uint32_t shader_frame_count = 0;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool mouse_pressed = false;
        std::string fps_text = "FPS: --";

        [[nodiscard]] bool openCaptureSource() {
            if (using_file) {
                return capture.open(input_filename);
            }
            return capture.open(camera_index);
        }

        void loadShaderIndex() {
            shader_files.clear();
            current_shader_index = 0;

            const std::string index_path = joinPath(shader_path, "index.txt");
            std::ifstream input(index_path);
            if (!input.is_open()) {
                if (shader_list_requested) {
                    throw mxvk::Exception(std::format("shader_viewer: failed to open shader index '{}'", index_path));
                }
                return;
            }

            std::string line;
            while (std::getline(input, line)) {
                const std::string entry = trimLine(line);
                if (entry.empty() || entry.front() == '#') {
                    continue;
                }
                const std::string shader_file = resolveShaderEntry(shader_path, entry);
                if (std::filesystem::path(shader_file).extension() != ".spv") {
                    std::cerr << std::format("shader_viewer: skipping non-SPIR-V shader entry '{}'\n", entry);
                    continue;
                }
                shader_files.push_back(shader_file);
            }

            if (shader_list_requested && shader_files.empty()) {
                throw mxvk::Exception(std::format("shader_viewer: shader index '{}' did not list any shaders", index_path));
            }
        }

        void setInitialShaderIndex(int index) {
            if (shader_files.empty()) {
                current_shader_index = 0;
                return;
            }

            const int shader_count = static_cast<int>(shader_files.size());
            current_shader_index = index % shader_count;
            if (current_shader_index < 0) {
                current_shader_index += shader_count;
            }
        }

        [[nodiscard]] std::string currentFragmentShader() const {
            if (shader_files.empty()) {
                return joinPath(shader_path, "fragment.frag.spv");
            }
            return shader_files[static_cast<std::size_t>(current_shader_index)];
        }

        void enableShaderUniforms() {
            if (camera_sprite == nullptr) {
                return;
            }
            camera_sprite->enableExtendedUBO();
        }

        void updateShaderUniforms(int target_w, int target_h) {
            if (camera_sprite == nullptr) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed_seconds = std::chrono::duration<float>(now - shader_start_time).count();
            const float delta_seconds = std::chrono::duration<float>(now - previous_shader_frame_time).count();
            previous_shader_frame_time = now;
            ++shader_frame_count;

            const float frame_rate = (delta_seconds > 0.0f) ? (1.0f / delta_seconds) : 0.0f;
            camera_sprite->setShaderParams(1.0f, 1.0f, 1.0f, elapsed_seconds);
            camera_sprite->setMouseState(mouse_x, mouse_y, mouse_pressed ? 1.0f : 0.0f);
            camera_sprite->setUniform0(1.0f, 1.0f, static_cast<float>(target_w), static_cast<float>(target_h));
            camera_sprite->setUniform1(delta_seconds, 0.0f, 0.0f, frame_rate);
            camera_sprite->setUniform2(static_cast<float>(shader_frame_count), elapsed_seconds, 48000.0f, 0.0f);
        }

        void updateCaptureFpsSample() {
            ++capture_fps_frame_count;
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - capture_fps_sample_time).count();
            if (elapsed < 0.5) {
                return;
            }

            std::lock_guard<std::mutex> lock(capture_mutex);
            current_capture_fps = static_cast<double>(capture_fps_frame_count) / elapsed;
            capture_fps_frame_count = 0;
            capture_fps_sample_time = now;
        }

        void startCaptureThread() {
            if (using_file || capture_thread_active.load()) {
                return;
            }

            capture_thread_active = true;
            capture_fps_sample_time = std::chrono::steady_clock::now();
            capture_thread = std::thread([this]() {
                while (capture_thread_active.load()) {
                    cv::Mat frame;
                    if (!capture.read(frame) || frame.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(capture_mutex);
                        latest_capture_frame = frame.clone();
                        latest_capture_frame_available = true;
                    }
                    updateCaptureFpsSample();
                }
            });
        }

        void stopCaptureThread() {
            capture_thread_active = false;
            if (capture_thread.joinable()) {
                capture_thread.join();
            }
        }

        void cleanupWindowResources() {
            stopCaptureThread();
            if (getDevice() != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(getDevice());
            }
            camera_sprite = nullptr;
            release();
            capture.close();
        }

        [[nodiscard]] bool consumeLatestCaptureFrame() {
            cv::Mat frame;
            {
                std::lock_guard<std::mutex> lock(capture_mutex);
                if (!latest_capture_frame_available) {
                    return false;
                }
                frame = latest_capture_frame.clone();
                latest_capture_frame_available = false;
            }

            return uploadFrameToSprite(frame);
        }

        [[nodiscard]] double configureCameraFps() {
            if (requested_fps > 0.0) {
                capture.set(cv::CAP_PROP_FPS, requested_fps);
                const double reportedFps = capture.get(cv::CAP_PROP_FPS);
                if (reportedFps > 0.0 && std::abs(reportedFps - requested_fps) > 0.5) {
                    std::cerr << std::format("shader_viewer: requested {:.1f} capture fps, backend reports {:.1f} fps\n", requested_fps, reportedFps);
                }
                return (reportedFps > 0.0) ? reportedFps : requested_fps;
            }

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

            const std::string vertex_shader;
            const std::string fragment_shader = currentFragmentShader();

            if (camera_sprite == nullptr) {
                camera_sprite = createSprite(frame_width, frame_height);
                enableShaderUniforms();
                if (camera_sprite != nullptr) {
                    camera_sprite->createEmptySprite(frame_width, frame_height, vertex_shader, fragment_shader);
                }
                return camera_sprite != nullptr;
            }

            camera_sprite->createEmptySprite(frame_width, frame_height, vertex_shader, fragment_shader);
            enableShaderUniforms();
            return true;
        }

        void selectShader(int direction) {
            if (shader_files.empty()) {
                return;
            }

            const int shader_count = static_cast<int>(shader_files.size());
            current_shader_index = (current_shader_index + direction) % shader_count;
            if (current_shader_index < 0) {
                current_shader_index += shader_count;
            }

            if (getDevice() != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(getDevice());
            }

            if (!createOrRefreshCameraSprite()) {
                throw mxvk::Exception("shader_viewer: failed to switch camera shader");
            }

            std::cout << std::format("shader_viewer: selected shader {} of {}: {}\n", current_shader_index + 1, shader_count, currentFragmentShader());
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
                throw mxvk::Exception("shader_viewer: failed to create capture sprite");
            }

            if (!using_file && consumeLatestCaptureFrame()) {
                return;
            }

            if (using_file && camera_sprite != nullptr && !capture.readToSprite(*camera_sprite)) {
                std::cerr << "shader_viewer: failed to upload initial camera frame\n";
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
                if (using_file) {
                    fps_text = std::format("FPS: {:.1f}", current_fps);
                } else {
                    double capture_fps = 0.0;
                    {
                        std::lock_guard<std::mutex> lock(capture_mutex);
                        capture_fps = current_capture_fps;
                    }
                    if (requested_fps > 0.0) {
                        fps_text = std::format("FPS: {:.1f}  Capture: {:.1f}/{:.1f}", current_fps, capture_fps, requested_fps);
                    } else {
                        fps_text = std::format("FPS: {:.1f}  Capture: {:.1f}", current_fps, capture_fps);
                    }
                }
            }

            printText(fps_text, 15, 15, SDL_Color{255, 255, 255, 255});
        }

      public:
        ExampleWindow(const Arguments &args, const std::string &text) : mxvk::VK_Window(text, args.width, args.height, args.fullscreen, MXVK_VALIDATION, args.enable_vsync) {
            try {
                current_path = args.path.empty() ? std::string(shader_viewer_ASSET_DIR) : args.path;
                shader_path = args.shaderPath.empty() ? std::string(shader_viewer_SHADER_DIR) : args.shaderPath;
                shader_list_requested = !args.shaderPath.empty();
                setenv("MXVK_CUDA_FLIP_Y", "0", 0);
                loadShaderIndex();
                setInitialShaderIndex(args.shader_index);
                std::string font_path = joinPath(current_path, "data/font.ttf");
                if (!std::filesystem::exists(font_path)) {
                    font_path = joinPath(shader_viewer_ASSET_DIR, "data/font.ttf");
                }
                setFont(font_path, 20);
                input_filename = args.filename;
                camera_index = args.camera_index;
                requested_fps = args.fps;
                using_file = !input_filename.empty();
                if (!openCaptureSource()) {
                    if (using_file) {
                        throw mxvk::Exception(std::format("shader_viewer: failed to open video file '{}'", input_filename));
                    }
                    throw mxvk::Exception(std::format("shader_viewer: failed to open camera index {}", camera_index));
                }
                fallback_width = args.width;
                fallback_height = args.height;
                if (!using_file) {
                    capture.set(cv::CAP_PROP_FRAME_WIDTH, fallback_width);
                    capture.set(cv::CAP_PROP_FRAME_HEIGHT, fallback_height);
                    fallback_width = capture.get(cv::CAP_PROP_FRAME_WIDTH);
                    fallback_height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
                    fps = configureCameraFps();
                } else {
                    fps = capture.get(cv::CAP_PROP_FPS);
                }
                std::cout << "mxvk_cv: Capture opened at: " << fallback_width << "x" << fallback_height << " @ " << fps << " fps\n";
                initializeCameraRendering();
                startCaptureThread();
            } catch (...) {
                cleanupWindowResources();
                throw;
            }
        }

        ~ExampleWindow() override {
            cleanupWindowResources();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_UP && !e.key.repeat) {
                selectShader(-1);
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_DOWN && !e.key.repeat) {
                selectShader(1);
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_pressed = true;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_pressed = false;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
            }
        }

        void onSwapchainRecreated() override {
            initializeCameraRendering();
        }

        void proc() override {
            if (!using_file) {
                [[maybe_unused]] const bool frame_uploaded = consumeLatestCaptureFrame();
            } else if (camera_sprite == nullptr || !capture.readToSprite(*camera_sprite)) {
                if (using_file) {
                    capture.close();
                    if (openCaptureSource()) {
                        if (camera_sprite != nullptr && !capture.readToSprite(*camera_sprite)) {
                            std::cerr << "shader_viewer: failed to upload restarted stream frame\n";
                        }
                    }
                } else {
                    fps = configureCameraFps();
                }
            }

            int target_w = fallback_width;
            int target_h = fallback_height;
            if (swapchain_extent.width > 0U && swapchain_extent.height > 0U) {
                target_w = static_cast<int>(swapchain_extent.width);
                target_h = static_cast<int>(swapchain_extent.height);
            }

            if (camera_sprite) {
                updateShaderUniforms(target_w, target_h);
                camera_sprite->drawSpriteRect(0, 0, target_w, target_h);
            }
            updateFpsOverlay();
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args, "Shader Viewer");
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
    }
    return EXIT_SUCCESS;
}
