#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_point_sprite_batch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace {

    constexpr int DEFAULT_TUXES = 8;
    constexpr int MAX_TUXES = 1000;
    constexpr int TUXES_PER_SPACE_PRESS = 25;
    constexpr int FRAME_COUNT = 16;
    constexpr float WORLD_HALF_HEIGHT = 4.5f;
    constexpr float MIN_SIZE = 306.0f;
    constexpr float MAX_SIZE = 450.0f;
    constexpr float SPRITE_COLLISION_SCALE = 0.44f;
    constexpr float SIZE_SCALE_STEP = 1.12f;
    constexpr float MIN_SIZE_SCALE = 0.6f;
    constexpr float MAX_SIZE_SCALE = 2.0f;
    constexpr float POPULATION_GROWTH_INTERVAL = 0.18f;
    constexpr int INITIAL_PLACEMENT_ATTEMPTS = 200;

    struct TuxSprite {
        glm::vec2 position{};
        glm::vec2 velocity{};
        float z = 0.0f;
        float size = 0.0f;
        float frame = 0.0f;
        float frames_per_second = 0.0f;
        float wobble_phase = 0.0f;
        float wobble_speed = 0.0f;
        float wobble_amount = 0.0f;
        float tint = 1.0f;
    };

    [[nodiscard]] float random_float(float min, float max) {
        static std::random_device rd;
        static std::default_random_engine engine(rd());
        std::uniform_real_distribution<float> dist(min, max);
        return dist(engine);
    }

} // namespace

namespace example {

    class PointSpriteWindow : public mxvk::VK_Window {
      public:
        PointSpriteWindow(const std::string &data_root, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MXVK Point Sprite Tux", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              data_root(data_root),
              tuxes(MAX_TUXES),
              vertices(MAX_TUXES) {
            setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            reset_tuxes(DEFAULT_TUXES);
            last_update_time = SDL_GetTicks();
        }

        ~PointSpriteWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            point_batch.cleanup();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            } else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && !e.key.repeat) {
                reset_size_scale();
                reset_tuxes(DEFAULT_TUXES);
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_PAGEUP && !e.key.repeat) {
                scale_tuxes(SIZE_SCALE_STEP);
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_PAGEDOWN && !e.key.repeat) {
                scale_tuxes(1.0f / SIZE_SCALE_STEP);
            }
        }

        void onSwapchainRecreated() override {
            point_batch.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            if (!ensure_resources()) {
                return;
            }

            const Uint32 current_time = SDL_GetTicks();
            float delta_time = static_cast<float>(current_time - last_update_time) / 1000.0f;
            last_update_time = current_time;
            delta_time = std::min(delta_time, 0.1f);
            global_time += delta_time;

            update_population_growth(delta_time);
            update_tuxes(delta_time);
            point_batch.upload_vertices(vertices.data(), active_tuxes);
            point_batch.update_mvp(image_index, make_mvp());
            point_batch.render(cmd, image_index);
        }

      private:
        bool ensure_resources() {
            if (point_batch.loaded()) {
                return true;
            }
            if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || getSwapchainImageCount() == 0U || getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return false;
            }

            point_batch.load(
                this,
                data_root + "/tux.png",
                data_root + "/pointsprite.vert.spv",
                data_root + "/pointsprite.frag.spv",
                vertices.size());
            point_batch.set_additive_blending(false);
            point_batch.set_depth_test_enabled(false);
            point_batch.set_depth_write_enabled(false);
            last_update_time = SDL_GetTicks();
            return true;
        }

        void reset_tuxes(size_t count) {
            active_tuxes = std::min(count, tuxes.size());
            for (size_t i = 0; i < active_tuxes; ++i) {
                randomize_tux(tuxes[i]);
                place_tux_without_overlap(i);
                write_vertex(vertices[i], tuxes[i]);
            }
        }

        void add_tuxes(size_t count) {
            const size_t old_count = active_tuxes;
            active_tuxes = std::min(active_tuxes + count, tuxes.size());
            for (size_t i = old_count; i < active_tuxes; ++i) {
                randomize_tux(tuxes[i]);
                place_tux_without_overlap(i);
                write_vertex(vertices[i], tuxes[i]);
            }
        }

        void update_population_growth(float delta_time) {
            const bool *keys = SDL_GetKeyboardState(nullptr);
            const bool space_down = keys != nullptr && keys[SDL_SCANCODE_SPACE];
            if (!space_down) {
                space_was_down = false;
                population_growth_elapsed = 0.0f;
                return;
            }

            if (!space_was_down) {
                add_tuxes(TUXES_PER_SPACE_PRESS);
                population_growth_elapsed = 0.0f;
                space_was_down = true;
            }

            if (active_tuxes >= tuxes.size()) {
                return;
            }

            population_growth_elapsed += delta_time;
            while (population_growth_elapsed >= POPULATION_GROWTH_INTERVAL && active_tuxes < tuxes.size()) {
                population_growth_elapsed -= POPULATION_GROWTH_INTERVAL;
                add_tuxes(TUXES_PER_SPACE_PRESS);
            }
        }

        void reset_size_scale() {
            size_scale = 1.0f;
        }

        void scale_tuxes(float factor) {
            const float previous_scale = size_scale;
            size_scale = std::clamp(size_scale * factor, MIN_SIZE_SCALE, MAX_SIZE_SCALE);
            const float applied_factor = size_scale / previous_scale;
            for (size_t i = 0; i < active_tuxes; ++i) {
                tuxes[i].size *= applied_factor;
            }
        }

        void randomize_tux(TuxSprite &tux) const {
            const float angle = random_float(0.0f, 6.28318f);
            const float speed = random_float(0.75f, 1.85f);
            tux.velocity = glm::vec2(std::cos(angle), std::sin(angle)) * speed;
            tux.z = random_float(-0.35f, 0.35f);
            tux.size = random_float(MIN_SIZE, MAX_SIZE) * size_scale;
            tux.frame = random_float(0.0f, static_cast<float>(FRAME_COUNT));
            tux.frames_per_second = random_float(7.0f, 14.0f);
            tux.wobble_phase = random_float(0.0f, 6.28318f);
            tux.wobble_speed = random_float(1.4f, 3.4f);
            tux.wobble_amount = random_float(0.08f, 0.18f);
            tux.tint = random_float(0.82f, 1.0f);
        }

        void place_tux_without_overlap(size_t index) {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 16.0f / 9.0f;
            const float half_width = WORLD_HALF_HEIGHT * aspect;
            auto &tux = tuxes[index];
            const float radius = collision_radius(tux);

            for (int attempt = 0; attempt < INITIAL_PLACEMENT_ATTEMPTS; ++attempt) {
                tux.position.x = random_float(-half_width + radius, half_width - radius);
                tux.position.y = random_float(-WORLD_HALF_HEIGHT + radius, WORLD_HALF_HEIGHT - radius);
                if (!overlaps_existing_tux(index)) {
                    return;
                }
            }

            const float columns = std::ceil(std::sqrt(static_cast<float>(active_tuxes) * aspect));
            const float rows = std::ceil(static_cast<float>(active_tuxes) / columns);
            const float column = std::fmod(static_cast<float>(index), columns);
            const float row = std::floor(static_cast<float>(index) / columns);
            tux.position.x = -half_width + ((column + 0.5f) / columns) * half_width * 2.0f;
            tux.position.y = -WORLD_HALF_HEIGHT + ((row + 0.5f) / rows) * WORLD_HALF_HEIGHT * 2.0f;
        }

        [[nodiscard]] bool overlaps_existing_tux(size_t index) const {
            const auto &tux = tuxes[index];
            const float radius = collision_radius(tux);
            for (size_t i = 0; i < index; ++i) {
                const float min_distance = radius + collision_radius(tuxes[i]);
                if (glm::length(tux.position - tuxes[i].position) < min_distance) {
                    return true;
                }
            }
            return false;
        }

        void update_tuxes(float delta_time) {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 16.0f / 9.0f;
            const float half_width = WORLD_HALF_HEIGHT * aspect;

            for (size_t i = 0; i < active_tuxes; ++i) {
                auto &tux = tuxes[i];
                tux.position += tux.velocity * delta_time;
                tux.position.y += std::sin(global_time * tux.wobble_speed + tux.wobble_phase) * tux.wobble_amount * delta_time;
                tux.frame += tux.frames_per_second * delta_time;
                if (tux.frame >= static_cast<float>(FRAME_COUNT)) {
                    tux.frame = std::fmod(tux.frame, static_cast<float>(FRAME_COUNT));
                }

                bounce_from_bounds(tux, half_width, WORLD_HALF_HEIGHT);
            }

            resolve_collisions();

            for (size_t i = 0; i < active_tuxes; ++i) {
                bounce_from_bounds(tuxes[i], half_width, WORLD_HALF_HEIGHT);
                write_vertex(vertices[i], tuxes[i]);
            }
        }

        void bounce_from_bounds(TuxSprite &tux, float half_width, float half_height) const {
            const float radius = collision_radius(tux);
            if (tux.position.x < -half_width + radius) {
                tux.position.x = -half_width + radius;
                tux.velocity.x = std::abs(tux.velocity.x);
            } else if (tux.position.x > half_width - radius) {
                tux.position.x = half_width - radius;
                tux.velocity.x = -std::abs(tux.velocity.x);
            }

            if (tux.position.y < -half_height + radius) {
                tux.position.y = -half_height + radius;
                tux.velocity.y = std::abs(tux.velocity.y);
            } else if (tux.position.y > half_height - radius) {
                tux.position.y = half_height - radius;
                tux.velocity.y = -std::abs(tux.velocity.y);
            }
        }

        void resolve_collisions() {
            for (size_t i = 0; i < active_tuxes; ++i) {
                for (size_t j = i + 1; j < active_tuxes; ++j) {
                    resolve_collision(tuxes[i], tuxes[j]);
                }
            }
        }

        void resolve_collision(TuxSprite &a, TuxSprite &b) const {
            glm::vec2 delta = b.position - a.position;
            float distance = glm::length(delta);
            const float min_distance = collision_radius(a) + collision_radius(b);
            if (distance >= min_distance) {
                return;
            }

            if (distance < 0.0001f) {
                const float angle = random_float(0.0f, 6.28318f);
                delta = glm::vec2(std::cos(angle), std::sin(angle));
                distance = 1.0f;
            }

            const glm::vec2 normal = delta / distance;
            const float overlap = min_distance - distance;
            a.position -= normal * (overlap * 0.5f);
            b.position += normal * (overlap * 0.5f);

            const glm::vec2 relative_velocity = b.velocity - a.velocity;
            const float velocity_along_normal = glm::dot(relative_velocity, normal);
            if (velocity_along_normal > 0.0f) {
                return;
            }

            constexpr float restitution = 0.95f;
            const float impulse = -(1.0f + restitution) * velocity_along_normal * 0.5f;
            a.velocity -= impulse * normal;
            b.velocity += impulse * normal;

            const glm::vec2 tangent(-normal.y, normal.x);
            const float deflect = random_float(-0.22f, 0.22f);
            a.velocity += tangent * deflect;
            b.velocity -= tangent * deflect;
            limit_speed(a.velocity);
            limit_speed(b.velocity);
        }

        void limit_speed(glm::vec2 &velocity) const {
            const float speed = glm::length(velocity);
            if (speed < 0.65f) {
                velocity = glm::normalize(velocity) * 0.65f;
            } else if (speed > 2.2f) {
                velocity = glm::normalize(velocity) * 2.2f;
            }
        }

        [[nodiscard]] float collision_radius(const TuxSprite &tux) const {
            const VkExtent2D extent = getSwapchainExtent();
            const float height = extent.height > 0U ? static_cast<float>(extent.height) : 720.0f;
            return (tux.size * SPRITE_COLLISION_SCALE / height) * WORLD_HALF_HEIGHT * 2.0f;
        }

        void write_vertex(mxvk::PointSpriteVertex &vertex, const TuxSprite &tux) const {
            vertex.position[0] = tux.position.x;
            vertex.position[1] = tux.position.y;
            vertex.position[2] = tux.z;
            vertex.size = tux.size;
            vertex.color[0] = std::floor(tux.frame) / static_cast<float>(FRAME_COUNT - 1);
            vertex.color[1] = tux.velocity.x < 0.0f ? 1.0f : 0.0f;
            vertex.color[2] = tux.tint;
            vertex.color[3] = 1.0f;
        }

        [[nodiscard]] glm::mat4 make_mvp() const {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 16.0f / 9.0f;
            glm::mat4 projection = glm::ortho(-WORLD_HALF_HEIGHT * aspect, WORLD_HALF_HEIGHT * aspect, -WORLD_HALF_HEIGHT, WORLD_HALF_HEIGHT, -1.0f, 1.0f);
            projection[1][1] *= -1.0f;
            return projection;
        }

        std::string data_root{};
        std::vector<TuxSprite> tuxes{};
        std::vector<mxvk::PointSpriteVertex> vertices{};
        mxvk::VK_PointSpriteBatch point_batch{};
        size_t active_tuxes = 0;
        float global_time = 0.0f;
        float population_growth_elapsed = 0.0f;
        float size_scale = 1.0f;
        Uint32 last_update_time = 0;
        bool space_was_down = false;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const std::string root = args.path.empty() ? std::string(POINTSPRITE_ASSET_DIR) : args.path;
        example::PointSpriteWindow window(root + "/data", args.width, args.height, args.fullscreen, args.enable_vsync);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", e.text());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
