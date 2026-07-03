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

    enum class StarType {
        NORMAL,
        BRIGHT,
        BLUE,
        ORANGE,
        RED,
        YELLOW
    };

    struct Particle {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float life = 0.0f;
        float twinkle = 0.0f;
        float twinkle_phase = 0.0f;
        StarType type = StarType::NORMAL;
        int layer = 0;
        int texture_index = 0;
        float pulse_speed = 0.0f;
        float base_size = 0.0f;
    };

    struct LayerConfig {
        float z_min = 0.0f;
        float z_max = 0.0f;
        float speed_multiplier = 0.0f;
        float size_multiplier = 0.0f;
        int count = 0;
    };

    constexpr int NUM_PARTICLES = 50000;
    constexpr int NUM_LAYERS = 3;
    constexpr int WARP_TRAIL_SEGMENTS = 5;

    constexpr std::array<LayerConfig, NUM_LAYERS> LAYERS{{
        {-12.0f, -6.0f, 0.2f, 0.6f, 28000},
        {-6.0f, -3.0f, 0.5f, 1.0f, 14000},
        {-3.0f, -1.0f, 1.0f, 1.6f, 8000},
    }};

    [[nodiscard]] float random_float(float min, float max) {
        static std::random_device rd;
        static std::default_random_engine engine(rd());
        std::uniform_real_distribution<float> dist(min, max);
        return dist(engine);
    }

    [[nodiscard]] StarType random_star_type() {
        const float r = random_float(0.0f, 1.0f);
        if (r < 0.50f) {
            return StarType::NORMAL;
        }
        if (r < 0.65f) {
            return StarType::BLUE;
        }
        if (r < 0.75f) {
            return StarType::YELLOW;
        }
        if (r < 0.85f) {
            return StarType::ORANGE;
        }
        if (r < 0.92f) {
            return StarType::RED;
        }
        return StarType::BRIGHT;
    }

    [[nodiscard]] glm::vec4 star_color(StarType type, float brightness) {
        switch (type) {
        case StarType::BLUE:
            return glm::vec4(0.6f * brightness, 0.8f * brightness, 1.0f * brightness, 1.0f);
        case StarType::ORANGE:
            return glm::vec4(1.0f * brightness, 0.7f * brightness, 0.3f * brightness, 1.0f);
        case StarType::RED:
            return glm::vec4(1.0f * brightness, 0.4f * brightness, 0.4f * brightness, 1.0f);
        case StarType::YELLOW:
            return glm::vec4(1.0f * brightness, 1.0f * brightness, 0.6f * brightness, 1.0f);
        case StarType::BRIGHT:
            return glm::vec4(1.0f * brightness, 1.0f * brightness, 1.0f * brightness, 1.0f);
        case StarType::NORMAL:
        default:
            return glm::vec4(0.9f * brightness, 0.9f * brightness, 1.0f * brightness, 1.0f);
        }
    }

} // namespace

namespace example {

    class StarfieldWindow : public mxvk::VK_Window {
      public:
        StarfieldWindow(const std::string &data_root, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("MXVK Starfield", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              data_root(data_root),
              particles(NUM_PARTICLES),
              vertices(NUM_PARTICLES * WARP_TRAIL_SEGMENTS) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            init_particles();
            last_update_time = SDL_GetTicks();
        }

        ~StarfieldWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            point_batch.cleanup();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                boost_requested = true;
            } else if (e.type == SDL_EVENT_KEY_UP && e.key.key == SDLK_SPACE) {
                boost_requested = false;
            }
        }

        void onSwapchainRecreated() override {
            point_batch.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
            if (!ensure_starfield_resources()) {
                return;
            }

            const Uint32 current_time = SDL_GetTicks();
            float delta_time = static_cast<float>(current_time - last_update_time) / 1000.0f;
            last_update_time = current_time;
            if (delta_time > 0.1f) {
                delta_time = 0.1f;
            }
            global_time += delta_time;
            update_boost(delta_time);

            const size_t active_vertices = update_particles(delta_time);
            point_batch.upload_vertices(vertices.data(), active_vertices);
            point_batch.update_mvp(image_index, make_mvp());
            point_batch.render(cmd, image_index);
        }

      private:
        bool ensure_starfield_resources() {
            if (point_batch.loaded()) {
                return true;
            }
            if (device == VK_NULL_HANDLE || command_pool == VK_NULL_HANDLE || getSwapchainImageCount() == 0U || getSwapchainFormat() == VK_FORMAT_UNDEFINED) {
                return false;
            }

            point_batch.load(
                this,
                data_root + "/star.png",
                data_root + "/starfield.vert.spv",
                data_root + "/starfield.frag.spv",
                vertices.size());
            point_batch.set_additive_blending(true);
            point_batch.set_depth_test_enabled(false);
            point_batch.set_depth_write_enabled(false);
            last_update_time = SDL_GetTicks();
            return true;
        }

        void init_particles() {
            int particle_index = 0;
            for (int layer = 0; layer < NUM_LAYERS; ++layer) {
                for (int i = 0; i < LAYERS[layer].count && particle_index < NUM_PARTICLES; ++i, ++particle_index) {
                    init_particle(particles[static_cast<size_t>(particle_index)], layer);
                    particles[static_cast<size_t>(particle_index)].z = random_float(LAYERS[layer].z_min, 0.0f);
                }
            }
        }

        void init_particle(Particle &particle, int layer) {
            const auto &cfg = LAYERS[static_cast<size_t>(layer)];
            particle.layer = layer;
            particle.x = random_float(-4.0f, 4.0f);
            particle.y = random_float(-4.0f, 4.0f);
            particle.z = random_float(cfg.z_min, cfg.z_max);
            particle.vx = random_float(-0.01f, 0.01f) * cfg.speed_multiplier;
            particle.vy = random_float(-0.01f, 0.01f) * cfg.speed_multiplier;
            particle.vz = random_float(0.15f, 0.35f) * cfg.speed_multiplier;
            particle.life = random_float(0.7f, 1.0f);
            particle.twinkle = random_float(2.0f, 8.0f);
            particle.twinkle_phase = random_float(0.0f, 6.28f);
            particle.type = random_star_type();
            particle.texture_index = random_float(0.0f, 1.0f) > 0.5f ? 1 : 0;
            particle.pulse_speed = random_float(0.5f, 3.0f);

            const float type_multiplier = particle.type == StarType::BRIGHT ? 2.0f : 1.0f;
            particle.base_size = random_float(12.0f, 28.0f) * cfg.size_multiplier * type_multiplier;
        }

        void update_boost(float delta_time) {
            const float target = boost_requested ? 1.0f : 0.0f;
            const float rate = boost_requested ? 4.0f : 2.5f;
            if (boost_amount < target) {
                boost_amount = std::min(target, boost_amount + delta_time * rate);
            } else if (boost_amount > target) {
                boost_amount = std::max(target, boost_amount - delta_time * rate);
            }
        }

        [[nodiscard]] size_t update_particles(float delta_time) {
            const float speed_multiplier = 1.0f + boost_amount * 10.0f;
            const float forward_boost = boost_amount * 2.85f;
            const float brightness_boost = 1.0f + boost_amount * 1.15f;
            const float size_boost = 1.0f + boost_amount * 0.85f;
            const float warp_trail_amount = glm::smoothstep(0.05f, 1.0f, boost_amount);
            size_t vertex_count = 0;

            for (int i = 0; i < NUM_PARTICLES; ++i) {
                auto &particle = particles[static_cast<size_t>(i)];
                particle.x += particle.vx * delta_time;
                particle.y += particle.vy * delta_time;
                particle.z += (particle.vz * speed_multiplier + forward_boost) * delta_time;

                if (particle.z > 0.0f) {
                    init_particle(particle, particle.layer);
                }

                if (particle.x > 4.0f) {
                    particle.x = -4.0f;
                }
                if (particle.x < -4.0f) {
                    particle.x = 4.0f;
                }
                if (particle.y > 4.0f) {
                    particle.y = -4.0f;
                }
                if (particle.y < -4.0f) {
                    particle.y = 4.0f;
                }

                const float twinkle1 = 0.5f * (1.0f + std::sin(global_time * particle.twinkle + particle.twinkle_phase));
                const float twinkle2 = 0.3f * (1.0f + std::sin(global_time * particle.pulse_speed * 2.0f + particle.twinkle_phase * 1.5f));
                const float twinkle_factor = 0.5f + 0.3f * twinkle1 + 0.2f * twinkle2;

                const auto &cfg = LAYERS[static_cast<size_t>(particle.layer)];
                float depth_factor = 1.0f - (particle.z / cfg.z_min);
                depth_factor = glm::clamp(depth_factor, 0.3f, 1.0f);

                const float brightness = particle.life * twinkle_factor * depth_factor * brightness_boost;
                const glm::vec4 color = star_color(particle.type, brightness);
                const float size_pulse = 1.0f + 0.2f * std::sin(global_time * particle.pulse_speed + particle.twinkle_phase);
                const float size = particle.base_size * size_pulse * depth_factor * size_boost;
                const float alpha = particle.life * glm::clamp(depth_factor + 0.2f, 0.0f, 1.0f);

                const float radial_distance = std::sqrt(particle.x * particle.x + particle.y * particle.y);
                const float radial_factor = radial_distance > 0.0001f ? 1.0f / radial_distance : 0.0f;
                const float direction_x = particle.x * radial_factor;
                const float direction_y = particle.y * radial_factor;
                const float warp_spread = 1.0f + warp_trail_amount * (0.35f + depth_factor * 1.8f);
                const float display_x = particle.x * warp_spread;
                const float display_y = particle.y * warp_spread;

                write_vertex(vertex_count++, display_x, display_y, particle.z, size, color, alpha);

                if (warp_trail_amount > 0.0f) {
                    const int trail_segments = 1 + static_cast<int>(warp_trail_amount * static_cast<float>(WARP_TRAIL_SEGMENTS - 1));
                    const float trail_spacing = warp_trail_amount * (0.05f + depth_factor * 0.18f);
                    for (int trail = 1; trail < trail_segments; ++trail) {
                        const float trail_step = static_cast<float>(trail) / static_cast<float>(WARP_TRAIL_SEGMENTS - 1);
                        const float trail_alpha = alpha * warp_trail_amount * (1.0f - trail_step) * 0.55f;
                        const float trail_size = size * (1.0f + warp_trail_amount * (0.9f - trail_step * 0.45f));
                        const float trail_x = display_x - direction_x * trail_spacing * static_cast<float>(trail);
                        const float trail_y = display_y - direction_y * trail_spacing * static_cast<float>(trail);
                        const float trail_z = particle.z - warp_trail_amount * 0.02f * static_cast<float>(trail);
                        write_vertex(vertex_count++, trail_x, trail_y, trail_z, trail_size, color, trail_alpha);
                    }
                }
            }

            return vertex_count;
        }

        void write_vertex(size_t index, float x, float y, float z, float size, const glm::vec4 &color, float alpha) {
            auto &vertex = vertices[index];
            vertex.position[0] = x;
            vertex.position[1] = y;
            vertex.position[2] = z;
            vertex.size = size;
            vertex.color[0] = color.r;
            vertex.color[1] = color.g;
            vertex.color[2] = color.b;
            vertex.color[3] = alpha;
        }

        [[nodiscard]] glm::mat4 make_mvp() const {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            projection[1][1] *= -1.0f;

            const glm::vec3 camera_pos(
                camera_zoom * std::sin(glm::radians(camera_rotation)),
                0.0f,
                camera_zoom * std::cos(glm::radians(camera_rotation)));
            const glm::mat4 view = glm::lookAt(camera_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            return projection * view;
        }

        std::string data_root{};
        std::vector<Particle> particles{};
        std::vector<mxvk::PointSpriteVertex> vertices{};
        mxvk::VK_PointSpriteBatch point_batch{};
        float camera_zoom = 0.09f;
        float camera_rotation = 356.0f;
        float global_time = 0.0f;
        Uint32 last_update_time = 0;
        bool boost_requested = false;
        float boost_amount = 0.0f;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const std::string root = args.path.empty() ? std::string(STARFIELD_ASSET_DIR) : args.path;
        example::StarfieldWindow window(root + "/data", args.width, args.height, args.fullscreen, args.enable_vsync);
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
