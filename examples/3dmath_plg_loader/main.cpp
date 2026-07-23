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

    [[nodiscard]] mxvk::MXCOLOR pack_color(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) {
        return ((alpha & 0xFFU) << 24U) |
               ((red & 0xFFU) << 16U) |
               ((green & 0xFFU) << 8U) |
               (blue & 0xFFU);
    }

    [[nodiscard]] mxvk::MXCOLOR interpolate_color(mxvk::MXCOLOR first, mxvk::MXCOLOR second, float fraction) {
        const std::uint32_t second_weight = static_cast<std::uint32_t>(
            std::clamp(static_cast<int>(fraction * 256.0f + 0.5f), 0, 256));
        const std::uint32_t first_weight = 256U - second_weight;
        const auto interpolate_channel = [first_weight, second_weight](std::uint8_t first_channel, std::uint8_t second_channel) {
            return (static_cast<std::uint32_t>(first_channel) * first_weight +
                    static_cast<std::uint32_t>(second_channel) * second_weight +
                    128U) >>
                   8U;
        };
        return pack_color(
            interpolate_channel(mxvk::color_r(first), mxvk::color_r(second)),
            interpolate_channel(mxvk::color_g(first), mxvk::color_g(second)),
            interpolate_channel(mxvk::color_b(first), mxvk::color_b(second)),
            interpolate_channel(mxvk::color_a(first), mxvk::color_a(second)));
    }

    struct MipLevel {
        int width = 0;
        int height = 0;
        std::vector<mxvk::MXCOLOR> pixels;

        [[nodiscard]] mxvk::MXCOLOR sample_bilinear(float u, float v) const {
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            const float texture_x = u * static_cast<float>(width - 1);
            const float texture_y = v * static_cast<float>(height - 1);
            const int first_x = static_cast<int>(std::floor(texture_x));
            const int first_y = static_cast<int>(std::floor(texture_y));
            const int second_x = std::min(first_x + 1, width - 1);
            const int second_y = std::min(first_y + 1, height - 1);
            const float x_fraction = texture_x - static_cast<float>(first_x);
            const float y_fraction = texture_y - static_cast<float>(first_y);
            const auto texel = [this](int x, int y) {
                return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            };
            const mxvk::MXCOLOR top = interpolate_color(texel(first_x, first_y), texel(second_x, first_y), x_fraction);
            const mxvk::MXCOLOR bottom = interpolate_color(texel(first_x, second_y), texel(second_x, second_y), x_fraction);
            return interpolate_color(top, bottom, y_fraction);
        }
    };

    struct Texture {
        std::vector<MipLevel> levels;

        [[nodiscard]] bool empty() const {
            return levels.empty() || levels.front().width <= 0 || levels.front().height <= 0 || levels.front().pixels.empty();
        }

        [[nodiscard]] int width() const {
            return empty() ? 0 : levels.front().width;
        }

        [[nodiscard]] int height() const {
            return empty() ? 0 : levels.front().height;
        }

        [[nodiscard]] mxvk::MXCOLOR sample(float u, float v, float level) const {
            level = std::clamp(level, 0.0f, static_cast<float>(levels.size() - 1));
            const std::size_t first_level = static_cast<std::size_t>(std::floor(level));
            const std::size_t second_level = std::min(first_level + 1, levels.size() - 1);
            const float fraction = level - static_cast<float>(first_level);
            const mxvk::MXCOLOR first_color = levels[first_level].sample_bilinear(u, v);
            if (first_level == second_level) {
                return first_color;
            }
            return interpolate_color(first_color, levels[second_level].sample_bilinear(u, v), fraction);
        }
    };

    [[nodiscard]] SurfacePtr create_frame_surface(int width, int height) {
        SurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (!surface) {
            throw mxvk::Exception(std::format("3dmath_plg_loader: failed to create frame surface: {}", SDL_GetError()));
        }
        return surface;
    }

    [[nodiscard]] std::filesystem::path model_path(const std::string &filename, const std::string &asset_path) {
        if (!filename.empty()) {
            return filename;
        }
        return std::filesystem::path(asset_path) / "data" / "sphere.plg";
    }

    [[nodiscard]] std::filesystem::path texture_path(const std::string &filename, const std::string &asset_path) {
        const std::filesystem::path requested(filename);
        if (requested.is_absolute() || std::filesystem::exists(requested)) {
            return requested;
        }

        const std::filesystem::path from_asset_path = std::filesystem::path(asset_path) / requested;
        if (std::filesystem::exists(from_asset_path)) {
            return from_asset_path;
        }
        return requested;
    }

    [[nodiscard]] Texture load_texture(const std::string &filename, const std::string &asset_path, bool generate_mipmaps) {
        if (filename.empty()) {
            return {};
        }

        const std::filesystem::path path = texture_path(filename, asset_path);
        SurfacePtr loaded(mxvk::LoadPNG(path.string().c_str()));
        if (!loaded) {
            throw mxvk::Exception(std::format("3dmath_plg_loader: failed to load texture '{}'", path.string()));
        }

        SurfacePtr rgba(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_RGBA32));
        if (!rgba) {
            throw mxvk::Exception(std::format("3dmath_plg_loader: failed to convert texture '{}': {}", path.string(), SDL_GetError()));
        }

        const SDL_PixelFormatDetails *format = SDL_GetPixelFormatDetails(rgba->format);
        if (format == nullptr) {
            throw mxvk::Exception(std::format("3dmath_plg_loader: failed to query texture format '{}': {}", path.string(), SDL_GetError()));
        }

        Texture texture;
        MipLevel base_level;
        base_level.width = rgba->w;
        base_level.height = rgba->h;
        base_level.pixels.resize(static_cast<std::size_t>(base_level.width) * static_cast<std::size_t>(base_level.height));
        for (int y = 0; y < base_level.height; ++y) {
            const auto *row = static_cast<const std::uint8_t *>(rgba->pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch);
            const auto *source = reinterpret_cast<const std::uint32_t *>(row);
            for (int x = 0; x < base_level.width; ++x) {
                std::uint8_t red = 0;
                std::uint8_t green = 0;
                std::uint8_t blue = 0;
                std::uint8_t alpha = 0;
                SDL_GetRGBA(source[x], format, nullptr, &red, &green, &blue, &alpha);
                base_level.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(base_level.width) + static_cast<std::size_t>(x)] =
                    pack_color(red, green, blue, alpha);
            }
        }
        texture.levels.push_back(std::move(base_level));

        while (generate_mipmaps && (texture.levels.back().width > 1 || texture.levels.back().height > 1)) {
            const MipLevel &source = texture.levels.back();
            MipLevel destination;
            destination.width = std::max(1, (source.width + 1) / 2);
            destination.height = std::max(1, (source.height + 1) / 2);
            destination.pixels.resize(static_cast<std::size_t>(destination.width) * static_cast<std::size_t>(destination.height));
            for (int y = 0; y < destination.height; ++y) {
                for (int x = 0; x < destination.width; ++x) {
                    const int first_x = std::min(x * 2, source.width - 1);
                    const int second_x = std::min(first_x + 1, source.width - 1);
                    const int first_y = std::min(y * 2, source.height - 1);
                    const int second_y = std::min(first_y + 1, source.height - 1);
                    const auto texel = [&source](int source_x, int source_y) {
                        return source.pixels[static_cast<std::size_t>(source_y) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(source_x)];
                    };
                    const std::array<mxvk::MXCOLOR, 4> colors = {
                        texel(first_x, first_y),
                        texel(second_x, first_y),
                        texel(first_x, second_y),
                        texel(second_x, second_y),
                    };
                    const auto average_channel = [&colors](auto component) {
                        std::uint32_t sum = 0;
                        for (const mxvk::MXCOLOR color : colors) {
                            sum += component(color);
                        }
                        return (sum + 2U) / 4U;
                    };
                    destination.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(destination.width) + static_cast<std::size_t>(x)] =
                        pack_color(
                            average_channel(mxvk::color_r),
                            average_channel(mxvk::color_g),
                            average_channel(mxvk::color_b),
                            average_channel(mxvk::color_a));
                }
            }
            texture.levels.push_back(std::move(destination));
        }

        std::cout << std::format(
            "3dmath_plg_loader: loaded texture '{}' ({}x{}, {} mip levels)\n",
            path.string(),
            texture.width(),
            texture.height(),
            texture.levels.size());
        return texture;
    }

    struct FaceDraw {
        std::array<mxvk::vec4D, 3> points{};
        std::array<mxvk::vec2D, 3> texcoords{};
        float intensity = 1.0f;
    };

    constexpr float MIN_CAMERA_DISTANCE = 2.5f;
    constexpr float MAX_CAMERA_DISTANCE = 12.0f;
    constexpr float CAMERA_ZOOM_STEP = 0.45f;
    constexpr float MOUSE_ROTATION_SENSITIVITY = 0.35f;
    constexpr float MAX_PITCH_DEGREES = 89.0f;
} // namespace

namespace example {
    class Math3DPlgLoaderWindow : public mxvk::VK_Window {
      public:
        Math3DPlgLoaderWindow(const std::string &filename, const std::string &texture_filename, const std::string &asset_path, const std::string &title, int width, int height, bool fullscreen, bool enable_vsync, bool disable_warp_fix, bool disable_mipmap, float mip_bias, const FramebufferDimensions &framebuffer)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              texture(load_texture(texture_filename, asset_path, !disable_mipmap)),
              frame_width(framebuffer.width),
              frame_height(framebuffer.height),
              fallback_width(width),
              fallback_height(height),
              warp_fix_enabled(!disable_warp_fix),
              mipmapping_enabled(!disable_mipmap),
              mip_level_bias(mip_bias) {
            setClearColor(0.012f, 0.015f, 0.022f, 1.0f);
            mxvk::BuildTables();

            const std::filesystem::path path = model_path(filename, asset_path);
            if (!model.LoadPLG(path.string(), mxvk::vec4D(2.0f, 2.0f, 2.0f), mxvk::vec4D(0.0f, 0.0f, 4.5f), mxvk::vec4D())) {
                throw mxvk::Exception(std::format("3dmath_plg_loader: failed to load PLG model '{}'", path.string()));
            }
            std::cout << std::format("3dmath_plg_loader: loaded '{}' ({} vertices, {} triangles)\n", path.string(), model.num_vertices, model.num_polys);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE && !e.key.repeat) {
                automatic_rotation = !automatic_rotation;
                return;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = true;
                last_mouse_x = e.button.x;
                last_mouse_y = e.button.y;
                return;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = false;
                return;
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                mouse_dragging = false;
                return;
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && mouse_dragging) {
                const float delta_x = e.motion.x - last_mouse_x;
                const float delta_y = e.motion.y - last_mouse_y;
                yaw_degrees = std::fmod(yaw_degrees + delta_x * MOUSE_ROTATION_SENSITIVITY, 360.0f);
                pitch_degrees = std::clamp(
                    pitch_degrees + delta_y * MOUSE_ROTATION_SENSITIVITY,
                    -MAX_PITCH_DEGREES,
                    MAX_PITCH_DEGREES);
                last_mouse_x = e.motion.x;
                last_mouse_y = e.motion.y;
                return;
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = e.wheel.y != 0.0f ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                camera_distance = std::clamp(camera_distance - delta * CAMERA_ZOOM_STEP, MIN_CAMERA_DISTANCE, MAX_CAMERA_DISTANCE);
            }
        }

        void proc() override {
            const int output_width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : fallback_width;
            const int output_height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : fallback_height;

            ensure_framebuffer();
            if (frame_sprite == nullptr || frame_surface == nullptr || frame_format == nullptr) {
                return;
            }

            clear_frame(mxvk::MXVK_RGB(3, 4, 8));
            std::ranges::fill(depth_buffer, std::numeric_limits<float>::infinity());

            const std::uint64_t current_ticks = SDL_GetTicks();
            const float elapsed_seconds = previous_frame_ticks == 0
                                              ? 0.0f
                                              : static_cast<float>(current_ticks - previous_frame_ticks) * 0.001f;
            previous_frame_ticks = current_ticks;
            if (automatic_rotation && !mouse_dragging) {
                yaw_degrees = std::fmod(yaw_degrees + elapsed_seconds * 42.0f, 360.0f);
            }

            mxvk::Mat4D rotation;
            rotation.BuildXYZ(pitch_degrees, yaw_degrees, 0.0f);

            std::vector<mxvk::vec4D> camera_vertices(model.local.size());
            std::vector<mxvk::vec4D> projected(model.local.size());
            for (std::size_t i = 0; i < model.local.size(); ++i) {
                camera_vertices[i] = rotation.MulVec(model.local[i]);
                camera_vertices[i].z += camera_distance;
                projected[i] = project_to_screen(camera_vertices[i], frame_width, frame_height);
            }

            mxvk::vec4D light_direction(-0.35f, -0.55f, -1.0f, 0.0f);
            light_direction.Normalize();
            std::vector<FaceDraw> faces;
            faces.reserve(model.vlist.size());

            for (const mxvk::Triangle &triangle : model.vlist) {
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
                const float intensity = std::clamp(0.35f + diffuse * 0.65f, 0.0f, 1.0f);
                faces.push_back({
                    {projected[first], projected[second], projected[third]},
                    {
                        model.texcoords[first],
                        model.texcoords[second],
                        model.texcoords[third],
                    },
                    intensity,
                });
            }

            for (const FaceDraw &face : faces) {
                draw_gradient_triangle(face);
            }

            frame_sprite->updateTexture(frame_surface->pixels, frame_width, frame_height, frame_surface->pitch);
            frame_sprite->drawSpriteRect(0, 0, output_width, output_height);
        }

      private:
        mxvk::mxObject model;
        Texture texture;
        SurfacePtr frame_surface;
        std::vector<float> depth_buffer;
        const SDL_PixelFormatDetails *frame_format = nullptr;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        int frame_width = 1280;
        int frame_height = 720;
        int fallback_width = 1280;
        int fallback_height = 720;
        float camera_distance = 4.25f;
        float pitch_degrees = 0.0f;
        float yaw_degrees = 0.0f;
        float last_mouse_x = 0.0f;
        float last_mouse_y = 0.0f;
        bool mouse_dragging = false;
        bool automatic_rotation = true;
        std::uint64_t previous_frame_ticks = 0;
        bool warp_fix_enabled = true;
        bool mipmapping_enabled = true;
        float mip_level_bias = 0.0f;

        void ensure_framebuffer() {
            if (frame_surface != nullptr) {
                return;
            }

            frame_surface = create_frame_surface(frame_width, frame_height);
            frame_format = SDL_GetPixelFormatDetails(frame_surface->format);
            if (frame_format == nullptr) {
                throw mxvk::Exception(std::format("3dmath_plg_loader: failed to query frame format: {}", SDL_GetError()));
            }

            depth_buffer.resize(static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height));
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

        [[nodiscard]] float texture_level_for_face(const FaceDraw &face, float area) const {
            if (texture.empty() || !mipmapping_enabled) {
                return 0.0f;
            }

            const mxvk::vec4D &a = face.points[0];
            const mxvk::vec4D &b = face.points[1];
            const mxvk::vec4D &c = face.points[2];
            const float weight_a_dx = (c.y - b.y) / area;
            const float weight_a_dy = -(c.x - b.x) / area;
            const float weight_b_dx = (a.y - c.y) / area;
            const float weight_b_dy = -(a.x - c.x) / area;
            const float weight_c_dx = (b.y - a.y) / area;
            const float weight_c_dy = -(b.x - a.x) / area;

            float u_dx = 0.0f;
            float u_dy = 0.0f;
            float v_dx = 0.0f;
            float v_dy = 0.0f;
            if (warp_fix_enabled) {
                const float inverse_z_a = 1.0f / a.z;
                const float inverse_z_b = 1.0f / b.z;
                const float inverse_z_c = 1.0f / c.z;
                const float inverse_z = (inverse_z_a + inverse_z_b + inverse_z_c) / 3.0f;
                const float inverse_z_dx =
                    weight_a_dx * inverse_z_a +
                    weight_b_dx * inverse_z_b +
                    weight_c_dx * inverse_z_c;
                const float inverse_z_dy =
                    weight_a_dy * inverse_z_a +
                    weight_b_dy * inverse_z_b +
                    weight_c_dy * inverse_z_c;
                const auto corrected_derivatives = [&](float first, float second, float third) {
                    const float value_over_z =
                        (first * inverse_z_a + second * inverse_z_b + third * inverse_z_c) / 3.0f;
                    const float value_over_z_dx =
                        weight_a_dx * first * inverse_z_a +
                        weight_b_dx * second * inverse_z_b +
                        weight_c_dx * third * inverse_z_c;
                    const float value_over_z_dy =
                        weight_a_dy * first * inverse_z_a +
                        weight_b_dy * second * inverse_z_b +
                        weight_c_dy * third * inverse_z_c;
                    const float denominator = inverse_z * inverse_z;
                    return std::array<float, 2>{
                        (value_over_z_dx * inverse_z - value_over_z * inverse_z_dx) / denominator,
                        (value_over_z_dy * inverse_z - value_over_z * inverse_z_dy) / denominator,
                    };
                };
                const std::array<float, 2> u_derivatives = corrected_derivatives(
                    face.texcoords[0].x,
                    face.texcoords[1].x,
                    face.texcoords[2].x);
                const std::array<float, 2> v_derivatives = corrected_derivatives(
                    face.texcoords[0].y,
                    face.texcoords[1].y,
                    face.texcoords[2].y);
                u_dx = u_derivatives[0];
                u_dy = u_derivatives[1];
                v_dx = v_derivatives[0];
                v_dy = v_derivatives[1];
            } else {
                u_dx =
                    weight_a_dx * face.texcoords[0].x +
                    weight_b_dx * face.texcoords[1].x +
                    weight_c_dx * face.texcoords[2].x;
                u_dy =
                    weight_a_dy * face.texcoords[0].x +
                    weight_b_dy * face.texcoords[1].x +
                    weight_c_dy * face.texcoords[2].x;
                v_dx =
                    weight_a_dx * face.texcoords[0].y +
                    weight_b_dx * face.texcoords[1].y +
                    weight_c_dx * face.texcoords[2].y;
                v_dy =
                    weight_a_dy * face.texcoords[0].y +
                    weight_b_dy * face.texcoords[1].y +
                    weight_c_dy * face.texcoords[2].y;
            }

            const float horizontal_footprint = std::hypot(
                u_dx * static_cast<float>(texture.width()),
                v_dx * static_cast<float>(texture.height()));
            const float vertical_footprint = std::hypot(
                u_dy * static_cast<float>(texture.width()),
                v_dy * static_cast<float>(texture.height()));
            return std::log2(std::max({mxvk::EPSILON, horizontal_footprint, vertical_footprint})) + mip_level_bias;
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
            const float texture_level = texture_level_for_face(face, area);

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

                    const float texture_weight_a = warp_fix_enabled ? (weight_a / a.z) * depth : weight_a;
                    const float texture_weight_b = warp_fix_enabled ? (weight_b / b.z) * depth : weight_b;
                    const float texture_weight_c = warp_fix_enabled ? (weight_c / c.z) * depth : weight_c;
                    const float u =
                        face.texcoords[0].x * texture_weight_a +
                        face.texcoords[1].x * texture_weight_b +
                        face.texcoords[2].x * texture_weight_c;
                    const float v =
                        face.texcoords[0].y * texture_weight_a +
                        face.texcoords[1].y * texture_weight_b +
                        face.texcoords[2].y * texture_weight_c;
                    const mxvk::MXCOLOR color = texture.empty() ? gradient_color(u, v) : texture.sample(u, v, texture_level);
                    put_pixel(x, y, mxvk::shade_color(color, face.intensity));
                }
            }
        }

        [[nodiscard]] static mxvk::MXCOLOR gradient_color(float u, float v) {
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);

            constexpr mxvk::MXCOLOR BOTTOM_LEFT = mxvk::MXVK_RGB(68, 214, 255);
            constexpr mxvk::MXCOLOR BOTTOM_RIGHT = mxvk::MXVK_RGB(92, 255, 142);
            constexpr mxvk::MXCOLOR TOP_LEFT = mxvk::MXVK_RGB(155, 105, 255);
            constexpr mxvk::MXCOLOR TOP_RIGHT = mxvk::MXVK_RGB(255, 82, 197);
            const auto bilinear_channel = [&](auto component) {
                const float bottom =
                    static_cast<float>(component(BOTTOM_LEFT)) +
                    (static_cast<float>(component(BOTTOM_RIGHT)) - static_cast<float>(component(BOTTOM_LEFT))) * u;
                const float top =
                    static_cast<float>(component(TOP_LEFT)) +
                    (static_cast<float>(component(TOP_RIGHT)) - static_cast<float>(component(TOP_LEFT))) * u;
                return std::clamp(static_cast<int>(std::lround(bottom + (top - bottom) * v)), 0, 255);
            };

            return mxvk::MXVK_RGB(
                bilinear_channel(mxvk::color_r),
                bilinear_channel(mxvk::color_g),
                bilinear_channel(mxvk::color_b));
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
        example::Math3DPlgLoaderWindow window(args.filename, args.texture, args.path, "MXVK 3D Math PLG Loader", args.width, args.height, args.fullscreen, args.enable_vsync, args.nowarpfix, args.disable_mipmap, args.mip_bias, args.framebuffer);
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
