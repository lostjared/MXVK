#include "defender_assets.hpp"

#include "defender_entities.hpp"

#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace defender {

    SpriteAlphaBounds calculate_alpha_bounds(SDL_Surface *surface, std::uint8_t threshold) {
        if (surface == nullptr || surface->w <= 0 || surface->h <= 0) {
            return {};
        }

        const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(surface->format);
        if (format_details == nullptr || !SDL_LockSurface(surface)) {
            return {};
        }

        int min_x = surface->w;
        int min_y = surface->h;
        int max_x = -1;
        int max_y = -1;
        const auto *bytes = static_cast<const std::uint8_t *>(surface->pixels);

        for (int y = 0; y < surface->h; ++y) {
            const auto *row = reinterpret_cast<const std::uint32_t *>(bytes + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch));
            for (int x = 0; x < surface->w; ++x) {
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                std::uint8_t a = 0;
                SDL_GetRGBA(row[x], format_details, nullptr, &r, &g, &b, &a);
                if (a <= threshold) {
                    continue;
                }
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }

        SDL_UnlockSurface(surface);

        if (max_x < min_x || max_y < min_y) {
            return {};
        }

        const float width = static_cast<float>(surface->w);
        const float height = static_cast<float>(surface->h);
        return {
            static_cast<float>(min_x) / width - 0.5f,
            static_cast<float>(max_x + 1) / width - 0.5f,
            0.5f - static_cast<float>(max_y + 1) / height,
            0.5f - static_cast<float>(min_y) / height,
        };
    }

    SDL_Surface *load_light_background_png(const std::string &path) {
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
        const int width = surface->w;
        const int height = surface->h;
        const int pixel_count = width * height;
        std::vector<std::uint8_t> background(static_cast<std::size_t>(pixel_count), 0);
        std::vector<int> stack;
        stack.reserve(static_cast<std::size_t>(width + height) * 2);

        const auto is_light_checker_pixel = [&](const int index) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            SDL_GetRGBA(pixels[index], format_details, nullptr, &r, &g, &b, &a);
            const int min_channel = std::min({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
            const int max_channel = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
            return a != 0 && min_channel >= 220 && (max_channel - min_channel) <= 18;
        };

        const auto push_background = [&](const int index) {
            if (index < 0 || index >= pixel_count || background[static_cast<std::size_t>(index)] != 0 || !is_light_checker_pixel(index)) {
                return;
            }
            background[static_cast<std::size_t>(index)] = 1;
            stack.push_back(index);
        };

        for (int x = 0; x < width; ++x) {
            push_background(x);
            push_background((height - 1) * width + x);
        }
        for (int y = 0; y < height; ++y) {
            push_background(y * width);
            push_background(y * width + width - 1);
        }

        while (!stack.empty()) {
            const int index = stack.back();
            stack.pop_back();
            const int x = index % width;
            const int y = index / width;
            if (x > 0) {
                push_background(index - 1);
            }
            if (x + 1 < width) {
                push_background(index + 1);
            }
            if (y > 0) {
                push_background(index - width);
            }
            if (y + 1 < height) {
                push_background(index + width);
            }
        }

        for (int i = 0; i < pixel_count; ++i) {
            if (background[static_cast<std::size_t>(i)] == 0) {
                continue;
            }
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);
            pixels[i] = SDL_MapRGBA(format_details, nullptr, r, g, b, 0);
        }

        SDL_UnlockSurface(surface);
        return surface;
    }

} // namespace defender
