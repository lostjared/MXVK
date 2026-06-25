/**
 * @file mxvk_console.hpp
 * @brief In-game command console for MXVK / Vulkan windows.
 */
#ifndef _MXVK_CONSOLE_H_
#define _MXVK_CONSOLE_H_

#include <SDL3/SDL.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

namespace mxvk {

    class VK_Window;
    class VK_Sprite;

    /**
     * @class VK_Console
     * @brief Lightweight command console renderer for `mxvk::VK_Window`.
     *
     * The console captures keyboard input, keeps command history, renders buffered
     * lines via `VK_Window::printText`, and allows applications to register a
     * command callback.
     */
    class VK_Console {
      public:
        using CommandCallback =
            std::function<bool(VK_Window &window, const std::vector<std::string> &args, std::ostream &output)>;

        VK_Console() = default;

        /** @brief Attach this console to a window and configure font rendering. */
        void attach(VK_Window &window, const std::string &fontPath, int fontSize = 18);

        /** @brief Handle keyboard/text events for the console. */
        void handleEvent(const SDL_Event &event);

        /** @brief Draw the current console overlay. Call once per frame from `proc()`. */
        void draw();

        /**
         * @brief Append one line to console output.
         * @param line Text to append. Embedded newlines are split into separate lines.
         * @param color Optional text color for this output line. Defaults to white.
         */
        void printLine(const std::string &line, SDL_Color color = SDL_Color{255, 255, 255, 255});

        /** @brief Clear all buffered output lines. */
        void clear();

        /** @brief Register a command callback. Return true to indicate the command was handled. */
        void setCommandCallback(CommandCallback callback);

        /** @brief Set the input prompt text. */
        void setPrompt(const std::string &prompt);

        /** @brief Show or hide the console. */
        void setVisible(bool value) noexcept;

        /** @brief Show console. */
        void show() noexcept;

        /** @brief Hide console. */
        void hide() noexcept;

        /** @brief Toggle console visibility. */
        void toggle() noexcept;

        /** @return true when the console is visible. */
        [[nodiscard]] bool isVisible() const noexcept;

        /** @brief Set max number of retained output lines. */
        void setMaxLines(std::size_t maxLines);

        /** @brief Set max number of lines rendered on screen. */
        void setMaxVisibleLines(std::size_t maxVisibleLines);

        /** @brief Select whether console sprite elements use top-left Y coordinates. */
        void setSpriteYOriginTopLeft(bool enabled) noexcept { sprite_y_origin_top_left = enabled; }

        /** @return Current input buffer. */
        [[nodiscard]] const std::string &inputBuffer() const noexcept;

      private:
        struct OutputLine {
            std::string text;
            SDL_Color color;
        };

        void pushOutputLine(std::string line);
        void pushOutputLine(std::string line, SDL_Color color);
        void trimOutputToLimits();
        void submitInput();
        void historyUp();
        void historyDown();
        void scrollUp(std::size_t amount = 1);
        void scrollDown(std::size_t amount = 1);
        void ensurePanelSprite();
        void ensureCursorSprite();
        void ensureScrollSprites();
        void updatePanelLayout();
        void refreshVisibleLineCount();
        void updateScrollbarGeometry();
        void invalidateWrappedCache();
        void ensureWrappedCache(int maxWidth) const;
        [[nodiscard]] int measureTextWidth(const std::string &text) const;
        [[nodiscard]] int usableTextWidth() const;
        [[nodiscard]] std::vector<std::string> wrapTextToWidth(const std::string &text, int maxWidth) const;
        [[nodiscard]] bool isPointInScrollbar(int x, int y) const noexcept;
        [[nodiscard]] bool isPointInScrollbarThumb(int x, int y) const noexcept;
        void updateScrollFromThumbY(int thumbY);
        [[nodiscard]] std::size_t effectiveVisibleLineCount() const noexcept;
        [[nodiscard]] std::size_t maxScrollOffset() const noexcept;
        [[nodiscard]] static std::vector<std::string> tokenize(const std::string &line);
        [[nodiscard]] bool handleDefaultCommand(const std::vector<std::string> &args, std::ostream &output);

        VK_Window *windowPtr = nullptr;
        VK_Sprite *panel_sprite = nullptr;
        VK_Sprite *cursor_sprite = nullptr;
        VK_Sprite *scroll_track_sprite = nullptr;
        VK_Sprite *scroll_thumb_sprite = nullptr;
        bool visible = false;
        bool fade_active = false;
        float fade_alpha = 0.0f;
        float fade_start_alpha = 0.0f;
        float fade_target_alpha = 0.0f;
        Uint64 fade_start_ns = 0;
        Uint64 fade_duration_ns = 220000000ULL;
        bool cursor_visible = true;
        Uint64 last_cursor_toggle_ns = 0;
        std::size_t cursor_pos = 0;
        int panel_x = 12;
        int panel_y = 12;
        int panel_w = 640;
        int panel_h = 320;
        int content_top_y = 0;
        int content_bottom_y = 0;
        int scrollbar_x = 0;
        int scrollbar_y = 0;
        int scrollbar_w = 10;
        int scrollbar_h = 0;
        int scrollbar_thumb_y = 0;
        int scrollbar_thumb_h = 0;
        bool scrollbar_dragging = false;
        bool sprite_y_origin_top_left = false;
        int scrollbar_drag_offset = 0;
        std::size_t scroll_offset = 0;
        std::size_t last_visible_line_count = 16;
        bool follow_tail = true;
        std::string promptText = "> ";
        std::string input{};
        std::deque<OutputLine> lines{};
        std::size_t total_line_chars = 0;
        mutable bool wrapped_cache_dirty = true;
        mutable int wrapped_cache_width = -1;
        mutable std::vector<OutputLine> wrapped_cache{};
        mutable std::size_t wrapped_cache_line_count = 0;
        std::vector<std::string> history{};
        int history_index = -1;
        std::size_t max_lines = 1200;
        std::size_t max_total_chars = 128 * 1024;
        std::size_t max_visible_lines = std::numeric_limits<std::size_t>::max();
        SDL_Color text_color{220, 235, 255, 255};
        SDL_Color prompt_color{120, 220, 160, 255};
        SDL_Color info_color{170, 170, 210, 255};
        CommandCallback commandCallback{};
    };

    using Console = VK_Console;

} // namespace mxvk

namespace console {
    using Console = mxvk::VK_Console;
} // namespace console

#endif
