#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_ff_capture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mxwrite.hpp>

namespace example {
    class VideoInput final {
      public:
        explicit VideoInput(const std::string &path) : filename(path) {
            if (!capture.open(filename) || !readNext()) {
                throw std::runtime_error("could not decode the first frame of '" + filename + "'");
            }
            initial_width = width;
            initial_height = height;
        }

        [[nodiscard]] bool readNext() {
            if (!capture.readRgba(pixels, width, height, pitch)) {
                return false;
            }
            if (width <= 0 || height <= 0 || pitch < width * 4 || pixels.size() < static_cast<size_t>(pitch) * static_cast<size_t>(height)) {
                throw std::runtime_error("invalid decoded frame from '" + filename + "'");
            }
            if (initial_width != 0 && (width != initial_width || height != initial_height)) {
                throw std::runtime_error("video dimensions changed in '" + filename + "'");
            }
            return true;
        }

        [[nodiscard]] int frameWidth() const { return width; }
        [[nodiscard]] int frameHeight() const { return height; }
        [[nodiscard]] int framePitch() const { return pitch; }
        [[nodiscard]] double fps() const { return capture.fps(); }
        [[nodiscard]] std::int64_t frame_count() const { return capture.frame_count(); }
        [[nodiscard]] const std::vector<std::uint8_t> &frame() const { return pixels; }

      private:
        std::string filename;
        mxvk::VK_FF_Capture capture{};
        std::vector<std::uint8_t> pixels{};
        int width = 0;
        int height = 0;
        int pitch = 0;
        int initial_width = 0;
        int initial_height = 0;
    };

    class FillPixel final : public mxvk::VK_Window {
      public:
        FillPixel(VideoInput &source, VideoInput &material, const std::string &output_path, const std::string &shader_path, float alpha, bool restore_black, EncodeOptions options)
            : mxvk::VK_Window("MXVK fill pixel", source.frameWidth(), source.frameHeight(), false, MXVK_VALIDATION, PresentModePreference::LowLatency, RuntimeMode::Headless), source(source), material(material), expected_frames(source.frame_count() > 0 && material.frame_count() > 0 ? std::min(source.frame_count(), material.frame_count()) : 0) {
            setEnableScreenshot(false);
            setFrameReadbackEnabled(true);
            setClearColor(0.0F, 0.0F, 0.0F, 1.0F);

            material_sprite = createSprite(material.frameWidth(), material.frameHeight());
            source_sprite = createSprite(source.frameWidth(), source.frameHeight(), "", shader_path);
            source_sprite->shareOriginalFrameTexture(*material_sprite);
            source_sprite->setShaderParams(restore_black ? 1.0F : 0.0F, alpha, 0.0F, 0.0F);

            options.block_when_full = true;
            if (!writer.open(output_path, source.frameWidth(), source.frameHeight(), static_cast<float>(source.fps()), options)) {
                throw std::runtime_error("could not open output video '" + output_path + "'");
            }
        }

        [[nodiscard]] bool run() {
            loop();
            if (written_frames > 0) {
                print_progress();
                std::cerr << '\n';
            }
            writer.close();
            std::cout << "fill_pixel: wrote " << written_frames << " frames\n";
            return written_frames == rendered_frames && written_frames > 0;
        }

      protected:
        void proc() override {
            if (inputs_finished) {
                exit();
                return;
            }

            material_sprite->updateTexture(material.frame().data(), material.frameWidth(), material.frameHeight(), material.framePitch());
            source_sprite->updateTexture(source.frame().data(), source.frameWidth(), source.frameHeight(), source.framePitch());
            source_sprite->drawSpriteRect(0, 0, source.frameWidth(), source.frameHeight());
            ++rendered_frames;

            const bool source_available = source.readNext();
            const bool material_available = material.readNext();
            inputs_finished = !source_available || !material_available;
        }

        void onFrameReadback(std::vector<std::uint8_t> &rgba_pixels, uint32_t width, uint32_t height) override {
            if (written_frames >= rendered_frames) {
                return;
            }
            if (width != static_cast<uint32_t>(source.frameWidth()) || height != static_cast<uint32_t>(source.frameHeight()) || rgba_pixels.size() != static_cast<size_t>(width) * height * 4U) {
                throw std::runtime_error("headless frame readback has unexpected dimensions");
            }
            writer.write(rgba_pixels.data());
            ++written_frames;
            const auto now = std::chrono::steady_clock::now();
            if (written_frames == 1 || now - last_progress >= std::chrono::milliseconds(250)) {
                print_progress();
                last_progress = now;
            }
        }

      private:
        void print_progress() const {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
            std::cerr << "\rfill_pixel: " << written_frames;
            if (expected_frames > 0) {
                const double percent = std::min(100.0, 100.0 * static_cast<double>(written_frames) / static_cast<double>(expected_frames));
                std::cerr << '/' << expected_frames << " (" << std::fixed << std::setprecision(1) << percent << "%)";
            } else {
                std::cerr << " frames";
            }
            std::cerr << " | " << std::fixed << std::setprecision(1) << (seconds > 0.0 ? static_cast<double>(written_frames) / seconds : 0.0) << " fps   " << std::flush;
        }

        VideoInput &source;
        VideoInput &material;
        mxvk::VK_Sprite *material_sprite = nullptr;
        mxvk::VK_Sprite *source_sprite = nullptr;
        Writer writer{};
        std::uint64_t rendered_frames = 0;
        std::uint64_t written_frames = 0;
        std::int64_t expected_frames = 0;
        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_progress = start_time;
        bool inputs_finished = false;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Argz<std::string> parser(argc, argv);
        parser.addOptionSingle('h', "Show help")
            .addOptionDouble(256, "help", "Show help")
            .addOptionDoubleValue('c', "codec", "Encoder policy or FFmpeg encoder name (default: auto)")
            .addOptionDoubleValue('b', "bitrate", "Target bitrate in bits per second (default: 0, use CRF/CQ)")
            .addOptionDoubleValue('p', "preset", "Encoder preset (default: medium)")
            .addOptionDoubleValue('t', "tune", "Encoder tune (default: encoder default)");

        std::vector<std::string> positional;
        EncodeOptions options{};
        Argument<std::string> argument;
        int code = 0;
        while ((code = parser.proc(argument)) != -1) {
            switch (code) {
            case '-': positional.push_back(argument.arg_value); break;
            case 'h':
            case 256:
                std::cout << "usage: fill_pixel <source-video> <material-video> <output-video> [alpha=1] [restore-black=0|1] [options]\n";
                parser.help(std::cout);
                return 0;
            case 'c': options.codec = argument.arg_value; break;
            case 'p': options.preset = argument.arg_value; break;
            case 't': options.tune = argument.arg_value; break;
            case 'b': {
                size_t parsed = 0;
                options.bit_rate = std::stoll(argument.arg_value, &parsed);
                if (parsed != argument.arg_value.size() || options.bit_rate < 0) {
                    throw std::invalid_argument("bitrate must be a non-negative integer in bits per second");
                }
                break;
            }
            default: throw std::invalid_argument("unknown argument");
            }
        }
        if (positional.size() < 3 || positional.size() > 5) {
            std::cerr << "usage: fill_pixel <source-video> <material-video> <output-video> [alpha=1] [restore-black=0|1] [options]\n";
            return 1;
        }
        const float alpha = positional.size() >= 4 ? std::stof(positional[3]) : 1.0F;
        const float restore_black = positional.size() >= 5 ? std::stof(positional[4]) : 0.0F;
        if (!std::isfinite(alpha) || (restore_black != 0.0F && restore_black != 1.0F)) {
            throw std::invalid_argument("alpha must be finite and restore-black must be 0 or 1");
        }

        example::VideoInput source(positional[0]);
        example::VideoInput material(positional[1]);
        const char *base_path = SDL_GetBasePath();
        if (base_path == nullptr) {
            throw std::runtime_error("could not locate the executable's shader directory");
        }
        const std::filesystem::path shader_path = std::filesystem::path(base_path) / "shaders" / "fill_pixel.frag.spv";
        example::FillPixel app(source, material, positional[2], shader_path.string(), alpha, restore_black == 1.0F, options);
        return app.run() ? 0 : 1;
    } catch (const ArgException<std::string> &ex) {
        std::cerr << "fill_pixel: " << ex.text() << '\n';
    } catch (const mxvk::Exception &ex) {
        std::cerr << "fill_pixel: " << ex.text() << '\n';
    } catch (const std::exception &ex) {
        std::cerr << "fill_pixel: " << ex.what() << '\n';
    }
    return 1;
}
