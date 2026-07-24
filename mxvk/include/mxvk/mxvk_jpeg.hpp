/**
 * @file mxvk_jpeg.hpp
 * @brief JPEG image loading and saving utilities.
 */
#pragma once

#if defined(MXVK_WITH_JPEG) || defined(WITH_JPEG)

#include <SDL3/SDL.h>

namespace mxvk {

    /**
     * @class VK_JPEG
     * @brief Static JPEG utility helpers for SDL surfaces and textures.
     */
    class VK_JPEG {
      public:
        /**
         * @brief Load a JPEG file into an SDL surface.
         * @param filename Path to the JPEG file.
         * @return Newly allocated `SDL_Surface`, or `nullptr` on failure.
         */
        static SDL_Surface *Load(const char *filename);

        /**
         * @brief Save an SDL texture as a JPEG file.
         * @param texture Source texture.
         * @param renderer Renderer used to read back texture pixels.
         * @param filename Destination file path.
         * @param quality JPEG quality in range [0, 100].
         * @return `true` on success, `false` on failure.
         */
        static bool SaveTexture(SDL_Texture *texture, SDL_Renderer *renderer, const char *filename, int quality = 90);

        /**
         * @brief Save an SDL surface as a JPEG file.
         * @param surface Source surface.
         * @param filename Destination file path.
         * @param quality JPEG quality in range [0, 100].
         * @return `true` on success, `false` on failure.
         */
        static bool SaveSurface(const SDL_Surface *surface, const char *filename, int quality = 90);
    };

    // Backward-compatible free-function wrappers.
    SDL_Surface *LoadJPEG(const char *filename);
    bool SaveJPEG(SDL_Texture *texture, SDL_Renderer *renderer, const char *filename, int quality = 90);

} // namespace mxvk

namespace jpeg {
    // Legacy namespace compatibility.
    inline SDL_Surface *LoadJPEG(const char *filename) {
        return mxvk::VK_JPEG::Load(filename);
    }

    inline bool SaveJPEG(SDL_Renderer *renderer, SDL_Texture *texture, const char *filename, int quality = 90) {
        return mxvk::VK_JPEG::SaveTexture(texture, renderer, filename, quality);
    }
} // namespace jpeg

#endif
