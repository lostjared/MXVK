#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_cv.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <opencv2/videoio.hpp>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#ifndef shader_viewer_ASSET_DIR
#define shader_viewer_ASSET_DIR "."
#endif

#ifndef shader_viewer_SHADER_DIR
#define shader_viewer_SHADER_DIR "."
#endif

#ifndef shader_viewer_SOURCE_DIR
#define shader_viewer_SOURCE_DIR "."
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
        std::string model_filename;
        std::string model_vertex_shader;
        std::vector<std::string> shader_files;
        int current_shader_index = 0;
        double fps = 60.0f;
        double requested_fps = 0.0;
        int camera_index = 0;
        bool using_file = false;
        bool using_model = false;
        bool shader_list_requested = false;
        mxvk::VK_Capture capture{};
        mxvk::VK_Sprite *camera_sprite = nullptr;
        mxvk::VKAbstractModel model{};
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
        bool wireframe = false;
        bool auto_rotate = true;
        bool show_help = true;
        bool mouse_dragging = false;
        int last_mouse_x = 0;
        int last_mouse_y = 0;
        float rotation_x_degrees = 0.0f;
        float rotation_y_degrees = 0.0f;
        float auto_rotation_radians = 0.0f;
        float camera_distance = 5.0f;
        std::chrono::steady_clock::time_point last_model_update_time{std::chrono::steady_clock::now()};

        [[nodiscard]] bool openCaptureSource() {
            if (using_file) {
                return capture.open(input_filename);
            }
            return capture.open(camera_index);
        }

        [[nodiscard]] std::string resolveModelPath(const std::string &name) const {
            namespace fs = std::filesystem;
            const fs::path requested(name);
            std::error_code ec{};
            if (fs::exists(requested, ec)) {
                return fs::weakly_canonical(requested, ec).string();
            }
            ec.clear();

            const fs::path source_root = fs::path(shader_viewer_SOURCE_DIR).parent_path().parent_path();
            const fs::path runtime_root = fs::path(shader_viewer_ASSET_DIR).parent_path().parent_path();
            const fs::path candidates[] = {
                fs::path(current_path) / requested,
                fs::path(current_path) / "data" / requested,
                source_root / requested,
                source_root / "models" / requested,
                source_root / "models" / requested.filename(),
                runtime_root / requested,
                runtime_root / "models" / requested.filename(),
            };

            for (const fs::path &candidate : candidates) {
                if (fs::exists(candidate, ec)) {
                    return fs::weakly_canonical(candidate, ec).string();
                }
                ec.clear();
            }
            throw mxvk::Exception(std::format("shader_viewer: failed to locate model '{}'", name));
        }

        [[nodiscard]] std::string resolveOptionalPath(const std::string &name) const {
            if (name.empty()) {
                return {};
            }
            namespace fs = std::filesystem;
            const fs::path requested(name);
            std::error_code ec{};
            const fs::path source_root = fs::path(shader_viewer_SOURCE_DIR).parent_path().parent_path();
            const fs::path candidates[] = {
                requested,
                fs::path(current_path) / requested,
                fs::path(current_path) / "data" / requested,
                source_root / requested,
                source_root / "models" / requested,
            };
            for (const fs::path &candidate : candidates) {
                if (fs::exists(candidate, ec)) {
                    return fs::weakly_canonical(candidate, ec).string();
                }
                ec.clear();
            }
            return name;
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

            current_capture_fps = static_cast<double>(capture_fps_frame_count) / elapsed;
            capture_fps_frame_count = 0;
            capture_fps_sample_time = now;
        }

        void cleanupWindowResources() {
            if (getDevice() != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(getDevice());
            }
            camera_sprite = nullptr;
            if (using_model) {
                model.cleanup(this);
            }
            release();
            if (capture.is_open()) {
                capture.close();
            }
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

            if (using_model) {
                model.setShaders(this, model_vertex_shader, currentFragmentShader());
            } else if (!createOrRefreshCameraSprite()) {
                throw mxvk::Exception("shader_viewer: failed to switch capture shader");
            }

            std::cout << std::format("shader_viewer: selected shader {} of {}: {}\n", current_shader_index + 1, shader_count, currentFragmentShader());
        }

        [[nodiscard]] bool uploadCaptureFrameToSprite() {
            if (camera_sprite == nullptr) {
                return false;
            }

            if (!capture.readToSprite(*camera_sprite, false)) {
                return false;
            }

            updateCaptureFpsSample();
            return true;
        }

        void initializeCameraRendering() {
            if (swapchain == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE) {
                createDevice();
            }

            if (!createOrRefreshCameraSprite()) {
                throw mxvk::Exception("shader_viewer: failed to create capture sprite");
            }

            if (camera_sprite != nullptr && !uploadCaptureFrameToSprite()) {
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
                } else if (requested_fps > 0.0) {
                    fps_text = std::format("FPS: {:.1f}  Capture: {:.1f}/{:.1f}", current_fps, current_capture_fps, requested_fps);
                } else {
                    fps_text = std::format("FPS: {:.1f}  Capture: {:.1f}", current_fps, current_capture_fps);
                }
            }

            printText(fps_text, 15, 15, SDL_Color{255, 255, 255, 255});
            if (using_model) {
                printText(std::format("Model: {}  Wire: {}  Auto: {}", std::filesystem::path(model_filename).filename().string(), wireframe ? "on" : "off", auto_rotate ? "on" : "off"),
                          15, 39, SDL_Color{220, 228, 240, 255});
                if (show_help) {
                    printText("Drag/Left/Right rotate  Wheel/A/S zoom  Up/Down shader  W wire  R auto  Home reset  Esc quit",
                              15, static_cast<int>(getSwapchainExtent().height) - 30, SDL_Color{195, 205, 220, 255});
                }
            }
        }

      public:
        ExampleWindow(const Arguments &args, const std::string &text) : mxvk::VK_Window(text, args.width, args.height, args.fullscreen, MXVK_VALIDATION, args.enable_vsync) {
            try {
                current_path = (args.path.empty() || args.path == ".") ? std::string(shader_viewer_ASSET_DIR) : args.path;
                shader_path = args.shaderPath.empty() ? current_path + "/data" : args.shaderPath;
                shader_list_requested = !args.shaderPath.empty();
                loadShaderIndex();
                setInitialShaderIndex(args.shader_index);
                std::string font_path = joinPath(current_path, "data/font.ttf");
                if (!std::filesystem::exists(font_path)) {
                    font_path = joinPath(shader_viewer_ASSET_DIR, "data/font.ttf");
                }
                setFont(font_path, 20);
                input_filename = args.filename;
                model_filename = args.model;
                camera_index = args.camera_index;
                requested_fps = args.fps;
                using_file = !input_filename.empty();
                using_model = !model_filename.empty();

                fallback_width = args.width;
                fallback_height = args.height;
                if (using_model) {
                    model_filename = resolveModelPath(model_filename);
                    const std::string texture_manifest = resolveOptionalPath(args.texture);
                    const std::string texture_base = args.resource_path.empty()
                                                         ? std::filesystem::path(texture_manifest.empty() ? model_filename : texture_manifest).parent_path().string()
                                                         : resolveOptionalPath(args.resource_path);
                    model_vertex_shader = joinPath(current_path, "data/model.vert.spv");
                    if (!std::filesystem::exists(model_vertex_shader)) {
                        model_vertex_shader = joinPath(shader_viewer_ASSET_DIR, "data/model.vert.spv");
                    }
                    std::cout << std::format("shader_viewer: model='{}' fragment='{}'\n", model_filename, currentFragmentShader());
                    model.enableExtendedFragmentUniforms();
                    model.load(this, model_filename, texture_manifest, texture_base, 1.0f);
                    model.setShaders(this, model_vertex_shader, currentFragmentShader());
                    if (using_file) {
                        if (!openCaptureSource()) {
                            throw mxvk::Exception(std::format("shader_viewer: failed to open video file '{}'", input_filename));
                        }
                        fps = capture.get(cv::CAP_PROP_FPS);
                        if (!capture.readToModelTexture(model)) {
                            throw mxvk::Exception(std::format("shader_viewer: failed to upload the first video frame from '{}'", input_filename));
                        }
                        updateCaptureFpsSample();
                        std::cout << std::format("shader_viewer: video texture='{}' @ {:.1f} fps\n", input_filename, fps);
                    }
                    return;
                }

                if (!openCaptureSource()) {
                    if (using_file) {
                        throw mxvk::Exception(std::format("shader_viewer: failed to open video file '{}'", input_filename));
                    }
                    throw mxvk::Exception(std::format("shader_viewer: failed to open camera index {}", camera_index));
                }
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
            } else if (using_model && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                switch (e.key.key) {
                case SDLK_W:
                    wireframe = !wireframe;
                    break;
                case SDLK_R:
                case SDLK_P:
                    auto_rotate = !auto_rotate;
                    break;
                case SDLK_H:
                case SDLK_SPACE:
                    show_help = !show_help;
                    break;
                case SDLK_HOME:
                    rotation_x_degrees = 0.0f;
                    rotation_y_degrees = 0.0f;
                    auto_rotation_radians = 0.0f;
                    camera_distance = 5.0f;
                    break;
                case SDLK_EQUALS:
                case SDLK_PLUS:
                    camera_distance = std::clamp(camera_distance - 0.5f, 0.4f, 100.0f);
                    break;
                case SDLK_MINUS:
                    camera_distance = std::clamp(camera_distance + 0.5f, 0.4f, 100.0f);
                    break;
                default:
                    break;
                }
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
                if (using_model && mouse_dragging) {
                    rotation_y_degrees += (e.motion.x - static_cast<float>(last_mouse_x)) * 0.5f;
                    rotation_x_degrees += (e.motion.y - static_cast<float>(last_mouse_y)) * 0.5f;
                    rotation_x_degrees = std::clamp(rotation_x_degrees, -89.0f, 89.0f);
                    last_mouse_x = static_cast<int>(e.motion.x);
                    last_mouse_y = static_cast<int>(e.motion.y);
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_pressed = true;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
                mouse_dragging = using_model;
                last_mouse_x = static_cast<int>(e.button.x);
                last_mouse_y = static_cast<int>(e.button.y);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_pressed = false;
                mouse_dragging = false;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
            } else if (using_model && e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                camera_distance = std::clamp(camera_distance - delta * 0.45f, 0.4f, 100.0f);
            }
        }

        void onSwapchainRecreated() override {
            if (using_model) {
                model.resize(this);
            } else {
                initializeCameraRendering();
            }
        }

        void proc() override {
            if (using_model) {
                if (using_file && !capture.readToModelTexture(model)) {
                    capture.close();
                    if (!openCaptureSource() || !capture.readToModelTexture(model)) {
                        std::cerr << "shader_viewer: failed to restart video texture stream\n";
                    }
                }
                if (using_file) {
                    updateCaptureFpsSample();
                }

                const auto now = std::chrono::steady_clock::now();
                const float delta_seconds = std::clamp(std::chrono::duration<float>(now - last_model_update_time).count(), 0.0f, 0.1f);
                last_model_update_time = now;
                const bool *keys = SDL_GetKeyboardState(nullptr);
                constexpr float ROTATE_SPEED = 120.0f;
                if (keys != nullptr) {
                    if (keys[SDL_SCANCODE_LEFT]) {
                        rotation_y_degrees -= ROTATE_SPEED * delta_seconds;
                    }
                    if (keys[SDL_SCANCODE_RIGHT]) {
                        rotation_y_degrees += ROTATE_SPEED * delta_seconds;
                    }
                    if (keys[SDL_SCANCODE_A]) {
                        camera_distance = std::clamp(camera_distance - 2.5f * delta_seconds, 0.4f, 100.0f);
                    }
                    if (keys[SDL_SCANCODE_S]) {
                        camera_distance = std::clamp(camera_distance + 2.5f * delta_seconds, 0.4f, 100.0f);
                    }
                }
                if (auto_rotate) {
                    auto_rotation_radians = std::fmod(auto_rotation_radians + 0.55f * delta_seconds, 6.28318530718f);
                }
                updateFpsOverlay();
                return;
            }

            if (!uploadCaptureFrameToSprite()) {
                if (using_file) {
                    capture.close();
                    if (openCaptureSource()) {
                        if (!uploadCaptureFrameToSprite()) {
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

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            if (!using_model) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            const float elapsed_seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - shader_start_time).count();

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(rotation_x_degrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(rotation_y_degrees) + auto_rotation_radians, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, camera_distance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsed_seconds, wireframe ? 1.0f : 0.0f, 0.0f, 0.0f);
            model.updateUBO(image_index, ubo);

            const float delta_seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - previous_shader_frame_time).count();
            previous_shader_frame_time = std::chrono::steady_clock::now();
            ++shader_frame_count;
            mxvk::ModelFragmentUniforms fragment_uniforms{};
            fragment_uniforms.mouse = glm::vec4(mouse_x, mouse_y, mouse_pressed ? 1.0f : 0.0f, 0.0f);
            fragment_uniforms.u0 = glm::vec4(1.0f, 1.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
            fragment_uniforms.u1 = glm::vec4(delta_seconds, 0.0f, 0.0f, delta_seconds > 0.0f ? 1.0f / delta_seconds : 0.0f);
            fragment_uniforms.u2 = glm::vec4(static_cast<float>(shader_frame_count), elapsed_seconds, 48000.0f, 0.0f);
            model.updateFragmentUBO(image_index, fragment_uniforms);

            mxvk::ModelFragmentPushConstants fragment_constants{};
            fragment_constants.screenWidth = static_cast<float>(extent.width);
            fragment_constants.screenHeight = static_cast<float>(extent.height);
            fragment_constants.spriteSizeW = static_cast<float>(extent.width);
            fragment_constants.spriteSizeH = static_cast<float>(extent.height);
            fragment_constants.params = glm::vec4(elapsed_seconds, 1.0f, 1.0f, 1.0f);
            model.setFragmentPushConstants(fragment_constants);
            if (using_file) {
                model.renderWithPushConstants(cmd, image_index, 0, ubo, wireframe);
            } else {
                model.render(cmd, image_index, wireframe);
            }
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
