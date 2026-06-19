#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_cfg.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

int main(void) {
    try {
        mxvk::VK_Config config("test.dat");
        auto it = config.itemAtKey("global", "key", "0");
        auto int_value = std::strtol(it.value.c_str(), 0, 0);
        int_value += 1;
        std::cout << "You ran this program: " << int_value << " times.\n";
        config.setItem("global", "key", std::to_string(int_value));
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
