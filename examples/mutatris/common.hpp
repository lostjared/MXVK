#ifndef MUTATRIS_COMMON_HPP
#define MUTATRIS_COMMON_HPP

#include <SDL3/SDL_stdinc.h>

namespace mutatris {

    constexpr int DESIGN_WIDTH = 1280;
    constexpr int DESIGN_HEIGHT = 720;
    constexpr int BLOCK_WIDTH = 16;
    constexpr int BLOCK_HEIGHT = 16;
    constexpr int BLOCK_DRAW_WIDTH = 14;
    constexpr int BLOCK_DRAW_HEIGHT = 14;
    constexpr int TOP_BOTTOM_GRID_PIXEL_WIDTH = 384;
    constexpr int SIDE_GRID_PIXEL_WIDTH = 448;
    constexpr int GRID_WIDTH = TOP_BOTTOM_GRID_PIXEL_WIDTH / BLOCK_WIDTH;
    constexpr int TOP_GRID_HEIGHT = (DESIGN_HEIGHT / BLOCK_HEIGHT / 2) + 1;
    constexpr int BOTTOM_GRID_HEIGHT = DESIGN_HEIGHT / BLOCK_HEIGHT / 2;
    constexpr int SIDE_GRID_WIDTH = GRID_WIDTH + 1;
    constexpr int SIDE_GRID_HEIGHT = SIDE_GRID_PIXEL_WIDTH / BLOCK_HEIGHT;
    constexpr int CENTER_GRID_X = (DESIGN_WIDTH - TOP_BOTTOM_GRID_PIXEL_WIDTH) / 2;
    constexpr int TOP_GRID_Y = 0;
    constexpr int BOTTOM_GRID_Y = DESIGN_HEIGHT - (BOTTOM_GRID_HEIGHT * BLOCK_HEIGHT);
    constexpr int SIDE_GRID_Y = (DESIGN_HEIGHT - (SIDE_GRID_WIDTH * BLOCK_WIDTH)) / 2;
    constexpr int RIGHT_GRID_X = DESIGN_WIDTH - SIDE_GRID_PIXEL_WIDTH;
    constexpr Uint32 STARTUP_FADE_MS = 2520;
    constexpr Uint32 STARTUP_HOLD_MS = 900;
    constexpr Uint32 KEY_REPEAT_MS = 120;
    constexpr Uint32 CLEAR_ANIMATION_MS = 400;
    constexpr int CLEAR_ANIMATION_FRAMES = 24;
    constexpr int CLEARS_PER_LEVEL = 8;
    constexpr int LEVEL_TIMEOUT_STEP_MS = 100;
    constexpr unsigned int MIN_DROP_TIMEOUT_MS = 125;

    enum class Screen {
        Startup,
        Title,
        Difficulty,
        Playing,
        GameOver,
    };

    struct Block {
        int color = 0;
        Uint32 clearElapsedMs = 0;
    };

} // namespace mutatris

#endif
