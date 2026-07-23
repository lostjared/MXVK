#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_USE_EIGEN_MATH)
#include "mxvk/mxvk_math_eigen.hpp"
#else
#include "mxvk/mxvk_math.h"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <string>

namespace {
    class SurfaceDeleter {
      public:
        void operator()(SDL_Surface *surface) const {
            SDL_DestroySurface(surface);
        }
    };

    using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

    SurfacePtr create_pixel_surface() {
        SurfacePtr surface(SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32));
        if (!surface) {
            throw mxvk::Exception(std::format("Failed to create 3dmath pixel surface: {}", SDL_GetError()));
        }

        const SDL_PixelFormatDetails *details = SDL_GetPixelFormatDetails(surface->format);
        if (details == nullptr) {
            throw mxvk::Exception(std::format("Failed to query 3dmath pixel format: {}", SDL_GetError()));
        }

        auto *pixel = static_cast<std::uint32_t *>(surface->pixels);
        *pixel = SDL_MapRGBA(details, nullptr, 255, 255, 255, 255);
        return surface;
    }
} // namespace

namespace example {
    class Math3DWindow : public mxvk::VK_Window {
      public:
        Math3DWindow(const std::string &, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              fallback_width(width),
              fallback_height(height) {
            setClearColor(0.015f, 0.016f, 0.022f, 1.0f);
            mxvk::BuildTables();

            SurfacePtr surface = create_pixel_surface();
            pixel = createSprite(surface.get());
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {
            if (pixel == nullptr) {
                return;
            }

            const int width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : fallback_width;
            const int height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : fallback_height;
            const float time = static_cast<float>(SDL_GetTicks()) * 0.001f;

            mxvk::vec4D vertices[3] = {
                {-0.75f, -0.45f, 2.6f, 1.0f},
                {0.75f, -0.45f, 2.6f, 1.0f},
                {0.0f, 0.75f, 2.6f, 1.0f},
            };

            mxvk::Mat4D rotation;
            rotation.BuildXYZ(25.0f, time * 42.0f, time * 18.0f);

            mxvk::RenderList render_list;
            mxvk::Triangle triangle;
            triangle.state = mxvk::MX_ACTIVE | mxvk::MX_VISIBLE;
            triangle.color = mxvk::MXVK_RGB(255, 255, 255);
            for (int i = 0; i < 3; ++i) {
                triangle.vlist[i] = vertices[i];
                triangle.tlist[i] = rotation.MulVec(vertices[i]);
                triangle.tlist[i].z += 3.0f;
            }
            render_list.BuildRenderList(triangle);

            project_to_screen(render_list, width, height);

            const int pixel_size = std::max(1, width / 360);
            mxvk::PipeLine pipeline;
            pipeline.Begin(*pixel, width, height, pixel_size);
            pipeline.DrawPolys(render_list);
            pipeline.End();
        }

      private:
        mxvk::VK_Sprite *pixel = nullptr;
        int fallback_width = 1280;
        int fallback_height = 720;

        static void project_to_screen(mxvk::RenderList &render_list, int width, int height) {
            const float scale = static_cast<float>(std::min(width, height)) * 0.42f;
            const float center_x = static_cast<float>(width) * 0.5f;
            const float center_y = static_cast<float>(height) * 0.5f;

            for (auto &poly : render_list.polys) {
                for (auto &vertex : poly.tlist) {
                    const float z = std::max(vertex.z, 0.001f);
                    vertex.x = center_x + (vertex.x / z) * scale;
                    vertex.y = center_y - (vertex.y / z) * scale;
                }
            }
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::Math3DWindow window(args.path, "MXVK 3D Math", args.width, args.height, args.fullscreen, args.enable_vsync);
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
