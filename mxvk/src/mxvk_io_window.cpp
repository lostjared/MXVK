#include "mxvk/mxvk_io_window.hpp"

#include <format>

namespace mxvk {

    VK_IOWindow::VK_IOWindow(const std::string &path, const std::string &title, const int width, const int height, const bool fullscreen) : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION) {
        const std::string base_path = path;
        console_.attach(*this, base_path + "/data/font.ttf", 20);
        console_.setPrompt("$> ");
        console_.printLine("Press F3 to open/close the console.");
        console_.printLine("Type 'help' for built-in commands.");
        console_.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
            if (args.empty()) {
                return true;
            }

            if (args[0] == "help") {
                out << "Built-in commands:\n"
                    << "  help          Show this help message\n"
                    << "  clear         Clear the console output\n"
                    << "  echo <text>   Print text to the console\n"
                    << "  about         Show sample/about information\n"
                    << "  quit | exit   Close the current window";
                appendConsoleHelp(out);
                return true;
            }

            if (args[0] == "echo") {
                for (std::size_t i = 1; i < args.size(); ++i) {
                    if (i > 1) {
                        out << ' ';
                    }
                    out << args[i];
                }
                return true;
            }

            if (args[0] == "quit" || args[0] == "exit") {
                out << "Closing window...";
                exit();
                return true;
            }

            if (args[0] == "about") {
                out << "console_demo: MXVK Vulkan console sample.\n(C) 2026 LostSideDead Software\n";
                return true;
            }

            if (handleConsoleCommand(args, out)) {
                return true;
            }

            return false;
        });
    }

    bool VK_IOWindow::handleConsoleCommand([[maybe_unused]] const std::vector<std::string> &args,
                                           [[maybe_unused]] std::ostream &out) {
        return false;
    }

    void VK_IOWindow::appendConsoleHelp([[maybe_unused]] std::ostream &out) const {
    }

    void VK_IOWindow::event(SDL_Event &e) {
        const bool is_escape_down = (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE);
        const bool was_console_visible = console_.isVisible();
        if (is_escape_down && was_console_visible && SDL_GetWindowRelativeMouseMode(getSDLWindow())) {
            console_event(e);
            return;
        }

        console_.handleEvent(e);

        if (is_escape_down && was_console_visible && !console_.isVisible()) {
            return;
        }

        if (console_.isVisible()) {
            return;
        }

        console_event(e);
    }

    void VK_IOWindow::print(const std::string &text, SDL_Color col) {
        console_.printLine(text, col);
    }

    void VK_IOWindow::proc() {
        console_proc();
        console_.draw();
    }

} // namespace mxvk