#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace example {
    class ExampleWindow : public mxvk::VK_Window {
        std::string current_path = ".";

      public:
        ExampleWindow(const std::string path, const std::string &text, int width, int height, bool fullscreen) : mxvk::VK_Window(text, width, height, fullscreen, MXVK_VALIDATION) {
            current_path = path.empty() ? std::string(text_example_ASSET_DIR) : path;
            setFont(current_path + "/data/font.ttf", 24);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {
            printText("Hello World", 15, 15, SDL_Color{255, 255, 255, 255});
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args.path, "VK_Example", args.width, args.height, args.fullscreen);
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
    }
    return EXIT_SUCCESS;
}
