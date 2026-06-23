#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include <SDL3/SDL.h>
#include <format>
#include <iostream>

namespace skeleton {
    class SkeletonWindow : public mxvk::VK_Window {
      public:
        SkeletonWindow(int width, int height, bool full) : mxvk::VK_Window(" -[ MXVK Skeleton ] - ", width, height, full, MXVK_VALIDATION) {
            std::cout << "skeleton: started example.\n";
        }
        void event(SDL_Event &e) override {
            switch (e.type) {
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                    return;
                }
                break;
            case SDL_EVENT_QUIT:
                exit();
                return;
            }
        }
        void proc() override {
            // drawing commands
        }
    };
} // namespace skeleton

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        skeleton::SkeletonWindow window(args.width, args.height, args.fullscreen);
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
