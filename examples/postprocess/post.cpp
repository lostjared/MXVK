#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace example {
    struct SurfaceDeleter {
        void operator()(SDL_Surface *surface) const {
            if (surface != nullptr) {
                SDL_DestroySurface(surface);
            }
        }
    };

    class PostprocessWindow : public mxvk::VK_Window {
      public:
        PostprocessWindow(const std::string &path, int width, int height, bool fullscreen)
            : mxvk::VK_Window("MXVK Postprocess Chain", width, height, fullscreen, MXVK_VALIDATION),
              asset_root((path.empty() || path == ".") ? std::string(postprocess_ASSET_DIR) : path) {
            resizeCanvas(width, height);
            sprite = createSprite(canvas.get());
            attachEffects(loadEffects(asset_root + "/data/shaders.txt"));
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
            if (e.type == SDL_EVENT_QUIT) {
                exit();
            }
        }

        void proc() override {
            const VkExtent2D extent = getSwapchainExtent();
            const int targetWidth = extent.width > 0U ? static_cast<int>(extent.width) : canvas_width;
            const int targetHeight = extent.height > 0U ? static_cast<int>(extent.height) : canvas_height;
            if (targetWidth != canvas_width || targetHeight != canvas_height) {
                resizeCanvas(targetWidth, targetHeight);
            }

            drawCanvas();
            if (sprite != nullptr) {
                sprite->updateTexture(canvas.get());
                sprite->drawSpriteRect(0, 0, canvas_width, canvas_height);
            }
        }

      private:
        static std::string trim(std::string value) {
            const auto first = std::ranges::find_if(value, [](unsigned char ch) {
                return !std::isspace(ch);
            });
            const auto last = std::ranges::find_if(value | std::views::reverse, [](unsigned char ch) {
                return !std::isspace(ch);
            }).base();
            if (first >= last) {
                return {};
            }
            return std::string(first, last);
        }

        std::vector<mxvk::VK_Window::PostProcessingEffect> loadEffects(const std::string &manifestPath) const {
            std::ifstream file(manifestPath);
            if (!file.is_open()) {
                throw mxvk::Exception("postprocess: failed to open " + manifestPath);
            }

            std::vector<mxvk::VK_Window::PostProcessingEffect> effects;
            std::string line;
            while (std::getline(file, line)) {
                const size_t comment = line.find('#');
                if (comment != std::string::npos) {
                    line.resize(comment);
                }
                line = trim(line);
                if (line.empty()) {
                    continue;
                }

                std::filesystem::path shader_path(line);
                if (shader_path.is_relative()) {
                    shader_path = std::filesystem::path(asset_root) / "data" / shader_path;
                }
                if (!std::filesystem::exists(shader_path)) {
                    throw mxvk::Exception("postprocess: shader listed in shaders.txt was not found: " + shader_path.string());
                }

                mxvk::VK_Window::PostProcessingEffect effect{};
                effect.fragmentShaderPath = shader_path.string();
                effect.params = {0.0f, 1.0f, 1.0f, 0.0f};
                effect.timeEnabled = true;
                effects.push_back(effect);
            }

            return effects;
        }

        void attachEffects(const std::vector<mxvk::VK_Window::PostProcessingEffect> &effects) {
            if (effects.empty()) {
                setPostProcessingEnabled(false);
                return;
            }

            attachPostProcessingShaders(effects);
            for (size_t i = 0; i < effects.size(); ++i) {
                setPostProcessingShaderTimeEnabled(i, effects[i].timeEnabled);
            }
            setPostProcessingEnabled(true);
            std::cout << std::format("postprocess: attached {} post-processing effect(s)\n", effects.size());
        }

        void resizeCanvas(int width, int height) {
            if (width <= 0 || height <= 0) {
                return;
            }
            if (canvas != nullptr && canvas_width == width && canvas_height == height) {
                return;
            }

            canvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (canvas == nullptr) {
                throw mxvk::Exception("postprocess: failed to create SDL surface: " + std::string(SDL_GetError()));
            }
            canvas_width = width;
            canvas_height = height;
        }

        void drawCanvas() {
            if (canvas == nullptr || !SDL_LockSurface(canvas.get())) {
                return;
            }

            ++frame;
            auto *pixels = static_cast<std::uint8_t *>(canvas->pixels);
            for (int y = 0; y < canvas_height; ++y) {
                std::uint8_t *row = pixels + y * canvas->pitch;
                for (int x = 0; x < canvas_width; ++x) {
                    const float nx = static_cast<float>(x) / static_cast<float>(std::max(canvas_width - 1, 1));
                    const float ny = static_cast<float>(y) / static_cast<float>(std::max(canvas_height - 1, 1));
                    const int checker = ((x / 48) + (y / 48)) & 1;
                    const std::uint8_t red = static_cast<std::uint8_t>(40.0f + nx * 180.0f);
                    const std::uint8_t green = static_cast<std::uint8_t>(50.0f + ny * 160.0f);
                    const std::uint8_t blue = static_cast<std::uint8_t>(checker != 0 ? 220 : 80);
                    std::uint8_t *pixel = row + x * 4;
                    pixel[0] = static_cast<std::uint8_t>((red + frame) & 0xFF);
                    pixel[1] = green;
                    pixel[2] = blue;
                    pixel[3] = 255;
                }
            }

            SDL_UnlockSurface(canvas.get());
        }

        std::string asset_root;
        mxvk::VK_Sprite *sprite = nullptr;
        std::unique_ptr<SDL_Surface, SurfaceDeleter> canvas;
        int canvas_width = 1280;
        int canvas_height = 720;
        std::uint8_t frame = 0;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::PostprocessWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
