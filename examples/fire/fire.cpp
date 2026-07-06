#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace example {
    class FireWindow : public mxvk::VK_Window {
      public:
        FireWindow(const std::string &path, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("Fire - Procedural Shader", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              shader_root((path.empty() ? std::string(FIRE_ASSET_DIR) : path) + "/data") {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            attachPostProcessingShader(shader_root + "/fire.frag.spv");
            setPostProcessingShaderTimeEnabled(true);
            setPostProcessingEnabled(true);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                zoom = std::clamp(zoom + e.wheel.y * 0.16f, 0.25f, 4.40f);
            }
        }

        void proc() override {
            setPostProcessingShaderParams(
                0.0f,
                0.5f,
                0.78f,
                zoom);
        }

      private:
        std::string shader_root;
        float zoom = 1.0f;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::FireWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
