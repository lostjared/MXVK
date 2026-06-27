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

            u = std::clamp(u, 0.0F, 1.0F);
            v = std::clamp(v, 0.0F, 1.0F);
            const int x = std::clamp(static_cast<int>(u * static_cast<float>(width - 1) + 0.5F), 0, width - 1);
            const int y = std::clamp(static_cast<int>(v * static_cast<float>(height - 1) + 0.5F), 0, height - 1);
            return pixels[static_cast<std::size_t>(y * width + x)];
        }
    };

    struct TexVertex {
        mxvk::vec4D position;
        mxvk::vec2D uv;
        float depth = 1.0F;
    };

    struct FaceDraw {
        std::array<int, 4> indices{};
        float depth = 0.0F;
        float intensity = 1.0F;
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
            setClearColor(0.012F, 0.015F, 0.022F, 1.0F);
            mxvk::BuildTables();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {
            const int width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : fallback_width;
            const int height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : fallback_height;

            ensure_framebuffer(width, height);
            if (frame_sprite == nullptr || frame_surface == nullptr || frame_format == nullptr) {
                return;
            }

            const float time = static_cast<float>(SDL_GetTicks()) * 0.001F;
            clear_frame(mxvk::MXVK_RGB(3, 4, 8));

            const std::array<mxvk::vec4D, 8> cube_vertices = {
                mxvk::vec4D{-1.0F, -1.0F, -1.0F, 1.0F},
                mxvk::vec4D{1.0F, -1.0F, -1.0F, 1.0F},
                mxvk::vec4D{1.0F, 1.0F, -1.0F, 1.0F},
                mxvk::vec4D{-1.0F, 1.0F, -1.0F, 1.0F},
                mxvk::vec4D{-1.0F, -1.0F, 1.0F, 1.0F},
                mxvk::vec4D{1.0F, -1.0F, 1.0F, 1.0F},
                mxvk::vec4D{1.0F, 1.0F, 1.0F, 1.0F},
                mxvk::vec4D{-1.0F, 1.0F, 1.0F, 1.0F},
            };

            mxvk::Mat4D rotation;
            rotation.BuildXYZ(time * 31.0F, time * 43.0F, time * 17.0F);

            std::array<mxvk::vec4D, 8> camera_vertices{};
            std::array<mxvk::vec4D, 8> projected{};
            for (std::size_t i = 0; i < cube_vertices.size(); ++i) {
                mxvk::vec4D point = rotation.MulVec(cube_vertices[i]);
                point.z += 4.25F;
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

            mxvk::vec3D light_dir(-0.35F, -0.55F, -1.0F);
            light_dir.Normalize();

            std::vector<FaceDraw> faces;
            faces.reserve(cube_faces.size());
            for (const auto &indices : cube_faces) {
                const mxvk::vec4D &a = camera_vertices[static_cast<std::size_t>(indices[0])];
                const mxvk::vec4D &b = camera_vertices[static_cast<std::size_t>(indices[1])];
                const mxvk::vec4D &c = camera_vertices[static_cast<std::size_t>(indices[2])];
                mxvk::vec4D normal = mxvk::vec4D().Build(a, b).CrossProduct(mxvk::vec4D().Build(a, c));
                normal.Normalize();

                const mxvk::vec4D center = (a + b + c + camera_vertices[static_cast<std::size_t>(indices[3])]) * 0.25F;
                const mxvk::vec4D view_vector(-center.x, -center.y, -center.z, 1.0F);
                if (normal.DotProduct(view_vector) <= 0.0F) {
                    continue;
                }

                const float diffuse = std::max(0.0F, normal.DotProduct(mxvk::vec4D(light_dir.x, light_dir.y, light_dir.z, 1.0F)));
                const float intensity = std::clamp(0.35F + diffuse * 0.65F, 0.0F, 1.0F);
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
                const TexVertex a{projected[index0], {0.0F, 1.0F}, camera_vertices[index0].z};
                const TexVertex b{projected[index1], {1.0F, 1.0F}, camera_vertices[index1].z};
                const TexVertex c{projected[index2], {1.0F, 0.0F}, camera_vertices[index2].z};
                const TexVertex d{projected[index3], {0.0F, 0.0F}, camera_vertices[index3].z};
                draw_textured_triangle(a, b, c, face.intensity);
                draw_textured_triangle(a, c, d, face.intensity);
            }

            const int outline_size = std::max(1, width / 640);
            mxvk::PipeLine outline_pipeline;
            outline_pipeline.Begin(width, height, [this, outline_size](int x, int y, mxvk::MXCOLOR color) { put_block(x, y, outline_size, color); });
            for (const FaceDraw &face : faces) {
                for (int i = 0; i < 4; ++i) {
                    const mxvk::vec4D &a = projected[static_cast<std::size_t>(face.indices[static_cast<std::size_t>(i)])];
                    const mxvk::vec4D &b = projected[static_cast<std::size_t>(face.indices[static_cast<std::size_t>((i + 1) % 4)])];
                    outline_pipeline.DrawClipedLine(static_cast<int>(a.x), static_cast<int>(a.y), static_cast<int>(b.x), static_cast<int>(b.y), mxvk::MXVK_RGB(235, 245, 255));
                }
            }
            outline_pipeline.End();

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

        void put_block(int x, int y, int size, mxvk::MXCOLOR color) {
            for (int by = 0; by < size; ++by) {
                for (int bx = 0; bx < size; ++bx) {
                    put_pixel(x + bx, y + by, color);
                }
            }
        }

        void draw_textured_triangle(const TexVertex &a, const TexVertex &b, const TexVertex &c, float intensity) {
            const mxvk::vec2D p0(a.position.x, a.position.y);
            const mxvk::vec2D p1(b.position.x, b.position.y);
            const mxvk::vec2D p2(c.position.x, c.position.y);
            const float area = mxvk::edge_function(p0, p1, p2);
            if (std::fabs(area) <= mxvk::EPSILON) {
                return;
            }

            const int min_x = std::max(0, static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))));
            const int max_x = std::min(frame_width - 1, static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))));
            const int min_y = std::max(0, static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))));
            const int max_y = std::min(frame_height - 1, static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))));

            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const mxvk::vec2D p(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
                    const float w0 = mxvk::edge_function(p1, p2, p);
                    const float w1 = mxvk::edge_function(p2, p0, p);
                    const float w2 = mxvk::edge_function(p0, p1, p);
                    if (!((area > 0.0F && w0 >= 0.0F && w1 >= 0.0F && w2 >= 0.0F) ||
                          (area < 0.0F && w0 <= 0.0F && w1 <= 0.0F && w2 <= 0.0F))) {
                        continue;
                    }

                    const float inv_area = 1.0F / area;
                    const float b0 = w0 * inv_area;
                    const float b1 = w1 * inv_area;
                    const float b2 = w2 * inv_area;
                    const float inv_z0 = 1.0F / std::max(a.depth, 0.001F);
                    const float inv_z1 = 1.0F / std::max(b.depth, 0.001F);
                    const float inv_z2 = 1.0F / std::max(c.depth, 0.001F);
                    const float inv_z = (b0 * inv_z0) + (b1 * inv_z1) + (b2 * inv_z2);
                    if (std::fabs(inv_z) <= mxvk::EPSILON) {
                        continue;
                    }
                    const float u = ((a.uv.x * inv_z0 * b0) + (b.uv.x * inv_z1 * b1) + (c.uv.x * inv_z2 * b2)) / inv_z;
                    const float v = ((a.uv.y * inv_z0 * b0) + (b.uv.y * inv_z1 * b1) + (c.uv.y * inv_z2 * b2)) / inv_z;
                    put_pixel(x, y, mxvk::shade_color(texture.sample(u, v), intensity));
                }
            }
        }

        static mxvk::vec4D project_to_screen(const mxvk::vec4D &point, int width, int height) {
            const float scale = static_cast<float>(std::min(width, height)) * 0.52F;
            const float center_x = static_cast<float>(width) * 0.5F;
            const float center_y = static_cast<float>(height) * 0.5F;
            const float z = std::max(point.z, 0.001F);
            return {center_x + (point.x / z) * scale, center_y - (point.y / z) * scale, point.z, 1.0F};
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
