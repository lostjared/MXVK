#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <random>

namespace surface {

    struct SurfaceDeleter {
        void operator()(SDL_Surface *surface) const {
            if (surface != nullptr) {
                SDL_DestroySurface(surface);
            }
        }
    };

    class SurfaceWindow : public mxvk::VK_Window {

    public:
        SurfaceWindow(int width, int height, bool full) : mxvk::VK_Window(" -[ MXVK Skeleton ] - ", width, height, full, MXVK_VALIDATION) {
            std::cout << "skeleton: started example.\n";
            resize_canvas(width, height);
            surf = createSprite(bg.get(), "", "");
        }
        void event(SDL_Event &e) override {
            switch (e.type) {
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) {
                        exit();
                        return;
                    }
                    break;
                case SDL_EVENT_QUIT:
                    exit();
                    return;
            }
        }
        void proc() override {
            SDL_Surface *canvas = bg.get();
            if (canvas == nullptr || !SDL_LockSurface(canvas)) {
                return;
            }
            auto *pixels = static_cast<Uint8 *>(canvas->pixels);
            for (int y = 0; y < canvas->h; ++y) {
                Uint8 *row = pixels + y * canvas->pitch;
                for (int x = 0; x < canvas->w; ++x) {
                    const std::uint32_t color = random_color(random_engine);
                    Uint8 *p = row + x * 4;
                    p[0] = static_cast<Uint8>(color & 0xFFU);
                    p[1] = static_cast<Uint8>((color >> 8U) & 0xFFU);
                    p[2] = static_cast<Uint8>((color >> 16U) & 0xFFU);
                    p[3] = static_cast<Uint8>(255);
                }
            }
            SDL_UnlockSurface(canvas);
            surf->updateTexture(canvas);
            surf->drawSpriteRect(0, 0, target_width, target_height);
        }

        void onSwapchainRecreated() override {
            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }
            resize_canvas(static_cast<int>(extent.width), static_cast<int>(extent.height));
        }

    private:
        void resize_canvas(int width, int height) {
            if (width <= 0 || height <= 0 || (bg != nullptr && bg->w == width && bg->h == height)) {
                return;
            }
            bg.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (bg == nullptr) {
                throw mxvk::Exception("Failed to create surface canvas: " + std::string(SDL_GetError()));
            }
            SDL_FillSurfaceRect(bg.get(), nullptr, SDL_MapSurfaceRGBA(bg.get(), 0, 0, 0, 255));
            target_width = width;
            target_height = height;
        }

        mxvk::VK_Sprite *surf = nullptr;
        std::unique_ptr<SDL_Surface, surface::SurfaceDeleter> bg;
        int target_width = 1280;
        int target_height = 720;
        std::mt19937 random_engine{std::random_device{}()};
        std::uniform_int_distribution<std::uint32_t> random_color{0U, 0x00FFFFFFU};
    };
} // namespace surface

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        surface::SurfaceWindow window(args.width, args.height, args.fullscreen);
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
