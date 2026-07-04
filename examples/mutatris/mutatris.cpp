#include <cstdlib>
#include <format>
#include <iostream>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"

#include "mutatris_window.hpp"

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        mutatris::MutatrisWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mutatris: MXVK exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << std::format("mutatris: argument exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
