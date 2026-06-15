#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace example {
    namespace {
        constexpr float k_pi = 3.14159265358979323846f;

        SDL_Surface *load_transparent_saucer(const std::string &png_path) {
            SDL_Surface *loaded_surface = mxvk::LoadPNG(png_path.c_str());
            if (loaded_surface == nullptr) {
                throw mxvk::Exception("Failed to load sprite3D image: " + png_path);
            }

            SDL_Surface *rgba_surface = SDL_ConvertSurface(loaded_surface, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(loaded_surface);
            if (rgba_surface == nullptr) {
                throw mxvk::Exception(std::format("Failed to convert sprite3D image to RGBA: {}", png_path));
            }

            const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(rgba_surface->format);
            if (format_details == nullptr) {
                SDL_DestroySurface(rgba_surface);
                throw mxvk::Exception("Failed to query pixel format details for: " + png_path);
            }

            if (!SDL_LockSurface(rgba_surface)) {
                SDL_DestroySurface(rgba_surface);
                throw mxvk::Exception("Failed to lock sprite3D surface: " + png_path);
            }

            auto *pixels = static_cast<std::uint32_t *>(rgba_surface->pixels);
            const int pixel_count = rgba_surface->w * rgba_surface->h;
            for (int i = 0; i < pixel_count; ++i) {
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                std::uint8_t a = 0;
                SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);

                if ((r | g | b) == 0) {
                    a = 0;
                } else {
                    a = 255;
                }

                pixels[i] = SDL_MapRGBA(format_details, nullptr, r, g, b, a);
            }

            SDL_UnlockSurface(rgba_surface);
            return rgba_surface;
        }

        SDL_Surface *create_star_surface() {
            SDL_Surface *surface = SDL_CreateSurface(40, 40, SDL_PIXELFORMAT_RGBA32);
            if (surface == nullptr) {
                return nullptr;
            }

            const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(surface->format);
            if (format_details == nullptr) {
                SDL_DestroySurface(surface);
                return nullptr;
            }

            if (!SDL_LockSurface(surface)) {
                SDL_DestroySurface(surface);
                return nullptr;
            }

            auto *pixels = static_cast<std::uint32_t *>(surface->pixels);
            for (int y = 0; y < surface->h; ++y) {
                for (int x = 0; x < surface->w; ++x) {
                    const float dx = (static_cast<float>(x) + 0.5f) - (static_cast<float>(surface->w) * 0.5f);
                    const float dy = (static_cast<float>(y) + 0.5f) - (static_cast<float>(surface->h) * 0.5f);
                    const float dist = std::sqrt((dx * dx) + (dy * dy)) / (static_cast<float>(surface->w) * 0.5f);
                    const float core = std::clamp(1.0f - dist * 1.9f, 0.0f, 1.0f);
                    const float glow = std::clamp(1.0f - dist * 3.8f, 0.0f, 1.0f);
                    const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround((core * core * 255.0f) + (glow * 80.0f)));
                    const std::uint8_t brightness = static_cast<std::uint8_t>(std::lround((core * 255.0f) + (glow * 150.0f)));
                    pixels[y * surface->w + x] = SDL_MapRGBA(format_details, nullptr, brightness, brightness, brightness, alpha);
                }
            }

            SDL_UnlockSurface(surface);
            return surface;
        }

        struct SaucerFlight {
            glm::vec3 orbit_center;
            float orbit_radius;
            float orbit_speed;
            float orbit_phase;
            float bob_amplitude;
            float bob_speed;
            float base_size;
            float pulse_amount;
            float pulse_speed;
            float roll_speed;
            glm::vec4 tint;
        };

        struct StarParticle {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float vx = 0.0f;
            float vy = 0.0f;
            float vz = 0.0f;
            float magnitude = 0.0f;
            float temperature = 0.0f;
            float twinkle = 0.0f;
            float size = 0.0f;
            bool is_constellation = false;
        };

        float random_float(float min_value, float max_value) {
            static thread_local std::default_random_engine engine{std::random_device{}()};
            std::uniform_real_distribution<float> dist(min_value, max_value);
            return dist(engine);
        }
    } // namespace

    class ExampleWindow : public mxvk::VK_Window {
      public:
        ExampleWindow(const std::string &path, const std::string &text, int width, int height, bool fullscreen)
            : mxvk::VK_Window(text, width, height, fullscreen, MXVK_VALIDATION) {
            current_path = path.empty() ? std::string(sprite3d_example_ASSET_DIR) : path;
            if (current_path == ".") {
                current_path = sprite3d_example_ASSET_DIR;
            }

            setClearColor(0.02f, 0.03f, 0.06f, 1.0f);

            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> star_surface(create_star_surface(), SDL_DestroySurface);
            if (star_surface == nullptr) {
                throw mxvk::Exception("Failed to create starfield sprite texture");
            }
            stars_sprite = createSprite3D(star_surface.get());
            if (stars_sprite == nullptr) {
                throw mxvk::Exception("Failed to create starfield sprite batch");
            }
            stars_sprite->setDepthTestEnabled(true);
            stars_sprite->setDepthWriteEnabled(false);
            stars_sprite->setAlphaDiscardThreshold(0.01f);

            const std::string image_path = current_path + "/data/saucer.png";
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> saucer_surface(load_transparent_saucer(image_path), SDL_DestroySurface);
            sprite = createSprite3D(saucer_surface.get());

            if (sprite == nullptr) {
                throw mxvk::Exception("Failed to create sprite3D example sprite");
            }

            sprite->setDepthTestEnabled(true);
            sprite->setDepthWriteEnabled(false);
            sprite->setAlphaDiscardThreshold(0.01f);

            initStars(25000);
            initSaucers();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = true;
                last_mouse_x = static_cast<int>(e.button.x);
                last_mouse_y = static_cast<int>(e.button.y);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = false;
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && mouse_dragging) {
                const int mouse_x = static_cast<int>(e.motion.x);
                const int mouse_y = static_cast<int>(e.motion.y);
                const int delta_x = mouse_x - last_mouse_x;
                const int delta_y = mouse_y - last_mouse_y;

                yaw_degrees += static_cast<float>(delta_x) * mouse_sensitivity;
                pitch_degrees -= static_cast<float>(delta_y) * mouse_sensitivity;
                pitch_degrees = std::clamp(pitch_degrees, -85.0f, 85.0f);

                last_mouse_x = mouse_x;
                last_mouse_y = mouse_y;
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                applyWheelZoom(delta);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
            }
        }

        void proc() override {}

        void onSwapchainRecreated() override {
            if (stars_sprite != nullptr) {
                stars_sprite->resize(this);
            }
            if (sprite != nullptr) {
                sprite->resize(this);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (sprite == nullptr) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const float elapsed_seconds = std::chrono::duration<float>(now - start_time).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            const float yaw_radians = glm::radians(yaw_degrees);
            const float pitch_radians = glm::radians(pitch_degrees);
            const glm::vec3 camera_position(
                camera_distance * std::cos(pitch_radians) * std::sin(yaw_radians),
                camera_distance * std::sin(pitch_radians),
                camera_distance * std::cos(pitch_radians) * std::cos(yaw_radians));
            glm::mat4 view = glm::lookAt(camera_position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 proj = glm::perspective(glm::radians(48.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            updateStars(elapsed_seconds);

            if (stars_sprite != nullptr) {
                stars_sprite->updateCamera(imageIndex, view, proj);
                for (const StarParticle &star : stars) {
                    const float twinkle = 0.78f + 0.22f * std::sin(elapsed_seconds * star.twinkle);
                    const float star_size = star.size * twinkle * (star.is_constellation ? 1.25f : 1.0f);
                    const float alpha = std::clamp(((6.5f - star.magnitude) / 6.5f) * twinkle, 0.0f, 1.0f);
                    const glm::vec3 color = starColor(star.temperature);
                    stars_sprite->drawSprite(glm::vec3(star.x, star.y, star.z),
                                             glm::vec2(star_size, star_size),
                                             glm::vec4(color, alpha));
                }
                stars_sprite->render(cmd, imageIndex);
                stars_sprite->clearQueue();
            }

            sprite->updateCamera(imageIndex, view, proj);

            for (const SaucerFlight &saucer : saucers) {
                const float orbit_angle = saucer.orbit_phase + elapsed_seconds * saucer.orbit_speed;
                const float pulse = 1.0f + std::sin(elapsed_seconds * saucer.pulse_speed + saucer.orbit_phase) * saucer.pulse_amount;
                const float bob = std::sin(elapsed_seconds * saucer.bob_speed + saucer.orbit_phase) * saucer.bob_amplitude;
                const float roll = orbit_angle * 0.85f + elapsed_seconds * saucer.roll_speed;

                const glm::vec3 position(
                    saucer.orbit_center.x + std::cos(orbit_angle) * saucer.orbit_radius,
                    saucer.orbit_center.y + bob,
                    saucer.orbit_center.z + std::sin(orbit_angle) * saucer.orbit_radius * 0.62f);

                const glm::vec2 size(saucer.base_size * pulse, saucer.base_size * pulse * 0.72f);
                sprite->drawSprite(position, size, saucer.tint, roll);
            }

            sprite->render(cmd, imageIndex);
            sprite->clearQueue();
        }

      private:
        void initSaucers() {
            static const std::array<SaucerFlight, 5> base_saucers{{
                {{0.0f, 0.0f, 0.0f}, 1.8f, 1.0f, 0.0f, 0.24f, 2.2f, 0.50f, 0.22f, 2.0f, 0.5f, {1.0f, 1.0f, 1.0f, 1.0f}},
                {{0.0f, 0.18f, 0.0f}, 2.35f, 0.82f, 1.3f, 0.30f, 1.5f, 0.42f, 0.18f, 1.6f, -0.7f, {1.0f, 0.95f, 0.88f, 1.0f}},
                {{0.0f, -0.16f, 0.0f}, 2.8f, 0.67f, 2.6f, 0.34f, 1.9f, 0.35f, 0.24f, 1.2f, 0.85f, {0.88f, 0.96f, 1.0f, 1.0f}},
                {{0.0f, 0.08f, 0.0f}, 3.25f, 0.54f, 4.0f, 0.28f, 1.2f, 0.30f, 0.30f, 0.9f, 0.55f, {1.0f, 0.90f, 0.96f, 1.0f}},
                {{0.0f, 0.0f, 0.0f}, 3.8f, 0.43f, 5.2f, 0.40f, 1.0f, 0.27f, 0.34f, 0.7f, 0.45f, {0.92f, 1.0f, 0.92f, 1.0f}},
            }};

            saucers.reserve(base_saucers.size() * 4U);
            for (int ring = 0; ring < 4; ++ring) {
                const float ring_offset = static_cast<float>(ring) * 0.28f;
                const float ring_speed_scale = 1.0f + static_cast<float>(ring) * 0.06f;
                const float ring_phase_offset = static_cast<float>(ring) * 0.9f;
                for (std::size_t i = 0; i < base_saucers.size(); ++i) {
                    SaucerFlight flight = base_saucers[i];
                    flight.orbit_center.x += (static_cast<float>(i) - 2.0f) * 0.12f;
                    flight.orbit_center.y += (static_cast<float>(ring) - 1.5f) * 0.06f;
                    flight.orbit_radius += ring_offset + static_cast<float>(i) * 0.05f;
                    flight.orbit_speed *= ring_speed_scale * 1.35f;
                    flight.orbit_phase += ring_phase_offset + static_cast<float>(i) * 0.4f;
                    flight.bob_amplitude *= 1.0f + static_cast<float>(ring) * 0.08f;
                    flight.base_size *= 1.0f + static_cast<float>(ring) * 0.05f;
                    flight.pulse_amount *= 1.0f + static_cast<float>(ring) * 0.03f;
                    flight.pulse_speed *= (1.0f + static_cast<float>(ring) * 0.05f) * 1.15f;
                    flight.roll_speed *= 1.25f;
                    saucers.push_back(flight);
                }
            }
        }

        void initStars(int count) {
            stars.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                stars.push_back(makeStar());
            }
        }

        void updateStars(float elapsed_seconds) {
            constexpr float max_radius = 120.0f;
            constexpr float min_radius = 20.0f;
            constexpr float dt = 1.0f / 60.0f;

            for (StarParticle &star : stars) {
                star.x += star.vx * dt;
                star.y += star.vy * dt;
                star.z += star.vz * dt;

                const float radius_squared = (star.x * star.x) + (star.y * star.y) + (star.z * star.z);
                if (radius_squared < (min_radius * min_radius) || radius_squared > (max_radius * max_radius)) {
                    star = makeStar();
                }
            }

            (void)elapsed_seconds;
        }

        StarParticle makeStar() const {
            StarParticle star{};
            constexpr float max_radius = 120.0f;
            constexpr float min_radius = 20.0f;

            const float theta = random_float(0.0f, 2.0f * k_pi);
            const float phi = std::acos(random_float(-1.0f, 1.0f));
            const float radius = random_float(min_radius, max_radius);

            star.x = radius * std::sin(phi) * std::cos(theta);
            star.y = radius * std::sin(phi) * std::sin(theta);
            star.z = radius * std::cos(phi);

            star.vx = random_float(-0.04f, 0.04f);
            star.vy = random_float(-0.04f, 0.04f);
            star.vz = random_float(-0.04f, 0.04f);

            const float rarity = random_float(0.0f, 1.0f);
            if (rarity < 0.05f) {
                star.magnitude = random_float(-1.0f, 2.0f);
            } else if (rarity < 0.3f) {
                star.magnitude = random_float(2.0f, 4.0f);
            } else {
                star.magnitude = random_float(4.0f, 6.5f);
            }

            if (star.magnitude < 3.0f) {
                star.temperature = random_float(4000.0f, 8000.0f);
            } else {
                star.temperature = random_float(2500.0f, 6000.0f);
            }

            star.twinkle = random_float(0.5f, 3.0f);
            star.size = std::clamp(0.52f - star.magnitude * 0.045f, 0.10f, 0.45f);
            star.is_constellation = (star.magnitude < 3.0f) && (random_float(0.0f, 1.0f) < 0.3f);
            return star;
        }

        static glm::vec3 starColor(float temperature) {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;

            if (temperature < 3700.0f) {
                r = 1.0f;
                g = temperature / 3700.0f * 0.6f;
                b = 0.0f;
            } else if (temperature < 5200.0f) {
                r = 1.0f;
                g = 0.6f + (temperature - 3700.0f) / 1500.0f * 0.4f;
                b = (temperature - 3700.0f) / 1500.0f * 0.3f;
            } else if (temperature < 6000.0f) {
                r = 1.0f;
                g = 1.0f;
                b = (temperature - 5200.0f) / 800.0f * 0.7f;
            } else if (temperature < 7500.0f) {
                r = 1.0f;
                g = 1.0f;
                b = 0.7f + (temperature - 6000.0f) / 1500.0f * 0.3f;
            } else {
                r = 0.7f - (temperature - 7500.0f) / 10000.0f * 0.4f;
                g = 0.8f + (temperature - 7500.0f) / 10000.0f * 0.2f;
                b = 1.0f;
            }

            return glm::vec3(r, g, b);
        }

        void applyWheelZoom(float wheel_y) {
            if (wheel_y == 0.0f) {
                return;
            }

            const float zoom_step = 1.15f;
            const float zoom_factor = std::pow(zoom_step, -wheel_y);
            camera_distance = std::clamp(camera_distance * zoom_factor, min_camera_distance, max_camera_distance);
        }

        std::string current_path = ".";
        mxvk::VK_Sprite3D *stars_sprite = nullptr;
        mxvk::VK_Sprite3D *sprite = nullptr;
        std::vector<StarParticle> stars;
        std::vector<SaucerFlight> saucers;
        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        float camera_distance = 10.0f;
        float min_camera_distance = 3.0f;
        float max_camera_distance = 20.0f;
        bool mouse_dragging = false;
        int last_mouse_x = 0;
        int last_mouse_y = 0;
        float yaw_degrees = 0.0f;
        float pitch_degrees = 10.0f;
        float mouse_sensitivity = 0.20f;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::ExampleWindow window(args.path, "VK_Sprite3D Example", args.width, args.height, args.fullscreen);
        window.loop();
    } catch (mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
