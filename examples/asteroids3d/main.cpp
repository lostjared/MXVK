#include "asteroids3d_window.hpp"

#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    try {
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
