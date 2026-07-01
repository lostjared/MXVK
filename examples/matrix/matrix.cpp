#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include "rain.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <string>

namespace example {
    class MatrixWindow : public mxvk::VK_Window {
      public:
        MatrixWindow(const std::string &path,
                     const std::string &title,
                     const int width,
                     const int height,
                     const bool fullscreen,
                     const bool enable_vsync,
                     const bool binary,
                     const int font_size,
                     const std::string &font_path,
                     const std::string &color)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              rain(std::make_unique<matrix::Rain>(
                  *this, [path, binary, font_size, font_path, color]() {
                      matrix::RainConfig config = matrix::make_matrix_rain_config(path.empty() ? std::string(matrix_ASSET_DIR) : path, binary);
                      config.font_size = std::max(1, font_size);
                      if (!font_path.empty()) {
                          config.font_path = font_path;
                      }
                      config.color = color;
                      return config;
                  }())) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }

        ~MatrixWindow() override {
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                }
            }

            if (rain) {
                rain->event(e);
            }
        }

        void proc() override {
            if (rain) {
                rain->update_and_render(*this);
            }
        }

        void onSwapchainRecreated() override {
            if (rain) {
                rain->on_swapchain_recreated(*this);
            }
        }

      private:
        std::unique_ptr<matrix::Rain> rain;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::MatrixWindow window(
            args.path, "-[ MXVK Matrix Digital Rain ]-", args.width, args.height, args.fullscreen, args.enable_vsync, args.binary,
            args.font_size, args.font_path, args.color);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
