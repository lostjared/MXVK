#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_math.h"
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
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
            throw mxvk::Exception(std::format("Failed to create 3dmath_texture frame surface: {}", SDL_GetError()));
        }
        return surface;
    }

    struct Texture {
        int width = 0;
        int height = 0;
        std::vector<mxvk::MXCOLOR> pixels;

        [[nodiscard]] mxvk::MXCOLOR sample(float u, float v) const {
            if (width <= 0 || height <= 0 || pixels.empty()) {
                return mxvk::MXVK_RGB(255, 255, 255);
            }

            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            const int x = std::clamp(static_cast<int>(u * static_cast<float>(width - 1) + 0.5f), 0, width - 1);
            const int y = std::clamp(static_cast<int>(v * static_cast<float>(height - 1) + 0.5f), 0, height - 1);
            return pixels[static_cast<std::size_t>(y * width + x)];
        }

        [[nodiscard]] mxvk::MXCOLOR sample_nearest(float u, float v) const {
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            const int x = static_cast<int>(u * static_cast<float>(width - 1) + 0.5f);
            const int y = static_cast<int>(v * static_cast<float>(height - 1) + 0.5f);
            return pixels[static_cast<std::size_t>(y * width + x)];
        }
    };

    struct TexVertex {
        mxvk::vec4D position;
        mxvk::vec2D uv;
        float depth = 1.0f;
    };

    struct FaceDraw {
        std::array<int, 4> indices{};
        float depth = 0.0f;
        float intensity = 1.0f;
    };

    [[nodiscard]] std::string resolve_texture_path(const Arguments &args) {
        std::string texture_path = !args.filename.empty() ? args.filename : args.texture;
        if (texture_path.empty()) {
            throw mxvk::Exception("3dmath_texture: pass a PNG with --filename <file.png> or --texture <file.png>");
        }

        namespace fs = std::filesystem;
        fs::path requested(texture_path);
        if (requested.is_absolute() || fs::exists(requested)) {
            return requested.string();
        }

        if (!args.path.empty()) {
            const fs::path from_asset_path = fs::path(args.path) / requested;
            if (fs::exists(from_asset_path)) {
                return from_asset_path.string();
            }
        }

        return requested.string();
    }

    [[nodiscard]] Texture load_texture(const std::string &path) {
        SurfacePtr loaded(mxvk::LoadPNG(path.c_str()));
        if (!loaded) {
            throw mxvk::Exception(std::format("3dmath_texture: failed to load PNG '{}'", path));
        }

        SurfacePtr rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));
        if (!rgba) {
            throw mxvk::Exception(std::format("3dmath_texture: failed to convert PNG '{}': {}", path, SDL_GetError()));
        }

        const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(rgba->format);
        if (format == nullptr) {
            throw mxvk::Exception(std::format("3dmath_texture: failed to query PNG format '{}': {}", path, SDL_GetError()));
        }

        Texture texture;
        texture.width = rgba->w;
        texture.height = rgba->h;
        texture.pixels.resize(static_cast<std::size_t>(texture.width * texture.height));

        for (int y = 0; y < texture.height; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(rgba->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch));
            const auto *src = reinterpret_cast<const std::uint32_t *>(row);
            for (int x = 0; x < texture.width; ++x) {
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                std::uint8_t a = 0;
                SDL_GetRGBA(src[x], format, nullptr, &r, &g, &b, &a);
                texture.pixels[static_cast<std::size_t>(y * texture.width + x)] =
                    (static_cast<mxvk::MXCOLOR>(a) << 24U) |
                    (static_cast<mxvk::MXCOLOR>(r) << 16U) |
                    (static_cast<mxvk::MXCOLOR>(g) << 8U) |
                    static_cast<mxvk::MXCOLOR>(b);
            }
        }

        return texture;
    }
} // namespace

namespace example {
    class Math3DTextureWindow : public mxvk::VK_Window {
      public:
        Math3DTextureWindow(const Arguments &args, const std::string &title)
            : mxvk::VK_Window(title, args.width, args.height, args.fullscreen, MXVK_VALIDATION),
              texture(load_texture(resolve_texture_path(args))),
              fallback_width(args.width),
              fallback_height(args.height) {
            setClearColor(0.012f, 0.015f, 0.022f, 1.0f);
            mxvk::BuildTables();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
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
                point.z += camera_distance;
                camera_vertices[i] = point;
                projected[i] = project_to_screen(point, width, height);
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
                const float intensity = std::clamp(0.35f + diffuse * 0.65f, 0.0f, 1.0f);
                faces.push_back({indices, center.z, intensity});
            }

            std::ranges::sort(faces, [](const FaceDraw &left, const FaceDraw &right) {
                return left.depth > right.depth;
            });

            for (const FaceDraw &face : faces) {
                const auto index0 = static_cast<std::size_t>(face.indices[0]);
                const auto index1 = static_cast<std::size_t>(face.indices[1]);
                const auto index2 = static_cast<std::size_t>(face.indices[2]);
                const auto index3 = static_cast<std::size_t>(face.indices[3]);
                const TexVertex a{projected[index0], {0.0f, 1.0f}, camera_vertices[index0].z};
                const TexVertex b{projected[index1], {1.0f, 1.0f}, camera_vertices[index1].z};
                const TexVertex c{projected[index2], {1.0f, 0.0f}, camera_vertices[index2].z};
                const TexVertex d{projected[index3], {0.0f, 0.0f}, camera_vertices[index3].z};
                draw_textured_triangle(a, b, c, face.intensity);
                draw_textured_triangle(a, c, d, face.intensity);
            }

            frame_sprite->updateTexture(frame_surface.get());
            frame_sprite->drawSpriteRect(0, 0, width, height);
        }

      private:
        Texture texture;
        SurfacePtr frame_surface;
        const SDL_PixelFormatDetails *frame_format = nullptr;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        int frame_width = 0;
        int frame_height = 0;
        int fallback_width = 1280;
        int fallback_height = 720;
        float camera_distance = 4.25f;
        static constexpr float MIN_CAMERA_DISTANCE = 2.2f;
        static constexpr float MAX_CAMERA_DISTANCE = 10.0f;
        static constexpr float CAMERA_ZOOM_STEP = 0.45f;

        void ensure_framebuffer(int width, int height) {
            if (frame_surface != nullptr && frame_width == width && frame_height == height) {
                return;
            }

            frame_surface = create_frame_surface(width, height);
            frame_format = SDL_GetPixelFormatDetails(frame_surface->format);
            if (frame_format == nullptr) {
                throw mxvk::Exception(std::format("Failed to query 3dmath_texture frame format: {}", SDL_GetError()));
            }

            frame_width = width;
            frame_height = height;

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

        void put_pixel_unchecked(int x, int y, mxvk::MXCOLOR color) {
            auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_surface->pitch));
            auto *pixel = reinterpret_cast<std::uint32_t *>(row) + x;
            *pixel = map_color(color);
        }

        [[nodiscard]] static mxvk::MXCOLOR shade_texture_color(mxvk::MXCOLOR color, float intensity) {
            const auto scale = [intensity](std::uint8_t component) {
                return static_cast<mxvk::MXCOLOR>(static_cast<int>(static_cast<float>(component) * intensity)) & 0xFFU;
            };

            return (static_cast<mxvk::MXCOLOR>(mxvk::color_a(color)) << 24U) |
                   (scale(mxvk::color_r(color)) << 16U) |
                   (scale(mxvk::color_g(color)) << 8U) |
                   scale(mxvk::color_b(color));
        }

        void draw_textured_triangle(const TexVertex &a, const TexVertex &b, const TexVertex &c, float intensity) {
            if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty()) {
                return;
            }

            const mxvk::vec2D p0(a.position.x, a.position.y);
            const mxvk::vec2D p1(b.position.x, b.position.y);
            const mxvk::vec2D p2(c.position.x, c.position.y);
            const float area = mxvk::edge_function(p0, p1, p2);
            if (std::fabs(area) <= mxvk::EPSILON) {
                return;
            }
            const bool positive_area = area > 0.0f;

            const int min_x = std::max(0, static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))));
            const int max_x = std::min(frame_width - 1, static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))));
            const int min_y = std::max(0, static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))));
            const int max_y = std::min(frame_height - 1, static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))));

            if (min_x > max_x || min_y > max_y) {
                return;
            }

            const float inv_area = 1.0f / area;
            const float inv_z0 = 1.0f / std::max(a.depth, 0.001f);
            const float inv_z1 = 1.0f / std::max(b.depth, 0.001f);
            const float inv_z2 = 1.0f / std::max(c.depth, 0.001f);
            const float u_over_z0 = a.uv.x * inv_z0;
            const float u_over_z1 = b.uv.x * inv_z1;
            const float u_over_z2 = c.uv.x * inv_z2;
            const float v_over_z0 = a.uv.y * inv_z0;
            const float v_over_z1 = b.uv.y * inv_z1;
            const float v_over_z2 = c.uv.y * inv_z2;

            const float w0_dx = p2.y - p1.y;
            const float w0_dy = -(p2.x - p1.x);
            const float w1_dx = p0.y - p2.y;
            const float w1_dy = -(p0.x - p2.x);
            const float w2_dx = p1.y - p0.y;
            const float w2_dy = -(p1.x - p0.x);

            const mxvk::vec2D row_start(static_cast<float>(min_x) + 0.5f, static_cast<float>(min_y) + 0.5f);
            float row_w0 = mxvk::edge_function(p1, p2, row_start);
            float row_w1 = mxvk::edge_function(p2, p0, row_start);
            float row_w2 = mxvk::edge_function(p0, p1, row_start);

            for (int y = min_y; y <= max_y; ++y) {
                float w0 = row_w0;
                float w1 = row_w1;
                float w2 = row_w2;

                for (int x = min_x; x <= max_x; ++x) {
                    if ((positive_area && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                        (!positive_area && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                        const float b0 = w0 * inv_area;
                        const float b1 = w1 * inv_area;
                        const float b2 = w2 * inv_area;
                        const float inv_z = (b0 * inv_z0) + (b1 * inv_z1) + (b2 * inv_z2);
                        if (std::fabs(inv_z) > mxvk::EPSILON) {
                            const float u = ((u_over_z0 * b0) + (u_over_z1 * b1) + (u_over_z2 * b2)) / inv_z;
                            const float v = ((v_over_z0 * b0) + (v_over_z1 * b1) + (v_over_z2 * b2)) / inv_z;
                            put_pixel_unchecked(x, y, shade_texture_color(texture.sample_nearest(u, v), intensity));
                        }
                    }

                    w0 += w0_dx;
                    w1 += w1_dx;
                    w2 += w2_dx;
                }

                row_w0 += w0_dy;
                row_w1 += w1_dy;
                row_w2 += w2_dy;
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
        example::Math3DTextureWindow window(args, "MXVK 3D Math Texture");
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
