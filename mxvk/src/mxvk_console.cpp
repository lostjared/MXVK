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
        windowPtr = &window;
        windowPtr->setFont(fontPath, fontSize);
        setFont(fontPath, fontSize);
        panel_sprite = nullptr;
        cursor_sprite = nullptr;
        scroll_track_sprite = nullptr;
        scroll_thumb_sprite = nullptr;
        visible = false;
        fade_active = false;
        fade_alpha = 0.0f;
        fade_start_alpha = 0.0f;
        fade_target_alpha = 0.0f;
        fade_start_ns = 0;
        history_index = -1;
        cursor_pos = 0;
        input.clear();
        scroll_offset = 0;
        follow_tail = true;
        scrollbar_dragging = false;
        scrollbar_drag_offset = 0;
    }

    void VK_Console::setFont(const std::string &fontPath, const int fontSize) {
        console_font.reset(fontPath, fontSize);
        invalidateLayoutCache();
        refreshVisibleLineCount();
    }

    void VK_Console::setCommandCallback(CommandCallback callback) {
        commandCallback = std::move(callback);
    }

    void VK_Console::setPrompt(const std::string &prompt) {
        if (!prompt.empty()) {
            promptText = prompt;
        }
    }

    void VK_Console::setVisible(const bool value) noexcept {
        if (value) {
            visible = true;
            fade_start_alpha = fade_alpha;
            fade_target_alpha = 1.0f;
            fade_start_ns = SDL_GetTicksNS();
            fade_active = true;
            if (windowPtr != nullptr) {
                SDL_StartTextInput(windowPtr->getSDLWindow());
            }
            follow_tail = true;
            scroll_offset = 0;
        } else {
            visible = false;
            fade_start_alpha = fade_alpha;
            fade_target_alpha = 0.0f;
            fade_start_ns = SDL_GetTicksNS();
            fade_active = true;
            if (windowPtr != nullptr) {
                SDL_StopTextInput(windowPtr->getSDLWindow());
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
        if (visible) {
            hide();
        } else {
            show();
        }
    }

    bool VK_Console::isVisible() const noexcept {
        return visible || fade_alpha > 0.0f || fade_active;
    }

    void VK_Console::setMaxLines(const std::size_t maxLines) {
        max_lines = std::max<std::size_t>(1, maxLines);
        trimOutputToLimits();
        invalidateWrappedCache();
        scroll_offset = std::min(scroll_offset, maxScrollOffset());
    }

    void VK_Console::setMaxVisibleLines(const std::size_t maxVisibleLines) {
        max_visible_lines = std::max<std::size_t>(1, maxVisibleLines);
    }

    void VK_Console::invalidateLayoutCache() {
        invalidateWrappedCache();
        scroll_offset = std::min(scroll_offset, maxScrollOffset());
    }

    const std::string &VK_Console::inputBuffer() const noexcept {
        return input;
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
        if (follow_tail) {
            scroll_offset = 0;
        } else {
            scroll_offset = std::min(scroll_offset, maxScrollOffset());
        }
    }

    void VK_Console::clear() {
        total_line_chars = 0;
        lines.clear();
        invalidateWrappedCache();
        input.clear();
        cursor_pos = 0;
        scroll_offset = 0;
        follow_tail = true;
        history_index = -1;
    }

    void VK_Console::pushOutputLine(std::string line) {
        pushOutputLine(std::move(line), text_color);
    }

    void VK_Console::pushOutputLine(std::string line, const SDL_Color color) {
        if (max_total_chars > 0 && line.size() > max_total_chars) {
            line = line.substr(line.size() - max_total_chars);
        }

        total_line_chars += line.size();
        lines.push_back(OutputLine{std::move(line), color});
        wrapped_cache_dirty = true;
    }

    void VK_Console::trimOutputToLimits() {
        while (!lines.empty() && (lines.size() > max_lines || total_line_chars > max_total_chars)) {
            total_line_chars -= lines.front().text.size();
            lines.pop_front();
        }
        wrapped_cache_dirty = true;
    }

    std::size_t VK_Console::effectiveVisibleLineCount() const noexcept {
        return std::max<std::size_t>(1, std::min(max_visible_lines, last_visible_line_count));
    }

    std::size_t VK_Console::maxScrollOffset() const noexcept {
        const int width = usableTextWidth();
        ensureWrappedCache(width);
        const std::size_t total_rows = wrapped_cache_line_count;
        const std::size_t visible_rows = effectiveVisibleLineCount();
        if (total_rows <= visible_rows) {
            return 0;
        }
        return total_rows - visible_rows;
    }

    int VK_Console::measureTextWidth(const std::string &text) const {
        if (windowPtr == nullptr || text.empty()) {
            return 0;
        }

        int width = 0;
        int height = 0;
        if (!windowPtr->getTextDimensions(text, width, height, console_font)) {
            return 0;
        }
        return width;
    }

    int VK_Console::usableTextWidth() const {
        constexpr int padding = 10;
        const int reserved_scrollbar = (scrollbar_h > 0) ? (scrollbar_w + padding) : 0;
        return std::max(1, panel_w - (padding * 2) - reserved_scrollbar);
    }

    void VK_Console::invalidateWrappedCache() {
        wrapped_cache_dirty = true;
        wrapped_cache_width = -1;
    }

    void VK_Console::ensureWrappedCache(const int maxWidth) const {
        if (!wrapped_cache_dirty && wrapped_cache_width == maxWidth) {
            return;
        }

        wrapped_cache.clear();
        wrapped_cache_line_count = 0;
        wrapped_cache_width = maxWidth;
        wrapped_cache_dirty = false;

        for (const OutputLine &line : lines) {
            const std::vector<std::string> rows = wrapTextToWidth(line.text, maxWidth);
            wrapped_cache_line_count += rows.size();
            for (const std::string &row : rows) {
                wrapped_cache.push_back(OutputLine{row, line.color});
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

        follow_tail = false;
        scroll_offset = std::min(max_offset, scroll_offset + amount);
    }

    void VK_Console::scrollDown(const std::size_t amount) {
        if (amount == 0) {
            return;
        }

        if (scroll_offset <= amount) {
            scroll_offset = 0;
            follow_tail = true;
            return;
        }

        scroll_offset -= amount;
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
        if (input.empty() || windowPtr == nullptr) {
            input.clear();
            cursor_pos = 0;
            history_index = -1;
            return;
        }

        printLine(promptText + input);
        history.push_back(input);
        history_index = -1;

        std::ostringstream reply;
        const std::vector<std::string> args = tokenize(input);

        bool handled = handleDefaultCommand(args, reply);
        if (!handled && commandCallback) {
            handled = commandCallback(*windowPtr, args, reply);
        }
        if (!handled && !args.empty()) {
            reply << std::format("Unknown command: {}", args.front());
        }

        const std::string out = reply.str();
        if (!out.empty()) {
            printLine(out);
        }

        input.clear();
        cursor_pos = 0;
    }

    void VK_Console::historyUp() {
        if (history.empty()) {
            return;
        }

        if (history_index < 0) {
            history_index = static_cast<int>(history.size()) - 1;
        } else if (history_index > 0) {
            --history_index;
        }

        if (history_index >= 0 && history_index < static_cast<int>(history.size())) {
            input = history[static_cast<std::size_t>(history_index)];
            cursor_pos = input.size();
        }
    }

    void VK_Console::historyDown() {
        if (history.empty()) {
            return;
        }

        if (history_index < 0) {
            return;
        }

        if (history_index < static_cast<int>(history.size()) - 1) {
            ++history_index;
            input = history[static_cast<std::size_t>(history_index)];
            cursor_pos = input.size();
        } else {
            history_index = -1;
            input.clear();
            cursor_pos = 0;
        }
    }

    void VK_Console::ensurePanelSprite() {
        if (panel_sprite != nullptr || windowPtr == nullptr) {
            return;
        }

        SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return;
        }

        const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, 170));
        try {
            panel_sprite = windowPtr->createSprite(surface);
        } catch (...) {
            panel_sprite = nullptr;
        }

        SDL_DestroySurface(surface);
    }

    void VK_Console::ensureCursorSprite() {
        if (cursor_sprite != nullptr || windowPtr == nullptr) {
            return;
        }

        SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return;
        }

        const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 210, 255, 220, 255));
        try {
            cursor_sprite = windowPtr->createSprite(surface);
        } catch (...) {
            cursor_sprite = nullptr;
        }

        SDL_DestroySurface(surface);
    }

    void VK_Console::ensureScrollSprites() {
        if (windowPtr == nullptr) {
            return;
        }

        if (scroll_track_sprite == nullptr) {
            SDL_Surface *trackSurface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (trackSurface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                SDL_FillSurfaceRect(trackSurface, nullptr, SDL_MapRGBA(fmt, nullptr, 255, 255, 255, 80));
                try {
                    scroll_track_sprite = windowPtr->createSprite(trackSurface);
                } catch (...) {
                    scroll_track_sprite = nullptr;
                }
                SDL_DestroySurface(trackSurface);
            }
        }

        if (scroll_thumb_sprite == nullptr) {
            SDL_Surface *thumbSurface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (thumbSurface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                SDL_FillSurfaceRect(thumbSurface, nullptr, SDL_MapRGBA(fmt, nullptr, 190, 225, 255, 180));
                try {
                    scroll_thumb_sprite = windowPtr->createSprite(thumbSurface);
                } catch (...) {
                    scroll_thumb_sprite = nullptr;
                }
                SDL_DestroySurface(thumbSurface);
            }
        }
    }

    void VK_Console::updatePanelLayout() {
        if (windowPtr == nullptr) {
            return;
        }

        const VkExtent2D extent = windowPtr->getSwapchainExtent();
        const int screen_w = static_cast<int>(extent.width);
        const int screen_h = static_cast<int>(extent.height);
        if (screen_w <= 0 || screen_h <= 0) {
            return;
        }

        constexpr int horizontal_margin = 10;
        constexpr int top_margin = 8;
        constexpr int bottom_margin = 24;

        panel_x = horizontal_margin;
        panel_y = top_margin;
        panel_w = std::max(300, screen_w - (horizontal_margin * 2));
        panel_h = std::clamp((screen_h * 42) / 100, 180, std::max(180, screen_h - bottom_margin));
    }

    void VK_Console::refreshVisibleLineCount() {
        if (windowPtr == nullptr) {
            return;
        }

        updatePanelLayout();

        int glyph_w = 8;
        int glyph_h = 18;
        if (!windowPtr->getTextDimensions("M", glyph_w, glyph_h, console_font)) {
            glyph_h = 18;
        }

        const int line_height = std::max(1, glyph_h + 2);
        const int padding = 10;
        const int title_y = panel_y + padding;
        const int first_line_y = title_y + line_height;
        const int input_y = panel_y + panel_h - padding - line_height;
        const int available_height = std::max(line_height, input_y - first_line_y - 6);

        content_top_y = first_line_y;
        content_bottom_y = first_line_y + available_height;
        last_visible_line_count = std::max<std::size_t>(1, static_cast<std::size_t>(available_height / line_height));
        if (follow_tail) {
            scroll_offset = 0;
        }

        const std::size_t max_offset = maxScrollOffset();
        scroll_offset = std::min(scroll_offset, max_offset);
        updateScrollbarGeometry();
    }

    void VK_Console::updateScrollbarGeometry() {
        const int padding = 10;
        scrollbar_w = 10;
        scrollbar_x = panel_x + panel_w - padding - scrollbar_w;
        scrollbar_y = content_top_y;
        scrollbar_h = std::max(0, content_bottom_y - content_top_y);

        if (scrollbar_h <= 0) {
            scrollbar_thumb_h = 0;
            scrollbar_thumb_y = scrollbar_y;
            return;
        }

        ensureWrappedCache(usableTextWidth());
        const std::size_t total_lines = wrapped_cache_line_count;
        const std::size_t visible_lines = effectiveVisibleLineCount();

        if (total_lines <= visible_lines) {
            scrollbar_thumb_h = scrollbar_h;
            scrollbar_thumb_y = scrollbar_y;
            return;
        }

        const std::size_t max_offset = maxScrollOffset();
        const double visible_ratio = static_cast<double>(visible_lines) / static_cast<double>(total_lines);
        scrollbar_thumb_h = std::clamp(static_cast<int>(std::lround(static_cast<double>(scrollbar_h) * visible_ratio)), 16, scrollbar_h);

        const int travel = std::max(0, scrollbar_h - scrollbar_thumb_h);
        if (travel == 0 || max_offset == 0) {
            scrollbar_thumb_y = scrollbar_y;
            return;
        }

        const double ratio_top = static_cast<double>(scroll_offset) / static_cast<double>(max_offset);
        const int offset_px = static_cast<int>(std::lround((1.0 - ratio_top) * static_cast<double>(travel)));
        scrollbar_thumb_y = scrollbar_y + std::clamp(offset_px, 0, travel);
    }

    bool VK_Console::isPointInScrollbar(const int x, const int y) const noexcept {
        return scrollbar_h > 0 && x >= scrollbar_x && x < (scrollbar_x + scrollbar_w) && y >= scrollbar_y && y < (scrollbar_y + scrollbar_h);
    }

    bool VK_Console::isPointInScrollbarThumb(const int x, const int y) const noexcept {
        return scrollbar_thumb_h > 0 && x >= scrollbar_x && x < (scrollbar_x + scrollbar_w) && y >= scrollbar_thumb_y && y < (scrollbar_thumb_y + scrollbar_thumb_h);
    }

    void VK_Console::updateScrollFromThumbY(const int thumbY) {
        const std::size_t max_offset = maxScrollOffset();
        if (max_offset == 0) {
            scroll_offset = 0;
            follow_tail = true;
            return;
        }

        const int travel = std::max(0, scrollbar_h - scrollbar_thumb_h);
        if (travel == 0) {
            scroll_offset = 0;
            follow_tail = true;
            return;
        }

        const int clamped_thumb_y = std::clamp(thumbY, scrollbar_y, scrollbar_y + travel);
        scrollbar_thumb_y = clamped_thumb_y;
        const double ratio_bottom_to_top = static_cast<double>(clamped_thumb_y - scrollbar_y) / static_cast<double>(travel);
        const double ratio_top = 1.0 - ratio_bottom_to_top;
        scroll_offset = static_cast<std::size_t>(std::lround(ratio_top * static_cast<double>(max_offset)));
        scroll_offset = std::min(scroll_offset, max_offset);
        follow_tail = (scroll_offset == 0);
    }

    void VK_Console::handleEvent(const SDL_Event &event) {
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F3)) {
            toggle();
            return;
        }

        if (!visible) {
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
                scrollbar_dragging = true;
                scrollbar_drag_offset = my - scrollbar_thumb_y;
                return;
            }

            if (isPointInScrollbar(mx, my)) {
                const std::size_t page = std::max<std::size_t>(1, effectiveVisibleLineCount() - 1);
                if (my < scrollbar_thumb_y) {
                    scrollUp(page);
                } else if (my >= (scrollbar_thumb_y + scrollbar_thumb_h)) {
                    scrollDown(page);
                }
                return;
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            scrollbar_dragging = false;
            return;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION && scrollbar_dragging) {
            updateScrollFromThumbY(event.motion.y - scrollbar_drag_offset);
            return;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            switch (event.key.key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                submitInput();
                return;
            case SDLK_BACKSPACE:
                if (cursor_pos > 0 && cursor_pos <= input.size()) {
                    input.erase(cursor_pos - 1, 1);
                    --cursor_pos;
                }
                return;
            case SDLK_DELETE:
                if (cursor_pos < input.size()) {
                    input.erase(cursor_pos, 1);
                }
                return;
            case SDLK_LEFT:
                if (cursor_pos > 0) {
                    --cursor_pos;
                }
                return;
            case SDLK_RIGHT:
                if (cursor_pos < input.size()) {
                    ++cursor_pos;
                }
                return;
            case SDLK_HOME:
                cursor_pos = 0;
                return;
            case SDLK_END:
                follow_tail = true;
                scroll_offset = 0;
                cursor_pos = input.size();
                return;
            case SDLK_UP:
                historyUp();
                return;
            case SDLK_DOWN:
                historyDown();
                return;
            case SDLK_PAGEUP:
                scrollUp(std::max<std::size_t>(1, effectiveVisibleLineCount() - 1));
                return;
            case SDLK_PAGEDOWN:
                scrollDown(std::max<std::size_t>(1, effectiveVisibleLineCount() - 1));
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
                input.insert(cursor_pos, event.text.text);
                cursor_pos += SDL_strlen(event.text.text);
            }
        }
    }

    void VK_Console::draw() {
        if (windowPtr == nullptr) {
            return;
        }

        if (fade_active) {
            const Uint64 now = SDL_GetTicksNS();
            const double elapsed = static_cast<double>(now - fade_start_ns);
            const double duration = static_cast<double>(fade_duration_ns);
            const float t = static_cast<float>(std::clamp(elapsed / std::max(1.0, duration), 0.0, 1.0));
            fade_alpha = fade_start_alpha + ((fade_target_alpha - fade_start_alpha) * t);
            if (t >= 1.0f) {
                fade_alpha = fade_target_alpha;
                fade_active = false;
            }
        }

        if (!(visible || fade_alpha > 0.0f)) {
            return;
        }

        const float alpha = std::clamp(fade_alpha, 0.0f, 1.0f);
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
            if (sprite_y_origin_top_left) {
                return std::max(0, y);
            }

            const int screen_h = static_cast<int>(windowPtr->getSwapchainExtent().height);
            return std::max(0, screen_h - y - h);
        };

        if (panel_sprite != nullptr) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int panel_alpha = static_cast<int>(std::lround(170.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 0, 0, 0, static_cast<Uint8>(std::clamp(panel_alpha, 0, 255))));
                panel_sprite->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            panel_sprite->drawSpriteRect(panel_x, consoleSpriteY(panel_y, panel_h), panel_w, panel_h);
        }

        int glyph_w = 8;
        int glyph_h = 18;
        if (!windowPtr->getTextDimensions("M", glyph_w, glyph_h, console_font)) {
            glyph_h = 18;
        }

        const int line_height = std::max(1, glyph_h + 2);
        const int padding = 10;
        const int text_width = usableTextWidth();
        ensureWrappedCache(text_width);

        int y = panel_y + padding;
        windowPtr->printText("MXVK Console (F3 to toggle)", panel_x + padding, y, scaledColor(info_color), console_font);
        y += line_height;

        const int input_y = panel_y + panel_h - padding - line_height;
        const int available_height = std::max(line_height, input_y - y - 6);
        content_top_y = y;
        content_bottom_y = y + available_height;
        last_visible_line_count = std::max<std::size_t>(1, static_cast<std::size_t>(available_height / line_height));
        if (follow_tail) {
            scroll_offset = 0;
        }

        const std::size_t max_offset = maxScrollOffset();
        scroll_offset = std::min(scroll_offset, max_offset);
        updateScrollbarGeometry();

        const std::size_t visible_lines = std::min(effectiveVisibleLineCount(), wrapped_cache.size());
        const std::size_t start = (wrapped_cache.size() > visible_lines) ? (wrapped_cache.size() - visible_lines - scroll_offset) : 0;
        const std::size_t end = std::min(wrapped_cache.size(), start + visible_lines);
        for (std::size_t i = start; i < end; ++i) {
            windowPtr->printText(wrapped_cache[i].text, panel_x + padding, y, scaledColor(wrapped_cache[i].color), console_font);
            y += line_height;
        }

        if (scroll_track_sprite != nullptr && scrollbar_h > 0) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int track_alpha = static_cast<int>(std::lround(96.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 255, 255, 255, static_cast<Uint8>(std::clamp(track_alpha, 0, 255))));
                scroll_track_sprite->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            scroll_track_sprite->drawSpriteRect(scrollbar_x, consoleSpriteY(scrollbar_y, scrollbar_h), scrollbar_w, scrollbar_h);
        }

        if (scroll_thumb_sprite != nullptr && scrollbar_thumb_h > 0) {
            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int base_alpha = scrollbar_dragging ? 240 : 190;
                const int thumb_alpha = static_cast<int>(std::lround(static_cast<double>(base_alpha) * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 190, 225, 255, static_cast<Uint8>(std::clamp(thumb_alpha, 0, 255))));
                scroll_thumb_sprite->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            scroll_thumb_sprite->drawSpriteRect(scrollbar_x,
                                                consoleSpriteY(scrollbar_thumb_y, scrollbar_thumb_h),
                                                scrollbar_w,
                                                scrollbar_thumb_h);
        }

        if (scroll_offset > 0) {
            windowPtr->printText(std::format("^ {} line(s) newer below", scroll_offset),
                                 panel_x + padding,
                                 input_y - line_height,
                                 scaledColor(info_color),
                                 console_font);
        }

        const Uint64 now = SDL_GetTicksNS();
        if (now - last_cursor_toggle_ns > 500000000ULL) {
            cursor_visible = !cursor_visible;
            last_cursor_toggle_ns = now;
        }

        cursor_pos = std::min(cursor_pos, input.size());

        const std::string line = promptText + input;
        windowPtr->printText(line, panel_x + padding, input_y, scaledColor(prompt_color), console_font);

        if (cursor_visible && cursor_sprite != nullptr) {
            int prefix_width = 0;
            int prefix_height = 0;
            const std::string prefix = promptText + input.substr(0, cursor_pos);
            if (!windowPtr->getTextDimensions(prefix, prefix_width, prefix_height, console_font)) {
                prefix_width = static_cast<int>((promptText.size() + cursor_pos) * static_cast<std::size_t>(glyph_w));
            }

            // Draw a VS Code-like thin insertion caret at the text boundary.
            const int cursor_w = 2;
            const int cursor_x = std::max(panel_x + padding, panel_x + padding + prefix_width - 1);
            const int cursor_y = input_y + 2;
            const int cursor_h = std::max(4, line_height - 4);

            SDL_Surface *surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                const SDL_PixelFormatDetails *const fmt = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
                const int cursor_alpha = static_cast<int>(std::lround(220.0 * alpha));
                SDL_FillSurfaceRect(surface, nullptr, SDL_MapRGBA(fmt, nullptr, 210, 255, 220, static_cast<Uint8>(std::clamp(cursor_alpha, 0, 255))));
                cursor_sprite->updateTexture(surface);
                SDL_DestroySurface(surface);
            }

            cursor_sprite->drawSpriteRect(cursor_x, consoleSpriteY(cursor_y, cursor_h), cursor_w, cursor_h);
        }
    }

} // namespace mxvk
