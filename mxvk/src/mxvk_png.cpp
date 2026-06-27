/**
 * @file mxvk_png.cpp
 * @brief Implementation of mxvk PNG loading and saving utilities.
 */
#include "mxvk/mxvk_png.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <memory>
#include <png.h>
#include <ranges>
#include <string>
#include <vector>

namespace mxvk {

    [[nodiscard]] bool HasPngExtension(const char *file) {
        if (file == nullptr) {
            return false;
        }

        std::string lower(file);
        std::ranges::transform(lower, lower.begin(),
                               [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return lower.ends_with(".png");
    }

    [[nodiscard]] bool WriteRgbaPng(const char *filename,
                                    const std::uint8_t *pixels,
                                    const int width,
                                    const int height,
                                    const int pitch,
                                    const int bit_depth,
                                    const bool swap_16bit_endianness) {
        if (filename == nullptr || pixels == nullptr || width <= 0 || height <= 0 || pitch <= 0) {
            std::cerr << "mx: Invalid PNG write parameters.\n";
            return false;
        }

        FILE *file = std::fopen(filename, "wb");
        if (file == nullptr) {
            std::cerr << "mx: Failed to open file for writing: " << filename << "\n";
            return false;
        }

        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (png == nullptr) {
            std::cerr << "mx: Failed to create PNG write struct.\n";
            std::fclose(file);
            return false;
        }

        png_infop info = png_create_info_struct(png);
        if (info == nullptr) {
            std::cerr << "mx: Failed to create PNG info struct.\n";
            png_destroy_write_struct(&png, nullptr);
            std::fclose(file);
            return false;
        }

        if (setjmp(png_jmpbuf(png))) {
            std::cerr << "mx: Error during PNG creation.\n";
            png_destroy_write_struct(&png, &info);
            std::fclose(file);
            return false;
        }

        png_init_io(png, file);
        png_set_IHDR(png,
                     info,
                     width,
                     height,
                     bit_depth,
                     PNG_COLOR_TYPE_RGBA,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);

        if (bit_depth == 16 && swap_16bit_endianness) {
            png_set_swap(png);
        }

        png_write_info(png, info);

        std::vector<png_bytep> rows(static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            rows[static_cast<std::size_t>(y)] = const_cast<png_bytep>(pixels + (y * pitch));
        }

        png_write_image(png, rows.data());
        png_write_end(png, nullptr);

        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return true;
    }

    SDL_Surface *LoadPNG(const char *file) {
        if (!HasPngExtension(file)) {
            return nullptr;
        }

        FILE *fp = std::fopen(file, "rb");
        if (fp == nullptr) {
            std::cerr << "Failed to open PNG file: " << file << "\n";
            return nullptr;
        }

        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (png == nullptr) {
            std::fclose(fp);
            return nullptr;
        }

        png_infop info = png_create_info_struct(png);
        if (info == nullptr) {
            png_destroy_read_struct(&png, nullptr, nullptr);
            std::fclose(fp);
            return nullptr;
        }

        if (setjmp(png_jmpbuf(png))) {
            png_destroy_read_struct(&png, &info, nullptr);
            std::fclose(fp);
            return nullptr;
        }

        png_init_io(png, fp);
        png_read_info(png, info);

        const int width = static_cast<int>(png_get_image_width(png, info));
        const int height = static_cast<int>(png_get_image_height(png, info));
        const png_byte color_type = png_get_color_type(png, info);
        const png_byte bit_depth = png_get_bit_depth(png, info);

        if (width <= 0 || height <= 0) {
            png_destroy_read_struct(&png, &info, nullptr);
            std::fclose(fp);
            return nullptr;
        }

        if (bit_depth == 16) {
            png_set_strip_16(png);
        }

        if (color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_palette_to_rgb(png);
        }

        if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
            png_set_tRNS_to_alpha(png);
        }

        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(png);
        }

        if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
            color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
        }

        png_read_update_info(png, info);

        SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            png_destroy_read_struct(&png, &info, nullptr);
            std::fclose(fp);
            return nullptr;
        }

        const png_size_t row_bytes = png_get_rowbytes(png, info);
        if (row_bytes > static_cast<png_size_t>(surface->pitch)) {
            std::cerr << "mx: Unexpected PNG row stride mismatch for file: " << file << "\n";
            SDL_DestroySurface(surface);
            png_destroy_read_struct(&png, &info, nullptr);
            std::fclose(fp);
            return nullptr;
        }

        auto *pixels = static_cast<std::uint8_t *>(surface->pixels);
        std::vector<png_bytep> row_pointers(static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            row_pointers[static_cast<std::size_t>(y)] = pixels + (y * surface->pitch);
        }

        png_read_image(png, row_pointers.data());
        png_read_end(png, nullptr);

        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return surface;
    }

    bool SavePNG(SDL_Texture *texture, SDL_Renderer *renderer, const char *filename) {
        if (texture == nullptr || renderer == nullptr || filename == nullptr) {
            std::cerr << "mx: Invalid SavePNG parameters.\n";
            return false;
        }

        float width_f = 0.0F;
        float height_f = 0.0F;
        if (!SDL_GetTextureSize(texture, &width_f, &height_f)) {
            std::cerr << "mx: Failed to query texture size: " << SDL_GetError() << "\n";
            return false;
        }

        if (!std::isfinite(width_f) || !std::isfinite(height_f) || width_f <= 0.0F || height_f <= 0.0F) {
            std::cerr << "mx: Invalid texture size while saving PNG.\n";
            return false;
        }

        const int width = static_cast<int>(width_f);
        const int height = static_cast<int>(height_f);
        if (width <= 0 || height <= 0) {
            std::cerr << "mx: Invalid integer texture size while saving PNG.\n";
            return false;
        }

        SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, texture)) {
            std::cerr << "mx: Failed to set render target: " << SDL_GetError() << "\n";
            return false;
        }

        SDL_Surface *captured = SDL_RenderReadPixels(renderer, nullptr);

        if (!SDL_SetRenderTarget(renderer, previous_target)) {
            std::cerr << "mx: Failed to restore render target: " << SDL_GetError() << "\n";
        }

        if (captured == nullptr) {
            std::cerr << "mx: Failed to read pixels from renderer: " << SDL_GetError() << "\n";
            return false;
        }

        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured_surface(captured, &SDL_DestroySurface);

        SDL_Surface *rgba32_surface_raw = captured_surface.get();
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> converted_surface(nullptr, &SDL_DestroySurface);
        if (captured_surface->format != SDL_PIXELFORMAT_RGBA32) {
            rgba32_surface_raw = SDL_ConvertSurface(captured_surface.get(), SDL_PIXELFORMAT_RGBA32);
            if (rgba32_surface_raw == nullptr) {
                std::cerr << "mx: Failed to convert surface to RGBA32: " << SDL_GetError() << "\n";
                return false;
            }
            converted_surface.reset(rgba32_surface_raw);
        }

        const auto *pixels = static_cast<const std::uint8_t *>(rgba32_surface_raw->pixels);
        return WriteRgbaPng(filename,
                            pixels,
                            rgba32_surface_raw->w,
                            rgba32_surface_raw->h,
                            rgba32_surface_raw->pitch,
                            8,
                            false);
    }

    bool SaveRawBytes(const char *filename, const void *buffer, size_t w, size_t h, size_t bpp) {
        if (filename == nullptr || buffer == nullptr || bpp == 0U) {
            std::cerr << "mx: Invalid SaveRawBytes parameters.\n";
            return false;
        }

        if (w != 0U && h > (std::numeric_limits<size_t>::max() / w)) {
            std::cerr << "mx: Raw byte size overflow while writing: " << filename << "\n";
            return false;
        }

        const size_t pixels = w * h;
        if (pixels != 0U && bpp > (std::numeric_limits<size_t>::max() / pixels)) {
            std::cerr << "mx: Raw byte size overflow while writing: " << filename << "\n";
            return false;
        }

        const size_t size = pixels * bpp;
        FILE *fptr = std::fopen(filename, "wb");
        if (fptr == nullptr) {
            std::cerr << "mx: Failed to open output file: " << filename << "\n";
            return false;
        }

        const size_t written = std::fwrite(buffer, sizeof(unsigned char), size, fptr);
        if (written != size) {
            std::cerr << "mx: Error writing frame: " << filename << "\n";
            std::fclose(fptr);
            return false;
        }

        std::fclose(fptr);
        return true;
    }

    bool SavePNG_RGBA(const char *filename, void *buffer, int w, int h) {
        const int pitch = w * 4;
        return WriteRgbaPng(filename,
                            static_cast<const std::uint8_t *>(buffer),
                            w,
                            h,
                            pitch,
                            8,
                            false);
    }

    bool SavePNG_RGBA16(const char *filename, const void *buffer, int w, int h) {
        const int pitch = w * 8;
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
        constexpr bool swap_16bit_endianness = true;
#else
        constexpr bool swap_16bit_endianness = false;
#endif
        return WriteRgbaPng(filename,
                            static_cast<const std::uint8_t *>(buffer),
                            w,
                            h,
                            pitch,
                            16,
                            swap_16bit_endianness);
    }
} // namespace mxvk
