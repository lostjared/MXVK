#include "asteroids3d_window.hpp"

#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    try {
        if (!SDL_SetAppMetadata("Asteroids 3D", nullptr, "asteroids3d")) {
            std::cerr << std::format("asteroids3d: could not set SDL application metadata: {}\n", SDL_GetError());
        }

        const Arguments args = proc_args(argc, argv);
        space::run_asteroids3d(args);
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
