#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

namespace example {
    class HeadlessExample final : public mxvk::VK_Window {
      public:
        HeadlessExample()
            : mxvk::VK_Window(
                  "MXVK Headless", WIDTH, HEIGHT, false, MXVK_VALIDATION,
                  PresentModePreference::LowLatency, RuntimeMode::Headless) {
            setEnableScreenshot(false);
            setFrameReadbackEnabled(true);
            setClearColor(0.125F, 0.25F, 0.5F, 1.0F);
        }

        [[nodiscard]] bool run() {
            loop();
            return readback_verified;
        }

      protected:
        void proc() override {
            ++processed_frames;
            if (processed_frames > MAX_FRAME_COUNT) {
                std::cerr << "headless example: timed out waiting for readback\n";
                exit();
            }
        }

        void onFrameReadback(std::vector<std::uint8_t> &rgba_pixels,
                             uint32_t width, uint32_t height) override {
            if (readback_verified) {
                return;
            }
            const bool dimensions_match = width == WIDTH && height == HEIGHT;
            const bool pixels_available =
                rgba_pixels.size() ==
                static_cast<size_t>(WIDTH) * HEIGHT * 4U;
            if (!dimensions_match || !pixels_available) {
                std::cerr << "headless example: invalid frame readback\n";
                exit();
                return;
            }

            const size_t center =
                (static_cast<size_t>(HEIGHT / 2U) * WIDTH + WIDTH / 2U) * 4U;
            const bool color_matches = rgba_pixels[center] >= 30U &&
                                       rgba_pixels[center] <= 34U &&
                                       rgba_pixels[center + 1U] >= 62U &&
                                       rgba_pixels[center + 1U] <= 66U &&
                                       rgba_pixels[center + 2U] >= 126U &&
                                       rgba_pixels[center + 2U] <= 130U;
            if (!color_matches) {
                std::cerr << "headless example: clear-color readback mismatch\n";
                exit();
                return;
            }

            readback_verified = true;
            std::cout << "headless example: surface-free RGBA readback verified "
                      << width << 'x' << height << '\n';
            exit();
        }

      private:
        static constexpr uint32_t WIDTH = 320;
        static constexpr uint32_t HEIGHT = 180;
        static constexpr uint32_t MAX_FRAME_COUNT = 8;
        uint32_t processed_frames = 0;
        bool readback_verified = false;
    };
} // namespace example

int main() {
    try {
        example::HeadlessExample app;
        return app.run() ? 0 : 1;
    } catch (const mxvk::Exception &ex) {
        std::cerr << "headless example: " << ex.text() << '\n';
    } catch (const std::exception &ex) {
        std::cerr << "headless example: " << ex.what() << '\n';
    }
    return 1;
}
