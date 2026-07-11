#ifndef ASTEROIDS3D_STARFIELD_HPP
#define ASTEROIDS3D_STARFIELD_HPP

/**
 * @file starfield.hpp
 * @brief Animated starfield used by the Asteroids scene.
 */

#include "asteroids3d_types.hpp"

namespace mxvk {
    class VK_Sprite3D;
    class VK_Window;
} // namespace mxvk

namespace space {

    /**
     * @class StarField
     * @brief Manages and renders a camera-centered field of animated stars.
     */
    class StarField {
      public:
        /** @brief Creates an uninitialized starfield. */
        StarField() = default;

        /**
         * @brief Creates the stars.
         * @param star_count Number of stars.
         * @param min_radius Inner spawn radius.
         * @param max_radius Outer spawn radius.
         */
        void init(int star_count, float min_radius, float max_radius);
        /**
         * @brief Assigns the non-owning sprite batch used to render stars.
         * @param sprite_batch Sprite batch, which must outlive this object.
         */
        void setSprite(mxvk::VK_Sprite3D *sprite_batch);
        /**
         * @brief Updates rendering resources after a window-size change.
         * @param window Active rendering window.
         */
        void resize(mxvk::VK_Window *window);
        /**
         * @brief Advances star movement and twinkling.
         * @param delta_time Frame duration in seconds.
         * @param camera_position Current camera position.
         * @param elapsed_time Total elapsed time in seconds.
         */
        void update(float delta_time, const glm::vec3 &camera_position, float elapsed_time);
        /** @brief Submits the current stars to the assigned sprite batch. */
        void draw();

      private:
        std::vector<Star> stars{};
        mxvk::VK_Sprite3D *sprite = nullptr;
        bool initialized = false;
        float min_radius = 50.0f;
        float max_radius = 200.0f;

        void respawn(Star &star, const glm::vec3 &center);
    };

} // namespace space

#endif
