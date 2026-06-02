/**
 * @file mxvk_console.cpp
 * @brief Implementation of mxvk::VK_Console.
 */

#include "mxvk/mxvk_console.hpp"

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_sprite.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace mxvk {

    void VK_Console::attach(VK_Window &window, const std::string &fontPath, const int fontSize) {
        window_ = &window;
        window_->setFont(fontPath, fontSize);
        panel_sprite_ = nullptr;
        visible_ = false;
        fade_active_ = false;
        fade_alpha_ = 0.0f;
        fade_start_alpha_ = 0.0f;
        fade_target_alpha_ = 0.0f;
        fade_start_ns_ = 0;
        history_index_ = -1;
        cursor_pos_ = 0;
        input_.clear();
        scroll_offset_ = 0;
        follow_tail_ = true;
    }

    void VK_Console::setCommandCallback(CommandCallback callback) {
        callback_ = std::move(callback);
    }

    void VK_Console::setPrompt(const std::string &prompt) {
        if (!prompt.empty()) {
            prompt_ = prompt;
        }
    }

    void VK_Console::setVisible(const bool value) noexcept {
        if (value) {
            visible_ = true;
            fade_start_alpha_ = fade_alpha_;
            fade_target_alpha_ = 1.0f;
            fade_start_ns_ = SDL_GetTicksNS();
            fade_active_ = true;
            if (window_ != nullptr) {
                SDL_StartTextInput(window_->getSDLWindow());
            }
            follow_tail_ = true;
            scroll_offset_ = 0;
        } else {
            visible_ = false;
            fade_start_alpha_ = fade_alpha_;
            fade_target_alpha_ = 0.0f;
            fade_start_ns_ = SDL_GetTicksNS();
            fade_active_ = true;
            if (window_ != nullptr) {
                SDL_StopTextInput(window_->getSDLWindow());
            }
        }
    }

    void VK_Console::show() noexcept {
        setVisible(true);
    }

    void VK_Console::hide() noexcept {
        setVisible(false);
    }

    void VK_Console::toggle() noexcept {
        if (visible_) {
            hide();
        } else {
            show();
        }
    }

    bool VK_Console::isVisible() const noexcept {
        return visible_ || fade_alpha_ > 0.0f || fade_active_;
    }

    void VK_Console::setMaxLines(const std::size_t maxLines) {
        max_lines_ = std::max<std::size_t>(1, maxLines);
        while (lines_.size() > max_lines_) {
            lines_.pop_front();
        }
        scroll_offset_ = std::min(scroll_offset_, maxScrollOffset());
    }

    void VK_Console::setMaxVisibleLines(const std::size_t maxVisibleLines) {
        max_visible_lines_ = std::max<std::size_t>(1, maxVisibleLines);
    }

    const std::string &VK_Console::inputBuffer() const noexcept {
        return input_;
    }

    void VK_Console::printLine(const std::string &line) {
        lines_.push_back(line);
        while (lines_.size() > max_lines_) {
            lines_.pop_front();
        }
        if (follow_tail_) {
            scroll_offset_ = 0;
        } else {
            scroll_offset_ = std::min(scroll_offset_, maxScrollOffset());
        }
    }

    void VK_Console::clear() {
        lines_.clear();
        input_.clear();
        cursor_pos_ = 0;
        scroll_offset_ = 0;
        follow_tail_ = true;
        history_index_ = -1;
    }

    std::size_t VK_Console::maxScrollOffset() const noexcept {
        if (lines_.size() <= last_visible_line_count_) {
            return 0;
        }
        return lines_.size() - last_visible_line_count_;
    }

    void VK_Console::scrollUp(const std::size_t amount) {
        if (amount == 0) {
            return;
        }

        const std::size_t max_offset = maxScrollOffset();
        if (max_offset == 0) {
            return;
        }

        follow_tail_ = false;
        scroll_offset_ = std::min(max_offset, scroll_offset_ + amount);
    }

    void VK_Console::scrollDown(const std::size_t amount) {
        if (amount == 0) {
            return;
        }

        if (scroll_offset_ <= amount) {
            scroll_offset_ = 0;
            follow_tail_ = true;
            return;
        }

        scroll_offset_ -= amount;
    }

    std::vector<std::string> VK_Console::tokenize(const std::string &line) {
        std::istringstream stream(line);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    bool VK_Console::handleDefaultCommand(const std::vector<std::string> &args, std::ostream &output) {
        if (args.empty()) {
            return true;
        }

        const std::string &cmd = args.front();
        if (cmd == "help") {
            output << "Built-in commands: help, clear";
            return true;
        }
        if (cmd == "clear") {
            clear();
            return true;
        }
        return false;
    }

    void VK_Console::submitInput() {
        if (input_.empty() || window_ == nullptr) {
            input_.clear();
            cursor_pos_ = 0;
            history_index_ = -1;
            return;
        }

        printLine(prompt_ + input_);
        history_.push_back(input_);
        history_index_ = -1;

        std::ostringstream reply;
        const std::vector<std::string> args = tokenize(input_);

        bool handled = handleDefaultCommand(args, reply);
        if (!handled && callback_) {
            handled = callback_(*window_, args, reply);
        }
        if (!handled && !args.empty()) {
            reply << std::format("Unknown command: {}", args.front());
        }

        const std::string out = reply.str();
        if (!out.empty()) {
            printLine(out);
        }

        input_.clear();
        cursor_pos_ = 0;
    }

    void VK_Console::historyUp() {
        if (history_.empty()) {
            return;
        }

        if (history_index_ < 0) {
            history_index_ = static_cast<int>(history_.size()) - 1;
        } else if (history_index_ > 0) {
            --history_index_;
        }

        if (history_index_ >= 0 && history_index_ < static_cast<int>(history_.size())) {
            input_ = history_[static_cast<std::size_t>(history_index_)];
            cursor_pos_ = input_.size();
        }
    }

    void VK_Console::historyDown() {
        if (history_.empty()) {
            return;
        }

        if (history_index_ < 0) {
            return;
        }

        if (history_index_ < static_cast<int>(history_.size()) - 1) {
            ++history_index_;
            input_ = history_[static_cast<std::size_t>(history_index_)];
            cursor_pos_ = input_.size();
        } else {
            history_index_ = -1;
            input_.clear();
            cursor_pos_ = 0;
        }
    }

    void VK_Console::ensurePanelSprite() {
        if (panel_sprite_ != nullptr || window_ == nullptr) {
            return;
        }

        SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return;
        }

        const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, 170));
        try {
            panel_sprite_ = window_->createSprite(surface);
        } catch (...) {
            panel_sprite_ = nullptr;
        }

        SDL_DestroySurface(surface);
    }

    void VK_Console::updatePanelLayout() {
        if (window_ == nullptr) {
            return;
        }

        const VkExtent2D extent = window_->getSwapchainExtent();
        const int screen_w = static_cast<int>(extent.width);
        const int screen_h = static_cast<int>(extent.height);
        if (screen_w <= 0 || screen_h <= 0) {
            return;
        }

        constexpr int horizontal_margin = 10;
        constexpr int top_margin = 8;
        constexpr int bottom_margin = 24;

        panel_x_ = horizontal_margin;
        panel_y_ = top_margin;
        panel_w_ = std::max(300, screen_w - (horizontal_margin * 2));
        panel_h_ = std::clamp((screen_h * 42) / 100, 180, std::max(180, screen_h - bottom_margin));
    }

    void VK_Console::handleEvent(const SDL_Event &event) {
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F3)) {
            toggle();
            return;
        }

        if (!visible_) {
            return;
        }

        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            if (event.wheel.y > 0) {
                scrollUp(static_cast<std::size_t>(event.wheel.y));
            } else if (event.wheel.y < 0) {
                scrollDown(static_cast<std::size_t>(-event.wheel.y));
            }
            return;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            switch (event.key.key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                submitInput();
                return;
            case SDLK_BACKSPACE:
                if (cursor_pos_ > 0 && cursor_pos_ <= input_.size()) {
                    input_.erase(cursor_pos_ - 1, 1);
                    --cursor_pos_;
                }
                return;
            case SDLK_DELETE:
                if (cursor_pos_ < input_.size()) {
                    input_.erase(cursor_pos_, 1);
                }
                return;
            case SDLK_LEFT:
                if (cursor_pos_ > 0) {
                    --cursor_pos_;
                }
                return;
            case SDLK_RIGHT:
                if (cursor_pos_ < input_.size()) {
                    ++cursor_pos_;
                }
                return;
            case SDLK_HOME:
                cursor_pos_ = 0;
                return;
            case SDLK_END:
                follow_tail_ = true;
                scroll_offset_ = 0;
                cursor_pos_ = input_.size();
                return;
            case SDLK_UP:
                historyUp();
                return;
            case SDLK_DOWN:
                historyDown();
                return;
            case SDLK_PAGEUP:
                scrollUp(std::max<std::size_t>(1, last_visible_line_count_ - 1));
                return;
            case SDLK_PAGEDOWN:
                scrollDown(std::max<std::size_t>(1, last_visible_line_count_ - 1));
                return;
            case SDLK_ESCAPE:
                hide();
                return;
            default:
                break;
            }
            return;
        }

        if (event.type == SDL_EVENT_TEXT_INPUT) {
            if (event.text.text[0] != '\0') {
                input_.insert(cursor_pos_, event.text.text);
                cursor_pos_ += SDL_strlen(event.text.text);
            }
        }
    }

    void VK_Console::draw() {
        if (window_ == nullptr) {
            return;
        }

        if (fade_active_) {
            const Uint64 now = SDL_GetTicksNS();
            const double elapsed = static_cast<double>(now - fade_start_ns_);
            const double duration = static_cast<double>(fade_duration_ns_);
            const float t = static_cast<float>(std::clamp(elapsed / std::max(1.0, duration), 0.0, 1.0));
            fade_alpha_ = fade_start_alpha_ + ((fade_target_alpha_ - fade_start_alpha_) * t);
            if (t >= 1.0f) {
                fade_alpha_ = fade_target_alpha_;
                fade_active_ = false;
            }
        }

        if (!(visible_ || fade_alpha_ > 0.0f)) {
            return;
        }

        const float alpha = std::clamp(fade_alpha_, 0.0f, 1.0f);
        if (alpha <= 0.0f) {
            return;
        }

        const auto scaledColor = [alpha](const SDL_Color &src) {
            SDL_Color out = src;
            const int scaled = static_cast<int>(std::lround(static_cast<double>(src.a) * alpha));
            out.a = static_cast<Uint8>(std::clamp(scaled, 0, 255));
            return out;
        };

        updatePanelLayout();
        ensurePanelSprite();
        if (panel_sprite_ != nullptr) {
            const VkExtent2D extent = window_->getSwapchainExtent();
            const int screen_h = static_cast<int>(extent.height);
            const int sprite_y = std::max(0, screen_h - panel_y_ - panel_h_);

            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int panel_alpha = static_cast<int>(std::lround(170.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, static_cast<Uint8>(std::clamp(panel_alpha, 0, 255))));
                panel_sprite_->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            panel_sprite_->drawSpriteRect(panel_x_, sprite_y, panel_w_, panel_h_);
        }

        int glyph_w = 8;
        int glyph_h = 18;
        if (!window_->getTextDimensions("M", glyph_w, glyph_h)) {
            glyph_h = 18;
        }

        const int line_height = std::max(1, glyph_h + 2);
        const int padding = 10;

        int y = panel_y_ + padding;
        window_->printText("MXVK Console (F3 to toggle)", panel_x_ + padding, y, scaledColor(info_color_));
        y += line_height;

        const int input_y = panel_y_ + panel_h_ - padding - line_height;
        const int available_height = std::max(line_height, input_y - y - 6);
        last_visible_line_count_ = std::max<std::size_t>(1, static_cast<std::size_t>(available_height / line_height));
        if (follow_tail_) {
            scroll_offset_ = 0;
        }

        const std::size_t max_offset = maxScrollOffset();
        scroll_offset_ = std::min(scroll_offset_, max_offset);

        const std::size_t visible_lines = std::min(std::min(max_visible_lines_, last_visible_line_count_), lines_.size());
        const std::size_t start = (lines_.size() > visible_lines) ? (lines_.size() - visible_lines - scroll_offset_) : 0;
        const std::size_t end = std::min(lines_.size(), start + visible_lines);
        for (std::size_t i = start; i < end; ++i) {
            window_->printText(lines_[i], panel_x_ + padding, y, scaledColor(text_color_));
            y += line_height;
        }

        if (scroll_offset_ > 0) {
            window_->printText(std::format("^ {} line(s) newer below", scroll_offset_), panel_x_ + padding, input_y - line_height, scaledColor(info_color_));
        }

        const Uint64 now = SDL_GetTicksNS();
        if (now - last_cursor_toggle_ns_ > 500000000ULL) {
            cursor_visible_ = !cursor_visible_;
            last_cursor_toggle_ns_ = now;
        }

        cursor_pos_ = std::min(cursor_pos_, input_.size());

        std::string line = prompt_ + input_;
        if (cursor_visible_) {
            line.insert(prompt_.size() + cursor_pos_, 1, '_');
        }
        window_->printText(line, panel_x_ + padding, input_y, scaledColor(prompt_color_));
    }

} // namespace mxvk
