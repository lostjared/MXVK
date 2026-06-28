#ifndef ASTEROIDS3D_TYPES_HPP
#define ASTEROIDS3D_TYPES_HPP

#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace space {

    constexpr float PI = 3.14159265358979323846f;
    constexpr int GAME_STARS = 22000;
    constexpr int MAX_PROJECTILES = 64;
    constexpr int MAX_ASTEROIDS = 64;
    constexpr int MAX_PARTICLES = 6000;
    constexpr int MAX_GENERATIONS = 1;
    constexpr int CHILDREN_PER_SPAWN = 2;
    constexpr int LARGE_ASTEROID_POINTS = 20;
    constexpr int MEDIUM_ASTEROID_POINTS = 50;
    constexpr int SMALL_ASTEROID_POINTS = 100;
    constexpr float PROJECTILE_SPEED = 52.0f;
    constexpr float PROJECTILE_LIFETIME = 3.0f;
    constexpr int FIRE_COOLDOWN = 5;
    constexpr int SHOTS_PER_BURST = 5;
    constexpr int FIRE_DELAY = 3;
    constexpr int EXPLOSION_DURATION_FRAMES = 90;
    constexpr float ROUND_TIME_LIMIT_SECONDS = 270.0f;
    constexpr float SHIP_MODEL_SCALE = 1.55f;
    constexpr float ASTEROID_SHIP_COLLISION_SCALE = 1.08f;
    constexpr float ASTEROID_PROJECTILE_COLLISION_SCALE = 1.18f;
    constexpr glm::vec4 PROJECTILE_COLOR{1.0f, 0.58f, 0.12f, 1.0f};
    constexpr Sint16 CONTROLLER_DEAD_ZONE = 8000;
    constexpr float CONTROLLER_AXIS_MAX = 32767.0f;
    constexpr float BOUNDARY_X_MIN = -150.0f;
    constexpr float BOUNDARY_X_MAX = 150.0f;
    constexpr float BOUNDARY_Y_MIN = -100.0f;
    constexpr float BOUNDARY_Y_MAX = 100.0f;
    constexpr float BOUNDARY_Z_MIN = -150.0f;
    constexpr float BOUNDARY_Z_MAX = 150.0f;
    constexpr float BOUNDARY_BOUNCE_FACTOR = 1.2f;

    enum class GameMode {
        Intro,
        Loading,
        Playing,
        GameComplete,
        GameOver
    };

    inline std::default_random_engine &rng() {
        static thread_local std::default_random_engine engine{std::random_device{}()};
        return engine;
    }

    inline float random_float(float min_value, float max_value) {
        std::uniform_real_distribution<float> dist(min_value, max_value);
        return dist(rng());
    }

    inline int random_int(int min_value, int max_value) {
        std::uniform_int_distribution<int> dist(min_value, max_value);
        return dist(rng());
    }

    inline SDL_Surface *load_color_keyed_png(const std::string &path, std::uint8_t threshold = 12, std::uint8_t softness = 48) {
        SDL_Surface *loaded_surface = mxvk::LoadPNG(path.c_str());
        if (loaded_surface == nullptr) {
            throw mxvk::Exception("Failed to load PNG: " + path);
        }

        SDL_Surface *surface = SDL_ConvertSurface(loaded_surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded_surface);
        if (surface == nullptr) {
            throw mxvk::Exception("Failed to convert PNG to RGBA: " + path);
        }

        const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(surface->format);
        if (format_details == nullptr) {
            SDL_DestroySurface(surface);
            throw mxvk::Exception("Failed to query pixel format details for: " + path);
        }

        if (!SDL_LockSurface(surface)) {
            SDL_DestroySurface(surface);
            throw mxvk::Exception("Failed to lock PNG surface: " + path);
        }

        auto *pixels = static_cast<std::uint32_t *>(surface->pixels);
        const int pixel_count = surface->w * surface->h;

        struct KeyedPixel {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
        };

        std::vector<KeyedPixel> keyed(static_cast<std::size_t>(pixel_count));
        for (int i = 0; i < pixel_count; ++i) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);
            const int brightness = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
            if (brightness <= threshold) {
                keyed[static_cast<std::size_t>(i)] = {r, g, b, 0};
                continue;
            }
            const int soft_end = static_cast<int>(threshold) + static_cast<int>(softness);
            if (brightness < soft_end) {
                const float t = static_cast<float>(brightness - threshold) / static_cast<float>(std::max<int>(1, softness));
                a = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a) * t)), 0, 255));
            }
            keyed[static_cast<std::size_t>(i)] = {r, g, b, a};
        }

        constexpr int COLOR_BLEED_PASSES = 5;
        for (int pass = 0; pass < COLOR_BLEED_PASSES; ++pass) {
            std::vector<KeyedPixel> next = keyed;
            for (int y = 0; y < surface->h; ++y) {
                for (int x = 0; x < surface->w; ++x) {
                    const int index = y * surface->w + x;
                    if (keyed[static_cast<std::size_t>(index)].a != 0) {
                        continue;
                    }

                    int red = 0;
                    int green = 0;
                    int blue = 0;
                    int count = 0;
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) {
                                continue;
                            }
                            const int nx = x + ox;
                            const int ny = y + oy;
                            if (nx < 0 || ny < 0 || nx >= surface->w || ny >= surface->h) {
                                continue;
                            }
                            const KeyedPixel &neighbor = keyed[static_cast<std::size_t>(ny * surface->w + nx)];
                            if (neighbor.a == 0) {
                                continue;
                            }
                            red += neighbor.r;
                            green += neighbor.g;
                            blue += neighbor.b;
                            ++count;
                        }
                    }
                    if (count > 0) {
                        KeyedPixel &out = next[static_cast<std::size_t>(index)];
                        out.r = static_cast<std::uint8_t>(red / count);
                        out.g = static_cast<std::uint8_t>(green / count);
                        out.b = static_cast<std::uint8_t>(blue / count);
                    }
                }
            }
            keyed = std::move(next);
        }

        for (int i = 0; i < pixel_count; ++i) {
            const KeyedPixel &px = keyed[static_cast<std::size_t>(i)];
            pixels[i] = SDL_MapRGBA(format_details, nullptr, px.r, px.g, px.b, px.a);
        }

        SDL_UnlockSurface(surface);
        return surface;
    }

    inline glm::vec3 normalize_or_zero(const glm::vec3 &value) {
        const float len = glm::length(value);
        if (len <= 1e-6f) {
            return glm::vec3(0.0f, 0.0f, -1.0f);
        }
        return value / len;
    }

    inline glm::mat4 build_model_matrix(const glm::vec3 &position,
                                        const glm::vec3 &rotation_degrees,
                                        float scale,
                                        const glm::vec3 &center_offset) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(scale));
        model = glm::translate(model, center_offset);
        return model;
    }

    struct Projectile {
        glm::vec3 position{0.0f};
        glm::vec3 prev_position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f, 0.58f, 0.12f, 1.0f};
        float lifetime = 0.0f;
        bool active = false;
    };

    struct Asteroid {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 rotation_speed{0.0f};
        float radius = 0.0f;
        bool active = false;
        int generation = 0;
        int model_index = 0;
    };

    struct ShipCollisionSample {
        glm::vec3 local_position{0.0f};
        float radius = 0.0f;
    };

    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float size = 0.0f;
        float lifetime = 0.0f;
        float max_lifetime = 0.0f;
        bool color_flash = false;
        bool active = false;
    };

    struct FlameVertex {
        glm::vec3 pos{};
        glm::vec4 color{};
    };

    struct FlamePushConstants {
        glm::mat4 mvp{1.0f};
        glm::vec4 params{0.0f};
    };

    struct Star {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 base_color{1.0f};
        glm::vec4 color{1.0f};
        float size = 1.0f;
        float brightness = 1.0f;
        float twinkle_phase = 0.0f;
        float twinkle_speed = 1.0f;
        int layer = 0;
    };

} // namespace space

#endif
