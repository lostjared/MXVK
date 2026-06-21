#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_console.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace example {
    class ConsoleDemoWindow : public mxvk::VK_Window {
      public:
        ConsoleDemoWindow(const std::string &path,
                          const std::string &title,
                          const int width,
                          const int height,
                          const bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION) {
            const std::string base_path = path.empty() ? std::string(console_demo_ASSET_DIR) : path;
            const std::string sprite_vert_path = base_path + "/data/sprite.vert.spv";
            const std::string shader_path = base_path + "/data/background_pulse.frag.spv";

            background = createSprite(base_path + "/data/background.png", sprite_vert_path, shader_path);
            console.attach(*this, base_path + "/data/font.ttf", 20);
            console.setSpriteYOriginTopLeft(true);
            console.setPrompt("mxvk> ");
            console.printLine("Press F3 to open/close the console.");
            console.printLine("Type 'help' for built-in commands.");

            console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
                if (args.empty()) {
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

                return false;
            });
        }

        void event(SDL_Event &e) override {
            console.handleEvent(e);

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && !console.isVisible()) {
                exit();
            }
        }

        void proc() override {
            if (background != nullptr) {
                const VkExtent2D extent = getSwapchainExtent();
                const int target_w = static_cast<int>(extent.width);
                const int target_h = static_cast<int>(extent.height);
                if (target_w > 0 && target_h > 0) {
                    const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
                    background->setShaderParams(elapsed, 0.0f, 0.0f, 0.0f);
                    background->drawSpriteRect(0, 0, target_w, target_h);
                }
            }

            if (!console.isVisible()) {
                printText("MXVK Console Demo", 14, 12, SDL_Color{255, 255, 255, 255});
                printText("Press F3 to toggle console. Press ESC to quit when console is hidden.",
                          14,
                          38,
                          SDL_Color{180, 180, 220, 255});
            }
            console.draw();
        }

      private:
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VK_Console console;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ConsoleDemoWindow window(args.path,
                                          "MXVK_Console_Demo",
                                          args.width,
                                          args.height,
                                          args.fullscreen);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
