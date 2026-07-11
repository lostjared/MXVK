#ifndef ASTEROIDS3D_STARFIELD_HPP
#define ASTEROIDS3D_STARFIELD_HPP

#include "asteroids3d_types.hpp"

namespace mxvk {
    class VK_Sprite3D;
    class VK_Window;
} // namespace mxvk

namespace space {

    class StarField {
      public:
        StarField() = default;

        void init(int star_count, float min_radius, float max_radius);
        void setSprite(mxvk::VK_Sprite3D *sprite_batch);
        void resize(mxvk::VK_Window *window);
        void update(float delta_time, const glm::vec3 &camera_position, float elapsed_time);
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
