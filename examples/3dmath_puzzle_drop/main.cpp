#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_USE_EIGEN_MATH)
#include "mxvk/mxvk_math_eigen.hpp"
#else
#include "mxvk/mxvk_math.h"
#endif
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef math3d_puzzle_drop_ASSET_DIR
#define math3d_puzzle_drop_ASSET_DIR "."
#endif

namespace {
    constexpr int BOARD_WIDTH = 20;
    constexpr int BOARD_HEIGHT = 22;
    constexpr int DEFAULT_FRAME_WIDTH = 640;
    constexpr int DEFAULT_FRAME_HEIGHT = 480;
    constexpr int LEVEL_COUNT = 8;
    constexpr float BLOCK_SPACING = 0.145f;
    constexpr float BLOCK_HALF_EXTENT = 0.064f;
    constexpr float FRAME_HALF_EXTENT = BLOCK_HALF_EXTENT * 0.72f;
    constexpr float FRAME_GAP = 0.016f;
    constexpr float CAMERA_DISTANCE = 4.1f;
    constexpr std::array<float, 3> FALL_SECONDS{0.86f, 0.68f, 0.50f};
    constexpr std::array<const char *, 10> BLOCK_TEXTURE_FILES{
        "red1.png",
        "red2.png",
        "red3.png",
        "green1.png",
        "green2.png",
        "green3.png",
        "blue1.png",
        "blue2.png",
        "blue3.png",
        "red3.png",
    };

    class SurfaceDeleter {
      public:
        void operator()(SDL_Surface *surface) const {
            SDL_DestroySurface(surface);
        }
    };

    using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

    enum class BlockType {
        Null = 0,
        Clear,
        Red1,
        Red2,
        Red3,
        Green1,
        Green2,
        Green3,
        Blue1,
        Blue2,
        Blue3,
        Match,
    };

    enum class ShiftDirection {
        Down,
        Up,
    };

    struct Block {
        int x = 0;
        int y = 0;
        BlockType type = BlockType::Null;
    };

    struct Piece {
        std::array<Block, 3> blocks{};
        int position = 0;

        void new_piece(int start_x, int start_y, std::mt19937 &rng) {
            blocks[0] = {start_x, start_y, random_type(rng)};
            blocks[1] = {start_x, start_y + 1, random_type(rng)};
            blocks[2] = {start_x, start_y + 2, random_type(rng)};
            position = 0;
        }

        void shift(ShiftDirection direction) {
            const std::array<BlockType, 3> types{blocks[0].type, blocks[1].type, blocks[2].type};
            if (direction == ShiftDirection::Down) {
                blocks[0].type = types[2];
                blocks[1].type = types[0];
                blocks[2].type = types[1];
            } else {
                blocks[0].type = types[1];
                blocks[1].type = types[2];
                blocks[2].type = types[0];
            }
        }

        void move_left() {
            for (Block &block : blocks) {
                --block.x;
            }
        }

        void move_right() {
            for (Block &block : blocks) {
                ++block.x;
            }
        }

        void move_down() {
            for (Block &block : blocks) {
                ++block.y;
            }
        }

        void rotate_left() {
            if (position == 0) {
                blocks[1].y -= 1;
                blocks[1].x -= 1;
                blocks[2].x -= 2;
                blocks[2].y -= 2;
                position = 1;
            } else if (position == 1) {
                blocks[1].y += 1;
                blocks[1].x += 1;
                blocks[2].y += 2;
                blocks[2].x += 2;
                position = 0;
            }
        }

        void rotate_right() {
            if (position == 0) {
                blocks[1].x += 1;
                blocks[1].y -= 1;
                blocks[2].x += 2;
                blocks[2].y -= 2;
                position = 2;
            } else if (position == 2) {
                blocks[1].x -= 1;
                blocks[1].y += 1;
                blocks[2].x -= 2;
                blocks[2].y += 2;
                position = 0;
            }
        }

      private:
        [[nodiscard]] static BlockType random_type(std::mt19937 &rng) {
            std::uniform_int_distribution<int> distribution(static_cast<int>(BlockType::Red1), static_cast<int>(BlockType::Match));
            return static_cast<BlockType>(distribution(rng));
        }
    };

    struct Cell {
        BlockType type = BlockType::Null;
        int clear_value = 0;
        int flash_counter = 0;
    };

    [[nodiscard]] bool is_play_block(BlockType type) {
        return type >= BlockType::Red1 && type <= BlockType::Match;
    }

    [[nodiscard]] int texture_index(BlockType type) {
        return is_play_block(type) ? static_cast<int>(type) - static_cast<int>(BlockType::Red1) : 0;
    }

    [[nodiscard]] bool same_or_match(BlockType actual, BlockType expected) {
        return actual == expected || actual == BlockType::Match;
    }

    struct TextureLevel {
        int width = 0;
        int height = 0;
        std::vector<mxvk::MXCOLOR> pixels;
    };

    struct Texture {
        int width = 0;
        int height = 0;
        std::vector<mxvk::MXCOLOR> pixels;
        std::vector<TextureLevel> mipmaps;

        [[nodiscard]] mxvk::MXCOLOR sample_filtered(float u, float v, float lod) const {
            const float clamped_lod = std::clamp(lod, 0.0f, static_cast<float>(mipmaps.size()));
            const int first_level = static_cast<int>(std::floor(clamped_lod));
            const int second_level = std::min(first_level + 1, static_cast<int>(mipmaps.size()));
            const float blend = clamped_lod - static_cast<float>(first_level);
            const mxvk::MXCOLOR first = sample_bilinear(first_level, u, v);
            const mxvk::MXCOLOR second = sample_bilinear(second_level, u, v);
            return blend_color(first, second, blend);
        }

      private:
        [[nodiscard]] mxvk::MXCOLOR sample_bilinear(int level, float u, float v) const {
            const int level_width = level == 0 ? width : mipmaps[static_cast<std::size_t>(level - 1)].width;
            const int level_height = level == 0 ? height : mipmaps[static_cast<std::size_t>(level - 1)].height;
            const std::vector<mxvk::MXCOLOR> &level_pixels = level == 0 ? pixels : mipmaps[static_cast<std::size_t>(level - 1)].pixels;
            const float source_x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(level_width - 1);
            const float source_y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(level_height - 1);
            const int x0 = static_cast<int>(std::floor(source_x));
            const int y0 = static_cast<int>(std::floor(source_y));
            const int x1 = std::min(x0 + 1, level_width - 1);
            const int y1 = std::min(y0 + 1, level_height - 1);
            const float x_blend = source_x - static_cast<float>(x0);
            const float y_blend = source_y - static_cast<float>(y0);
            const mxvk::MXCOLOR top = blend_color(
                level_pixels[static_cast<std::size_t>(y0 * level_width + x0)],
                level_pixels[static_cast<std::size_t>(y0 * level_width + x1)],
                x_blend);
            const mxvk::MXCOLOR bottom = blend_color(
                level_pixels[static_cast<std::size_t>(y1 * level_width + x0)],
                level_pixels[static_cast<std::size_t>(y1 * level_width + x1)],
                x_blend);
            return blend_color(top, bottom, y_blend);
        }

        [[nodiscard]] static mxvk::MXCOLOR blend_color(mxvk::MXCOLOR first, mxvk::MXCOLOR second, float amount) {
            const auto blend_channel = [amount](std::uint8_t left, std::uint8_t right) {
                return static_cast<std::uint8_t>(
                    std::clamp(
                        static_cast<float>(left) + (static_cast<float>(right) - static_cast<float>(left)) * amount,
                        0.0f,
                        255.0f) +
                    0.5f);
            };
            const std::uint8_t red = blend_channel(mxvk::color_r(first), mxvk::color_r(second));
            const std::uint8_t green = blend_channel(mxvk::color_g(first), mxvk::color_g(second));
            const std::uint8_t blue = blend_channel(mxvk::color_b(first), mxvk::color_b(second));
            const std::uint8_t alpha = blend_channel(mxvk::color_a(first), mxvk::color_a(second));
            return (static_cast<mxvk::MXCOLOR>(alpha) << 24U) |
                   (static_cast<mxvk::MXCOLOR>(red) << 16U) |
                   (static_cast<mxvk::MXCOLOR>(green) << 8U) |
                   static_cast<mxvk::MXCOLOR>(blue);
        }
    };

    void build_mipmaps(Texture &texture) {
        int source_width = texture.width;
        int source_height = texture.height;
        const std::vector<mxvk::MXCOLOR> *source_pixels = &texture.pixels;
        while (source_width > 1 || source_height > 1) {
            TextureLevel level;
            level.width = std::max(1, source_width / 2);
            level.height = std::max(1, source_height / 2);
            level.pixels.resize(static_cast<std::size_t>(level.width * level.height));
            for (int y = 0; y < level.height; ++y) {
                for (int x = 0; x < level.width; ++x) {
                    std::uint32_t red = 0;
                    std::uint32_t green = 0;
                    std::uint32_t blue = 0;
                    std::uint32_t alpha = 0;
                    for (int offset_y = 0; offset_y < 2; ++offset_y) {
                        for (int offset_x = 0; offset_x < 2; ++offset_x) {
                            const int source_x = std::min(x * 2 + offset_x, source_width - 1);
                            const int source_y = std::min(y * 2 + offset_y, source_height - 1);
                            const mxvk::MXCOLOR color = (*source_pixels)[static_cast<std::size_t>(source_y * source_width + source_x)];
                            red += mxvk::color_r(color);
                            green += mxvk::color_g(color);
                            blue += mxvk::color_b(color);
                            alpha += mxvk::color_a(color);
                        }
                    }
                    level.pixels[static_cast<std::size_t>(y * level.width + x)] =
                        ((alpha / 4U) << 24U) |
                        ((red / 4U) << 16U) |
                        ((green / 4U) << 8U) |
                        (blue / 4U);
                }
            }
            texture.mipmaps.push_back(std::move(level));
            source_width = texture.mipmaps.back().width;
            source_height = texture.mipmaps.back().height;
            source_pixels = &texture.mipmaps.back().pixels;
        }
    }

    [[nodiscard]] Texture load_texture(const std::string &path, bool generate_mipmaps = false) {
        SurfacePtr loaded(mxvk::LoadPNG(path.c_str()));
        if (!loaded) {
            throw mxvk::Exception(std::format("3dmath_puzzle_drop: failed to load PNG '{}'", path));
        }
        SurfacePtr rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));
        if (!rgba) {
            throw mxvk::Exception(std::format("3dmath_puzzle_drop: failed to convert PNG '{}': {}", path, SDL_GetError()));
        }
        const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(rgba->format);
        if (format == nullptr) {
            throw mxvk::Exception(std::format("3dmath_puzzle_drop: failed to query PNG format '{}'", path));
        }

        Texture texture;
        texture.width = rgba->w;
        texture.height = rgba->h;
        texture.pixels.resize(static_cast<std::size_t>(texture.width * texture.height));
        for (int y = 0; y < texture.height; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(rgba->pixels) + static_cast<std::size_t>(y * rgba->pitch);
            const auto *source = reinterpret_cast<const std::uint32_t *>(row);
            for (int x = 0; x < texture.width; ++x) {
                std::uint8_t red = 0;
                std::uint8_t green = 0;
                std::uint8_t blue = 0;
                std::uint8_t alpha = 0;
                SDL_GetRGBA(source[x], format, nullptr, &red, &green, &blue, &alpha);
                texture.pixels[static_cast<std::size_t>(y * texture.width + x)] =
                    (static_cast<mxvk::MXCOLOR>(alpha) << 24U) |
                    (static_cast<mxvk::MXCOLOR>(red) << 16U) |
                    (static_cast<mxvk::MXCOLOR>(green) << 8U) |
                    static_cast<mxvk::MXCOLOR>(blue);
            }
        }
        if (generate_mipmaps) {
            build_mipmaps(texture);
        }
        return texture;
    }

    struct RasterVertex {
        mxvk::vec4D position;
        mxvk::vec2D uv;
    };

    class SoftwareRenderer {
      public:
        SoftwareRenderer(int width, int height, const std::string &data_root)
            : frame_width(width),
              frame_height(height),
              depth_buffer(static_cast<std::size_t>(width * height)),
              background(load_texture(data_root + "/level1.png")),
              intro(load_texture(data_root + "/intro1.png")) {
            frame_surface.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (!frame_surface) {
                throw mxvk::Exception(std::format("3dmath_puzzle_drop: failed to create framebuffer: {}", SDL_GetError()));
            }
            for (const char *filename : BLOCK_TEXTURE_FILES) {
                block_textures.push_back(load_texture(data_root + "/" + filename, true));
            }
        }

        [[nodiscard]] SDL_Surface *surface() const {
            return frame_surface.get();
        }

        [[nodiscard]] int width() const {
            return frame_width;
        }

        [[nodiscard]] int height() const {
            return frame_height;
        }

        void set_view(float yaw, float pitch, float distance) {
            camera_rotation.BuildXYZ(pitch, yaw, 0.0f);
            camera_distance = distance;
        }

        void begin_frame(bool show_intro) {
            std::ranges::fill(depth_buffer, std::numeric_limits<float>::infinity());
            draw_flat_image(show_intro ? intro : background);
            if (!show_intro) {
                fill_translucent_rectangle(0, 0, frame_width, frame_height, mxvk::MXVK_RGB(3, 8, 16), 150);
            }
        }

        void draw_block(BlockType type, float x, float y, float z, float half_extent, const mxvk::vec4D &tint) {
            draw_cube(&block_textures[static_cast<std::size_t>(texture_index(type))], x, y, z, half_extent, tint);
        }

        void draw_wildcard(float x, float y, float z, float half_extent, const mxvk::vec4D &color) {
            mxvk::vec4D neon(
                std::max(color.x, 0.08f),
                std::max(color.y, 0.08f),
                std::max(color.z, 0.08f),
                1.0f);
            const float brightest_channel = std::max({neon.x, neon.y, neon.z});
            neon.x /= brightest_channel;
            neon.y /= brightest_channel;
            neon.z /= brightest_channel;
            draw_cube(nullptr, x, y, z, half_extent, neon, true);
        }

        void draw_solid_cube(float x, float y, float z, float half_extent, mxvk::MXCOLOR color) {
            const mxvk::vec4D tint(
                static_cast<float>(mxvk::color_r(color)) / 255.0f,
                static_cast<float>(mxvk::color_g(color)) / 255.0f,
                static_cast<float>(mxvk::color_b(color)) / 255.0f,
                1.0f);
            draw_cube(nullptr, x, y, z, half_extent, tint);
        }

        void draw_rectangle(int left, int top, int width, int height, mxvk::MXCOLOR color) {
            const int first_x = std::clamp(left, 0, frame_width);
            const int first_y = std::clamp(top, 0, frame_height);
            const int last_x = std::clamp(left + width, 0, frame_width);
            const int last_y = std::clamp(top + height, 0, frame_height);
            for (int y = first_y; y < last_y; ++y) {
                auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + static_cast<std::size_t>(y * frame_surface->pitch);
                for (int x = first_x; x < last_x; ++x) {
                    write_pixel(row + static_cast<std::size_t>(x * 4), color);
                }
            }
        }

        void draw_block_image(BlockType type, int left, int top, int width, int height) {
            if (!is_play_block(type) || width <= 0 || height <= 0) {
                return;
            }

            const Texture &texture = block_textures[static_cast<std::size_t>(texture_index(type))];
            const int first_x = std::clamp(left, 0, frame_width);
            const int first_y = std::clamp(top, 0, frame_height);
            const int last_x = std::clamp(left + width, 0, frame_width);
            const int last_y = std::clamp(top + height, 0, frame_height);
            for (int y = first_y; y < last_y; ++y) {
                const int source_y = std::clamp((y - top) * texture.height / height, 0, texture.height - 1);
                auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + static_cast<std::size_t>(y * frame_surface->pitch);
                for (int x = first_x; x < last_x; ++x) {
                    const int source_x = std::clamp((x - left) * texture.width / width, 0, texture.width - 1);
                    const mxvk::MXCOLOR color = texture.pixels[static_cast<std::size_t>(source_y * texture.width + source_x)];
                    blend_pixel(row + static_cast<std::size_t>(x * 4), color);
                }
            }
        }

        void draw_text(TTF_Font *font, const std::string &text, int x, int y, const SDL_Color &color) {
            if (font == nullptr || text.empty()) {
                return;
            }

            SurfacePtr text_surface(TTF_RenderText_Blended(font, text.c_str(), 0, color));
            if (!text_surface) {
                return;
            }
            SDL_SetSurfaceBlendMode(text_surface.get(), SDL_BLENDMODE_BLEND);
            const SDL_Rect destination{x, y, text_surface->w, text_surface->h};
            SDL_BlitSurface(text_surface.get(), nullptr, frame_surface.get(), &destination);
        }

      private:
        SurfacePtr frame_surface;
        int frame_width = 0;
        int frame_height = 0;
        std::vector<float> depth_buffer;
        Texture background;
        Texture intro;
        std::vector<Texture> block_textures;
        mxvk::Mat4D camera_rotation;
        float camera_distance = CAMERA_DISTANCE;

        static constexpr std::array<mxvk::vec4D, 8> CUBE_VERTICES{{
            {-1.0f, -1.0f, -1.0f, 1.0f},
            {1.0f, -1.0f, -1.0f, 1.0f},
            {1.0f, 1.0f, -1.0f, 1.0f},
            {-1.0f, 1.0f, -1.0f, 1.0f},
            {-1.0f, -1.0f, 1.0f, 1.0f},
            {1.0f, -1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},
            {-1.0f, 1.0f, 1.0f, 1.0f},
        }};

        static constexpr std::array<std::array<int, 4>, 6> CUBE_FACES{{
            {0, 3, 2, 1},
            {4, 5, 6, 7},
            {0, 4, 7, 3},
            {1, 2, 6, 5},
            {3, 7, 6, 2},
            {0, 1, 5, 4},
        }};

        static const std::array<std::array<mxvk::vec2D, 4>, 6> CUBE_FACE_UVS;

        static void write_pixel(std::uint8_t *pixel, mxvk::MXCOLOR color) {
            pixel[0] = mxvk::color_r(color);
            pixel[1] = mxvk::color_g(color);
            pixel[2] = mxvk::color_b(color);
            pixel[3] = mxvk::color_a(color);
        }

        static void blend_pixel(std::uint8_t *pixel, mxvk::MXCOLOR color) {
            const int alpha = mxvk::color_a(color);
            const int inverse_alpha = 255 - alpha;
            pixel[0] = static_cast<std::uint8_t>((mxvk::color_r(color) * alpha + pixel[0] * inverse_alpha) / 255);
            pixel[1] = static_cast<std::uint8_t>((mxvk::color_g(color) * alpha + pixel[1] * inverse_alpha) / 255);
            pixel[2] = static_cast<std::uint8_t>((mxvk::color_b(color) * alpha + pixel[2] * inverse_alpha) / 255);
            pixel[3] = 255;
        }

        void draw_flat_image(const Texture &texture) {
            for (int y = 0; y < frame_height; ++y) {
                const int source_y = y * texture.height / frame_height;
                auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + static_cast<std::size_t>(y * frame_surface->pitch);
                for (int x = 0; x < frame_width; ++x) {
                    const int source_x = x * texture.width / frame_width;
                    const mxvk::MXCOLOR color = texture.pixels[static_cast<std::size_t>(source_y * texture.width + source_x)];
                    write_pixel(row + static_cast<std::size_t>(x * 4), color | 0xFF000000U);
                }
            }
        }

        void fill_translucent_rectangle(int left, int top, int width, int height, mxvk::MXCOLOR color, std::uint8_t alpha) {
            const int inverse_alpha = 255 - alpha;
            for (int y = top; y < top + height; ++y) {
                auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + static_cast<std::size_t>(y * frame_surface->pitch);
                for (int x = left; x < left + width; ++x) {
                    auto *pixel = row + static_cast<std::size_t>(x * 4);
                    pixel[0] = static_cast<std::uint8_t>((pixel[0] * inverse_alpha + mxvk::color_r(color) * alpha) / 255);
                    pixel[1] = static_cast<std::uint8_t>((pixel[1] * inverse_alpha + mxvk::color_g(color) * alpha) / 255);
                    pixel[2] = static_cast<std::uint8_t>((pixel[2] * inverse_alpha + mxvk::color_b(color) * alpha) / 255);
                }
            }
        }

        [[nodiscard]] mxvk::vec4D project(const mxvk::vec4D &point) const {
            const float scale = static_cast<float>(std::min(frame_width, frame_height)) * 0.71f;
            const float z = std::max(point.z, 0.001f);
            return {
                static_cast<float>(frame_width) * 0.43f + point.x / z * scale,
                static_cast<float>(frame_height) * 0.50f - point.y / z * scale,
                point.z,
                1.0f,
            };
        }

        void draw_cube(const Texture *texture, float x, float y, float z, float half_extent, const mxvk::vec4D &tint, bool neon = false) {
            std::array<mxvk::vec4D, 8> camera_vertices{};
            std::array<mxvk::vec4D, 8> projected{};
            for (std::size_t index = 0; index < CUBE_VERTICES.size(); ++index) {
                mxvk::vec4D point(
                    CUBE_VERTICES[index].x * half_extent + x,
                    CUBE_VERTICES[index].y * half_extent + y,
                    CUBE_VERTICES[index].z * half_extent + z,
                    1.0f);
                point = camera_rotation.MulVec(point);
                point.z += camera_distance;
                camera_vertices[index] = point;
                projected[index] = project(point);
            }

            const mxvk::vec4D light_direction(-0.35f, 0.65f, -1.0f, 0.0f);
            for (std::size_t face_index = 0; face_index < CUBE_FACES.size(); ++face_index) {
                const auto &face = CUBE_FACES[face_index];
                const auto &face_uvs = CUBE_FACE_UVS[face_index];
                const mxvk::vec4D &a = camera_vertices[static_cast<std::size_t>(face[0])];
                const mxvk::vec4D &b = camera_vertices[static_cast<std::size_t>(face[1])];
                const mxvk::vec4D &c = camera_vertices[static_cast<std::size_t>(face[2])];
                mxvk::vec4D normal = mxvk::vec4D().Build(a, b).CrossProduct(mxvk::vec4D().Build(a, c));
                normal.Normalize();
                const mxvk::vec4D center = (a + b + c + camera_vertices[static_cast<std::size_t>(face[3])]) * 0.25f;
                if (normal.DotProduct({-center.x, -center.y, -center.z, 0.0f}) <= 0.0f) {
                    continue;
                }
                mxvk::vec4D normalized_light = light_direction;
                normalized_light.Normalize();
                float intensity = std::clamp(0.40f + std::max(0.0f, normal.DotProduct(normalized_light)) * 0.60f, 0.0f, 1.0f);
                if (neon) {
                    mxvk::vec4D key_light(-0.18f, 0.58f, -0.80f, 0.0f);
                    mxvk::vec4D fill_light(0.12f, 0.08f, -0.99f, 0.0f);
                    mxvk::vec4D view_direction(-center.x, -center.y, -center.z, 0.0f);
                    key_light.Normalize();
                    fill_light.Normalize();
                    view_direction.Normalize();
                    const float key_diffuse = std::max(normal.DotProduct(key_light), 0.0f);
                    const float fill_diffuse = std::max(normal.DotProduct(fill_light), 0.0f);
                    const float diffuse = std::min(key_diffuse * 0.50f + fill_diffuse * 0.62f, 1.0f);
                    const float rim_amount = 1.0f - std::max(normal.DotProduct(view_direction), 0.0f);
                    const float rim_fraction = std::clamp((rim_amount - 0.12f) / 0.88f, 0.0f, 1.0f);
                    const float neon_rim = rim_fraction * rim_fraction * (3.0f - 2.0f * rim_fraction);
                    intensity = 0.50f + diffuse * 0.52f + neon_rim * 0.34f + 0.12f;
                }
                const RasterVertex vertex_a{projected[static_cast<std::size_t>(face[0])], face_uvs[0]};
                const RasterVertex vertex_b{projected[static_cast<std::size_t>(face[1])], face_uvs[1]};
                const RasterVertex vertex_c{projected[static_cast<std::size_t>(face[2])], face_uvs[2]};
                const RasterVertex vertex_d{projected[static_cast<std::size_t>(face[3])], face_uvs[3]};
                rasterize_triangle(vertex_a, vertex_b, vertex_c, texture, tint, intensity);
                rasterize_triangle(vertex_a, vertex_c, vertex_d, texture, tint, intensity);
            }
        }

        void rasterize_triangle(const RasterVertex &a,
                                const RasterVertex &b,
                                const RasterVertex &c,
                                const Texture *texture,
                                const mxvk::vec4D &tint,
                                float intensity) {
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
            const float inverse_area = 1.0f / area;
            const float inverse_z0 = 1.0f / a.position.z;
            const float inverse_z1 = 1.0f / b.position.z;
            const float inverse_z2 = 1.0f / c.position.z;
            float texture_lod = 0.0f;
            if (texture != nullptr) {
                const auto texels_per_pixel = [texture](const RasterVertex &first, const RasterVertex &second) {
                    const float screen_width = second.position.x - first.position.x;
                    const float screen_height = second.position.y - first.position.y;
                    const float screen_distance = std::max(std::hypot(screen_width, screen_height), 0.001f);
                    const float texture_width = (second.uv.x - first.uv.x) * static_cast<float>(texture->width);
                    const float texture_height = (second.uv.y - first.uv.y) * static_cast<float>(texture->height);
                    return std::hypot(texture_width, texture_height) / screen_distance;
                };
                const float minification = std::max({
                    texels_per_pixel(a, b),
                    texels_per_pixel(b, c),
                    texels_per_pixel(c, a),
                    1.0f,
                });
                constexpr float MIP_SHARPNESS_BIAS = 0.75f;
                texture_lod = std::max(0.0f, std::log2(minification) - MIP_SHARPNESS_BIAS);
            }

            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const mxvk::vec2D point(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                    const float edge0 = mxvk::edge_function(p1, p2, point);
                    const float edge1 = mxvk::edge_function(p2, p0, point);
                    const float edge2 = mxvk::edge_function(p0, p1, point);
                    if ((area > 0.0f && (edge0 < 0.0f || edge1 < 0.0f || edge2 < 0.0f)) ||
                        (area < 0.0f && (edge0 > 0.0f || edge1 > 0.0f || edge2 > 0.0f))) {
                        continue;
                    }
                    const float weight0 = edge0 * inverse_area;
                    const float weight1 = edge1 * inverse_area;
                    const float weight2 = edge2 * inverse_area;
                    const float inverse_z = weight0 * inverse_z0 + weight1 * inverse_z1 + weight2 * inverse_z2;
                    const float depth = 1.0f / inverse_z;
                    const std::size_t pixel_index = static_cast<std::size_t>(y * frame_width + x);
                    if (depth >= depth_buffer[pixel_index]) {
                        continue;
                    }
                    depth_buffer[pixel_index] = depth;
                    mxvk::MXCOLOR color = mxvk::MXVK_RGB(255, 255, 255);
                    if (texture != nullptr) {
                        const float u = (weight0 * a.uv.x * inverse_z0 + weight1 * b.uv.x * inverse_z1 + weight2 * c.uv.x * inverse_z2) / inverse_z;
                        const float v = (weight0 * a.uv.y * inverse_z0 + weight1 * b.uv.y * inverse_z1 + weight2 * c.uv.y * inverse_z2) / inverse_z;
                        color = texture->sample_filtered(u, v, texture_lod);
                    }
                    auto *row = static_cast<std::uint8_t *>(frame_surface->pixels) + static_cast<std::size_t>(y * frame_surface->pitch);
                    auto *pixel = row + static_cast<std::size_t>(x * 4);
                    pixel[0] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(mxvk::color_r(color)) * tint.x * intensity, 0.0f, 255.0f));
                    pixel[1] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(mxvk::color_g(color)) * tint.y * intensity, 0.0f, 255.0f));
                    pixel[2] = static_cast<std::uint8_t>(std::clamp(static_cast<float>(mxvk::color_b(color)) * tint.z * intensity, 0.0f, 255.0f));
                    pixel[3] = 255;
                }
            }
        }
    };

    const std::array<std::array<mxvk::vec2D, 4>, 6> SoftwareRenderer::CUBE_FACE_UVS{{
        {{{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}},
        {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}},
        {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}},
        {{{0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}}},
        {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}},
        {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}},
    }};

    class PuzzleDropWindow final : public mxvk::VK_Window {
      public:
        PuzzleDropWindow(const Arguments &args, const FramebufferDimensions &framebuffer)
            : mxvk::VK_Window("MXVK 3D Math Puzzle Drop", args.width, args.height, args.fullscreen, MXVK_VALIDATION, args.enable_vsync),
              data_root(((args.path.empty() || args.path == ".") ? std::string(math3d_puzzle_drop_ASSET_DIR) : args.path) + "/data"),
              renderer(framebuffer.width, framebuffer.height, data_root),
              ui_font(data_root + "/font.ttf", std::max(8, static_cast<int>(std::round(22.0f * framebuffer_scale(framebuffer))))) {
            setClearColor(0.01f, 0.02f, 0.03f, 1.0f);
            mxvk::BuildTables();
            std::random_device random_device;
            rng.seed(random_device());
            try_open_first_gamepad();
            reset_game();
            intro_start = std::chrono::steady_clock::now();
        }

        ~PuzzleDropWindow() override {
            close_gamepad();
        }

        void event(SDL_Event &event) override {
            if (event.type == SDL_EVENT_QUIT) {
                exit();
                return;
            }

            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (!open_gamepad(event.gdevice.which)) {
                    try_open_first_gamepad();
                }
                return;
            }

            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad != nullptr && event.gdevice.which == gamepad_id) {
                    close_gamepad();
                    try_open_first_gamepad();
                }
                return;
            }

            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                handle_gamepad_button_down(event.gbutton.button);
                return;
            }

            if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
                return;
            }
            if (intro_active && (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
                finish_intro();
                return;
            }
            switch (event.key.key) {
            case SDLK_ESCAPE:
                exit();
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (game_over) {
                    reset_game();
                    game_started = true;
                }
                break;
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
                difficulty = static_cast<int>(event.key.key - SDLK_1);
                reset_game();
                game_started = true;
                break;
            case SDLK_Z:
                rotate_left();
                break;
            case SDLK_X:
                rotate_right();
                break;
            default:
                break;
            }
        }

        void proc() override {
            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::chrono::duration<float>(now - last_input_update).count();
            last_input_update = now;
            try_open_first_gamepad();
            randomize_wildcard_color();
            if (intro_active && std::chrono::duration<float>(now - intro_start).count() >= 3.5f) {
                finish_intro();
            }

            if (!intro_active) {
                const bool *keys = SDL_GetKeyboardState(nullptr);
                if (keys != nullptr) {
                    handle_view_controls(keys, delta_seconds);
                    handle_piece_controls(keys, delta_seconds);
                }
                handle_gamepad_input(delta_seconds);
            }

            if (game_started && !game_over) {
                if (std::chrono::duration<float>(now - last_fall).count() >= FALL_SECONDS[static_cast<std::size_t>(difficulty)]) {
                    key_down();
                    last_fall = now;
                }
                if (std::chrono::duration<float>(now - last_process).count() >= 0.018f) {
                    proc_blocks();
                    proc_move_down();
                    last_process = now;
                }
            }

            draw_scene();
            draw_interface();
            ensure_frame_sprite();
            frame_sprite->updateTexture(renderer.surface());
            const int output_width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : 1280;
            const int output_height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : 720;
            frame_sprite->drawSpriteRect(0, 0, output_width, output_height);
        }

      private:
        std::string data_root;
        SoftwareRenderer renderer;
        mxvk::Font ui_font;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        std::mt19937 rng{};
        std::array<std::array<Cell, BOARD_WIDTH>, BOARD_HEIGHT> board{};
        Piece piece{};
        Piece next_piece{};
        SDL_Gamepad *gamepad = nullptr;
        SDL_JoystickID gamepad_id = 0;
        std::chrono::steady_clock::time_point intro_start{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_fall{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_process{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point last_input_update{std::chrono::steady_clock::now()};
        float horizontal_move_timer = 0.0f;
        float soft_drop_timer = 0.0f;
        float cycle_timer = 0.0f;
        float gamepad_move_repeat_timer = 0.0f;
        float gamepad_soft_drop_repeat_timer = 0.0f;
        float gamepad_cycle_repeat_timer = 0.0f;
        float gamepad_move_held_seconds = 0.0f;
        int horizontal_move_direction = 0;
        int gamepad_move_direction = 0;
        bool soft_drop_held = false;
        bool cycle_held = false;
        bool gamepad_soft_drop_held = false;
        bool gamepad_cycle_held = false;
        int difficulty = 0;
        int level = 1;
        int lines = 0;
        bool intro_active = true;
        bool game_started = false;
        bool game_over = false;
        float grid_yaw = -10.0f;
        float grid_pitch = -8.0f;
        float camera_distance = CAMERA_DISTANCE;
        mxvk::vec4D wildcard_color{1.0f, 0.0f, 1.0f, 1.0f};
        static constexpr Sint16 GAMEPAD_DEADZONE = 10000;
        static constexpr float GAMEPAD_MOVE_INITIAL_DELAY_SECONDS = 0.22f;
        static constexpr float GAMEPAD_MOVE_REPEAT_SECONDS = 0.12f;
        static constexpr float GAMEPAD_SOFT_DROP_INITIAL_DELAY_SECONDS = 0.18f;
        static constexpr float GAMEPAD_SOFT_DROP_REPEAT_SECONDS = 0.08f;
        static constexpr float GAMEPAD_CYCLE_INITIAL_DELAY_SECONDS = 0.16f;
        static constexpr float GAMEPAD_CYCLE_REPEAT_SECONDS = 0.11f;
        static constexpr float GAMEPAD_STICK_ROTATE_SPEED = 120.0f;
        static constexpr float GAMEPAD_STICK_PITCH_SPEED = 100.0f;
        static constexpr float GAMEPAD_STICK_SCALE = 1.0f / 32768.0f;

        [[nodiscard]] static float framebuffer_scale(const FramebufferDimensions &framebuffer) {
            return std::min(
                static_cast<float>(framebuffer.width) / static_cast<float>(DEFAULT_FRAME_WIDTH),
                static_cast<float>(framebuffer.height) / static_cast<float>(DEFAULT_FRAME_HEIGHT));
        }

        [[nodiscard]] int scaled(int value) const {
            return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * framebuffer_scale({renderer.width(), renderer.height()}))));
        }

        void ensure_frame_sprite() {
            if (frame_sprite != nullptr) {
                return;
            }

            frame_sprite = createSprite(renderer.surface());
            frame_sprite->setTextureFilter(VK_FILTER_NEAREST);
        }

        void draw_interface() {
            const SDL_Color primary{255, 244, 223, 255};
            if (intro_active) {
                renderer.draw_text(ui_font.get(), "Press Enter", scaled(24), scaled(54), primary);
            } else if (game_over) {
                renderer.draw_text(ui_font.get(), std::format("Game Over: Lines cleared: {}", lines), scaled(24), scaled(22), primary);
                renderer.draw_text(ui_font.get(), "Press Enter to Restart", scaled(24), scaled(50), primary);
            } else {
                renderer.draw_text(
                    ui_font.get(),
                    std::format("Level {}   Lines {}   Difficulty {}", level, lines, difficulty + 1),
                    scaled(24),
                    scaled(22),
                    primary);
            }
            draw_next_piece_preview();
        }

        void draw_next_piece_preview() {
            if (!game_started || intro_active || game_over) {
                return;
            }

            const int panel_size = std::min({
                scaled(180),
                static_cast<int>(static_cast<float>(renderer.width()) * 0.22f),
                static_cast<int>(static_cast<float>(renderer.height()) * 0.30f),
            });
            if (panel_size < scaled(72)) {
                return;
            }

            const int margin = scaled(24);
            const int panel_x = renderer.width() - panel_size - margin;
            const int panel_y = scaled(88);
            const int border = scaled(4);
            const mxvk::MXCOLOR white = mxvk::MXVK_RGB(255, 255, 255);
            renderer.draw_rectangle(panel_x, panel_y, panel_size, border, white);
            renderer.draw_rectangle(panel_x, panel_y + panel_size - border, panel_size, border, white);
            renderer.draw_rectangle(panel_x, panel_y, border, panel_size, white);
            renderer.draw_rectangle(panel_x + panel_size - border, panel_y, border, panel_size, white);
            renderer.draw_text(ui_font.get(), "Next", panel_x + scaled(12), panel_y - scaled(28), SDL_Color{255, 255, 255, 255});

            int min_x = next_piece.blocks[0].x;
            int max_x = next_piece.blocks[0].x;
            int min_y = next_piece.blocks[0].y;
            int max_y = next_piece.blocks[0].y;
            for (const Block &block : next_piece.blocks) {
                min_x = std::min(min_x, block.x);
                max_x = std::max(max_x, block.x);
                min_y = std::min(min_y, block.y);
                max_y = std::max(max_y, block.y);
            }

            const float inner_padding = static_cast<float>(scaled(28));
            const float inner_size = static_cast<float>(panel_size) - inner_padding * 2.0f;
            const int cells_wide = max_x - min_x + 1;
            const int cells_high = max_y - min_y + 1;
            const int block_size = static_cast<int>(std::min(static_cast<float>(scaled(34)), inner_size / static_cast<float>(std::max(cells_wide, cells_high))));
            const float piece_width = static_cast<float>(cells_wide * block_size);
            const float piece_height = static_cast<float>(cells_high * block_size);
            const float origin_x = static_cast<float>(panel_x) + static_cast<float>(panel_size) * 0.5f - piece_width * 0.5f;
            const float origin_y = static_cast<float>(panel_y) + static_cast<float>(panel_size) * 0.5f - piece_height * 0.5f;

            for (const Block &block : next_piece.blocks) {
                const int x = static_cast<int>(origin_x + static_cast<float>(block.x - min_x) * static_cast<float>(block_size));
                const int y = static_cast<int>(origin_y + static_cast<float>(block.y - min_y) * static_cast<float>(block_size));
                renderer.draw_block_image(block.type, x, y, block_size, block_size);
            }
        }

        void finish_intro() {
            intro_active = false;
            game_started = true;
            const auto now = std::chrono::steady_clock::now();
            last_fall = now;
            last_process = now;
            last_input_update = now;
            reset_held_piece_input();
            reset_held_gamepad_input();
        }

        void randomize_wildcard_color() {
            std::uniform_int_distribution<int> distribution(0, 254);
            wildcard_color = {
                static_cast<float>(distribution(rng)) / 255.0f,
                static_cast<float>(distribution(rng)) / 255.0f,
                static_cast<float>(distribution(rng)) / 255.0f,
                1.0f,
            };
        }

        void draw_scene() {
            renderer.set_view(grid_yaw, grid_pitch, camera_distance);
            renderer.begin_frame(intro_active);
            if (intro_active) {
                return;
            }

            const float center_x = static_cast<float>(BOARD_WIDTH - 1) * 0.5f;
            const float center_y = static_cast<float>(BOARD_HEIGHT - 1) * 0.5f;
            const auto draw_cell = [&](BlockType type, int x, int y, float z = 0.0f) {
                const float block_x = (static_cast<float>(x) - center_x) * BLOCK_SPACING;
                const float block_y = (center_y - static_cast<float>(y)) * BLOCK_SPACING;
                if (type == BlockType::Match || type == BlockType::Clear) {
                    renderer.draw_wildcard(block_x, block_y, z, BLOCK_HALF_EXTENT, wildcard_color);
                    return;
                }
                renderer.draw_block(
                    type,
                    block_x,
                    block_y,
                    z,
                    BLOCK_HALF_EXTENT,
                    {1.0f, 1.0f, 1.0f, 1.0f});
            };

            const float frame_x = center_x * BLOCK_SPACING + BLOCK_HALF_EXTENT + FRAME_HALF_EXTENT + FRAME_GAP;
            const float frame_y = center_y * BLOCK_SPACING + BLOCK_HALF_EXTENT + FRAME_HALF_EXTENT + FRAME_GAP;
            for (int y = -1; y <= BOARD_HEIGHT; ++y) {
                renderer.draw_solid_cube(-frame_x, (center_y - static_cast<float>(y)) * BLOCK_SPACING, 0.04f, FRAME_HALF_EXTENT, mxvk::MXVK_RGB(110, 124, 142));
                renderer.draw_solid_cube(frame_x, (center_y - static_cast<float>(y)) * BLOCK_SPACING, 0.04f, FRAME_HALF_EXTENT, mxvk::MXVK_RGB(110, 124, 142));
            }
            for (int x = 0; x < BOARD_WIDTH; ++x) {
                renderer.draw_solid_cube((static_cast<float>(x) - center_x) * BLOCK_SPACING, -frame_y, 0.04f, FRAME_HALF_EXTENT, mxvk::MXVK_RGB(110, 124, 142));
            }

            for (int y = 0; y < BOARD_HEIGHT; ++y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    const Cell &cell = board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                    if (cell.type == BlockType::Null || (cell.type == BlockType::Clear && ((cell.flash_counter / 6) % 2) != 0)) {
                        continue;
                    }
                    draw_cell(cell.type, x, y);
                }
            }
            if (game_started && !game_over) {
                for (const Block &block : piece.blocks) {
                    draw_cell(block.type, block.x, block.y, -0.03f);
                }
            }
        }

        void handle_view_controls(const bool *keys, float delta_seconds) {
            if (keys[SDL_SCANCODE_A]) {
                grid_yaw -= 115.0f * delta_seconds;
            }
            if (keys[SDL_SCANCODE_D]) {
                grid_yaw += 115.0f * delta_seconds;
            }
            if (keys[SDL_SCANCODE_W]) {
                grid_pitch = std::clamp(grid_pitch + 90.0f * delta_seconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_S]) {
                grid_pitch = std::clamp(grid_pitch - 90.0f * delta_seconds, -70.0f, 70.0f);
            }
            if (keys[SDL_SCANCODE_PAGEUP]) {
                camera_distance = std::max(2.7f, camera_distance - 2.0f * delta_seconds);
            }
            if (keys[SDL_SCANCODE_PAGEDOWN]) {
                camera_distance = std::min(7.0f, camera_distance + 2.0f * delta_seconds);
            }
        }

        void handle_piece_controls(const bool *keys, float delta_seconds) {
            if (!game_started || game_over) {
                reset_held_piece_input();
                return;
            }

            const bool left = keys[SDL_SCANCODE_LEFT];
            const bool right = keys[SDL_SCANCODE_RIGHT];
            const int direction = (left == right) ? 0 : (left ? -1 : 1);
            if (direction == 0) {
                horizontal_move_direction = 0;
                horizontal_move_timer = 0.0f;
            } else {
                constexpr float INITIAL_DELAY_SECONDS = 0.16f;
                constexpr float REPEAT_SECONDS = 0.065f;
                if (horizontal_move_direction != direction) {
                    horizontal_move_direction = direction;
                    horizontal_move_timer = -INITIAL_DELAY_SECONDS;
                    move_piece_horizontal(direction);
                } else {
                    horizontal_move_timer += delta_seconds;
                    while (horizontal_move_timer >= 0.0f) {
                        horizontal_move_timer -= REPEAT_SECONDS;
                        move_piece_horizontal(direction);
                    }
                }
            }

            if (keys[SDL_SCANCODE_DOWN]) {
                constexpr float SOFT_DROP_REPEAT_SECONDS = 0.045f;
                if (!soft_drop_held) {
                    soft_drop_held = true;
                    soft_drop_timer = 0.0f;
                    key_down();
                    last_fall = std::chrono::steady_clock::now();
                } else {
                    soft_drop_timer += delta_seconds;
                    while (soft_drop_timer >= SOFT_DROP_REPEAT_SECONDS) {
                        soft_drop_timer -= SOFT_DROP_REPEAT_SECONDS;
                        key_down();
                        last_fall = std::chrono::steady_clock::now();
                    }
                }
            } else {
                soft_drop_held = false;
                soft_drop_timer = 0.0f;
            }

            if (keys[SDL_SCANCODE_UP]) {
                constexpr float CYCLE_INITIAL_DELAY_SECONDS = 0.16f;
                constexpr float CYCLE_REPEAT_SECONDS = 0.11f;
                if (!cycle_held) {
                    cycle_held = true;
                    cycle_timer = -CYCLE_INITIAL_DELAY_SECONDS;
                    cycle_piece_blocks();
                } else {
                    cycle_timer += delta_seconds;
                    while (cycle_timer >= 0.0f) {
                        cycle_timer -= CYCLE_REPEAT_SECONDS;
                        cycle_piece_blocks();
                    }
                }
            } else {
                cycle_held = false;
                cycle_timer = 0.0f;
            }
        }

        void handle_gamepad_input(float delta_seconds) {
            if (gamepad == nullptr || !game_started || game_over) {
                reset_held_gamepad_input();
                return;
            }

            const Sint16 left_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
            const Sint16 left_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
            const Sint16 right_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
            const Sint16 right_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);

            const bool dpad_left = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            const bool dpad_right = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            const bool dpad_down = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            const bool dpad_up = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);

            const int move_direction = dpad_left == dpad_right
                                           ? ((left_x < -GAMEPAD_DEADZONE) ? -1 : (left_x > GAMEPAD_DEADZONE) ? 1
                                                                                                              : 0)
                                           : (dpad_left ? -1 : 1);
            if (move_direction == 0) {
                gamepad_move_direction = 0;
                gamepad_move_held_seconds = 0.0f;
                gamepad_move_repeat_timer = 0.0f;
            } else if (move_direction != gamepad_move_direction) {
                gamepad_move_direction = move_direction;
                gamepad_move_held_seconds = 0.0f;
                gamepad_move_repeat_timer = 0.0f;
                move_piece_horizontal(gamepad_move_direction);
            } else {
                gamepad_move_held_seconds += delta_seconds;
                const float threshold = (gamepad_move_held_seconds < GAMEPAD_MOVE_INITIAL_DELAY_SECONDS)
                                            ? GAMEPAD_MOVE_INITIAL_DELAY_SECONDS
                                            : GAMEPAD_MOVE_REPEAT_SECONDS;
                gamepad_move_repeat_timer += delta_seconds;
                if (gamepad_move_repeat_timer >= threshold) {
                    move_piece_horizontal(gamepad_move_direction);
                    gamepad_move_repeat_timer = 0.0f;
                }
            }

            const bool soft_drop_down = dpad_down || left_y > GAMEPAD_DEADZONE;
            if (!soft_drop_down) {
                gamepad_soft_drop_held = false;
                gamepad_soft_drop_repeat_timer = 0.0f;
            } else {
                const float threshold = gamepad_soft_drop_held ? GAMEPAD_SOFT_DROP_REPEAT_SECONDS : GAMEPAD_SOFT_DROP_INITIAL_DELAY_SECONDS;
                gamepad_soft_drop_repeat_timer += delta_seconds;
                if (gamepad_soft_drop_repeat_timer >= threshold) {
                    key_down();
                    last_fall = std::chrono::steady_clock::now();
                    gamepad_soft_drop_repeat_timer = 0.0f;
                    gamepad_soft_drop_held = true;
                }
            }

            if (!dpad_up) {
                gamepad_cycle_held = false;
                gamepad_cycle_repeat_timer = 0.0f;
            } else {
                const float threshold = gamepad_cycle_held ? GAMEPAD_CYCLE_REPEAT_SECONDS : GAMEPAD_CYCLE_INITIAL_DELAY_SECONDS;
                gamepad_cycle_repeat_timer += delta_seconds;
                if (gamepad_cycle_repeat_timer >= threshold) {
                    cycle_piece_blocks();
                    gamepad_cycle_repeat_timer = 0.0f;
                    gamepad_cycle_held = true;
                }
            }

            if (std::abs(right_x) > GAMEPAD_DEADZONE) {
                grid_yaw += static_cast<float>(right_x) * GAMEPAD_STICK_SCALE * GAMEPAD_STICK_ROTATE_SPEED * delta_seconds;
            }
            if (std::abs(right_y) > GAMEPAD_DEADZONE) {
                grid_pitch = std::clamp(
                    grid_pitch - static_cast<float>(right_y) * GAMEPAD_STICK_SCALE * GAMEPAD_STICK_PITCH_SPEED * delta_seconds,
                    -70.0f,
                    70.0f);
            }

            constexpr float ZOOM_SPEED = 2.0f;
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                camera_distance = std::min(7.0f, camera_distance + ZOOM_SPEED * delta_seconds);
            }
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                camera_distance = std::max(2.7f, camera_distance - ZOOM_SPEED * delta_seconds);
            }
        }

        void handle_gamepad_button_down(Uint8 button) {
            if (intro_active) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    finish_intro();
                }
                return;
            }

            if (game_over) {
                if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START) {
                    reset_game();
                    game_started = true;
                } else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                    exit();
                }
                return;
            }

            if (!game_started) {
                return;
            }

            if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
                rotate_right();
            } else if (button == SDL_GAMEPAD_BUTTON_WEST) {
                rotate_left();
            } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
                hard_drop();
            } else if (button == SDL_GAMEPAD_BUTTON_BACK) {
                exit();
            }
        }

        void reset_held_piece_input() {
            horizontal_move_timer = 0.0f;
            soft_drop_timer = 0.0f;
            cycle_timer = 0.0f;
            horizontal_move_direction = 0;
            soft_drop_held = false;
            cycle_held = false;
        }

        void reset_held_gamepad_input() {
            gamepad_move_repeat_timer = 0.0f;
            gamepad_soft_drop_repeat_timer = 0.0f;
            gamepad_cycle_repeat_timer = 0.0f;
            gamepad_move_held_seconds = 0.0f;
            gamepad_move_direction = 0;
            gamepad_soft_drop_held = false;
            gamepad_cycle_held = false;
        }

        void move_piece_horizontal(int direction) {
            if (!check_piece(piece, direction, 0)) {
                return;
            }
            if (direction < 0) {
                piece.move_left();
            } else {
                piece.move_right();
            }
        }

        void cycle_piece_blocks() {
            piece.shift(ShiftDirection::Up);
        }

        void hard_drop() {
            if (!game_started || game_over) {
                return;
            }
            while (check_piece(piece, 0, 1)) {
                piece.move_down();
            }
            key_down();
            last_fall = std::chrono::steady_clock::now();
        }

        bool open_gamepad(SDL_JoystickID id) {
            if (gamepad != nullptr && gamepad_id == id) {
                return true;
            }
            close_gamepad();
            gamepad = SDL_OpenGamepad(id);
            if (gamepad == nullptr) {
                return false;
            }
            gamepad_id = id;
            return true;
        }

        void close_gamepad() {
            if (gamepad != nullptr) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                gamepad_id = 0;
            }
        }

        void try_open_first_gamepad() {
            if (gamepad != nullptr) {
                return;
            }
            int count = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&count);
            if (ids == nullptr || count <= 0) {
                if (ids != nullptr) {
                    SDL_free(ids);
                }
                return;
            }
            open_gamepad(ids[0]);
            SDL_free(ids);
        }

        void reset_game() {
            reset_held_piece_input();
            reset_held_gamepad_input();
            for (auto &row : board) {
                for (Cell &cell : row) {
                    cell = {};
                }
            }
            level = 1;
            lines = 0;
            game_over = false;
            piece.new_piece(BOARD_WIDTH / 2, 0, rng);
            next_piece.new_piece(BOARD_WIDTH / 2, 0, rng);
            last_fall = std::chrono::steady_clock::now();
            last_process = last_fall;
        }

        void key_down() {
            if (check_piece(piece, 0, 1)) {
                piece.move_down();
                return;
            }
            set_piece();
            piece = next_piece;
            next_piece.new_piece(BOARD_WIDTH / 2, 0, rng);
            if (!check_piece(piece, 0, 0)) {
                game_over = true;
            }
        }

        [[nodiscard]] bool check_piece(const Piece &test_piece, int offset_x, int offset_y) const {
            for (const Block &block : test_piece.blocks) {
                const int x = block.x + offset_x;
                const int y = block.y + offset_y;
                if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
                    return false;
                }
                const BlockType type = board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].type;
                if (type != BlockType::Null && type != BlockType::Clear) {
                    return false;
                }
            }
            return true;
        }

        void set_piece() {
            for (const Block &block : piece.blocks) {
                if (block.x < 0 || block.x >= BOARD_WIDTH || block.y < 0 || block.y >= BOARD_HEIGHT) {
                    continue;
                }
                Cell &cell = board[static_cast<std::size_t>(block.y)][static_cast<std::size_t>(block.x)];
                cell.type = block.type;
                cell.clear_value = 0;
                cell.flash_counter = 0;
                if (block.y == 0) {
                    game_over = true;
                }
            }
        }

        void rotate_left() {
            if (!game_started || game_over) {
                return;
            }
            Piece test_piece = piece;
            test_piece.rotate_left();
            if (check_piece(test_piece, 0, 0)) {
                piece = test_piece;
            }
        }

        void rotate_right() {
            if (!game_started || game_over) {
                return;
            }
            Piece test_piece = piece;
            test_piece.rotate_right();
            if (check_piece(test_piece, 0, 0)) {
                piece = test_piece;
            }
        }

        bool proc_blocks() {
            constexpr std::array<std::array<int, 2>, 4> DIRECTIONS{{
                {{1, 0}},
                {{0, 1}},
                {{1, 1}},
                {{1, -1}},
            }};
            constexpr std::array<BlockType, 3> COLOR_STARTS{BlockType::Red1, BlockType::Green1, BlockType::Blue1};
            for (int y = 0; y < BOARD_HEIGHT; ++y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    for (const auto &direction : DIRECTIONS) {
                        for (BlockType start : COLOR_STARTS) {
                            const BlockType one = start;
                            const BlockType two = static_cast<BlockType>(static_cast<int>(start) + 1);
                            const BlockType three = static_cast<BlockType>(static_cast<int>(start) + 2);
                            if (check_sequence(x, y, direction[0], direction[1], one, two, three) ||
                                check_sequence(x, y, direction[0], direction[1], three, two, one)) {
                                mark_clear(x, y, direction[0], direction[1]);
                                add_score();
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }

        bool proc_move_down() {
            for (int y = BOARD_HEIGHT - 2; y >= 0; --y) {
                for (int x = 0; x < BOARD_WIDTH; ++x) {
                    Cell &source = board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                    Cell &target = board[static_cast<std::size_t>(y + 1)][static_cast<std::size_t>(x)];
                    if (is_play_block(source.type) && target.type == BlockType::Null) {
                        target = source;
                        source = {};
                        return true;
                    }
                }
            }
            bool updated = false;
            for (auto &row : board) {
                for (Cell &cell : row) {
                    if (cell.type == BlockType::Clear) {
                        ++cell.clear_value;
                        ++cell.flash_counter;
                        if (cell.clear_value > 50) {
                            cell = {};
                        }
                        updated = true;
                    }
                }
            }
            return updated;
        }

        [[nodiscard]] bool check_sequence(int x, int y, int dx, int dy, BlockType first, BlockType second, BlockType third) const {
            return check_block(x, y, first) && check_block(x + dx, y + dy, second) && check_block(x + dx * 2, y + dy * 2, third);
        }

        [[nodiscard]] bool check_block(int x, int y, BlockType expected) const {
            if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
                return false;
            }
            return same_or_match(board[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)].type, expected);
        }

        void mark_clear(int x, int y, int dx, int dy) {
            for (int index = 0; index < 3; ++index) {
                Cell &cell = board[static_cast<std::size_t>(y + dy * index)][static_cast<std::size_t>(x + dx * index)];
                cell.type = BlockType::Clear;
                cell.clear_value = 1;
                cell.flash_counter = 0;
            }
        }

        void add_score() {
            ++lines;
            if ((lines % 6) == 0 && level < LEVEL_COUNT) {
                ++level;
            }
        }
    };
} // namespace

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const FramebufferDimensions framebuffer = args.framebufferSpecified
                                                      ? args.framebuffer
                                                      : FramebufferDimensions{DEFAULT_FRAME_WIDTH, DEFAULT_FRAME_HEIGHT};
        PuzzleDropWindow window(args, framebuffer);
        window.loop();
    } catch (const mxvk::Exception &exception) {
        std::cerr << std::format("mxvk: Exception: {}\n", exception.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &exception) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", exception.text());
        return EXIT_FAILURE;
    } catch (const std::exception &exception) {
        std::cerr << std::format("3dmath_puzzle_drop: Exception: {}\n", exception.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
