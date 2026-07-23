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
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
            throw mxvk::Exception(std::format("Failed to create 3dmath_cube frame surface: {}", SDL_GetError()));
        }
        return surface;
    }

    struct FaceDraw {
        std::array<int, 4> indices{};
        float depth = 0.0f;
        mxvk::MXCOLOR color = mxvk::MXVK_RGB(255, 255, 255);
    };

} // namespace

namespace example {
    class Math3DCubeWindow : public mxvk::VK_Window {
      public:
        Math3DCubeWindow(const std::string &, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync, const FramebufferDimensions &framebuffer)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              frame_width(framebuffer.width),
              frame_height(framebuffer.height),
              fallback_width(width),
              fallback_height(height) {
            setClearColor(0.012f, 0.015f, 0.022f, 1.0f);
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

            const float time = static_cast<float>(SDL_GetTicks()) * 0.001f;
            clear_frame(mxvk::MXVK_RGB(3, 4, 8));

            const std::array<mxvk::vec4D, 8> cube_vertices = {
                mxvk::vec4D{-1.0f, -1.0f, -1.0f, 1.0f},
                mxvk::vec4D{1.0f, -1.0f, -1.0f, 1.0f},
                mxvk::vec4D{1.0f, 1.0f, -1.0f, 1.0f},
                mxvk::vec4D{-1.0f, 1.0f, -1.0f, 1.0f},
                mxvk::vec4D{-1.0f, -1.0f, 1.0f, 1.0f},
                mxvk::vec4D{1.0f, -1.0f, 1.0f, 1.0f},
                mxvk::vec4D{1.0f, 1.0f, 1.0f, 1.0f},
                mxvk::vec4D{-1.0f, 1.0f, 1.0f, 1.0f},
            };

            mxvk::Mat4D rotation;
            rotation.BuildXYZ(time * 31.0f, time * 43.0f, time * 17.0f);

            std::array<mxvk::vec4D, 8> camera_vertices{};
            std::array<mxvk::vec4D, 8> projected{};
            for (std::size_t i = 0; i < cube_vertices.size(); ++i) {
                mxvk::vec4D point = rotation.MulVec(cube_vertices[i]);
                point.z += 4.25f;
                camera_vertices[i] = point;
                projected[i] = project_to_screen(point, frame_width, frame_height);
            }

            const std::array<std::array<int, 4>, 6> cube_faces = {{
                {0, 3, 2, 1},
                {4, 5, 6, 7},
                {0, 4, 7, 3},
                {1, 2, 6, 5},
                {3, 7, 6, 2},
                {0, 1, 5, 4},
            }};

            mxvk::vec3D light_dir(-0.35f, -0.55f, -1.0f);
            light_dir.Normalize();

            std::vector<FaceDraw> faces;
            faces.reserve(cube_faces.size());
            for (const auto &indices : cube_faces) {
                const mxvk::vec4D &a = camera_vertices[static_cast<std::size_t>(indices[0])];
                const mxvk::vec4D &b = camera_vertices[static_cast<std::size_t>(indices[1])];
                const mxvk::vec4D &c = camera_vertices[static_cast<std::size_t>(indices[2])];
                mxvk::vec4D normal = mxvk::vec4D().Build(a, b).CrossProduct(mxvk::vec4D().Build(a, c));
                normal.Normalize();

                const mxvk::vec4D center = (a + b + c + camera_vertices[static_cast<std::size_t>(indices[3])]) * 0.25f;
                const mxvk::vec4D view_vector(-center.x, -center.y, -center.z, 1.0f);
                if (normal.DotProduct(view_vector) <= 0.0f) {
                    continue;
                }

                const float diffuse = std::max(0.0f, normal.DotProduct(mxvk::vec4D(light_dir.x, light_dir.y, light_dir.z, 1.0f)));
                const float intensity = std::clamp(0.25f + diffuse * 0.75f, 0.0f, 1.0f);
                const mxvk::MXCOLOR color = mxvk::shade_color(mxvk::MXVK_RGB(76, 164, 255), intensity);
                faces.push_back({indices, center.z, color});
            }

            std::ranges::sort(faces, [](const FaceDraw &left, const FaceDraw &right) {
                return left.depth > right.depth;
            });

            for (const FaceDraw &face : faces) {
                mxvk::PipeLine fill_pipeline;
                fill_pipeline.Begin(frame_width, frame_height, [this](int x, int y, mxvk::MXCOLOR color) { put_pixel(x, y, color); });
                const mxvk::vec4D &a = projected[static_cast<std::size_t>(face.indices[0])];
                const mxvk::vec4D &b = projected[static_cast<std::size_t>(face.indices[1])];
                const mxvk::vec4D &c = projected[static_cast<std::size_t>(face.indices[2])];
                const mxvk::vec4D &d = projected[static_cast<std::size_t>(face.indices[3])];
                fill_pipeline.DrawFilledTriangle(a, b, c, face.color);
                fill_pipeline.DrawFilledTriangle(a, c, d, face.color);
                fill_pipeline.End();
            }

            const int outline_size = std::max(1, frame_width / 640);
            mxvk::PipeLine outline_pipeline;
            outline_pipeline.Begin(frame_width, frame_height, [this, outline_size](int x, int y, mxvk::MXCOLOR color) { put_block(x, y, outline_size, color); });
            for (const FaceDraw &face : faces) {
                for (int i = 0; i < 4; ++i) {
                    const mxvk::vec4D &a = projected[static_cast<std::size_t>(face.indices[static_cast<std::size_t>(i)])];
                    const mxvk::vec4D &b = projected[static_cast<std::size_t>(face.indices[static_cast<std::size_t>((i + 1) % 4)])];
                    outline_pipeline.DrawClipedLine(static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y), mxvk::MXVK_RGB(235, 245, 255));
                }
            }
            outline_pipeline.End();

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
                throw mxvk::Exception(std::format("Failed to query 3dmath_cube frame format: {}", SDL_GetError()));
            }

            clear_frame(mxvk::MXVK_RGB(3, 4, 8));
            frame_sprite = createSprite(frame_surface.get());
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
            for (int by = 0; by < size; ++by) {
                for (int bx = 0; bx < size; ++bx) {
                    put_pixel(x + bx, y + by, color);
                }
            }
        }

        static mxvk::vec4D project_to_screen(const mxvk::vec4D &point, int width, int height) {
            const float scale = static_cast<float>(std::min(width, height)) * 0.52f;
            const float center_x = static_cast<float>(width) * 0.5f;
            const float center_y = static_cast<float>(height) * 0.5f;
            const float z = std::max(point.z, 0.001f);
            return {center_x + (point.x / z) * scale, center_y - (point.y / z) * scale, point.z, 1.0f};
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::Math3DCubeWindow window(args.path, "MXVK 3D Math Cube", args.width, args.height, args.fullscreen, args.enable_vsync, args.framebuffer);
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
