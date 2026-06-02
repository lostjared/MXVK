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

        /** @brief Append one line to console output. */
        void printLine(const std::string &line);

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

        /** @return Current input buffer. */
        [[nodiscard]] const std::string &inputBuffer() const noexcept;

      private:
        void submitInput();
        void historyUp();
        void historyDown();
        void scrollUp(std::size_t amount = 1);
        void scrollDown(std::size_t amount = 1);
        void ensurePanelSprite();
        void ensureCursorSprite();
        void updatePanelLayout();
        [[nodiscard]] std::size_t maxScrollOffset() const noexcept;
        [[nodiscard]] static std::vector<std::string> tokenize(const std::string &line);
        [[nodiscard]] bool handleDefaultCommand(const std::vector<std::string> &args, std::ostream &output);

        VK_Window *window_ = nullptr;
        VK_Sprite *panel_sprite_ = nullptr;
        VK_Sprite *cursor_sprite_ = nullptr;
        bool visible_ = false;
        bool fade_active_ = false;
        float fade_alpha_ = 0.0f;
        float fade_start_alpha_ = 0.0f;
        float fade_target_alpha_ = 0.0f;
        Uint64 fade_start_ns_ = 0;
        Uint64 fade_duration_ns_ = 220000000ULL;
        bool cursor_visible_ = true;
        Uint64 last_cursor_toggle_ns_ = 0;
        std::size_t cursor_pos_ = 0;
        int panel_x_ = 12;
        int panel_y_ = 12;
        int panel_w_ = 640;
        int panel_h_ = 320;
        std::size_t scroll_offset_ = 0;
        std::size_t last_visible_line_count_ = 16;
        bool follow_tail_ = true;
        std::string prompt_ = "> ";
        std::string input_{};
        std::deque<std::string> lines_{};
        std::vector<std::string> history_{};
        int history_index_ = -1;
        std::size_t max_lines_ = 300;
        std::size_t max_visible_lines_ = 18;
        SDL_Color text_color_{220, 235, 255, 255};
        SDL_Color prompt_color_{120, 220, 160, 255};
        SDL_Color info_color_{170, 170, 210, 255};
        CommandCallback callback_{};
    };

    using Console = VK_Console;

} // namespace mxvk

namespace console {
    using Console = mxvk::VK_Console;
} // namespace console

#endif
