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
        cursor_sprite_ = nullptr;
        scroll_track_sprite_ = nullptr;
        scroll_thumb_sprite_ = nullptr;
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
        scrollbar_dragging_ = false;
        scrollbar_drag_offset_ = 0;
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
        trimOutputToLimits();
        invalidateWrappedCache();
        scroll_offset_ = std::min(scroll_offset_, maxScrollOffset());
    }

    void VK_Console::setMaxVisibleLines(const std::size_t maxVisibleLines) {
        max_visible_lines_ = std::max<std::size_t>(1, maxVisibleLines);
    }

    const std::string &VK_Console::inputBuffer() const noexcept {
        return input_;
    }

    void VK_Console::printLine(const std::string &line, const SDL_Color color) {
        std::size_t start = 0;
        while (start <= line.size()) {
            const std::size_t nl = line.find('\n', start);
            if (nl == std::string::npos) {
                pushOutputLine(line.substr(start), color);
                break;
            }

            pushOutputLine(line.substr(start, nl - start), color);
            start = nl + 1;

            // Preserve a trailing newline as an empty visual line.
            if (start == line.size()) {
                pushOutputLine(std::string{}, color);
                break;
            }
        }

        trimOutputToLimits();
        invalidateWrappedCache();
        if (follow_tail_) {
            scroll_offset_ = 0;
        } else {
            scroll_offset_ = std::min(scroll_offset_, maxScrollOffset());
        }
    }

    void VK_Console::clear() {
        total_line_chars_ = 0;
        lines_.clear();
        invalidateWrappedCache();
        input_.clear();
        cursor_pos_ = 0;
        scroll_offset_ = 0;
        follow_tail_ = true;
        history_index_ = -1;
    }

    void VK_Console::pushOutputLine(std::string line) {
        pushOutputLine(std::move(line), text_color_);
    }

    void VK_Console::pushOutputLine(std::string line, const SDL_Color color) {
        if (max_total_chars_ > 0 && line.size() > max_total_chars_) {
            line = line.substr(line.size() - max_total_chars_);
        }

        total_line_chars_ += line.size();
        lines_.push_back(OutputLine{std::move(line), color});
        wrapped_cache_dirty_ = true;
    }

    void VK_Console::trimOutputToLimits() {
        while (!lines_.empty() && (lines_.size() > max_lines_ || total_line_chars_ > max_total_chars_)) {
            total_line_chars_ -= lines_.front().text.size();
            lines_.pop_front();
        }
        wrapped_cache_dirty_ = true;
    }

    std::size_t VK_Console::maxScrollOffset() const noexcept {
        const int width = usableTextWidth();
        ensureWrappedCache(width);
        const std::size_t total_rows = wrapped_cache_line_count_;
        if (total_rows <= last_visible_line_count_) {
            return 0;
        }
        return total_rows - last_visible_line_count_;
    }

    int VK_Console::measureTextWidth(const std::string &text) const {
        if (window_ == nullptr || text.empty()) {
            return 0;
        }

        int width = 0;
        int height = 0;
        if (!window_->getTextDimensions(text, width, height)) {
            return 0;
        }
        return width;
    }

    int VK_Console::usableTextWidth() const {
        constexpr int padding = 10;
        const int reserved_scrollbar = (scrollbar_h_ > 0) ? (scrollbar_w_ + padding) : 0;
        return std::max(1, panel_w_ - (padding * 2) - reserved_scrollbar);
    }

    void VK_Console::invalidateWrappedCache() {
        wrapped_cache_dirty_ = true;
        wrapped_cache_width_ = -1;
    }

    void VK_Console::ensureWrappedCache(const int maxWidth) const {
        if (!wrapped_cache_dirty_ && wrapped_cache_width_ == maxWidth) {
            return;
        }

        wrapped_cache_.clear();
        wrapped_cache_line_count_ = 0;
        wrapped_cache_width_ = maxWidth;
        wrapped_cache_dirty_ = false;

        for (const OutputLine &line : lines_) {
            const std::vector<std::string> rows = wrapTextToWidth(line.text, maxWidth);
            wrapped_cache_line_count_ += rows.size();
            for (const std::string &row : rows) {
                wrapped_cache_.push_back(OutputLine{row, line.color});
            }
        }
    }

    std::vector<std::string> VK_Console::wrapTextToWidth(const std::string &text, const int maxWidth) const {
        std::vector<std::string> wrapped;
        if (text.empty()) {
            wrapped.emplace_back();
            return wrapped;
        }

        if (maxWidth <= 0) {
            wrapped.push_back(text);
            return wrapped;
        }

        auto appendChunk = [&](std::string chunk) {
            if (!chunk.empty() || wrapped.empty()) {
                wrapped.push_back(std::move(chunk));
            }
        };

        std::string current;
        std::size_t index = 0;
        while (index < text.size()) {
            const std::size_t tokenStart = index;
            const bool isSpace = (text[index] == ' ' || text[index] == '\t');
            while (index < text.size() && ((text[index] == ' ' || text[index] == '\t') == isSpace)) {
                ++index;
            }

            const std::string token = text.substr(tokenStart, index - tokenStart);
            const std::string combined = current + token;
            if (!current.empty() && measureTextWidth(combined) > maxWidth) {
                appendChunk(std::move(current));
                current.clear();
            }

            if (measureTextWidth(current + token) <= maxWidth) {
                current += token;
                continue;
            }

            if (!current.empty()) {
                appendChunk(std::move(current));
                current.clear();
            }

            std::string fragment;
            for (char ch : token) {
                const std::string candidate = fragment + ch;
                if (!fragment.empty() && measureTextWidth(candidate) > maxWidth) {
                    appendChunk(std::move(fragment));
                    fragment.clear();
                }
                fragment.push_back(ch);
                if (measureTextWidth(fragment) > maxWidth) {
                    const char last = fragment.back();
                    fragment.pop_back();
                    appendChunk(std::move(fragment));
                    fragment.clear();
                    fragment.push_back(last);
                }
            }
            current += fragment;
        }

        appendChunk(std::move(current));
        return wrapped;
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

    void VK_Console::ensureCursorSprite() {
        if (cursor_sprite_ != nullptr || window_ == nullptr) {
            return;
        }

        SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return;
        }

        const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 210, 255, 220, 255));
        try {
            cursor_sprite_ = window_->createSprite(surface);
        } catch (...) {
            cursor_sprite_ = nullptr;
        }

        SDL_DestroySurface(surface);
    }

    void VK_Console::ensureScrollSprites() {
        if (window_ == nullptr) {
            return;
        }

        if (scroll_track_sprite_ == nullptr) {
            SDL_Surface *trackSurface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (trackSurface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                SDL_FillSurfaceRect(trackSurface, nullptr, SDL_MapRGBA(fmt, nullptr, 255, 255, 255, 80));
                try {
                    scroll_track_sprite_ = window_->createSprite(trackSurface);
                } catch (...) {
                    scroll_track_sprite_ = nullptr;
                }
                SDL_DestroySurface(trackSurface);
            }
        }

        if (scroll_thumb_sprite_ == nullptr) {
            SDL_Surface *thumbSurface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (thumbSurface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                SDL_FillSurfaceRect(thumbSurface, nullptr, SDL_MapRGBA(fmt, nullptr, 190, 225, 255, 180));
                try {
                    scroll_thumb_sprite_ = window_->createSprite(thumbSurface);
                } catch (...) {
                    scroll_thumb_sprite_ = nullptr;
                }
                SDL_DestroySurface(thumbSurface);
            }
        }
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

    void VK_Console::refreshVisibleLineCount() {
        if (window_ == nullptr) {
            return;
        }

        updatePanelLayout();

        int glyph_w = 8;
        int glyph_h = 18;
        if (!window_->getTextDimensions("M", glyph_w, glyph_h)) {
            glyph_h = 18;
        }

        const int line_height = std::max(1, glyph_h + 2);
        const int padding = 10;
        const int title_y = panel_y_ + padding;
        const int first_line_y = title_y + line_height;
        const int input_y = panel_y_ + panel_h_ - padding - line_height;
        const int available_height = std::max(line_height, input_y - first_line_y - 6);

        content_top_y_ = first_line_y;
        content_bottom_y_ = first_line_y + available_height;
        last_visible_line_count_ = std::max<std::size_t>(1, static_cast<std::size_t>(available_height / line_height));
        if (follow_tail_) {
            scroll_offset_ = 0;
        }

        const std::size_t max_offset = maxScrollOffset();
        scroll_offset_ = std::min(scroll_offset_, max_offset);
        updateScrollbarGeometry();
    }

    void VK_Console::updateScrollbarGeometry() {
        const int padding = 10;
        scrollbar_w_ = 10;
        scrollbar_x_ = panel_x_ + panel_w_ - padding - scrollbar_w_;
        scrollbar_y_ = content_top_y_;
        scrollbar_h_ = std::max(0, content_bottom_y_ - content_top_y_);

        if (scrollbar_h_ <= 0) {
            scrollbar_thumb_h_ = 0;
            scrollbar_thumb_y_ = scrollbar_y_;
            return;
        }

        ensureWrappedCache(usableTextWidth());
        const std::size_t total_lines = wrapped_cache_line_count_;
        const std::size_t visible_lines = std::max<std::size_t>(1, std::min(max_visible_lines_, last_visible_line_count_));

        if (total_lines <= visible_lines) {
            scrollbar_thumb_h_ = scrollbar_h_;
            scrollbar_thumb_y_ = scrollbar_y_;
            return;
        }

        const std::size_t max_offset = maxScrollOffset();
        const double visible_ratio = static_cast<double>(visible_lines) / static_cast<double>(total_lines);
        scrollbar_thumb_h_ = std::clamp(static_cast<int>(std::lround(static_cast<double>(scrollbar_h_) * visible_ratio)), 16, scrollbar_h_);

        const int travel = std::max(0, scrollbar_h_ - scrollbar_thumb_h_);
        if (travel == 0 || max_offset == 0) {
            scrollbar_thumb_y_ = scrollbar_y_;
            return;
        }

        const double ratio_top = static_cast<double>(scroll_offset_) / static_cast<double>(max_offset);
        const int offset_px = static_cast<int>(std::lround((1.0 - ratio_top) * static_cast<double>(travel)));
        scrollbar_thumb_y_ = scrollbar_y_ + std::clamp(offset_px, 0, travel);
    }

    bool VK_Console::isPointInScrollbar(const int x, const int y) const noexcept {
        return scrollbar_h_ > 0 && x >= scrollbar_x_ && x < (scrollbar_x_ + scrollbar_w_) && y >= scrollbar_y_ && y < (scrollbar_y_ + scrollbar_h_);
    }

    bool VK_Console::isPointInScrollbarThumb(const int x, const int y) const noexcept {
        return scrollbar_thumb_h_ > 0 && x >= scrollbar_x_ && x < (scrollbar_x_ + scrollbar_w_) && y >= scrollbar_thumb_y_ && y < (scrollbar_thumb_y_ + scrollbar_thumb_h_);
    }

    void VK_Console::updateScrollFromThumbY(const int thumbY) {
        const std::size_t max_offset = maxScrollOffset();
        if (max_offset == 0) {
            scroll_offset_ = 0;
            follow_tail_ = true;
            return;
        }

        const int travel = std::max(0, scrollbar_h_ - scrollbar_thumb_h_);
        if (travel == 0) {
            scroll_offset_ = 0;
            follow_tail_ = true;
            return;
        }

        const int clamped_thumb_y = std::clamp(thumbY, scrollbar_y_, scrollbar_y_ + travel);
        scrollbar_thumb_y_ = clamped_thumb_y;
        const double ratio_bottom_to_top = static_cast<double>(clamped_thumb_y - scrollbar_y_) / static_cast<double>(travel);
        const double ratio_top = 1.0 - ratio_bottom_to_top;
        scroll_offset_ = static_cast<std::size_t>(std::lround(ratio_top * static_cast<double>(max_offset)));
        scroll_offset_ = std::min(scroll_offset_, max_offset);
        follow_tail_ = (scroll_offset_ == 0);
    }

    void VK_Console::handleEvent(const SDL_Event &event) {
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F3)) {
            toggle();
            return;
        }

        if (!visible_) {
            return;
        }

        refreshVisibleLineCount();

        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            if (event.wheel.y > 0) {
                scrollUp(static_cast<std::size_t>(event.wheel.y));
            } else if (event.wheel.y < 0) {
                scrollDown(static_cast<std::size_t>(-event.wheel.y));
            }
            return;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            const int mx = event.button.x;
            const int my = event.button.y;
            if (isPointInScrollbarThumb(mx, my)) {
                scrollbar_dragging_ = true;
                scrollbar_drag_offset_ = my - scrollbar_thumb_y_;
                return;
            }

            if (isPointInScrollbar(mx, my)) {
                const std::size_t page = std::max<std::size_t>(1, last_visible_line_count_ - 1);
                if (my < scrollbar_thumb_y_) {
                    scrollUp(page);
                } else if (my >= (scrollbar_thumb_y_ + scrollbar_thumb_h_)) {
                    scrollDown(page);
                }
                return;
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            scrollbar_dragging_ = false;
            return;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION && scrollbar_dragging_) {
            updateScrollFromThumbY(event.motion.y - scrollbar_drag_offset_);
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
        ensureCursorSprite();
        ensureScrollSprites();
        const auto consoleSpriteY = [this](const int y, const int h) {
            if (sprite_y_origin_top_left_) {
                return std::max(0, y);
            }

            const int screen_h = static_cast<int>(window_->getSwapchainExtent().height);
            return std::max(0, screen_h - y - h);
        };

        if (panel_sprite_ != nullptr) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int panel_alpha = static_cast<int>(std::lround(170.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, static_cast<Uint8>(std::clamp(panel_alpha, 0, 255))));
                panel_sprite_->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            panel_sprite_->drawSpriteRect(panel_x_, consoleSpriteY(panel_y_, panel_h_), panel_w_, panel_h_);
        }

        int glyph_w = 8;
        int glyph_h = 18;
        if (!window_->getTextDimensions("M", glyph_w, glyph_h)) {
            glyph_h = 18;
        }

        const int line_height = std::max(1, glyph_h + 2);
        const int padding = 10;
        const int text_width = usableTextWidth();
        ensureWrappedCache(text_width);

        int y = panel_y_ + padding;
        window_->printText("MXVK Console (F3 to toggle)", panel_x_ + padding, y, scaledColor(info_color_));
        y += line_height;

        const int input_y = panel_y_ + panel_h_ - padding - line_height;
        const int available_height = std::max(line_height, input_y - y - 6);
        content_top_y_ = y;
        content_bottom_y_ = y + available_height;
        last_visible_line_count_ = std::max<std::size_t>(1, static_cast<std::size_t>(available_height / line_height));
        if (follow_tail_) {
            scroll_offset_ = 0;
        }

        const std::size_t max_offset = maxScrollOffset();
        scroll_offset_ = std::min(scroll_offset_, max_offset);
        updateScrollbarGeometry();

        const std::size_t visible_lines = std::min(std::min(max_visible_lines_, last_visible_line_count_), wrapped_cache_.size());
        const std::size_t start = (wrapped_cache_.size() > visible_lines) ? (wrapped_cache_.size() - visible_lines - scroll_offset_) : 0;
        const std::size_t end = std::min(wrapped_cache_.size(), start + visible_lines);
        for (std::size_t i = start; i < end; ++i) {
            window_->printText(wrapped_cache_[i].text, panel_x_ + padding, y, scaledColor(wrapped_cache_[i].color));
            y += line_height;
        }

        if (scroll_track_sprite_ != nullptr && scrollbar_h_ > 0) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int track_alpha = static_cast<int>(std::lround(96.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 255, 255, 255, static_cast<Uint8>(std::clamp(track_alpha, 0, 255))));
                scroll_track_sprite_->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            scroll_track_sprite_->drawSpriteRect(scrollbar_x_, consoleSpriteY(scrollbar_y_, scrollbar_h_), scrollbar_w_, scrollbar_h_);
        }

        if (scroll_thumb_sprite_ != nullptr && scrollbar_thumb_h_ > 0) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int base_alpha = scrollbar_dragging_ ? 240 : 190;
                const int thumb_alpha = static_cast<int>(std::lround(static_cast<double>(base_alpha) * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 190, 225, 255, static_cast<Uint8>(std::clamp(thumb_alpha, 0, 255))));
                scroll_thumb_sprite_->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            scroll_thumb_sprite_->drawSpriteRect(scrollbar_x_,
                                                 consoleSpriteY(scrollbar_thumb_y_, scrollbar_thumb_h_),
                                                 scrollbar_w_,
                                                 scrollbar_thumb_h_);
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

        const std::string line = prompt_ + input_;
        window_->printText(line, panel_x_ + padding, input_y, scaledColor(prompt_color_));

        if (cursor_visible_ && cursor_sprite_ != nullptr) {
            int prefix_width = 0;
            int prefix_height = 0;
            const std::string prefix = prompt_ + input_.substr(0, cursor_pos_);
            if (!window_->getTextDimensions(prefix, prefix_width, prefix_height)) {
                prefix_width = static_cast<int>((prompt_.size() + cursor_pos_) * static_cast<std::size_t>(glyph_w));
            }

            // Draw a VS Code-like thin insertion caret at the text boundary.
            const int cursor_w = 2;
            const int cursor_x = std::max(panel_x_ + padding, panel_x_ + padding + prefix_width - 1);
            const int cursor_y = input_y + 2;
            const int cursor_h = std::max(4, line_height - 4);

            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int cursor_alpha = static_cast<int>(std::lround(220.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 210, 255, 220, static_cast<Uint8>(std::clamp(cursor_alpha, 0, 255))));
                cursor_sprite_->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            cursor_sprite_->drawSpriteRect(cursor_x, consoleSpriteY(cursor_y, cursor_h), cursor_w, cursor_h);
        }
    }

} // namespace mxvk
