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
        mxvk::VK_Sprite *sprite = nullptr;
        int fallback_width = 1280;
        int fallback_height = 720;

      public:
        ExampleWindow(const std::string path, const std::string &text, int width, int height, bool fullscreen, bool enable_vsync) : mxvk::VK_Window(text, width, height, fullscreen, MXVK_VALIDATION, enable_vsync) {
            current_path = path.empty() ? std::string(sprite_example_ASSET_DIR) : path;
            if (current_path == ".") {
                current_path = sprite_example_ASSET_DIR;
            }
            setFont(current_path + "/data/font.ttf", 24);
            fallback_width = width;
            fallback_height = height;
            const std::string image_path = current_path + "/data/intro.png";
            const std::string vertex_shader = current_path + "/data/sprite.vert.spv";
            const std::string fragment_shader = current_path + "/data/fragment.frag.spv";
            sprite = createSprite(image_path, vertex_shader, fragment_shader);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {
            if (sprite == nullptr) {
                return;
            }
            int target_w = fallback_width;
            int target_h = fallback_height;
            if (swapchain_extent.width > 0U && swapchain_extent.height > 0U) {
                target_w = static_cast<int>(swapchain_extent.width);
                target_h = static_cast<int>(swapchain_extent.height);
            }
            sprite->drawSpriteRect(0, 0, target_w, target_h);
            printText("Hello, World!", 15, 15, {255, 255, 255, 255});
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::ExampleWindow ex_window(args.path, "VK_Example", args.width, args.height, args.fullscreen, args.enable_vsync);
        ex_window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
    }
    return EXIT_SUCCESS;
}
