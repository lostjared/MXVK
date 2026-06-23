#include "rain.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <optional>
#include <mutex>
#include <utility>

namespace matrix {
    namespace {
        constexpr int default_surface_width = 1280;
        constexpr int default_surface_height = 720;

        std::mutex ttf_mutex;
        std::size_t ttf_refcount = 0;

        bool acquire_ttf() {
            std::lock_guard<std::mutex> lock(ttf_mutex);
            if (ttf_refcount == 0 && !TTF_Init()) {
                return false;
            }
            ++ttf_refcount;
            return true;
        }

        void release_ttf() {
            std::lock_guard<std::mutex> lock(ttf_mutex);
            if (ttf_refcount == 0) {
                return;
            }
            --ttf_refcount;
            if (ttf_refcount == 0) {
                TTF_Quit();
            }
        }

        std::string trim_copy(const std::string &value) {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return {};
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        std::optional<Uint8> parse_u8_component(const std::string &value, int base) {
            int parsed = 0;
            const std::string trimmed = trim_copy(value);
            const char *begin = trimmed.data();
            const char *end = trimmed.data() + trimmed.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed, base);
            if (result.ec != std::errc{} || result.ptr != end || parsed < 0 || parsed > 255) {
                return std::nullopt;
            }
            return static_cast<Uint8>(parsed);
        }

        std::optional<SDL_Color> parse_color_spec(const std::string &spec) {
            const std::string value = trim_copy(spec);
            if (value.empty()) {
                return std::nullopt;
            }

            if (value.front() == '#') {
                if (value.size() != 7) {
                    return std::nullopt;
                }
                const auto r = parse_u8_component(value.substr(1, 2), 16);
                const auto g = parse_u8_component(value.substr(3, 2), 16);
                const auto b = parse_u8_component(value.substr(5, 2), 16);
                if (!r || !g || !b) {
                    return std::nullopt;
                }
                return SDL_Color{*r, *g, *b, 255};
            }

            const std::size_t first = value.find(',');
            if (first == std::string::npos) {
                return std::nullopt;
            }
            const std::size_t second = value.find(',', first + 1);
            if (second == std::string::npos || value.find(',', second + 1) != std::string::npos) {
                return std::nullopt;
            }

            const auto r = parse_u8_component(value.substr(0, first), 10);
            const auto g = parse_u8_component(value.substr(first + 1, second - first - 1), 10);
            const auto b = parse_u8_component(value.substr(second + 1), 10);
            if (!r || !g || !b) {
                return std::nullopt;
            }
            return SDL_Color{*r, *g, *b, 255};
        }

        std::function<SDL_Color(int)> make_tinted_level_color(SDL_Color base_color) {
            return [base_color](int level) {
                constexpr int max_level = 7;
                const float t = std::clamp(static_cast<float>(level) / static_cast<float>(max_level), 0.0f, 1.0f);
                const Uint8 r = static_cast<Uint8>(std::lerp(0.0f, static_cast<float>(base_color.r), std::pow(t, 1.0f)));
                const Uint8 g = static_cast<Uint8>(std::lerp(0.0f, static_cast<float>(base_color.g), std::pow(t, 1.0f)));
                const Uint8 b = static_cast<Uint8>(std::lerp(0.0f, static_cast<float>(base_color.b), std::pow(t, 1.0f)));
                const Uint8 a = static_cast<Uint8>(std::lerp(95.0f, 235.0f, t));
                return SDL_Color{r, g, b, a};
            };
        }

        std::function<SDL_Color()> make_tinted_head_color(SDL_Color base_color) {
            return [base_color]() {
                return SDL_Color{base_color.r, base_color.g, base_color.b, 255};
            };
        }
    } // namespace

    void Rain::SurfaceDeleter::operator()(SDL_Surface *surface) const {
        if (surface != nullptr) {
            SDL_DestroySurface(surface);
        }
    }

    void Rain::TtfDeleter::operator()(TTF_Font *font) const {
        if (font != nullptr) {
            TTF_CloseFont(font);
        }
    }

    SDL_Color Rain::matrix_color(int level) {
        constexpr int max_level = 7;
        const float t = std::clamp(static_cast<float>(level) / static_cast<float>(max_level), 0.0f, 1.0f);
        const Uint8 r = static_cast<Uint8>(std::lerp(0.0f, 48.0f, std::pow(t, 2.9f)));
        const Uint8 g = static_cast<Uint8>(std::lerp(42.0f, 205.0f, std::pow(t, 0.82f)));
        const Uint8 b = static_cast<Uint8>(std::lerp(0.0f, 24.0f, std::pow(t, 3.1f)));
        const Uint8 a = static_cast<Uint8>(std::lerp(95.0f, 235.0f, t));
        return SDL_Color{r, g, b, a};
    }

    SDL_Color Rain::head_color() {
        return SDL_Color{208, 255, 200, 255};
    }

    std::vector<Uint32> Rain::full_symbol_set() {
        return {
            0xFF66, 0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D,
            0xFF6E, 0xFF6F, 0xFF71, 0xFF72, 0xFF73, 0xFF74, 0xFF75, 0xFF76,
            0xFF77, 0xFF78, 0xFF79, 0xFF7A, 0xFF7B, 0xFF7C, 0xFF7D, 0xFF7E,
            0xFF7F, 0xFF80, 0xFF81, 0xFF82, 0xFF83, 0xFF84, 0xFF85, 0xFF86,
            0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C, 0xFF8D, 0xFF8E,
            0xFF8F, 0xFF90, 0xFF91, 0xFF92, 0xFF93, 0xFF94, 0xFF95, 0xFF96,
            0xFF97, 0xFF98, 0xFF99, 0xFF9A, 0xFF9B, 0xFF9C, 0xFF9D};
    }

    std::vector<Uint32> Rain::binary_symbol_set() {
        return {U'0', U'1'};
    }

    RainConfig make_matrix_rain_config(const std::string &asset_root, bool binary_glyph_mode) {
        RainConfig config;
        config.font_path = asset_root + "/data/NotoSansCJK-Bold.ttc";
        config.symbols = binary_glyph_mode ? Rain::binary_symbol_set() : Rain::full_symbol_set();
        config.level_color = &Rain::matrix_color;
        config.head_color = &Rain::head_color;
        return config;
    }

    Rain::Rain(mxvk::VK_Window &window, RainConfig rain_config)
        : Rain(std::move(rain_config), SurfaceMode::window_driven) {
        resize(window);
    }

    Rain::Rain(RainConfig rain_config)
        : Rain(std::move(rain_config), SurfaceMode::explicit_default) {}

    Rain::Rain(RainConfig rain_config, SurfaceMode mode)
        : config(std::move(rain_config)), rng(std::random_device{}()) {
        if (config.font_path.empty()) {
            throw mxvk::Exception("Matrix rain requires a font_path");
        }
        if (config.symbols.empty()) {
            throw mxvk::Exception("Matrix rain requires at least one symbol");
        }
        if (!config.level_color) {
            config.level_color = &Rain::matrix_color;
        }
        if (!config.head_color) {
            config.head_color = &Rain::head_color;
        }
        if (!config.color.empty()) {
            const std::optional<SDL_Color> parsed_color = parse_color_spec(config.color);
            if (!parsed_color) {
                throw mxvk::Exception("Invalid matrix rain color: " + config.color + " (expected #RRGGBB or R,G,B)");
            }
            config.level_color = make_tinted_level_color(*parsed_color);
            config.head_color = make_tinted_head_color(*parsed_color);
        }
        if (mode == SurfaceMode::explicit_default && config.surface_width <= 0 && config.surface_height <= 0) {
            config.surface_width = default_surface_width;
            config.surface_height = default_surface_height;
        }
        if (!acquire_ttf()) {
            throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
        }
        ttf_acquired = true;

        font.reset(TTF_OpenFont(config.font_path.c_str(), config.font_size));
        if (!font) {
            release_ttf();
            ttf_acquired = false;
            throw mxvk::Exception("Failed to load matrix font: " + config.font_path + ": " + std::string(SDL_GetError()));
        }

        TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);
        load_glyphs();
        if (config.surface_width > 0 && config.surface_height > 0) {
            resize(config.surface_width, config.surface_height);
        }
        last_frame = std::chrono::steady_clock::now();
    }

    Rain::Rain(mxvk::VK_Window &window, const std::string &asset_root_path, const bool binary_glyph_mode)
        : Rain(window, make_matrix_rain_config(asset_root_path, binary_glyph_mode)) {}

    Rain::Rain(const std::string &asset_root_path, const bool binary_glyph_mode)
        : Rain(make_matrix_rain_config(asset_root_path, binary_glyph_mode), SurfaceMode::explicit_default) {}

    Rain::~Rain() {
        glyphs.clear();
        font.reset();
        background.reset();
        canvas.reset();
        if (ttf_acquired) {
            release_ttf();
        }
    }

    void Rain::event(SDL_Event &e) {
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
            randomize_streams();
            clear_canvas();
        }
    }

    void Rain::resize(mxvk::VK_Window &window) {
        rebuild_for_extent(window);
    }

    void Rain::resize(int width, int height) {
        if (width <= 0 || height <= 0 || (canvas != nullptr && canvas->w == width && canvas->h == height)) {
            return;
        }

        canvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (canvas == nullptr) {
            throw mxvk::Exception("Failed to create matrix canvas: " + std::string(SDL_GetError()));
        }
        clear_canvas();

        columns = std::max(1, (width + cell_w - 1) / cell_w);
        rows = std::max(1, (height + cell_h - 1) / cell_h);
        streams.assign(columns, {});
        randomize_streams();
    }

    void Rain::update(float dt) {
        dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);
        if (canvas == nullptr || glyphs.empty()) {
            return;
        }
        update_rain(dt);
    }

    void Rain::sync_texture() {
        if (rain_sprite == nullptr || canvas == nullptr) {
            return;
        }
        rain_sprite->updateTexture(canvas->pixels, canvas->w, canvas->h, canvas->pitch);
    }

    void Rain::render() {
        sync_texture();
        if (rain_sprite == nullptr || canvas == nullptr) {
            return;
        }
        rain_sprite->drawSpriteRect(0, 0, canvas->w, canvas->h);
    }

    void Rain::update_and_render(mxvk::VK_Window &window) {
        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_frame).count();
        last_frame = now;
        resize(window);
        update(dt);
        render();
    }

    void Rain::on_swapchain_recreated(mxvk::VK_Window &window) {
        resize(window);
    }

    mxvk::VK_Sprite *Rain::sprite() const {
        return rain_sprite;
    }

    SDL_Surface *Rain::surface() const {
        return canvas.get();
    }

    const void *Rain::pixels() const {
        return canvas != nullptr ? canvas->pixels : nullptr;
    }

    int Rain::width() const {
        return canvas != nullptr ? canvas->w : 0;
    }

    int Rain::height() const {
        return canvas != nullptr ? canvas->h : 0;
    }

    int Rain::pitch() const {
        return canvas != nullptr ? canvas->pitch : 0;
    }

    void Rain::load_glyphs() {
        const std::vector<Uint32> &symbols = config.symbols;
        int max_glyph_w = 0;
        int max_glyph_h = 0;

        glyphs.reserve(symbols.size());
        for (const Uint32 codepoint : symbols) {
            Glyph glyph;
            glyph.codepoint = codepoint;
            glyph.levels.reserve(config.glyph_levels + 1);
            for (int level = 0; level <= config.glyph_levels; ++level) {
                SDL_Surface *rendered = TTF_RenderGlyph_Blended(font.get(), codepoint, config.level_color(level));
                if (rendered == nullptr) {
                    continue;
                }
                SurfacePtr converted(SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32));
                SDL_DestroySurface(rendered);
                if (converted != nullptr) {
                    SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
                    max_glyph_w = std::max(max_glyph_w, converted->w);
                    max_glyph_h = std::max(max_glyph_h, converted->h);
                    glyph.levels.emplace_back(std::move(converted));
                }
            }
            SDL_Surface *rendered_head = TTF_RenderGlyph_Blended(font.get(), codepoint, config.head_color());
            if (rendered_head != nullptr) {
                SurfacePtr converted(SDL_ConvertSurface(rendered_head, SDL_PIXELFORMAT_RGBA32));
                SDL_DestroySurface(rendered_head);
                if (converted != nullptr) {
                    SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
                    max_glyph_w = std::max(max_glyph_w, converted->w);
                    max_glyph_h = std::max(max_glyph_h, converted->h);
                    glyph.levels.emplace_back(std::move(converted));
                }
            }
            if (glyph.levels.size() == static_cast<std::size_t>(config.glyph_levels + 2)) {
                glyphs.emplace_back(std::move(glyph));
            }
        }

        if (glyphs.empty()) {
            throw mxvk::Exception("Matrix demo could not render any glyphs from the bundled font");
        }

        cell_w = std::max(config.min_cell_width, max_glyph_w + config.cell_padding_width);
        cell_h = std::max(config.min_cell_height, max_glyph_h + config.cell_padding_height);
    }

    void Rain::rebuild_for_extent(mxvk::VK_Window &window) {
        const VkExtent2D extent = window.getSwapchainExtent();
        const int window_width = static_cast<int>(extent.width);
        const int window_height = static_cast<int>(extent.height);
        if (window_width <= 0 || window_height <= 0) {
            return;
        }

        const int target_width = (config.surface_width > 0)
                                     ? config.surface_width
                                     : std::max(1, static_cast<int>(std::lround(static_cast<float>(window_width) * config.surface_scale)));
        const int target_height = (config.surface_height > 0)
                                      ? config.surface_height
                                      : std::max(1, static_cast<int>(std::lround(static_cast<float>(window_height) * config.surface_scale)));

        const bool resized = (canvas == nullptr || canvas->w != target_width || canvas->h != target_height);
        if (resized) {
            resize(target_width, target_height);
        }

        if (rain_sprite == nullptr) {
            rain_sprite = window.createSprite(canvas.get());
        } else {
            rain_sprite->updateTexture(canvas.get());
        }

        if (resized || streams.empty()) {
            columns = std::max(1, (target_width + cell_w - 1) / cell_w);
            rows = std::max(1, (target_height + cell_h - 1) / cell_h);
            streams.assign(columns, {});
            randomize_streams();
        }
    }

    void Rain::randomize_streams() {
        if (streams.empty() || glyphs.empty()) {
            return;
        }

        std::uniform_real_distribution<float> head_dist(-static_cast<float>(rows) * config.initial_head_margin, 0.0f);
        std::uniform_real_distribution<float> speed_dist(config.initial_speed_min, config.initial_speed_max);
        std::uniform_int_distribution<int> length_dist(config.initial_length_min, config.initial_length_max);
        std::uniform_int_distribution<int> glyph_dist(0, static_cast<int>(glyphs.size() - 1));
        std::uniform_int_distribution<int> shimmer_dist(0, 1200);

        for (Stream &stream : streams) {
            stream.head = head_dist(rng);
            stream.speed = speed_dist(rng);
            stream.length = std::min(std::max(config.initial_min_tail_rows, rows), length_dist(rng));
            stream.glyphOffset = glyph_dist(rng);
            stream.shimmer = shimmer_dist(rng);
        }
    }

    void Rain::update_rain(float dt) {
        fade_canvas(dt);

        for (int column = 0; column < columns; ++column) {
            Stream &stream = streams[column];
            stream.head += stream.speed * dt;
            if (stream.head - static_cast<float>(stream.length) > static_cast<float>(rows + config.overscan_rows)) {
                reset_stream(stream);
            }
            draw_stream(column, stream);
        }
        ++frame_counter;
    }

    void Rain::reset_stream(Stream &stream) {
        std::uniform_real_distribution<float> head_dist(-static_cast<float>(rows) * config.reset_head_margin, -1.0f);
        std::uniform_real_distribution<float> speed_dist(config.reset_speed_min, config.reset_speed_max);
        std::uniform_int_distribution<int> length_dist(config.reset_length_min, config.reset_length_max);
        std::uniform_int_distribution<int> glyph_dist(0, static_cast<int>(glyphs.size() - 1));
        std::uniform_int_distribution<int> shimmer_dist(0, 1200);
        stream.head = head_dist(rng);
        stream.speed = speed_dist(rng);
        stream.length = std::min(std::max(config.reset_min_tail_rows, rows + rows / 4), length_dist(rng));
        stream.glyphOffset = glyph_dist(rng);
        stream.shimmer = shimmer_dist(rng);
    }

    void Rain::fade_canvas(float dt) {
        const float fade = std::pow(config.fade_base, dt * config.fade_exponent);
        if (!SDL_LockSurface(canvas.get())) {
            return;
        }
        auto *pixels = static_cast<Uint8 *>(canvas->pixels);
        for (int y = 0; y < canvas->h; ++y) {
            Uint8 *row = pixels + y * canvas->pitch;
            for (int x = 0; x < canvas->w; ++x) {
                Uint8 *p = row + x * 4;
                p[0] = static_cast<Uint8>(static_cast<float>(p[0]) * fade * config.fade_red_scale);
                p[1] = static_cast<Uint8>(static_cast<float>(p[1]) * fade * config.fade_green_scale);
                p[2] = static_cast<Uint8>(static_cast<float>(p[2]) * fade * config.fade_blue_scale);
                p[3] = 255;
            }
        }
        SDL_UnlockSurface(canvas.get());
    }

    void Rain::draw_stream(int column, const Stream &stream) {
        const int head_row = static_cast<int>(std::floor(stream.head));
        const int x = column * cell_w;
        for (int tail = stream.length; tail >= 0; --tail) {
            const int row = head_row - tail;
            if (row < -1 || row >= rows) {
                continue;
            }
            const float age = static_cast<float>(tail) / static_cast<float>(std::max(1, stream.length));
            int level = config.glyph_levels - static_cast<int>(std::round(age * static_cast<float>(config.glyph_levels + 1)));
            if (tail == 0) {
                level = config.glyph_levels + 1;
            } else if (tail <= config.near_head_tail_threshold) {
                level = config.glyph_levels;
            }
            level = std::clamp(level, 0, config.glyph_levels + 1);
            const int glyph_index = (stream.glyphOffset + row * 17 + column * 11 + stream.shimmer + frame_counter / 3 + tail * 5) %
                                    static_cast<int>(glyphs.size());
            const Glyph &glyph = glyphs[glyph_index < 0 ? glyph_index + static_cast<int>(glyphs.size()) : glyph_index];
            if (glyph.levels.empty()) {
                continue;
            }
            const SDL_Rect clear_rect{x, row * cell_h, cell_w, cell_h};
            SDL_FillSurfaceRect(canvas.get(), &clear_rect, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
            SDL_Surface *surface = glyph.levels[level].get();
            Uint8 previous_alpha = 255;
            SDL_GetSurfaceAlphaMod(surface, &previous_alpha);

            const int glyph_x = x + (cell_w - glyph.levels[level]->w) / 2;
            const int glyph_y = row * cell_h;
            const int glow_alpha = (tail == 0)
                                       ? config.head_glow_alpha
                                       : (tail <= config.near_head_tail_threshold ? config.near_head_glow_alpha
                                                                                   : config.trail_glow_alpha);

            SDL_SetSurfaceAlphaMod(surface, static_cast<Uint8>(glow_alpha));
            SDL_Rect glow_dst{glyph_x - 1, glyph_y, glyph.levels[level]->w, glyph.levels[level]->h};
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glow_dst);
            glow_dst.x = glyph_x + 1;
            glow_dst.y = glyph_y;
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glow_dst);
            glow_dst.x = glyph_x;
            glow_dst.y = glyph_y + 1;
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glow_dst);

            SDL_SetSurfaceAlphaMod(surface, previous_alpha);
            SDL_Rect dst{glyph_x, glyph_y, glyph.levels[level]->w, glyph.levels[level]->h};
            SDL_BlitSurface(surface, nullptr, canvas.get(), &dst);
        }
    }

    void Rain::clear_canvas() {
        if (canvas != nullptr) {
            SDL_FillSurfaceRect(canvas.get(), nullptr, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
        }
    }
} // namespace matrix
