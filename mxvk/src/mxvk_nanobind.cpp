#include "mxvk_nanobind.hpp"

#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>
#include <nanobind/trampoline.h>

#include <cstring>
#include <memory>
#include <stdexcept>

#include <mxvk/mxvk.hpp>
#include <mxvk/mxvk_abstract_model.hpp>
#include <mxvk/mxvk_cfg.hpp>
#include <mxvk/mxvk_console.hpp>
#include <mxvk/mxvk_controller.hpp>
#include <mxvk/mxvk_io_window.hpp>
#include <mxvk/mxvk_model.hpp>
#include <mxvk/mxvk_png.hpp>
#include <mxvk/mxvk_point_sprite_batch.hpp>
#include <mxvk/mxvk_resource.hpp>
#include <mxvk/mxvk_runtime_options.hpp>
#include <mxvk/mxvk_stencil.hpp>
#include <mxvk/mxvk_stopwatch.hpp>
#if defined(MXVK_WITH_JPEG)
#include <mxvk/mxvk_jpeg.hpp>
#endif

#if defined(MXVK_WITH_CV)
#include <mxvk/mxvk_cv.hpp>
#endif
#if defined(MXVK_WITH_FFMPEG_CAPTURE)
#include <mxvk/mxvk_ff_capture.hpp>
#endif
#if defined(MXVK_WITH_MIXER)
#include <mxvk/mxvk_sound.hpp>
#endif
#include <mxvk/mxvk_version.hpp>

namespace nb = nanobind;

namespace {
    glm::mat4 matrix_from_rows(const std::array<float, 16> &values) {
        glm::mat4 matrix{1.0F};
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                matrix[column][row] = values[row * 4 + column];
        return matrix;
    }

    std::array<float, 16> matrix_to_rows(const glm::mat4 &matrix) {
        std::array<float, 16> values{};
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                values[row * 4 + column] = matrix[column][row];
        return values;
    }

    glm::vec4 vector4(const std::array<float, 4> &values) { return {values[0], values[1], values[2], values[3]}; }
    std::array<float, 4> vector4_array(const glm::vec4 &value) { return {value.x, value.y, value.z, value.w}; }

    class PythonEvent {
      public:
        explicit PythonEvent(const SDL_Event *event) : value(*event) {
            if (value.type == SDL_EVENT_TEXT_INPUT && event->text.text != nullptr)
                text_value = event->text.text;
        }
        [[nodiscard]] uint32_t type() const { return value.type; }
        [[nodiscard]] uint64_t timestamp() const { return value.common.timestamp; }
        [[nodiscard]] int32_t key() const { return value.key.key; }
        [[nodiscard]] uint32_t scancode() const { return value.key.scancode; }
        [[nodiscard]] uint16_t modifiers() const { return value.key.mod; }
        [[nodiscard]] bool down() const { return value.key.down; }
        [[nodiscard]] bool repeat() const { return value.key.repeat; }
        [[nodiscard]] const std::string &text() const { return text_value; }
        [[nodiscard]] float x() const { return value.motion.x; }
        [[nodiscard]] float y() const { return value.motion.y; }
        [[nodiscard]] float relative_x() const { return value.motion.xrel; }
        [[nodiscard]] float relative_y() const { return value.motion.yrel; }
        [[nodiscard]] uint8_t button() const { return value.button.button; }

      private:
        SDL_Event value{};
        std::string text_value{};
    };

    class PythonIOWindow : public mxvk::VK_IOWindow {
      public:
        NB_TRAMPOLINE(mxvk::VK_IOWindow);

        void console_proc() override { NB_OVERRIDE_PURE(console_proc); }

        void console_event(SDL_Event &event) override {
            constexpr uint64_t hash = nanobind::detail::str_hash("console_event");
            nanobind::detail::ticket ticket(nb_trampoline, "console_event", hash, true);
            nb_trampoline.base().attr(ticket.key)(PythonEvent{&event});
        }

        void proc() override { NB_OVERRIDE(proc); }

        void event(SDL_Event &event) override {
            constexpr uint64_t hash = nanobind::detail::str_hash("event");
            nanobind::detail::ticket ticket(nb_trampoline, "event", hash, false);
            if (ticket.key.is_valid())
                nb_trampoline.base().attr(ticket.key)(PythonEvent{&event});
            else
                NBBase::event(event);
        }
    };

#if defined(MXVK_WITH_CV) || (defined(MXVK_WITH_FFMPEG_CAPTURE) && defined(MXVK_CUDA))
    nb::object opencv_rgba_array(cv::Mat &&frame) {
        if (frame.empty())
            return nb::none();
        if (frame.type() != CV_8UC4)
            throw nb::value_error("capture did not produce an RGBA8 frame");
        auto *storage = new cv::Mat(std::move(frame));
        nb::capsule owner(storage, [](void *pointer) noexcept { delete static_cast<cv::Mat *>(pointer); });
        nb::ndarray<nb::numpy, uint8_t> array(storage->data, {static_cast<size_t>(storage->rows), static_cast<size_t>(storage->cols), 4}, owner, {static_cast<int64_t>(storage->step), 4, 1});
        return nb::cast(array);
    }
#endif

#if defined(MXVK_WITH_FFMPEG_CAPTURE)
    nb::object ffmpeg_rgba8_array(std::vector<uint8_t> &&pixels, int width, int height, int pitch) {
        auto *storage = new std::vector<uint8_t>(std::move(pixels));
        nb::capsule owner(storage, [](void *pointer) noexcept { delete static_cast<std::vector<uint8_t> *>(pointer); });
        nb::ndarray<nb::numpy, uint8_t> array(storage->data(), {static_cast<size_t>(height), static_cast<size_t>(width), 4}, owner, {pitch, 4, 1});
        return nb::cast(array);
    }

    nb::object ffmpeg_rgba16_array(std::vector<uint16_t> &&pixels, int width, int height, int pitch) {
        auto *storage = new std::vector<uint16_t>(std::move(pixels));
        nb::capsule owner(storage, [](void *pointer) noexcept { delete static_cast<std::vector<uint16_t> *>(pointer); });
        nb::ndarray<nb::numpy, uint16_t> array(storage->data(), {static_cast<size_t>(height), static_cast<size_t>(width), 4}, owner, {pitch / static_cast<int64_t>(sizeof(uint16_t)), 4, 1});
        return nb::cast(array);
    }
#endif

    class PythonBuffer {
      public:
        PythonBuffer(const mxvk::VulkanContext &context, VkDeviceSize size, VkBufferUsageFlags usage) : device(context.device) { mxvk::create_buffer(context, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, resource); }

        ~PythonBuffer() { close(); }

        PythonBuffer(const PythonBuffer &) = delete;
        PythonBuffer &operator=(const PythonBuffer &) = delete;

        void write(nanobind::ndarray<uint8_t, nanobind::c_contig, nanobind::device::cpu> bytes) {
            if (bytes.nbytes() > resource.size)
                throw nanobind::value_error("buffer upload is larger than the allocated buffer");
            mxvk::map_buffer(device, resource);
            std::memcpy(resource.mapped, bytes.data(), bytes.nbytes());
            mxvk::unmap_buffer(device, resource);
        }

        void close() {
            if (device != VK_NULL_HANDLE && resource.buffer != VK_NULL_HANDLE)
                mxvk::destroy_buffer(device, resource);
            device = VK_NULL_HANDLE;
        }

        [[nodiscard]] size_t size() const { return static_cast<size_t>(resource.size); }
        [[nodiscard]] bool valid() const { return resource.buffer != VK_NULL_HANDLE; }

      private:
        VkDevice device = VK_NULL_HANDLE;
        mxvk::BufferResource resource{};
    };

    class PythonTexture {
      public:
        PythonTexture(const mxvk::VulkanContext &context, const std::string &path) : device(context.device) { mxvk::create_texture_from_png(context, path, resource); }
        ~PythonTexture() { close(); }

        PythonTexture(const PythonTexture &) = delete;
        PythonTexture &operator=(const PythonTexture &) = delete;

        void close() {
            if (device != VK_NULL_HANDLE && resource.image != VK_NULL_HANDLE)
                mxvk::destroy_texture(device, resource);
            device = VK_NULL_HANDLE;
        }

        [[nodiscard]] uint32_t width() const { return resource.width; }
        [[nodiscard]] uint32_t height() const { return resource.height; }
        [[nodiscard]] bool valid() const { return resource.image != VK_NULL_HANDLE; }

      private:
        VkDevice device = VK_NULL_HANDLE;
        mxvk::TextureResource resource{};
    };
} // namespace

namespace mxvk {
    void bind_nanobind_module(nb::module_ &module) {
        module.doc() = "Python bindings for the MXVK Vulkan framework.";

        module.attr("version") = nb::make_tuple(MXVK_VERSION_CODE_MAJOR, MXVK_VERSION_CODE_MINOR, MXVK_VERSION_CODE_PATCH);
        module.attr("has_cv") = false;
        module.attr("has_ffmpeg_capture") = false;
        module.attr("has_mixer") = false;
        module.attr("has_jpeg") = false;
        module.attr("EVENT_KEY_DOWN") = static_cast<uint32_t>(SDL_EVENT_KEY_DOWN);
        module.attr("KEY_ESCAPE") = static_cast<int32_t>(SDLK_ESCAPE);

        module.def("set_default_enable_screenshot", &setDefaultEnableScreenshot, nb::arg("enabled"));
        module.def("default_enable_screenshot", &defaultEnableScreenshot);
        module.def("set_default_executable_name", &setDefaultExecutableName, nb::arg("name"));
        module.def("default_executable_name", &defaultExecutableName, nb::rv_policy::copy);
        module.def("save_png_rgba", [](const std::string &path, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> pixels, int width, int height) { return SavePNG_RGBA(path.c_str(), pixels.data(), width, height); }, nb::arg("path"), nb::arg("pixels"), nb::arg("width"), nb::arg("height"));
        module.def("save_png_rgba16", [](const std::string &path, nb::ndarray<uint16_t, nb::c_contig, nb::device::cpu> pixels, int width, int height) { return SavePNG_RGBA16(path.c_str(), pixels.data(), width, height); }, nb::arg("path"), nb::arg("pixels"), nb::arg("width"), nb::arg("height"));
        module.def("inspect_spirv_file", [](const std::string &path) { return inspect_spirv(load_spv(path)); }, nb::arg("path"));

        nb::enum_<StorageImageFormat>(module, "StorageImageFormat").value("unknown", StorageImageFormat::Unknown).value("rgba8", StorageImageFormat::Rgba8).value("rgba16_float", StorageImageFormat::Rgba16Float);
        nb::class_<ShaderModuleInfo>(module, "ShaderModuleInfo").def_ro("stage", &ShaderModuleInfo::stage).def_ro("local_size_x", &ShaderModuleInfo::localSizeX).def_ro("local_size_y", &ShaderModuleInfo::localSizeY).def_ro("local_size_z", &ShaderModuleInfo::localSizeZ).def_ro("uses_history_texture", &ShaderModuleInfo::usesHistoryTexture).def_ro("uses_spectrum_texture", &ShaderModuleInfo::usesSpectrumTexture).def_ro("uses_spectrum_history_texture", &ShaderModuleInfo::usesSpectrumHistoryTexture).def_ro("storage_image_format", &ShaderModuleInfo::storageImageFormat);

        nb::class_<PythonBuffer>(module, "GpuBuffer").def("write", &PythonBuffer::write, nb::arg("bytes")).def("close", &PythonBuffer::close).def_prop_ro("size", &PythonBuffer::size).def_prop_ro("valid", &PythonBuffer::valid);
        nb::class_<PythonTexture>(module, "GpuTexture").def("close", &PythonTexture::close).def_prop_ro("width", &PythonTexture::width).def_prop_ro("height", &PythonTexture::height).def_prop_ro("valid", &PythonTexture::valid);
        module.def("create_uniform_buffer", [](VK_Window &window, size_t size) { return std::make_shared<PythonBuffer>(window.context(), size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT); }, nb::arg("window"), nb::arg("size"), nb::keep_alive<0, 1>());
        module.def("create_storage_buffer", [](VK_Window &window, size_t size) { return std::make_shared<PythonBuffer>(window.context(), size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); }, nb::arg("window"), nb::arg("size"), nb::keep_alive<0, 1>());
        module.def("load_texture", [](VK_Window &window, const std::string &path) { return std::make_shared<PythonTexture>(window.context(), path); }, nb::arg("window"), nb::arg("path"), nb::keep_alive<0, 1>());

        nb::class_<StopWatch<SteadyClockPolicy>>(module, "SteadyStopwatch").def(nb::init<std::string_view>(), nb::arg("name") = "mxvk").def("start", &StopWatch<SteadyClockPolicy>::Start, nb::arg("name")).def("stop", &StopWatch<SteadyClockPolicy>::Stop).def("echo", &StopWatch<SteadyClockPolicy>::Echo, nb::arg("name")).def("time_passed", &StopWatch<SteadyClockPolicy>::TimePassed);

        nb::class_<StopWatch<HighResolutionClockPolicy>>(module, "HighResolutionStopwatch").def(nb::init<std::string_view>(), nb::arg("name") = "mxvk").def("start", &StopWatch<HighResolutionClockPolicy>::Start, nb::arg("name")).def("stop", &StopWatch<HighResolutionClockPolicy>::Stop).def("echo", &StopWatch<HighResolutionClockPolicy>::Echo, nb::arg("name")).def("time_passed", &StopWatch<HighResolutionClockPolicy>::TimePassed);

#if defined(MXVK_WITH_JPEG)
        module.attr("has_jpeg") = true;
        module.def(
            "load_jpeg_rgba",
            [](const std::string &path) -> nb::object {
                SDL_Surface *loaded = VK_JPEG::Load(path.c_str());
                if (loaded == nullptr)
                    return nb::none();
                SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
                SDL_DestroySurface(loaded);
                if (rgba == nullptr)
                    return nb::none();
                const size_t byte_count = static_cast<size_t>(rgba->pitch) * static_cast<size_t>(rgba->h);
                nb::bytes pixels(static_cast<const char *>(rgba->pixels), byte_count);
                nb::tuple result = nb::make_tuple(pixels, rgba->w, rgba->h, rgba->pitch);
                SDL_DestroySurface(rgba);
                return result;
            },
            nb::arg("path"));
        module.def(
            "save_jpeg_rgba",
            [](const std::string &path, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> pixels, int width, int height, int quality, int pitch) {
                if (width <= 0 || height <= 0)
                    throw nb::value_error("width and height must be positive");
                if (pitch == 0)
                    pitch = width * 4;
                if (pitch < width * 4 || pixels.nbytes() < static_cast<size_t>(pitch) * static_cast<size_t>(height))
                    throw nb::value_error("RGBA buffer is smaller than pitch * height");
                SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), pitch);
                if (surface == nullptr)
                    throw std::runtime_error(SDL_GetError());
                const bool saved = VK_JPEG::SaveSurface(surface, path.c_str(), quality);
                SDL_DestroySurface(surface);
                return saved;
            },
            nb::arg("path"),
            nb::arg("pixels"),
            nb::arg("width"),
            nb::arg("height"),
            nb::arg("quality") = 90,
            nb::arg("pitch") = 0);
#endif

#if defined(MXVK_WITH_MIXER)
        module.attr("has_mixer") = true;
        nb::class_<VK_Mixer>(module, "Mixer").def(nb::init<>()).def("init", &VK_Mixer::init).def("load_wav", &VK_Mixer::loadWav, nb::arg("path")).def("load_music", &VK_Mixer::loadMusic, nb::arg("path")).def("play_music", &VK_Mixer::playMusic, nb::arg("id"), nb::arg("loops") = 0).def("play_wav", &VK_Mixer::playWav, nb::arg("id"), nb::arg("loops") = 0, nb::arg("channel") = -1).def("is_playing", &VK_Mixer::isPlaying, nb::arg("channel")).def("is_music_playing", &VK_Mixer::isMusicPlaying, nb::arg("id")).def("stop_music", &VK_Mixer::stopMusic).def("cleanup", &VK_Mixer::cleanup);
#endif

#if defined(MXVK_WITH_CV)
        module.attr("has_cv") = true;
        nb::class_<VK_Capture>(module, "Capture")
            .def(nb::init<>())
            .def("open", nb::overload_cast<const std::string &>(&VK_Capture::open), nb::arg("path"))
            .def("open_camera", nb::overload_cast<int, int>(&VK_Capture::open), nb::arg("index"), nb::arg("backend") = 0)
            .def("close", &VK_Capture::close)
            .def("is_open", &VK_Capture::is_open)
            .def("reset_sprite", &VK_Capture::resetSprite)
            .def("draw", nb::overload_cast<int, int>(&VK_Capture::draw), nb::arg("x"), nb::arg("y"))
            .def("draw", nb::overload_cast<int, int, int, int>(&VK_Capture::draw), nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))
            .def("read", nb::overload_cast<>(&VK_Capture::read))
            .def("grab", &VK_Capture::grab)
            .def(
                "read_rgba",
                [](VK_Capture &capture, bool flip_y) {
                    cv::Mat frame;
                    if (!capture.readRgba(frame, flip_y))
                        return nb::object(nb::none());
                    return opencv_rgba_array(std::move(frame));
                },
                nb::arg("flip_y") = false)
            .def("read_to_sprite", nb::overload_cast<VK_Sprite &>(&VK_Capture::readToSprite), nb::arg("sprite"))
            .def("read_to_sprite", nb::overload_cast<VK_Sprite &, bool>(&VK_Capture::readToSprite), nb::arg("sprite"), nb::arg("flip_y"))
            .def("read_to_model_texture", &VK_Capture::readToModelTexture, nb::arg("model"), nb::arg("flip_y") = false)
            .def("reload", &VK_Capture::reload, nb::arg("width"), nb::arg("height"), nb::arg("vertex_shader_path"), nb::arg("fragment_shader_path"))
            .def_prop_ro("sprite", &VK_Capture::getSprite, nb::rv_policy::reference_internal)
            .def("set", &VK_Capture::set, nb::arg("property"), nb::arg("value"))
            .def("get", &VK_Capture::get, nb::arg("property"))
#if defined(MXVK_CUDA)
            .def(
                "read_gpu_rgba_to_host",
                [](VK_Capture &capture, bool flip_y) {
                    cv::cuda::GpuMat gpu;
                    if (!capture.readGpuRgba(gpu, flip_y))
                        return nb::object(nb::none());
                    cv::Mat host;
                    gpu.download(host, capture.cudaStream());
                    capture.cudaStream().waitForCompletion();
                    return opencv_rgba_array(std::move(host));
                },
                nb::arg("flip_y") = false)
#endif
            ;
#endif

#if defined(MXVK_WITH_FFMPEG_CAPTURE)
        module.attr("has_ffmpeg_capture") = true;
        nb::class_<VK_FF_Capture>(module, "FFmpegCapture")
            .def(nb::init<>())
            .def("open", nb::overload_cast<const std::string &>(&VK_FF_Capture::open), nb::arg("path"))
            .def("open", nb::overload_cast<const std::string &, int>(&VK_FF_Capture::open), nb::arg("path"), nb::arg("cuda_device"))
            .def("seek_start", &VK_FF_Capture::seek_start)
            .def("skip", &VK_FF_Capture::skip)
            .def("close", &VK_FF_Capture::close)
            .def("is_open", &VK_FF_Capture::is_open)
            .def(
                "read_rgba",
                [](VK_FF_Capture &capture, bool flip_y) -> nb::object {
                    std::vector<uint8_t> pixels;
                    int width = 0, height = 0, pitch = 0;
                    if (!capture.readRgba(pixels, width, height, pitch, flip_y))
                        return nb::none();
                    return ffmpeg_rgba8_array(std::move(pixels), width, height, pitch);
                },
                nb::arg("flip_y") = false)
            .def(
                "read_rgba16",
                [](VK_FF_Capture &capture, bool flip_y) -> nb::object {
                    std::vector<uint16_t> pixels;
                    int width = 0, height = 0, pitch = 0;
                    if (!capture.readRgba16(pixels, width, height, pitch, flip_y))
                        return nb::none();
                    return ffmpeg_rgba16_array(std::move(pixels), width, height, pitch);
                },
                nb::arg("flip_y") = false)
            .def_prop_ro("width", &VK_FF_Capture::width)
            .def_prop_ro("height", &VK_FF_Capture::height)
            .def_prop_ro("fps", &VK_FF_Capture::fps)
            .def_prop_ro("using_hardware_decode", &VK_FF_Capture::using_hardware_decode)
            .def_prop_ro("hardware_decode_device", &VK_FF_Capture::hardware_decode_device)
#if defined(MXVK_CUDA)
            .def(
                "read_gpu_rgba_to_host",
                [](VK_FF_Capture &capture, bool flip_y) {
                    cv::cuda::GpuMat gpu;
                    cv::cuda::Stream stream;
                    if (!capture.readGpuRgba(gpu, stream, flip_y))
                        return nb::object(nb::none());
                    stream.waitForCompletion();
                    cv::Mat host;
                    gpu.download(host);
                    return opencv_rgba_array(std::move(host));
                },
                nb::arg("flip_y") = false)
#endif
            ;
#endif

        nb::class_<VK_ConfigItem>(module, "ConfigItem").def(nb::init<>()).def_rw("key", &VK_ConfigItem::key).def_rw("value", &VK_ConfigItem::value);

        nb::class_<VK_Config>(module, "Config").def(nb::init<>()).def(nb::init<const std::string &>(), nb::arg("file_path")).def("item_at_key", &VK_Config::itemAtKey, nb::arg("section"), nb::arg("key"), nb::arg("default_value") = "").def("set_item", &VK_Config::setItem, nb::arg("section"), nb::arg("key"), nb::arg("value")).def("load_file", &VK_Config::loadFile, nb::arg("path")).def("save_file", &VK_Config::saveFile, nb::arg("path")).def("split_by_comma", &VK_Config::splitByComma, nb::arg("value"));

        nb::class_<VK_Joystick>(module, "Joystick").def(nb::init<>()).def("open", &VK_Joystick::open, nb::arg("index")).def("close", &VK_Joystick::close).def("name", &VK_Joystick::name).def("index", &VK_Joystick::joystickIndex).def_static("count", &VK_Joystick::joysticks).def("num_buttons", &VK_Joystick::numButtons).def("num_hats", &VK_Joystick::numHats).def("num_axes", &VK_Joystick::numAxes).def("button", &VK_Joystick::getButton, nb::arg("index")).def("hat", &VK_Joystick::getHat, nb::arg("index")).def("axis", &VK_Joystick::getAxis, nb::arg("index"));

        nb::class_<VK_Controller>(module, "Controller").def(nb::init<>()).def("open", &VK_Controller::open, nb::arg("index")).def("close", &VK_Controller::close).def("name", &VK_Controller::name).def("index", &VK_Controller::controllerIndex).def_static("count", &VK_Controller::joysticks).def("button", [](const VK_Controller &controller, int button) { return controller.getButton(static_cast<SDL_GamepadButton>(button)); }, nb::arg("button")).def("hat", &VK_Controller::getHat, nb::arg("index")).def("axis", [](const VK_Controller &controller, int axis) { return controller.getAxis(static_cast<SDL_GamepadAxis>(axis)); }, nb::arg("axis")).def("active", &VK_Controller::active);

        nb::class_<VKVertex>(module, "Vertex").def(nb::init<>()).def_prop_rw("position", [](const VKVertex &value) { return nb::make_tuple(value.pos[0], value.pos[1], value.pos[2]); }, [](VKVertex &value, const std::array<float, 3> &position) { std::copy(position.begin(), position.end(), value.pos); }).def_prop_rw("tex_coord", [](const VKVertex &value) { return nb::make_tuple(value.texCoord[0], value.texCoord[1]); }, [](VKVertex &value, const std::array<float, 2> &tex_coord) { std::copy(tex_coord.begin(), tex_coord.end(), value.texCoord); }).def_prop_rw("normal", [](const VKVertex &value) { return nb::make_tuple(value.normal[0], value.normal[1], value.normal[2]); }, [](VKVertex &value, const std::array<float, 3> &normal) { std::copy(normal.begin(), normal.end(), value.normal); });

        nb::class_<SubMesh>(module, "SubMesh").def(nb::init<>()).def_rw("first_index", &SubMesh::firstIndex).def_rw("index_count", &SubMesh::indexCount).def_rw("texture_index", &SubMesh::textureIndex).def_rw("material_name", &SubMesh::materialName);

        nb::class_<MXMaterial>(module, "Material").def(nb::init<>()).def_rw("name", &MXMaterial::name).def_prop_rw("ambient", [](const MXMaterial &value) { return nb::make_tuple(value.ka[0], value.ka[1], value.ka[2]); }, [](MXMaterial &value, const std::array<float, 3> &ambient) { std::copy(ambient.begin(), ambient.end(), value.ka); }).def_prop_rw("diffuse", [](const MXMaterial &value) { return nb::make_tuple(value.kd[0], value.kd[1], value.kd[2]); }, [](MXMaterial &value, const std::array<float, 3> &diffuse) { std::copy(diffuse.begin(), diffuse.end(), value.kd); }).def_prop_rw("specular", [](const MXMaterial &value) { return nb::make_tuple(value.ks[0], value.ks[1], value.ks[2]); }, [](MXMaterial &value, const std::array<float, 3> &specular) { std::copy(specular.begin(), specular.end(), value.ks); }).def_rw("shininess", &MXMaterial::ns).def_rw("opacity", &MXMaterial::d).def_rw("illumination_model", &MXMaterial::illum).def_rw("diffuse_map", &MXMaterial::map_kd);

        nb::class_<MXModel>(module, "Model")
            .def(nb::init<>())
            .def("load", nb::overload_cast<const std::string &, float>(&MXModel::load), nb::arg("path"), nb::arg("position_scale") = 1.0F)
            .def("load", nb::overload_cast<const std::string &, const std::string &, const std::string &, float>(&MXModel::load), nb::arg("path"), nb::arg("texture_manifest_path"), nb::arg("texture_base_path"), nb::arg("position_scale") = 1.0F)
            .def("export_obj", nb::overload_cast<const std::string &, const std::string &>(&MXModel::exportOBJ, nb::const_), nb::arg("obj_path"), nb::arg("mtl_path") = "")
            .def_static("export_obj_from_file", nb::overload_cast<const std::string &, const std::string &, const std::string &, const std::string &, float>(&MXModel::exportOBJ), nb::arg("model_path"), nb::arg("texture_manifest_path"), nb::arg("texture_base_path"), nb::arg("obj_path"), nb::arg("position_scale") = 1.0F)
            .def("compress_indices", &MXModel::compressIndices)
            .def_prop_ro("vertices", &MXModel::vertices, nb::rv_policy::copy)
            .def_prop_ro("indices", &MXModel::indices, nb::rv_policy::copy)
            .def_prop_ro("index_count", &MXModel::indexCount)
            .def_prop_ro("sub_mesh_count", &MXModel::subMeshCount)
            .def_prop_ro("sub_meshes", &MXModel::subMeshes, nb::rv_policy::copy)
            .def_prop_ro("materials", &MXModel::materials, nb::rv_policy::copy)
            .def_prop_ro("material_library_path", &MXModel::mtlLibPath, nb::rv_policy::copy);

        nb::class_<SDL_Color>(module, "Color").def(nb::init<>()).def(nb::init<uint8_t, uint8_t, uint8_t, uint8_t>(), nb::arg("red"), nb::arg("green"), nb::arg("blue"), nb::arg("alpha") = 255).def_rw("red", &SDL_Color::r).def_rw("green", &SDL_Color::g).def_rw("blue", &SDL_Color::b).def_rw("alpha", &SDL_Color::a);

        nb::class_<VK_Console>(module, "Console")
            .def(nb::init<>())
            .def("attach", &VK_Console::attach, nb::arg("window"), nb::arg("font_path"), nb::arg("font_size") = 18)
            .def("set_font", &VK_Console::setFont, nb::arg("font_path"), nb::arg("font_size"))
            .def("draw", &VK_Console::draw)
            .def("print_line", &VK_Console::printLine, nb::arg("line"), nb::arg("color") = SDL_Color{255, 255, 255, 255})
            .def("clear", &VK_Console::clear)
            .def("set_prompt", &VK_Console::setPrompt, nb::arg("prompt"))
            .def("set_visible", &VK_Console::setVisible, nb::arg("visible"))
            .def("show", &VK_Console::show)
            .def("hide", &VK_Console::hide)
            .def("toggle", &VK_Console::toggle)
            .def("visible", &VK_Console::isVisible)
            .def("set_max_lines", &VK_Console::setMaxLines, nb::arg("count"))
            .def("set_max_visible_lines", &VK_Console::setMaxVisibleLines, nb::arg("count"))
            .def("set_sprite_y_origin_top_left", &VK_Console::setSpriteYOriginTopLeft, nb::arg("enabled"))
            .def_prop_ro("input_buffer", &VK_Console::inputBuffer, nb::rv_policy::copy);

        nb::class_<PointSpriteVertex>(module, "PointSpriteVertex").def(nb::init<>()).def_prop_rw("position", [](const PointSpriteVertex &value) { return nb::make_tuple(value.position[0], value.position[1], value.position[2]); }, [](PointSpriteVertex &value, const std::array<float, 3> &position) { std::copy(position.begin(), position.end(), value.position); }).def_rw("size", &PointSpriteVertex::size).def_prop_rw("color", [](const PointSpriteVertex &value) { return nb::make_tuple(value.color[0], value.color[1], value.color[2], value.color[3]); }, [](PointSpriteVertex &value, const std::array<float, 4> &color) { std::copy(color.begin(), color.end(), value.color); });

        nb::class_<VK_PointSpriteBatch>(module, "PointSpriteBatch")
            .def(nb::init<>())
            .def("load", &VK_PointSpriteBatch::load, nb::arg("window"), nb::arg("texture_path"), nb::arg("vertex_shader_path"), nb::arg("fragment_shader_path"), nb::arg("max_vertices"))
            .def("resize", &VK_PointSpriteBatch::resize, nb::arg("window"))
            .def("cleanup", &VK_PointSpriteBatch::cleanup)
            .def(
                "upload_vertices",
                [](VK_PointSpriteBatch &batch, const std::vector<PointSpriteVertex> &vertices) { batch.upload_vertices(vertices.data(), vertices.size()); },
                nb::arg("vertices"))
            .def("set_additive_blending", &VK_PointSpriteBatch::set_additive_blending, nb::arg("enabled"))
            .def("set_depth_test_enabled", &VK_PointSpriteBatch::set_depth_test_enabled, nb::arg("enabled"))
            .def("set_depth_write_enabled", &VK_PointSpriteBatch::set_depth_write_enabled, nb::arg("enabled"))
            .def_prop_ro("loaded", &VK_PointSpriteBatch::loaded)
            .def_prop_ro("capacity", &VK_PointSpriteBatch::capacity)
            .def_prop_ro("vertex_count", &VK_PointSpriteBatch::vertex_count);

        nb::class_<BufferResource>(module, "BufferResource").def(nb::init<>()).def_rw("size", &BufferResource::size).def_prop_ro("buffer", [](const BufferResource &value) { return reinterpret_cast<uintptr_t>(value.buffer); }).def_prop_ro("memory", [](const BufferResource &value) { return reinterpret_cast<uintptr_t>(value.memory); }).def_prop_ro("mapped", [](const BufferResource &value) { return reinterpret_cast<uintptr_t>(value.mapped); });

        nb::class_<TextureResource>(module, "TextureResource").def(nb::init<>()).def_rw("width", &TextureResource::width).def_rw("height", &TextureResource::height).def_prop_ro("image", [](const TextureResource &value) { return reinterpret_cast<uintptr_t>(value.image); }).def_prop_ro("memory", [](const TextureResource &value) { return reinterpret_cast<uintptr_t>(value.memory); }).def_prop_ro("view", [](const TextureResource &value) { return reinterpret_cast<uintptr_t>(value.view); }).def_prop_ro("sampler", [](const TextureResource &value) { return reinterpret_cast<uintptr_t>(value.sampler); });

        nb::class_<VK_Stencil::PushConstants>(module, "StencilPushConstants").def(nb::init<>()).def_rw("time", &VK_Stencil::PushConstants::time).def_rw("aspect", &VK_Stencil::PushConstants::aspect).def_rw("phase", &VK_Stencil::PushConstants::phase).def_rw("scale", &VK_Stencil::PushConstants::scale);

        nb::class_<VK_Stencil>(module, "Stencil")
            .def(nb::init<>())
            .def("destroy", &VK_Stencil::destroy)
            .def(
                "resize",
                [](VK_Stencil &value, uint32_t width, uint32_t height) { value.resize(VkExtent2D{width, height}); },
                nb::arg("width"),
                nb::arg("height"))
            .def("valid", &VK_Stencil::valid)
            .def_prop_ro("format", [](const VK_Stencil &value) { return static_cast<int>(value.stencil_format()); })
            .def_prop_ro("extent", [](const VK_Stencil &value) {
                const auto extent = value.extent();
                return nb::make_tuple(extent.width, extent.height);
            });

        nb::class_<Font>(module, "Font").def(nb::init<>()).def(nb::init<const std::string &, int>(), nb::arg("path"), nb::arg("size")).def("reset", nb::overload_cast<>(&Font::reset)).def("reset", nb::overload_cast<const std::string &, int>(&Font::reset), nb::arg("path"), nb::arg("size")).def("valid", [](const Font &font) { return static_cast<bool>(font); }).def_prop_ro("path", &Font::path).def_prop_ro("size", &Font::size);

        nb::class_<UniformBufferObject>(module, "ModelUniforms").def(nb::init<>()).def_prop_rw("model", [](const UniformBufferObject &value) { return matrix_to_rows(value.model); }, [](UniformBufferObject &value, const std::array<float, 16> &matrix) { value.model = matrix_from_rows(matrix); }).def_prop_rw("view", [](const UniformBufferObject &value) { return matrix_to_rows(value.view); }, [](UniformBufferObject &value, const std::array<float, 16> &matrix) { value.view = matrix_from_rows(matrix); }).def_prop_rw("projection", [](const UniformBufferObject &value) { return matrix_to_rows(value.proj); }, [](UniformBufferObject &value, const std::array<float, 16> &matrix) { value.proj = matrix_from_rows(matrix); }).def_prop_rw("effects", [](const UniformBufferObject &value) { return vector4_array(value.fx); }, [](UniformBufferObject &value, const std::array<float, 4> &effects) { value.fx = vector4(effects); });

        nb::class_<ModelFragmentPushConstants>(module, "ModelFragmentPushConstants").def(nb::init<>()).def_rw("screen_width", &ModelFragmentPushConstants::screenWidth).def_rw("screen_height", &ModelFragmentPushConstants::screenHeight).def_rw("sprite_x", &ModelFragmentPushConstants::spritePosX).def_rw("sprite_y", &ModelFragmentPushConstants::spritePosY).def_rw("sprite_width", &ModelFragmentPushConstants::spriteSizeW).def_rw("sprite_height", &ModelFragmentPushConstants::spriteSizeH).def_rw("effects_enabled", &ModelFragmentPushConstants::effectsOn).def_prop_rw("params", [](const ModelFragmentPushConstants &value) { return vector4_array(value.params); }, [](ModelFragmentPushConstants &value, const std::array<float, 4> &params) { value.params = vector4(params); });

        nb::class_<ModelFragmentUniforms>(module, "ModelFragmentUniforms")
            .def(nb::init<>())
            .def_prop_rw(
                "mouse",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.mouse); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.mouse = vector4(data); })
            .def_prop_rw(
                "uniform0",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.u0); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.u0 = vector4(data); })
            .def_prop_rw(
                "uniform1",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.u1); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.u1 = vector4(data); })
            .def_prop_rw(
                "uniform2",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.u2); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.u2 = vector4(data); })
            .def_prop_rw(
                "uniform3",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.u3); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.u3 = vector4(data); })
            .def_prop_rw(
                "custom_uniforms",
                [](const ModelFragmentUniforms &value) {
                    std::vector<std::array<float, 4>> result;
                    result.reserve(value.custom_uniforms.size());
                    for (const auto &entry : value.custom_uniforms)
                        result.push_back(vector4_array(entry));
                    return result;
                },
                [](ModelFragmentUniforms &value, const std::vector<std::array<float, 4>> &data) {
                    if (data.size() > value.custom_uniforms.size())
                        throw nb::value_error("at most 16 custom uniforms are supported");
                    value.custom_uniforms.fill(glm::vec4{0.0F});
                    for (size_t index = 0; index < data.size(); ++index)
                        value.custom_uniforms[index] = vector4(data[index]);
                })
            .def_prop_rw(
                "audio_bands",
                [](const ModelFragmentUniforms &value) { return vector4_array(value.audio_bands); },
                [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.audio_bands = vector4(data); })
            .def_prop_rw("audio_history", [](const ModelFragmentUniforms &value) { return vector4_array(value.audio_history); }, [](ModelFragmentUniforms &value, const std::array<float, 4> &data) { value.audio_history = vector4(data); });

        nb::class_<VKAbstractModel>(module, "AbstractModel")
            .def(nb::init<>())
            .def("load", nb::overload_cast<VK_Window *, const std::string &, const std::string &, const std::string &, float>(&VKAbstractModel::load), nb::arg("window"), nb::arg("model_path"), nb::arg("texture_manifest_path") = "", nb::arg("texture_base_path") = "", nb::arg("scale") = 1.0F, nb::keep_alive<1, 2>())
            .def("set_shaders", &VKAbstractModel::setShaders, nb::arg("window"), nb::arg("vertex_shader_path"), nb::arg("fragment_shader_path"))
            .def("update_uniforms", &VKAbstractModel::updateUBO, nb::arg("image_index"), nb::arg("uniforms"))
            .def("enable_extended_fragment_uniforms", &VKAbstractModel::enableExtendedFragmentUniforms)
            .def("update_fragment_uniforms", &VKAbstractModel::updateFragmentUBO, nb::arg("image_index"), nb::arg("uniforms"))
            .def("set_fragment_push_constants", &VKAbstractModel::setFragmentPushConstants, nb::arg("constants"))
            .def(
                "update_primary_texture",
                [](VKAbstractModel &model, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> pixels, int width, int height, int pitch) {
                    if (width <= 0 || height <= 0)
                        throw nb::value_error("width and height must be positive");
                    if (pitch == 0)
                        pitch = width * 4;
                    if (pitch < width * 4 || pixels.nbytes() < static_cast<size_t>(pitch) * static_cast<size_t>(height))
                        throw nb::value_error("RGBA buffer is smaller than pitch * height");
                    return model.updatePrimaryTexture(pixels.data(), width, height, pitch);
                },
                nb::arg("pixels"),
                nb::arg("width"),
                nb::arg("height"),
                nb::arg("pitch") = 0)
            .def(
                "render",
                [](const VKAbstractModel &model, nb::capsule command_buffer, uint32_t image_index, bool wireframe) {
                    auto command = static_cast<VkCommandBuffer>(command_buffer.data());
                    if (command == VK_NULL_HANDLE)
                        throw nb::value_error("command_buffer capsule must contain a VkCommandBuffer pointer");
                    model.render(command, image_index, wireframe);
                },
                nb::arg("command_buffer"),
                nb::arg("image_index"),
                nb::arg("wireframe") = false)
            .def(
                "render_with_push_constants",
                [](VKAbstractModel &model, nb::capsule command_buffer, uint32_t image_index, size_t texture_index, const UniformBufferObject &uniforms, bool wireframe) {
                    auto command = static_cast<VkCommandBuffer>(command_buffer.data());
                    if (command == VK_NULL_HANDLE)
                        throw nb::value_error("command_buffer capsule must contain a VkCommandBuffer pointer");
                    model.renderWithPushConstants(command, image_index, texture_index, uniforms, wireframe);
                },
                nb::arg("command_buffer"),
                nb::arg("image_index"),
                nb::arg("texture_index"),
                nb::arg("uniforms"),
                nb::arg("wireframe") = false)
            .def("resize", &VKAbstractModel::resize, nb::arg("window"))
            .def("cleanup", &VKAbstractModel::cleanup, nb::arg("window"))
            .def("set_backface_culling", &VKAbstractModel::setBackfaceCulling, nb::arg("enabled"))
            .def("set_alpha_blending", &VKAbstractModel::setAlphaBlending, nb::arg("enabled"))
            .def(
                "set_color_attachment_format",
                [](VKAbstractModel &model, int format) { model.setColorAttachmentFormat(static_cast<VkFormat>(format)); },
                nb::arg("format"))
            .def_prop_ro("model", &VKAbstractModel::model, nb::rv_policy::reference_internal)
            .def_prop_ro("center_offset",
                         [](const VKAbstractModel &model) {
                             const auto value = model.modelCenterOffset();
                             return nb::make_tuple(value.x, value.y, value.z);
                         })
            .def_prop_ro("render_scale", &VKAbstractModel::modelRenderScale)
            .def_prop_ro("axis_extent",
                         [](const VKAbstractModel &model) {
                             const auto value = model.modelAxisExtent();
                             return nb::make_tuple(value.x, value.y, value.z);
                         })
            .def_prop_ro("loaded", &VKAbstractModel::isLoaded);

        auto sprite = nb::class_<VK_Sprite>(module, "Sprite");
        sprite.def("load", nb::overload_cast<const std::string &, const std::string &>(&VK_Sprite::loadSprite), nb::arg("path"), nb::arg("fragment_shader_path") = "")
            .def("create_empty", &VK_Sprite::createEmptySprite, nb::arg("width"), nb::arg("height"), nb::arg("vertex_shader_path") = "", nb::arg("fragment_shader_path") = "")
            .def("create_empty_rgba16", &VK_Sprite::createEmptySpriteRgba16, nb::arg("width"), nb::arg("height"), nb::arg("vertex_shader_path") = "", nb::arg("fragment_shader_path") = "")
            .def("draw", nb::overload_cast<int, int>(&VK_Sprite::drawSprite), nb::arg("x"), nb::arg("y"))
            .def("draw", nb::overload_cast<int, int, float, float>(&VK_Sprite::drawSprite), nb::arg("x"), nb::arg("y"), nb::arg("scale_x"), nb::arg("scale_y"))
            .def("draw_rotated", nb::overload_cast<int, int, float, float, float>(&VK_Sprite::drawSprite), nb::arg("x"), nb::arg("y"), nb::arg("scale_x"), nb::arg("scale_y"), nb::arg("rotation"))
            .def("draw_rect", &VK_Sprite::drawSpriteRect, nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"))
            .def(
                "update_texture",
                [](VK_Sprite &value, nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu> pixels, int width, int height, int pitch) { value.updateTexture(pixels.data(), width, height, pitch); },
                nb::arg("pixels"),
                nb::arg("width"),
                nb::arg("height"),
                nb::arg("pitch") = 0)
            .def(
                "update_texture_rgba16",
                [](VK_Sprite &value, nb::ndarray<uint16_t, nb::c_contig, nb::device::cpu> pixels, int width, int height, int pitch) { value.updateTextureRgba16(pixels.data(), width, height, pitch); },
                nb::arg("pixels"),
                nb::arg("width"),
                nb::arg("height"),
                nb::arg("pitch") = 0)
            .def("set_shader_params", &VK_Sprite::setShaderParams, nb::arg("p1") = 0.0F, nb::arg("p2") = 0.0F, nb::arg("p3") = 0.0F, nb::arg("p4") = 0.0F)
            .def("set_effects_enabled", &VK_Sprite::setEffectsEnabled, nb::arg("enabled"))
            .def("effects_enabled", &VK_Sprite::getEffectsEnabled)
            .def("clear_queue", &VK_Sprite::clearQueue)
            .def("set_vertex_shader_path", &VK_Sprite::setVertexShaderPath, nb::arg("path"))
            .def("set_fragment_shader_path", &VK_Sprite::setFragmentShaderPath, nb::arg("path"))
            .def("enable_compute_shader", &VK_Sprite::enableComputeShader, nb::arg("path"), nb::arg("local_size_x"), nb::arg("local_size_y"), nb::arg("local_size_z") = 1)
            .def("enable_instancing", &VK_Sprite::enableInstancing, nb::arg("max_instances"), nb::arg("vertex_shader_path"), nb::arg("fragment_shader_path"))
            .def("instancing_enabled", &VK_Sprite::isInstancingEnabled)
            .def("enable_extended_ubo", &VK_Sprite::enableExtendedUBO)
            .def("extended_ubo_enabled", &VK_Sprite::isExtendedUBOEnabled)
            .def("set_mouse_state", &VK_Sprite::setMouseState, nb::arg("x"), nb::arg("y"), nb::arg("pressed"), nb::arg("reserved") = 0.0F)
            .def("set_uniform0", &VK_Sprite::setUniform0)
            .def("set_uniform1", &VK_Sprite::setUniform1)
            .def("set_uniform2", &VK_Sprite::setUniform2)
            .def("set_uniform3", &VK_Sprite::setUniform3)
            .def("set_audio_bands", &VK_Sprite::setAudioBands, nb::arg("low"), nb::arg("mid"), nb::arg("high"), nb::arg("reserved") = 0.0F)
            .def("set_custom_uniforms", &VK_Sprite::setCustomUniforms, nb::arg("values"))
            .def("enable_history_texture", &VK_Sprite::enableHistoryTexture, nb::arg("width"), nb::arg("height"), nb::arg("layers"))
            .def("enable_history_texture_rgba16", &VK_Sprite::enableHistoryTextureRgba16Float, nb::arg("width"), nb::arg("height"), nb::arg("layers"))
            .def("share_history_texture", &VK_Sprite::shareHistoryTexture, nb::arg("source"))
            .def("enable_spectrum_texture", &VK_Sprite::enableSpectrumTexture, nb::arg("bins"))
            .def("enable_spectrum_history_texture", &VK_Sprite::enableSpectrumHistoryTexture, nb::arg("bins"), nb::arg("layers"))
            .def_prop_ro("width", &VK_Sprite::getWidth)
            .def_prop_ro("height", &VK_Sprite::getHeight)
            .def_prop_ro("has_own_pipeline", &VK_Sprite::hasOwnPipeline);

        auto sprite3d = nb::class_<VK_Sprite3D>(module, "Sprite3D");
        sprite3d.def(nb::init<>())
            .def("load", nb::overload_cast<VK_Window *, const std::string &, const std::string &, const std::string &>(&VK_Sprite3D::load), nb::arg("window"), nb::arg("path"), nb::arg("vertex_shader_path") = "", nb::arg("fragment_shader_path") = "")
            .def(
                "update_camera",
                [](VK_Sprite3D &value, uint32_t image_index, const std::array<float, 16> &view, const std::array<float, 16> &projection) { value.updateCamera(image_index, matrix_from_rows(view), matrix_from_rows(projection)); },
                nb::arg("image_index"),
                nb::arg("view"),
                nb::arg("projection"))
            .def(
                "draw",
                [](VK_Sprite3D &value, const std::array<float, 3> &position, const std::array<float, 2> &size, const std::array<float, 4> &color, float rotation) { value.drawSprite(glm::vec3{position[0], position[1], position[2]}, glm::vec2{size[0], size[1]}, vector4(color), rotation); },
                nb::arg("position"),
                nb::arg("size"),
                nb::arg("color") = std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F},
                nb::arg("rotation") = 0.0F)
            .def(
                "render",
                [](VK_Sprite3D &value, nb::capsule command_buffer, uint32_t image_index) {
                    auto command = static_cast<VkCommandBuffer>(command_buffer.data());
                    if (command == VK_NULL_HANDLE)
                        throw nb::value_error("command_buffer capsule must contain a VkCommandBuffer pointer");
                    value.render(command, image_index);
                },
                nb::arg("command_buffer"),
                nb::arg("image_index"))
            .def("clear_queue", &VK_Sprite3D::clearQueue)
            .def("set_depth_test_enabled", &VK_Sprite3D::setDepthTestEnabled, nb::arg("enabled"))
            .def("set_depth_write_enabled", &VK_Sprite3D::setDepthWriteEnabled, nb::arg("enabled"))
            .def("set_alpha_discard_threshold", &VK_Sprite3D::setAlphaDiscardThreshold, nb::arg("threshold"))
            .def("resize", &VK_Sprite3D::resize, nb::arg("window"))
            .def("cleanup", &VK_Sprite3D::cleanup)
            .def_prop_ro("loaded", &VK_Sprite3D::loaded)
            .def_prop_ro("width", &VK_Sprite3D::getWidth)
            .def_prop_ro("height", &VK_Sprite3D::getHeight);

        nb::enum_<ShaderStage>(module, "ShaderStage").value("unknown", ShaderStage::Unknown).value("vertex", ShaderStage::Vertex).value("fragment", ShaderStage::Fragment).value("compute", ShaderStage::Compute);

        nb::enum_<VK_Window::PresentModePreference>(module, "PresentModePreference").value("low_latency", VK_Window::PresentModePreference::LowLatency).value("vsync", VK_Window::PresentModePreference::Vsync);

        nb::enum_<VK_Window::RuntimeMode>(module, "RuntimeMode").value("windowed", VK_Window::RuntimeMode::Windowed).value("headless", VK_Window::RuntimeMode::Headless);

        nb::class_<VulkanContext>(module, "VulkanContext").def_prop_ro("device", [](const VulkanContext &context) { return reinterpret_cast<uintptr_t>(context.device); }).def_prop_ro("physical_device", [](const VulkanContext &context) { return reinterpret_cast<uintptr_t>(context.physical_device); }).def_prop_ro("graphics_queue", [](const VulkanContext &context) { return reinterpret_cast<uintptr_t>(context.graphics_queue); }).def_prop_ro("command_pool", [](const VulkanContext &context) { return reinterpret_cast<uintptr_t>(context.command_pool); });

        nb::class_<VK_Window::PostProcessingEffect>(module, "PostProcessingEffect").def(nb::init<>()).def_rw("fragment_shader_path", &VK_Window::PostProcessingEffect::fragmentShaderPath).def_rw("params", &VK_Window::PostProcessingEffect::params).def_rw("time_enabled", &VK_Window::PostProcessingEffect::timeEnabled).def_rw("spectrum_bin_count", &VK_Window::PostProcessingEffect::spectrumBinCount).def_rw("spectrum_history_layer_count", &VK_Window::PostProcessingEffect::spectrumHistoryLayerCount).def_rw("stage", &VK_Window::PostProcessingEffect::stage).def_rw("history_source", &VK_Window::PostProcessingEffect::historySource);

        nb::class_<VK_Window>(module, "Window")
            .def(nb::init<>())
            .def(nb::init<const std::string &, int, int, bool, bool, VK_Window::PresentModePreference, VK_Window::RuntimeMode>(), nb::arg("title"), nb::arg("width"), nb::arg("height"), nb::arg("fullscreen") = false, nb::arg("validation") = true, nb::arg("present_mode") = VK_Window::PresentModePreference::LowLatency, nb::arg("runtime_mode") = VK_Window::RuntimeMode::Windowed)
            .def(nb::init<const std::string &, int, int, bool, bool, bool>(), nb::arg("title"), nb::arg("width"), nb::arg("height"), nb::arg("fullscreen"), nb::arg("validation"), nb::arg("enable_vsync"))
            .def("release", &VK_Window::release)
            .def("init_vulkan", &VK_Window::initVulkan, nb::arg("validation") = true)
            .def(
                "event",
                [](VK_Window &window, nb::capsule event) {
                    auto *sdl_event = static_cast<SDL_Event *>(event.data());
                    if (sdl_event == nullptr)
                        throw nb::value_error("event capsule must contain an SDL_Event pointer");
                    window.event(*sdl_event);
                },
                nb::arg("event"))
            .def("loop", &VK_Window::loop)
            .def("render", &VK_Window::render)
            .def(
                "on_prepare_frame_rendering",
                [](VK_Window &window, nb::capsule command_buffer, uint32_t image_index) {
                    auto command_buffer_handle = static_cast<VkCommandBuffer>(command_buffer.data());
                    if (command_buffer_handle == VK_NULL_HANDLE)
                        throw nb::value_error("command_buffer capsule must contain a VkCommandBuffer pointer");
                    window.onPrepareFrameRendering(command_buffer_handle, image_index);
                },
                nb::arg("command_buffer"),
                nb::arg("image_index"))
            .def("proc", &VK_Window::proc)
            .def("save_snapshot", &VK_Window::saveSnapshot, nb::arg("path"))
            .def("set_font", &VK_Window::setFont, nb::arg("path"), nb::arg("size") = 24)
            .def("set_preview_font", &VK_Window::setPreviewFont, nb::arg("path"), nb::arg("size") = 24)
            .def("print_text", nb::overload_cast<const std::string &, int, int, const SDL_Color &>(&VK_Window::printText), nb::arg("text"), nb::arg("x"), nb::arg("y"), nb::arg("color"))
            .def("print_text", nb::overload_cast<const std::string &, int, int, const SDL_Color &, const Font &>(&VK_Window::printText), nb::arg("text"), nb::arg("x"), nb::arg("y"), nb::arg("color"), nb::arg("font"))
            .def("print_preview_text", nb::overload_cast<const std::string &, int, int, const SDL_Color &>(&VK_Window::printPreviewText), nb::arg("text"), nb::arg("x"), nb::arg("y"), nb::arg("color"))
            .def("print_preview_text", nb::overload_cast<const std::string &, int, int, const SDL_Color &, const Font &>(&VK_Window::printPreviewText), nb::arg("text"), nb::arg("x"), nb::arg("y"), nb::arg("color"), nb::arg("font"))
            .def("clear_text_queue", &VK_Window::clearTextQueue)
            .def("set_clear_color", &VK_Window::setClearColor, nb::arg("red"), nb::arg("green"), nb::arg("blue"), nb::arg("alpha") = 1.0F)
            .def("set_enable_screenshot", &VK_Window::setEnableScreenshot, nb::arg("enabled"))
            .def("screenshot_enabled", &VK_Window::screenshotEnabled)
            .def("set_frame_readback_enabled", &VK_Window::setFrameReadbackEnabled, nb::arg("enabled"))
            .def("set_frame_readback_rgba16_enabled", &VK_Window::setFrameReadbackRgba16Enabled, nb::arg("enabled"))
            .def("set_hdr_render_intermediates_enabled", &VK_Window::setHdrRenderIntermediatesEnabled, nb::arg("enabled"))
            .def("set_render_extent", &VK_Window::setRenderExtent, nb::arg("width"), nb::arg("height"))
            .def("headless", &VK_Window::headless)
            .def("ensure_render_resources", &VK_Window::ensureRenderResources)
            .def("trim_memory", &VK_Window::trimMemory)
            .def(
                "get_text_dimensions",
                [](VK_Window &window, const std::string &text) -> nb::object {
                    int width = 0;
                    int height = 0;
                    if (!window.getTextDimensions(text, width, height))
                        return nb::none();
                    return nb::cast(nb::make_tuple(width, height));
                },
                nb::arg("text"))
            .def(
                "get_text_dimensions",
                [](VK_Window &window, const std::string &text, const Font &font) -> nb::object {
                    int width = 0;
                    int height = 0;
                    if (!window.getTextDimensions(text, width, height, font))
                        return nb::none();
                    return nb::cast(nb::make_tuple(width, height));
                },
                nb::arg("text"),
                nb::arg("font"))
            .def(
                "create_sprite",
                [](VK_Window &window, const std::string &path, const std::string &vertex_shader_path, const std::string &fragment_shader_path) { return window.createSprite(path, vertex_shader_path, fragment_shader_path); },
                nb::arg("path"),
                nb::arg("vertex_shader_path") = "",
                nb::arg("fragment_shader_path") = "",
                nb::rv_policy::reference_internal)
            .def(
                "create_sprite_from_surface",
                [](VK_Window &window, nb::capsule surface, const std::string &vertex_shader_path, const std::string &fragment_shader_path) {
                    auto *sdl_surface = static_cast<SDL_Surface *>(surface.data());
                    if (sdl_surface == nullptr)
                        throw nb::value_error("surface capsule must contain an SDL_Surface pointer");
                    return window.createSprite(sdl_surface, vertex_shader_path, fragment_shader_path);
                },
                nb::arg("surface"),
                nb::arg("vertex_shader_path") = "",
                nb::arg("fragment_shader_path") = "",
                nb::rv_policy::reference_internal)
            .def(
                "create_sprite",
                [](VK_Window &window, int width, int height, const std::string &vertex_shader_path, const std::string &fragment_shader_path, uint32_t spectrum_bin_count) { return window.createSprite(width, height, vertex_shader_path, fragment_shader_path, spectrum_bin_count); },
                nb::arg("width"),
                nb::arg("height"),
                nb::arg("vertex_shader_path") = "",
                nb::arg("fragment_shader_path") = "",
                nb::arg("spectrum_bin_count") = 0,
                nb::rv_policy::reference_internal)
            .def(
                "create_sprite_with_spectrum_history",
                [](VK_Window &window, int width, int height, const std::string &vertex_shader_path, const std::string &fragment_shader_path, uint32_t spectrum_bin_count, uint32_t spectrum_history_layer_count) { return window.createSprite(width, height, vertex_shader_path, fragment_shader_path, spectrum_bin_count, spectrum_history_layer_count); },
                nb::arg("width"),
                nb::arg("height"),
                nb::arg("vertex_shader_path"),
                nb::arg("fragment_shader_path"),
                nb::arg("spectrum_bin_count"),
                nb::arg("spectrum_history_layer_count"),
                nb::rv_policy::reference_internal)
            .def("attach_post_processing_shader", &VK_Window::attachPostProcessingShader, nb::arg("fragment_shader_path"), nb::arg("p1") = 0.0F, nb::arg("p2") = 0.0F, nb::arg("p3") = 0.0F, nb::arg("p4") = 0.0F, nb::rv_policy::reference_internal)
            .def("attach_post_processing_shaders", &VK_Window::attachPostProcessingShaders, nb::arg("effects"), nb::rv_policy::reference_internal)
            .def("detach_post_processing_shader", &VK_Window::detachPostProcessingShader)
            .def("set_post_processing_shader_params", nb::overload_cast<float, float, float, float>(&VK_Window::setPostProcessingShaderParams), nb::arg("p1") = 0.0F, nb::arg("p2") = 0.0F, nb::arg("p3") = 0.0F, nb::arg("p4") = 0.0F)
            .def("set_post_processing_effect_params", nb::overload_cast<size_t, float, float, float, float>(&VK_Window::setPostProcessingShaderParams), nb::arg("effect_index"), nb::arg("p1") = 0.0F, nb::arg("p2") = 0.0F, nb::arg("p3") = 0.0F, nb::arg("p4") = 0.0F)
            .def("set_post_processing_shader_time_enabled", nb::overload_cast<bool>(&VK_Window::setPostProcessingShaderTimeEnabled), nb::arg("enabled"))
            .def("set_post_processing_effect_time_enabled", nb::overload_cast<size_t, bool>(&VK_Window::setPostProcessingShaderTimeEnabled), nb::arg("effect_index"), nb::arg("enabled"))
            .def("enable_post_processing", &VK_Window::enablePostProcessing, nb::arg("sprite"))
            .def("set_post_processing_enabled", &VK_Window::setPostProcessingEnabled, nb::arg("enabled"))
            .def("set_post_processing_texture_consumer_enabled", &VK_Window::setPostProcessingTextureConsumerEnabled, nb::arg("enabled"))
            .def("set_post_processing_present_fragment_shader", &VK_Window::setPostProcessingPresentFragmentShader, nb::arg("path"))
            .def(
                "create_sprite3d",
                [](VK_Window &window, const std::string &path, const std::string &vertex_shader_path, const std::string &fragment_shader_path) { return window.createSprite3D(path, vertex_shader_path, fragment_shader_path); },
                nb::arg("path"),
                nb::arg("vertex_shader_path") = "",
                nb::arg("fragment_shader_path") = "",
                nb::rv_policy::reference_internal)
            .def(
                "create_sprite3d_from_surface",
                [](VK_Window &window, nb::capsule surface, const std::string &vertex_shader_path, const std::string &fragment_shader_path) {
                    auto *sdl_surface = static_cast<SDL_Surface *>(surface.data());
                    if (sdl_surface == nullptr)
                        throw nb::value_error("surface capsule must contain an SDL_Surface pointer");
                    return window.createSprite3D(sdl_surface, vertex_shader_path, fragment_shader_path);
                },
                nb::arg("surface"),
                nb::arg("vertex_shader_path") = "",
                nb::arg("fragment_shader_path") = "",
                nb::rv_policy::reference_internal)
            .def("show_cursor", &VK_Window::showCursor, nb::arg("visible"))
            .def("validation_enabled", &VK_Window::validationEnabled)
            .def("context", &VK_Window::context)
            .def_prop_ro("sdl_window", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getSDLWindow()); })
            .def_prop_ro("device", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getDevice()); })
            .def_prop_ro("physical_device", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getPhysicalDevice()); })
            .def_prop_ro("graphics_queue", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getGraphicsQueue()); })
            .def_prop_ro("command_pool", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getCommandPool()); })
            .def_prop_ro("pipeline_cache", [](const VK_Window &window) { return reinterpret_cast<uintptr_t>(window.getPipelineCache()); })
            .def_prop_ro("swapchain_format", [](const VK_Window &window) { return static_cast<int>(window.getSwapchainFormat()); })
            .def_prop_ro("scene_color_format", [](const VK_Window &window) { return static_cast<int>(window.getSceneColorFormat()); })
            .def_prop_ro("depth_format", [](const VK_Window &window) { return static_cast<int>(window.getDepthFormat()); })
            .def_prop_ro("swapchain_extent",
                         [](const VK_Window &window) {
                             const VkExtent2D extent = window.getSwapchainExtent();
                             return nb::make_tuple(extent.width, extent.height);
                         })
            .def_prop_ro("render_extent",
                         [](const VK_Window &window) {
                             const VkExtent2D extent = window.getRenderExtent();
                             return nb::make_tuple(extent.width, extent.height);
                         })
            .def_prop_ro("swapchain_image_count", &VK_Window::getSwapchainImageCount);

        nb::class_<PythonEvent>(module, "Event").def_prop_ro("type", &PythonEvent::type).def_prop_ro("timestamp", &PythonEvent::timestamp).def_prop_ro("key", &PythonEvent::key).def_prop_ro("scancode", &PythonEvent::scancode).def_prop_ro("modifiers", &PythonEvent::modifiers).def_prop_ro("down", &PythonEvent::down).def_prop_ro("repeat", &PythonEvent::repeat).def_prop_ro("text", &PythonEvent::text).def_prop_ro("x", &PythonEvent::x).def_prop_ro("y", &PythonEvent::y).def_prop_ro("relative_x", &PythonEvent::relative_x).def_prop_ro("relative_y", &PythonEvent::relative_y).def_prop_ro("button", &PythonEvent::button);

        nb::class_<VK_IOWindow, VK_Window, PythonIOWindow>(module, "IOWindow")
            .def(nb::init<const std::string &, const std::string &, int, int, bool, bool>(), nb::arg("path"), nb::arg("title"), nb::arg("width"), nb::arg("height"), nb::arg("fullscreen") = false, nb::arg("enable_vsync") = false)
            .def("print", &VK_IOWindow::print, nb::arg("text"), nb::arg("color") = SDL_Color{255, 255, 255, 255})
            .def("request_exit", &VK_IOWindow::requestExit)
            .def("console_proc", &VK_IOWindow::console_proc)
            .def(
                "console_event",
                [](VK_IOWindow &window, nb::capsule event) {
                    auto *value = static_cast<SDL_Event *>(event.data());
                    if (value == nullptr)
                        throw nb::value_error("event capsule must contain an SDL_Event pointer");
                    window.console_event(*value);
                },
                nb::arg("event"))
            .def_prop_ro("visible", &VK_IOWindow::visible);
    }
} // namespace mxvk

NB_MODULE(mxvk_ext, module) { mxvk::bind_nanobind_module(module); }
