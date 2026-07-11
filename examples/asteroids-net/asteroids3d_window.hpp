#ifndef ASTEROIDS3D_WINDOW_HPP
#define ASTEROIDS3D_WINDOW_HPP

/**
 * @file asteroids3d_window.hpp
 * @brief Entry point for the networked 3D Asteroids application.
 */

#include "mxvk/argz.hpp"

namespace space {

    /**
     * @brief Runs the Asteroids application.
     * @param args Parsed command-line arguments controlling the application.
     */
    void run_asteroids3d(const Arguments &args);

} // namespace space

#endif
