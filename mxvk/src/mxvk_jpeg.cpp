/**
 * @file mxvk_jpeg.cpp
 * @brief Implementation of mxvk JPEG loading and saving utilities.
 */

#include "mxvk/mxvk_jpeg.hpp"

#if defined(MXVK_WITH_JPEG) || defined(WITH_JPEG)

#include <jpeglib.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

namespace mxvk {

    namespace {

        struct JpegErrorManager {
            jpeg_error_mgr pub{};
            std::jmp_buf jump_buffer{};
        };

        using JpegErrorPtr = JpegErrorManager *;

        void JpegErrorExit(j_common_ptr cinfo) {
            auto *err = reinterpret_cast<JpegErrorPtr>(cinfo->err);
            (*cinfo->err->output_message)(cinfo);
            std::longjmp(err->jump_buffer, 1);
        }

        [[nodiscard]] bool WriteJpegFromRgb24Surface(const SDL_Surface *surface,
                                                     const char *filename,
                                                     const int quality) {
            if (surface == nullptr || filename == nullptr) {
                std::cerr << "mxvk: Invalid JPEG save parameters.\n";
                return false;
            }

            const int clamped_quality = std::clamp(quality, 0, 100);

            FILE *file = std::fopen(filename, "wb");
            if (file == nullptr) {
                std::cerr << "mxvk: Failed to open JPEG output: " << filename << "\n";
                return false;
            }

            jpeg_compress_struct cinfo{};
            jpeg_error_mgr jerr{};
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_compress(&cinfo);
            jpeg_stdio_dest(&cinfo, file);

            cinfo.image_width = surface->w;
            cinfo.image_height = surface->h;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;

            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, clamped_quality, TRUE);
            jpeg_start_compress(&cinfo, TRUE);

            while (cinfo.next_scanline < cinfo.image_height) {
                JSAMPROW row = static_cast<JSAMPROW>(
                    static_cast<void *>(static_cast<Uint8 *>(surface->pixels) + (cinfo.next_scanline * surface->pitch)));
                jpeg_write_scanlines(&cinfo, &row, 1);
            }

            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            std::fclose(file);
            return true;
        }

    } // namespace

    SDL_Surface *VK_JPEG::Load(const char *filename) {
        if (filename == nullptr) {
            return nullptr;
        }

        FILE *file = std::fopen(filename, "rb");
        if (file == nullptr) {
            return nullptr;
        }

        jpeg_decompress_struct cinfo{};
        JpegErrorManager jerr{};
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = JpegErrorExit;

        if (setjmp(jerr.jump_buffer) != 0) {
            jpeg_destroy_decompress(&cinfo);
            std::fclose(file);
            return nullptr;
        }

        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, file);
        jpeg_read_header(&cinfo, TRUE);
        jpeg_start_decompress(&cinfo);

        const int width = static_cast<int>(cinfo.output_width);
        const int height = static_cast<int>(cinfo.output_height);
        if (width <= 0 || height <= 0) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            std::fclose(file);
            return nullptr;
        }

        // Decode into tightly packed RGB24 first and then convert to RGBA32 to
        // align with MXVK's texture upload path expectations.
        SDL_Surface *rgb_surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24);
        if (rgb_surface == nullptr) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            std::fclose(file);
            return nullptr;
        }

        while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW row = static_cast<JSAMPROW>(
                static_cast<void *>(static_cast<Uint8 *>(rgb_surface->pixels) + (cinfo.output_scanline * rgb_surface->pitch)));
            jpeg_read_scanlines(&cinfo, &row, 1);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        std::fclose(file);

        SDL_Surface *rgba_surface = SDL_ConvertSurface(rgb_surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(rgb_surface);
        return rgba_surface;
    }

    bool VK_JPEG::SaveTexture(SDL_Texture *texture, SDL_Renderer *renderer, const char *filename, const int quality) {
        if (texture == nullptr || renderer == nullptr || filename == nullptr) {
            std::cerr << "mxvk: Invalid SaveTexture JPEG parameters.\n";
            return false;
        }

        SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
        if (!SDL_SetRenderTarget(renderer, texture)) {
            std::cerr << "mxvk: Failed to set render target for JPEG save: " << SDL_GetError() << "\n";
            return false;
        }

        SDL_Surface *captured = SDL_RenderReadPixels(renderer, nullptr);

        if (!SDL_SetRenderTarget(renderer, previous_target)) {
            std::cerr << "mxvk: Failed to restore render target after JPEG save: " << SDL_GetError() << "\n";
        }

        if (captured == nullptr) {
            std::cerr << "mxvk: Failed to read texture pixels for JPEG save: " << SDL_GetError() << "\n";
            return false;
        }

        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> captured_surface(captured, &SDL_DestroySurface);
        return SaveSurface(captured_surface.get(), filename, quality);
    }

    bool VK_JPEG::SaveSurface(const SDL_Surface *surface, const char *filename, const int quality) {
        if (surface == nullptr || filename == nullptr) {
            return false;
        }

        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgb_surface(nullptr, &SDL_DestroySurface);
        const SDL_Surface *source_surface = surface;

        if (surface->format != SDL_PIXELFORMAT_RGB24) {
            SDL_Surface *converted = SDL_ConvertSurface(const_cast<SDL_Surface *>(surface), SDL_PIXELFORMAT_RGB24);
            if (converted == nullptr) {
                std::cerr << "mxvk: Failed to convert surface to RGB24 for JPEG save: " << SDL_GetError() << "\n";
                return false;
            }
            rgb_surface.reset(converted);
            source_surface = rgb_surface.get();
        }

        return WriteJpegFromRgb24Surface(source_surface, filename, quality);
    }

    SDL_Surface *LoadJPEG(const char *filename) {
        return VK_JPEG::Load(filename);
    }

    bool SaveJPEG(SDL_Texture *texture, SDL_Renderer *renderer, const char *filename, const int quality) {
        return VK_JPEG::SaveTexture(texture, renderer, filename, quality);
    }

} // namespace mxvk

#endif