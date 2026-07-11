#ifndef ASTEROIDS3D_TYPES_HPP
#define ASTEROIDS3D_TYPES_HPP

/**
 * @file asteroids3d_types.hpp
 * @brief Shared simulation constants, data types, and utility functions.
 */

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

    /** @name Simulation constants
     * @{
     */
    constexpr float PI = 3.14159265358979323846f;                   ///< Single-precision value of pi.
    constexpr int GAME_STARS = 22000;                               ///< Number of stars in the background field.
    constexpr int MAX_PROJECTILES = 64;                             ///< Maximum locally simulated projectiles.
    constexpr int MAX_ASTEROIDS = 64;                               ///< Maximum locally simulated asteroids.
    constexpr int MAX_PARTICLES = 6000;                             ///< Maximum particles in the shared particle pool.
    constexpr int MAX_GENERATIONS = 1;                              ///< Maximum asteroid split generation.
    constexpr int CHILDREN_PER_SPAWN = 2;                           ///< Child asteroids created by a split.
    constexpr int LARGE_ASTEROID_POINTS = 20;                       ///< Score awarded for a large asteroid.
    constexpr int MEDIUM_ASTEROID_POINTS = 50;                      ///< Score awarded for a medium asteroid.
    constexpr int SMALL_ASTEROID_POINTS = 100;                      ///< Score awarded for a small asteroid.
    constexpr float PROJECTILE_SPEED = 52.0f;                       ///< Projectile travel speed in world units per second.
    constexpr float PROJECTILE_LIFETIME = 3.0f;                     ///< Projectile lifetime in seconds.
    constexpr int FIRE_COOLDOWN = 5;                                ///< Frames between firing bursts.
    constexpr int SHOTS_PER_BURST = 5;                              ///< Projectiles fired in one burst.
    constexpr int FIRE_DELAY = 3;                                   ///< Frames between shots within a burst.
    constexpr int EXPLOSION_DURATION_FRAMES = 90;                   ///< Ship explosion duration in frames.
    constexpr float ROUND_TIME_LIMIT_SECONDS = 270.0f;              ///< Multiplayer round limit in seconds.
    constexpr float SHIP_MODEL_SCALE = 1.55f;                       ///< Uniform ship model scale.
    constexpr float ASTEROID_SHIP_COLLISION_SCALE = 1.08f;          ///< Ship collision-radius adjustment.
    constexpr float ASTEROID_PROJECTILE_COLLISION_SCALE = 1.18f;    ///< Projectile collision-radius adjustment.
    constexpr glm::vec4 PROJECTILE_COLOR{1.0f, 0.58f, 0.12f, 1.0f}; ///< Default projectile color.
    constexpr Sint16 CONTROLLER_DEAD_ZONE = 8000;                   ///< Controller axis dead-zone magnitude.
    constexpr float CONTROLLER_AXIS_MAX = 32767.0f;                 ///< Maximum signed controller axis magnitude.
    constexpr float BOUNDARY_X_MIN = -150.0f;                       ///< Minimum simulation x-coordinate.
    constexpr float BOUNDARY_X_MAX = 150.0f;                        ///< Maximum simulation x-coordinate.
    constexpr float BOUNDARY_Y_MIN = -100.0f;                       ///< Minimum simulation y-coordinate.
    constexpr float BOUNDARY_Y_MAX = 100.0f;                        ///< Maximum simulation y-coordinate.
    constexpr float BOUNDARY_Z_MIN = -150.0f;                       ///< Minimum simulation z-coordinate.
    constexpr float BOUNDARY_Z_MAX = 150.0f;                        ///< Maximum simulation z-coordinate.
    constexpr float BOUNDARY_BOUNCE_FACTOR = 1.2f;                  ///< Boundary collision response multiplier.
    /** @} */

    /** @brief High-level state of the Asteroids application. */
    enum class GameMode {
        Intro,        ///< Introductory screen.
        Loading,      ///< Asset-loading screen.
        Lobby,        ///< Multiplayer lobby.
        Playing,      ///< Active gameplay.
        MatchOver,    ///< Completed multiplayer round.
        GameComplete, ///< Completed game.
        GameOver      ///< Player has no remaining lives.
    };

    /** @brief Returns the thread-local random number engine used by simulation helpers. */
    inline std::default_random_engine &rng() {
        static thread_local std::default_random_engine engine{std::random_device{}()};
        return engine;
    }

    /**
     * @brief Generates a uniformly distributed floating-point value.
     * @param min_value Inclusive lower bound.
     * @param max_value Inclusive upper bound.
     * @return Generated value.
     */
    inline float random_float(float min_value, float max_value) {
        std::uniform_real_distribution<float> dist(min_value, max_value);
        return dist(rng());
    }

    /**
     * @brief Generates a uniformly distributed integer.
     * @param min_value Inclusive lower bound.
     * @param max_value Inclusive upper bound.
     * @return Generated value.
     */
    inline int random_int(int min_value, int max_value) {
        std::uniform_int_distribution<int> dist(min_value, max_value);
        return dist(rng());
    }

    /**
     * @brief Loads a PNG and fades dark color-key pixels to transparency.
     * @param path PNG file path.
     * @param threshold Brightness at or below which pixels become transparent.
     * @param softness Brightness range over which alpha fades in.
     * @return Newly allocated RGBA surface owned by the caller.
     * @throws mxvk::Exception If the image cannot be loaded, converted, queried, or locked.
     */
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

    /**
     * @brief Normalizes a direction with a stable fallback.
     * @param value Direction to normalize.
     * @return Normalized value, or forward when its length is near zero.
     */
    inline glm::vec3 normalize_or_zero(const glm::vec3 &value) {
        const float len = glm::length(value);
        if (len <= 1e-6f) {
            return glm::vec3(0.0f, 0.0f, -1.0f);
        }
        return value / len;
    }

    /**
     * @brief Builds a translated, rotated, scaled model matrix.
     * @param position World-space translation.
     * @param rotation_degrees Euler rotation in degrees.
     * @param scale Uniform scale.
     * @param center_offset Model-space center correction.
     * @return Composed model matrix.
     */
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

    /** @brief Runtime state for a ship projectile. */
    struct Projectile {
        glm::vec3 position{0.0f};                  ///< Current world-space position.
        glm::vec3 prev_position{0.0f};             ///< Position from the previous simulation step.
        glm::vec3 velocity{0.0f};                  ///< World-space velocity.
        glm::vec4 color{1.0f, 0.58f, 0.12f, 1.0f}; ///< Render color.
        float lifetime = 0.0f;                     ///< Remaining lifetime in seconds.
        bool active = false;                       ///< Whether this slot is active.
    };

    /** @brief Runtime state for an asteroid. */
    struct Asteroid {
        glm::vec3 position{0.0f};       ///< Current world-space position.
        glm::vec3 velocity{0.0f};       ///< World-space velocity.
        glm::vec3 rotation{0.0f};       ///< Euler rotation in degrees.
        glm::vec3 rotation_speed{0.0f}; ///< Angular velocity in degrees per second.
        float radius = 0.0f;            ///< Collision radius.
        bool active = false;            ///< Whether this slot is active.
        int generation = 0;             ///< Split generation used to determine size and scoring.
        int model_index = 0;            ///< Asteroid model variant.
    };

    /** @brief One spherical sample used by the compound ship collision shape. */
    struct ShipCollisionSample {
        glm::vec3 local_position{0.0f}; ///< Sample center in ship-local space.
        float radius = 0.0f;            ///< Sample sphere radius.
    };

    /** @brief Runtime state for an explosion or engine particle. */
    struct Particle {
        glm::vec3 position{0.0f};  ///< Current world-space position.
        glm::vec3 velocity{0.0f};  ///< World-space velocity.
        glm::vec4 color{1.0f};     ///< Render color.
        float size = 0.0f;         ///< Rendered point size.
        float lifetime = 0.0f;     ///< Remaining lifetime in seconds.
        float max_lifetime = 0.0f; ///< Initial lifetime in seconds.
        bool color_flash = false;  ///< Whether the particle flashes as it ages.
        bool active = false;       ///< Whether this slot is active.
    };

    /** @brief Vertex consumed by the procedural engine-flame pipeline. */
    struct FlameVertex {
        glm::vec3 pos{};   ///< Vertex position.
        glm::vec4 color{}; ///< Vertex color.
    };

    /** @brief Push-constant payload for the engine-flame shaders. */
    struct FlamePushConstants {
        glm::mat4 mvp{1.0f};    ///< Model-view-projection matrix.
        glm::vec4 params{0.0f}; ///< Shader-specific animation parameters.
    };

    /** @brief Runtime state for one animated background star. */
    struct Star {
        glm::vec3 position{0.0f};   ///< World-space position.
        glm::vec3 velocity{0.0f};   ///< World-space velocity.
        glm::vec4 base_color{1.0f}; ///< Color before brightness modulation.
        glm::vec4 color{1.0f};      ///< Current rendered color.
        float size = 1.0f;          ///< Rendered point size.
        float brightness = 1.0f;    ///< Base brightness multiplier.
        float twinkle_phase = 0.0f; ///< Twinkle animation phase.
        float twinkle_speed = 1.0f; ///< Twinkle animation rate.
        int layer = 0;              ///< Parallax layer index.
    };

} // namespace space

#endif
