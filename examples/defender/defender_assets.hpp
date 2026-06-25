#ifndef DEFENDER_ASSETS_HPP
#define DEFENDER_ASSETS_HPP

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace defender {

    struct SpriteAlphaBounds;

    [[nodiscard]] SpriteAlphaBounds calculate_alpha_bounds(SDL_Surface *surface, std::uint8_t threshold = 5);
    [[nodiscard]] SDL_Surface *load_light_background_png(const std::string &path);

} // namespace defender

#endif
