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

    SurfacePtr create_frame_surface(int width, int height) {
        SurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (!surface) {
            throw mxvk::Exception(std::format("Failed to create 3dmath frame surface: {}", SDL_GetError()));
        }
        return surface;
    }

} // namespace

namespace example {
    class Math3DWindow : public mxvk::VK_Window {
      public:
        Math3DWindow(const std::string &, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync, const FramebufferDimensions &framebuffer)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              frame_width(framebuffer.width),
              frame_height(framebuffer.height),
              fallback_width(width),
              fallback_height(height) {
            setClearColor(0.015f, 0.016f, 0.022f, 1.0f);
            mxvk::BuildTables();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {
            const int output_width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : fallback_width;
            const int output_height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : fallback_height;

            ensure_framebuffer();
            if (frame_sprite == nullptr || frame_surface == nullptr || frame_format == nullptr) {
                return;
            }

            clear_frame(mxvk::MXVK_RGB(4, 4, 6));
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

            project_to_screen(render_list, frame_width, frame_height);

            const int pixel_size = std::max(1, frame_width / 360);
            mxvk::PipeLine pipeline;
            pipeline.Begin(frame_width, frame_height, [this, pixel_size](int x, int y, mxvk::MXCOLOR color) {
                put_block(x, y, pixel_size, color);
            });
            pipeline.DrawPolys(render_list);
            pipeline.End();

            frame_sprite->updateTexture(frame_surface.get());
            frame_sprite->drawSpriteRect(0, 0, output_width, output_height);
        }

      private:
        SurfacePtr frame_surface;
        const SDL_PixelFormatDetails *frame_format = nullptr;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        int frame_width = 1280;
        int frame_height = 720;
        int fallback_width = 1280;
        int fallback_height = 720;

        void ensure_framebuffer() {
            if (frame_surface != nullptr) {
                return;
            }

            frame_surface = create_frame_surface(frame_width, frame_height);
            frame_format = SDL_GetPixelFormatDetails(frame_surface->format);
            if (frame_format == nullptr) {
                throw mxvk::Exception(std::format("Failed to query 3dmath frame format: {}", SDL_GetError()));
            }

            clear_frame(mxvk::MXVK_RGB(4, 4, 6));
            frame_sprite = createSprite(frame_surface.get());
            frame_sprite->setTextureFilter(VK_FILTER_NEAREST);
        }

        [[nodiscard]] std::uint32_t map_color(mxvk::MXCOLOR color) const {
            return SDL_MapRGBA(frame_format, nullptr, mxvk::color_r(color), mxvk::color_g(color), mxvk::color_b(color), mxvk::color_a(color));
        }

        void clear_frame(mxvk::MXCOLOR color) {
            SDL_FillSurfaceRect(frame_surface.get(), nullptr, map_color(color));
        }

        void put_pixel(int x, int y, mxvk::MXCOLOR color) {
            if (x < 0 || y < 0 || x >= frame_width || y >= frame_height) {
                return;
            }

            auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_surface->pitch));
            auto *pixel = reinterpret_cast<std::uint32_t *>(row) + x;
            *pixel = map_color(color);
        }

        void put_block(int x, int y, int size, mxvk::MXCOLOR color) {
            for (int block_y = 0; block_y < size; ++block_y) {
                for (int block_x = 0; block_x < size; ++block_x) {
                    put_pixel(x + block_x, y + block_y, color);
                }
            }
        }

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
        example::Math3DWindow window(args.path, "MXVK 3D Math", args.width, args.height, args.fullscreen, args.enable_vsync, args.framebuffer);
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
