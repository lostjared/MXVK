#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <random>

namespace surface {
    struct Point {
        float x = 0.0f;
        float y = 0.0f;
    };

    constexpr float PI = 3.14159265358979323846f;

    std::array<Point, 10> create_star_points(float center_x, float center_y, float outer_radius, float inner_radius) {
        std::array<Point, 10> points{};
        constexpr float start_angle = -PI * 0.5f;
        constexpr float step = PI / 5.0f;

        for (std::size_t i = 0; i < points.size(); ++i) {
            const float radius = (i % 2U == 0U) ? outer_radius : inner_radius;
            const float angle = start_angle + static_cast<float>(i) * step;
            points[i] = {
                .x = center_x + std::cos(angle) * radius,
                .y = center_y + std::sin(angle) * radius,
            };
        }
        return points;
    }

    bool point_in_star(float x, float y, const std::array<Point, 10> &points) {
        bool inside = false;
        for (std::size_t i = 0, j = points.size() - 1U; i < points.size(); j = i++) {
            const Point &a = points[i];
            const Point &b = points[j];
            if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) {
                inside = !inside;
            }
        }
        return inside;
    }

    struct SurfaceDeleter {
        void operator()(SDL_Surface *surface) const {
            if (surface != nullptr) {
                SDL_DestroySurface(surface);
            }
        }
    };

    class SurfaceWindow : public mxvk::VK_Window {

    public:
        SurfaceWindow(std::string shader_path, int width, int height, bool full, bool enable_vsync) : mxvk::VK_Window(" -[ MXVK Stencil Surface ] - ", width, height, full, MXVK_VALIDATION, enable_vsync) {
            std::cout << "stencil_surface: started example.\n";
            resize_canvas(width, height);
            surf = createSprite(bg.get(), "", shader_path);
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
            const float center_x = static_cast<float>(canvas->w) * 0.5f;
            const float center_y = static_cast<float>(canvas->h) * 0.5f;
            const float outer_radius = static_cast<float>(std::min(canvas->w, canvas->h)) * 0.42f;
            const std::array<Point, 10> star = create_star_points(center_x, center_y, outer_radius, outer_radius * 0.42f);

            auto *pixels = static_cast<Uint8 *>(canvas->pixels);
            for (int y = 0; y < canvas->h; ++y) {
                Uint8 *row = pixels + y * canvas->pitch;
                for (int x = 0; x < canvas->w; ++x) {
                    Uint8 *p = row + x * 4;
                    if (point_in_star(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, star)) {
                        const std::uint32_t color = random_color(random_engine);
                        p[0] = static_cast<Uint8>(color & 0xFFU);
                        p[1] = static_cast<Uint8>((color >> 8U) & 0xFFU);
                        p[2] = static_cast<Uint8>((color >> 16U) & 0xFFU);
                    } else {
                        p[0] = 8U;
                        p[1] = 10U;
                        p[2] = 16U;
                    }
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
        surface::SurfaceWindow window(args.filename, args.width, args.height, args.fullscreen, args.enable_vsync);
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
