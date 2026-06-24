#ifndef MATRIX_RAIN_H
#define MATRIX_RAIN_H

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "mxvk/mxvk.hpp"

namespace matrix {
    struct RainConfig {
        std::string font_path;
        std::string color;
        int font_size = 22;
        std::vector<Uint32> symbols{};
        int glyph_levels = 7;
        int min_cell_width = 16;
        int min_cell_height = 24;
        int cell_padding_width = 3;
        int cell_padding_height = 2;
        float fade_base = 0.50f;
        float fade_exponent = 8.0f;
        float fade_red_scale = 0.72f;
        float fade_green_scale = 1.0f;
        float fade_blue_scale = 0.55f;
        int head_glow_alpha = 110;
        int near_head_glow_alpha = 72;
        int trail_glow_alpha = 34;
        int near_head_tail_threshold = 2;
        int initial_min_tail_rows = 6;
        int reset_min_tail_rows = 8;
        float initial_head_margin = 1.5f;
        float reset_head_margin = 0.8f;
        float initial_speed_min = 9.0f;
        float initial_speed_max = 26.0f;
        float reset_speed_min = 9.0f;
        float reset_speed_max = 27.0f;
        int initial_length_min = 12;
        int initial_length_max = 38;
        int reset_length_min = 10;
        int reset_length_max = 42;
        int overscan_rows = 3;
        int surface_width = 0;
        int surface_height = 0;
        float surface_scale = 1.0f;
        std::function<SDL_Color(int)> level_color{};
        std::function<SDL_Color()> head_color{};
    };

    RainConfig make_matrix_rain_config(const std::string &asset_root, bool binary_glyph_mode);

    class Rain {
      public:
        Rain(mxvk::VK_Window &window, RainConfig config);
        explicit Rain(RainConfig config);
        Rain(mxvk::VK_Window &window, const std::string &asset_root, bool binary_glyph_mode);
        Rain(const std::string &asset_root, bool binary_glyph_mode);
        ~Rain();

        Rain(const Rain &) = delete;
        Rain &operator=(const Rain &) = delete;
        Rain(Rain &&) = delete;
        Rain &operator=(Rain &&) = delete;

        void event(SDL_Event &e);
        void resize(mxvk::VK_Window &window);
        void resize(int width, int height);
        void update(float dt);
        void sync_texture();
        void render();
        void set_opacity(float opacity);
        void reset();
        void update_and_render(mxvk::VK_Window &window);
        void on_swapchain_recreated(mxvk::VK_Window &window);

        static SDL_Color matrix_color(int level);
        static SDL_Color head_color();
        static std::vector<Uint32> full_symbol_set();
        static std::vector<Uint32> binary_symbol_set();

        [[nodiscard]] mxvk::VK_Sprite *sprite() const;
        [[nodiscard]] SDL_Surface *surface() const;
        [[nodiscard]] const void *pixels() const;
        [[nodiscard]] int width() const;
        [[nodiscard]] int height() const;
        [[nodiscard]] int pitch() const;

      private:
        enum class SurfaceMode {
            explicit_default,
            window_driven,
        };

        struct SurfaceDeleter {
            void operator()(SDL_Surface *surface) const;
        };

        struct TtfDeleter {
            void operator()(TTF_Font *font) const;
        };

        using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;
        using FontPtr = std::unique_ptr<TTF_Font, TtfDeleter>;

        struct Glyph {
            Uint32 codepoint = 0;
            std::vector<SurfacePtr> levels{};
        };

        struct Stream {
            float head = 0.0f;
            float speed = 0.0f;
            int length = 0;
            int glyphOffset = 0;
            int shimmer = 0;
        };

        void load_glyphs();
        void rebuild_for_extent(mxvk::VK_Window &window);
        void randomize_streams();
        void update_rain(float dt);
        void reset_stream(Stream &stream);
        void fade_canvas(float dt);
        void draw_stream(int column, const Stream &stream);
        void clear_canvas();

        Rain(RainConfig config, SurfaceMode mode);

        RainConfig config;
        FontPtr font;
        SurfacePtr canvas;
        SurfacePtr background;
        mxvk::VK_Sprite *rain_sprite = nullptr;
        std::vector<Glyph> glyphs;
        std::vector<Stream> streams;
        std::mt19937 rng;
        std::chrono::steady_clock::time_point last_frame{};
        int columns = 0;
        int rows = 0;
        int cell_w = 20;
        int cell_h = 28;
        int frame_counter = 0;
        float opacity = 1.0f;
        bool ttf_acquired = false;
    };
} // namespace matrix

#endif
