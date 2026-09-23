#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_ff_capture.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
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
        FillPixel(VideoInput &source, VideoInput &material, const std::string &output_path, const std::string &shader_path, float alpha, bool restore_black)
            : mxvk::VK_Window("MXVK fill pixel", source.frameWidth(), source.frameHeight(), false, MXVK_VALIDATION, PresentModePreference::LowLatency, RuntimeMode::Headless), source(source), material(material) {
            setEnableScreenshot(false);
            setFrameReadbackEnabled(true);
            setClearColor(0.0F, 0.0F, 0.0F, 1.0F);

            material_sprite = createSprite(material.frameWidth(), material.frameHeight());
            source_sprite = createSprite(source.frameWidth(), source.frameHeight(), "", shader_path);
            source_sprite->shareOriginalFrameTexture(*material_sprite);
            source_sprite->setShaderParams(restore_black ? 1.0F : 0.0F, alpha, 0.0F, 0.0F);

            EncodeOptions options{};
            options.block_when_full = true;
            if (!writer.open(output_path, source.frameWidth(), source.frameHeight(), static_cast<float>(source.fps()), options)) {
                throw std::runtime_error("could not open output video '" + output_path + "'");
            }
        }

        [[nodiscard]] bool run() {
            loop();
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
        }

      private:
        VideoInput &source;
        VideoInput &material;
        mxvk::VK_Sprite *material_sprite = nullptr;
        mxvk::VK_Sprite *source_sprite = nullptr;
        Writer writer{};
        std::uint64_t rendered_frames = 0;
        std::uint64_t written_frames = 0;
        bool inputs_finished = false;
    };
} // namespace example

int main(int argc, char **argv) {
    if (argc < 4 || argc > 6) {
        std::cerr << "usage: fill_pixel <source-video> <material-video> <output-video> [alpha=1] [restore-black=0|1]\n";
        return 1;
    }

    try {
        const float alpha = argc >= 5 ? std::stof(argv[4]) : 1.0F;
        const float restore_black = argc >= 6 ? std::stof(argv[5]) : 0.0F;
        if (!std::isfinite(alpha) || (restore_black != 0.0F && restore_black != 1.0F)) {
            throw std::invalid_argument("alpha must be finite and restore-black must be 0 or 1");
        }

        example::VideoInput source(argv[1]);
        example::VideoInput material(argv[2]);
        const char *base_path = SDL_GetBasePath();
        if (base_path == nullptr) {
            throw std::runtime_error("could not locate the executable's shader directory");
        }
        const std::filesystem::path shader_path = std::filesystem::path(base_path) / "shaders" / "fill_pixel.frag.spv";
        example::FillPixel app(source, material, argv[3], shader_path.string(), alpha, restore_black == 1.0F);
        return app.run() ? 0 : 1;
    } catch (const mxvk::Exception &ex) {
        std::cerr << "fill_pixel: " << ex.text() << '\n';
    } catch (const std::exception &ex) {
        std::cerr << "fill_pixel: " << ex.what() << '\n';
    }
    return 1;
}
