#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_point_sprite_batch.hpp"

#include <algorithm>
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
#include <glm/gtc/random.hpp>

namespace {

    constexpr std::size_t PARTICLES_PER_EXPLOSION = 250;
    constexpr std::size_t MAX_EXPLOSIONS = 256;
    constexpr std::size_t MAX_PARTICLES = PARTICLES_PER_EXPLOSION * MAX_EXPLOSIONS;
    constexpr float MIN_LIFETIME = 0.5f;
    constexpr float MAX_LIFETIME = 2.0f;
    constexpr float PARTICLE_SPEED = 1.0f;
    constexpr float PARTICLE_SIZE = 25.0f;

    struct Particle {
        glm::vec3 position{};
        glm::vec3 velocity{};
        glm::vec4 color{};
        float lifetime = 0.0f;
        float initial_lifetime = 1.0f;
    };

    struct Explosion {
        std::vector<Particle> particles{};
    };

    [[nodiscard]] float random_float(float min, float max) {
        static std::random_device rd;
        static std::default_random_engine engine(rd());
        std::uniform_real_distribution<float> dist(min, max);
        return dist(engine);
    }

} // namespace

namespace example {

    class FireworksWindow : public mxvk::VK_Window {
      public:
        FireworksWindow(const std::string &data_root, int width, int height, bool fullscreen, bool enable_vsync)
            : mxvk::VK_Window("Stars - [3D Particle Effect]", width, height, fullscreen, MXVK_VALIDATION, enable_vsync),
              data_root(data_root),
              vertices(MAX_PARTICLES) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            setFont(data_root + "/font.ttf", 24);
            last_update_time = SDL_GetTicks();
        }

        ~FireworksWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            point_batch.cleanup();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                trigger_random_explosion();
            }
        }

        void proc() override {
            printText("Explosion - Press Space to Trigger", 25, 25, SDL_Color{255, 64, 64, 255});
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

            update_particles(delta_time);
            write_vertices();

            point_batch.upload_vertices(vertices.data(), active_vertices);
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
                data_root + "/star.png",
                data_root + "/fireworks.vert.spv",
                data_root + "/fireworks.frag.spv",
                vertices.size());
            point_batch.set_additive_blending(true);
            point_batch.set_depth_test_enabled(false);
            point_batch.set_depth_write_enabled(false);
            last_update_time = SDL_GetTicks();
            return true;
        }

        void trigger_random_explosion() {
            trigger_explosion(glm::vec3(random_float(-3.0f, 3.0f), random_float(-3.0f, 3.0f), 0.0f));
        }

        void trigger_explosion(const glm::vec3 &origin) {
            if (explosions.size() >= MAX_EXPLOSIONS) {
                return;
            }

            Explosion explosion;
            explosion.particles.resize(PARTICLES_PER_EXPLOSION);
            for (Particle &particle : explosion.particles) {
                reset_particle(particle, origin);
            }
            explosions.push_back(std::move(explosion));
        }

        void reset_particle(Particle &particle, const glm::vec3 &origin) const {
            particle.position = origin;
            particle.velocity = glm::sphericalRand(PARTICLE_SPEED);
            particle.lifetime = random_float(MIN_LIFETIME, MAX_LIFETIME);
            particle.initial_lifetime = particle.lifetime;
            particle.color = glm::vec4(random_float(0.2f, 1.0f), random_float(0.2f, 1.0f), random_float(0.2f, 1.0f), 1.0f);
        }

        void update_particles(float delta_time) {
            for (Explosion &explosion : explosions) {
                for (Particle &particle : explosion.particles) {
                    if (particle.lifetime <= 0.0f) {
                        continue;
                    }
                    particle.lifetime -= delta_time;
                    particle.position += particle.velocity * delta_time;
                    particle.color.a = particle.lifetime;
                }
            }

            std::erase_if(explosions, [](const Explosion &explosion) {
                return std::ranges::none_of(explosion.particles, [](const Particle &particle) {
                    return particle.lifetime > 0.0f;
                });
            });
        }

        void write_vertices() {
            active_vertices = 0;
            for (const Explosion &explosion : explosions) {
                for (const Particle &particle : explosion.particles) {
                    if (particle.lifetime <= 0.0f || active_vertices >= vertices.size()) {
                        continue;
                    }

                    mxvk::PointSpriteVertex &vertex = vertices[active_vertices++];
                    vertex.position[0] = particle.position.x;
                    vertex.position[1] = particle.position.y;
                    vertex.position[2] = particle.position.z;
                    vertex.size = PARTICLE_SIZE;
                    vertex.color[0] = particle.color.r;
                    vertex.color[1] = particle.color.g;
                    vertex.color[2] = particle.color.b;
                    vertex.color[3] = particle.color.a;
                }
            }
        }

        [[nodiscard]] glm::mat4 make_mvp() const {
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = extent.height > 0U ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 16.0f / 9.0f;
            glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            projection[1][1] *= -1.0f;
            const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 model(1.0f);
            return projection * view * model;
        }

        std::string data_root{};
        std::vector<Explosion> explosions{};
        std::vector<mxvk::PointSpriteVertex> vertices{};
        mxvk::VK_PointSpriteBatch point_batch{};
        std::size_t active_vertices = 0;
        Uint32 last_update_time = 0;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const std::string root = args.path.empty() ? std::string(FIREWORKS_ASSET_DIR) : args.path;
        example::FireworksWindow window(root + "/data", args.width, args.height, args.fullscreen, args.enable_vsync);
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
