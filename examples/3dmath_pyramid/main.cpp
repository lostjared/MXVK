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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
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

    [[nodiscard]] SurfacePtr create_frame_surface(int width, int height) {
        SurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (!surface) {
            throw mxvk::Exception(std::format("3dmath_pyramid: failed to create frame surface: {}", SDL_GetError()));
        }
        return surface;
    }

    [[nodiscard]] std::filesystem::path pyramid_path(const std::string &asset_path) {
        return std::filesystem::path(asset_path) / "data" / "pyramid.plg";
    }

    struct FaceDraw {
        std::array<mxvk::vec4D, 3> points{};
        std::array<mxvk::MXCOLOR, 3> colors{};
    };

    constexpr float MIN_CAMERA_DISTANCE = 2.5f;
    constexpr float MAX_CAMERA_DISTANCE = 12.0f;
    constexpr float CAMERA_ZOOM_STEP = 0.45f;
} // namespace

namespace example {
    class Math3DPyramidWindow : public mxvk::VK_Window {
      public:
        Math3DPyramidWindow(const std::string &asset_path, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              fallback_width(width),
              fallback_height(height) {
            setClearColor(0.012f, 0.015f, 0.022f, 1.0f);
            mxvk::BuildTables();

            const std::filesystem::path model_path = pyramid_path(asset_path);
            if (!pyramid.LoadPLG(model_path.string(), mxvk::vec4D(2.0f, 2.0f, 2.0f), mxvk::vec4D(0.0f, 0.0f, 4.5f), mxvk::vec4D())) {
                throw mxvk::Exception(std::format("3dmath_pyramid: failed to load PLG model '{}'", model_path.string()));
            }
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = e.wheel.y != 0.0f ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                camera_distance = std::clamp(camera_distance - delta * CAMERA_ZOOM_STEP, MIN_CAMERA_DISTANCE, MAX_CAMERA_DISTANCE);
            }
        }

        void proc() override {
            const int width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : fallback_width;
            const int height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : fallback_height;

            ensure_framebuffer(width, height);
            if (frame_sprite == nullptr || frame_surface == nullptr || frame_format == nullptr) {
                return;
            }

            clear_frame(mxvk::MXVK_RGB(3, 4, 8));
            std::ranges::fill(depth_buffer, std::numeric_limits<float>::infinity());

            const float time = static_cast<float>(SDL_GetTicks()) * 0.001f;
            mxvk::Mat4D rotation;
            rotation.BuildXYZ(0.0f, time * 42.0f, 0.0f);

            std::vector<mxvk::vec4D> camera_vertices(pyramid.local.size());
            std::vector<mxvk::vec4D> projected(pyramid.local.size());
            for (std::size_t i = 0; i < pyramid.local.size(); ++i) {
                camera_vertices[i] = rotation.MulVec(pyramid.local[i]);
                camera_vertices[i].z += camera_distance;
                projected[i] = project_to_screen(camera_vertices[i], width, height);
            }

            mxvk::vec4D light_direction(-0.35f, -0.55f, -1.0f, 0.0f);
            light_direction.Normalize();
            std::vector<FaceDraw> faces;
            faces.reserve(pyramid.vlist.size());

            for (const mxvk::Triangle &triangle : pyramid.vlist) {
                const auto first = static_cast<std::size_t>(triangle.vert[0]);
                const auto second = static_cast<std::size_t>(triangle.vert[1]);
                const auto third = static_cast<std::size_t>(triangle.vert[2]);

                const mxvk::vec4D &a = camera_vertices[first];
                const mxvk::vec4D &b = camera_vertices[second];
                const mxvk::vec4D &c = camera_vertices[third];
                mxvk::vec4D normal = mxvk::vec4D().Build(a, b).CrossProduct(mxvk::vec4D().Build(a, c));
                normal.Normalize();

                const mxvk::vec4D center = (a + b + c) * (1.0f / 3.0f);
                const mxvk::vec4D view_direction(-center.x, -center.y, -center.z, 0.0f);
                if (normal.DotProduct(view_direction) <= 0.0f) {
                    continue;
                }

                const float diffuse = std::max(0.0f, normal.DotProduct(light_direction));
                const float intensity = std::clamp(0.28f + diffuse * 0.72f, 0.0f, 1.0f);
                faces.push_back({
                    {projected[first], projected[second], projected[third]},
                    {
                        mxvk::shade_color(vertex_color(pyramid.local[first]), intensity),
                        mxvk::shade_color(vertex_color(pyramid.local[second]), intensity),
                        mxvk::shade_color(vertex_color(pyramid.local[third]), intensity),
                    },
                });
            }

            for (const FaceDraw &face : faces) {
                draw_gradient_triangle(face);
            }

            frame_sprite->updateTexture(frame_surface.get());
            frame_sprite->drawSpriteRect(0, 0, width, height);
        }

      private:
        mxvk::mxObject pyramid;
        SurfacePtr frame_surface;
        std::vector<float> depth_buffer;
        const SDL_PixelFormatDetails *frame_format = nullptr;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        int frame_width = 0;
        int frame_height = 0;
        int fallback_width = 1280;
        int fallback_height = 720;
        float camera_distance = 4.5f;

        void ensure_framebuffer(int width, int height) {
            if (frame_surface != nullptr && frame_width == width && frame_height == height) {
                return;
            }

            frame_surface = create_frame_surface(width, height);
            frame_format = SDL_GetPixelFormatDetails(frame_surface->format);
            if (frame_format == nullptr) {
                throw mxvk::Exception(std::format("3dmath_pyramid: failed to query frame format: {}", SDL_GetError()));
            }

            frame_width = width;
            frame_height = height;
            depth_buffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            clear_frame(mxvk::MXVK_RGB(3, 4, 8));

            if (frame_sprite == nullptr) {
                frame_sprite = createSprite(frame_surface.get());
            } else {
                frame_sprite->updateTexture(frame_surface.get());
            }
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

        void draw_gradient_triangle(const FaceDraw &face) {
            const mxvk::vec4D &a = face.points[0];
            const mxvk::vec4D &b = face.points[1];
            const mxvk::vec4D &c = face.points[2];
            const auto edge = [](const mxvk::vec4D &first, const mxvk::vec4D &second, float x, float y) {
                return (x - first.x) * (second.y - first.y) - (y - first.y) * (second.x - first.x);
            };

            const float area = edge(b, c, a.x, a.y);
            if (std::abs(area) <= mxvk::EPSILON) {
                return;
            }

            const int min_x = std::clamp(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))), 0, frame_width - 1);
            const int max_x = std::clamp(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))), 0, frame_width - 1);
            const int min_y = std::clamp(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))), 0, frame_height - 1);
            const int max_y = std::clamp(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))), 0, frame_height - 1);

            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const float sample_x = static_cast<float>(x) + 0.5f;
                    const float sample_y = static_cast<float>(y) + 0.5f;
                    const float weight_a = edge(b, c, sample_x, sample_y) / area;
                    const float weight_b = edge(c, a, sample_x, sample_y) / area;
                    const float weight_c = edge(a, b, sample_x, sample_y) / area;
                    if (weight_a < -mxvk::EPSILON || weight_b < -mxvk::EPSILON || weight_c < -mxvk::EPSILON) {
                        continue;
                    }

                    const float reciprocal_depth =
                        weight_a / a.z +
                        weight_b / b.z +
                        weight_c / c.z;
                    if (reciprocal_depth <= mxvk::EPSILON) {
                        continue;
                    }

                    const float depth = 1.0f / reciprocal_depth;
                    const std::size_t pixel_index =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_width) +
                        static_cast<std::size_t>(x);
                    if (depth >= depth_buffer[pixel_index]) {
                        continue;
                    }
                    depth_buffer[pixel_index] = depth;

                    const float perspective_a = (weight_a / a.z) * depth;
                    const float perspective_b = (weight_b / b.z) * depth;
                    const float perspective_c = (weight_c / c.z) * depth;
                    put_pixel(x, y, interpolate_color(face.colors, perspective_a, perspective_b, perspective_c));
                }
            }
        }

        [[nodiscard]] static mxvk::MXCOLOR interpolate_color(const std::array<mxvk::MXCOLOR, 3> &colors, float weight_a, float weight_b, float weight_c) {
            const auto channel = [&](auto component) {
                const float value =
                    static_cast<float>(component(colors[0])) * weight_a +
                    static_cast<float>(component(colors[1])) * weight_b +
                    static_cast<float>(component(colors[2])) * weight_c;
                return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
            };

            return mxvk::MXVK_RGB(channel(mxvk::color_r), channel(mxvk::color_g), channel(mxvk::color_b));
        }

        [[nodiscard]] static mxvk::MXCOLOR vertex_color(const mxvk::vec4D &vertex) {
            if (vertex.y > 0.0f) {
                return mxvk::MXVK_RGB(255, 82, 197);
            }
            if (vertex.x < 0.0f && vertex.z > 0.0f) {
                return mxvk::MXVK_RGB(68, 214, 255);
            }
            if (vertex.x > 0.0f && vertex.z > 0.0f) {
                return mxvk::MXVK_RGB(92, 255, 142);
            }
            if (vertex.x > 0.0f && vertex.z < 0.0f) {
                return mxvk::MXVK_RGB(255, 222, 74);
            }
            return mxvk::MXVK_RGB(155, 105, 255);
        }

        [[nodiscard]] static mxvk::vec4D project_to_screen(const mxvk::vec4D &point, int width, int height) {
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
        example::Math3DPyramidWindow window(args.path, "MXVK 3D Math Pyramid", args.width, args.height, args.fullscreen, args.enable_vsync);
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
