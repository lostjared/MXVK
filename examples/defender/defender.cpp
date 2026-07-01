#include "defender_window.hpp"

#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        defender::DefenderWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();

    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
