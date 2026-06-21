#include <cstdlib>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

namespace example {

    struct PyramidPlacement {
        glm::vec3 surface_normal;
        float scale;
        float yaw_degrees;
    };

    struct StarParticle {
        glm::vec3 position{0.0f};
        float speed = 0.0f;
        float size = 0.0f;
        float brightness = 0.0f;
        float twinkle_speed = 0.0f;
        float twinkle_phase = 0.0f;
        float drift = 0.0f;
        glm::vec3 color{1.0f};
    };

    SDL_Surface *loadColorKeyedPNG(const std::string &path, std::uint8_t threshold = 12, std::uint8_t softness = 48) {
        SDL_Surface *loaded_surface = mxvk::LoadPNG(path.c_str());
        if (loaded_surface == nullptr) {
            throw mxvk::Exception("Failed to load PNG: " + path);
        }

        SDL_Surface *surface = SDL_ConvertSurface(loaded_surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded_surface);
        if (surface == nullptr) {
            throw mxvk::Exception("Failed to convert PNG to RGBA: " + path);
        }

        const SDL_PixelFormatDetails *format_details = SDL_GetPixelFormatDetails(surface->format);
        if (format_details == nullptr) {
            SDL_DestroySurface(surface);
            throw mxvk::Exception("Failed to query pixel format details for: " + path);
        }

        if (!SDL_LockSurface(surface)) {
            SDL_DestroySurface(surface);
            throw mxvk::Exception("Failed to lock PNG surface: " + path);
        }

        auto *pixels = static_cast<std::uint32_t *>(surface->pixels);
        const int pixel_count = surface->w * surface->h;
        for (int i = 0; i < pixel_count; ++i) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);

            const int brightness = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
            if (brightness <= threshold) {
                a = 0;
            } else if (brightness < static_cast<int>(threshold) + static_cast<int>(softness)) {
                const float fade = static_cast<float>(brightness - threshold) / static_cast<float>(std::max<int>(1, softness));
                a = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a) * fade)), 0, 255));
            }

            pixels[i] = SDL_MapRGBA(format_details, nullptr, r, g, b, a);
        }

        SDL_UnlockSurface(surface);
        return surface;
    }

    class MoonWindow : public mxvk::VK_Window {
      public:
        MoonWindow(const std::string &filename,
                   const std::string &path,
                   const std::string &fragment_path,
                   const std::string &title,
                   int width,
                   int height,
                   bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              asset_root(path.empty() ? std::string(MOON_ASSET_DIR) : path) {
            const std::string model_path = filename.empty() ? (asset_root + "/data/moon.obj") : filename;
            const std::string texture_base_path = asset_root + "/data";
            const std::string vert_path = std::string(MOON_SHADER_DIR) + "/model.vert.spv";
            const std::string frag_path = fragment_path.empty() ? (std::string(MOON_SHADER_DIR) + "/model.frag.spv") : fragment_path;

            model.load(this, model_path, "", texture_base_path, 1.0f);
            model.setShaders(this, vert_path, frag_path);

            const std::string pyramid_path = asset_root + "/data/pyramid.obj";
            for (mxvk::VKAbstractModel &pyramid : pyramids) {
                pyramid.load(this, pyramid_path, "", texture_base_path, 1.0f);
                pyramid.setShaders(this, vert_path, frag_path);
            }

            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> star_surface(loadColorKeyedPNG(asset_root + "/data/star.png"), SDL_DestroySurface);
            star_sprite = createSprite3D(star_surface.get());
            if (star_sprite == nullptr) {
                throw mxvk::Exception("Failed to create moon starfield sprite batch");
            }
            star_sprite->setDepthTestEnabled(true);
            star_sprite->setDepthWriteEnabled(false);
            star_sprite->setAlphaDiscardThreshold(0.01f);
            initStars();
        }

        ~MoonWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
            for (mxvk::VKAbstractModel &pyramid : pyramids) {
                pyramid.cleanup(this);
            }
            if (star_sprite != nullptr) {
                star_sprite->cleanup();
            }
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                if (!e.key.repeat) {
                    auto_spin_enabled = !auto_spin_enabled;
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = true;
                last_mouse_x = static_cast<int>(e.button.x);
                last_mouse_y = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouse_dragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouse_dragging) {
                const int x = static_cast<int>(e.motion.x);
                const int y = static_cast<int>(e.motion.y);
                const int delta_x = x - last_mouse_x;
                const int delta_y = y - last_mouse_y;

                yaw_degrees += static_cast<float>(delta_x) * mouse_sensitivity;
                pitch_degrees += static_cast<float>(delta_y) * mouse_sensitivity;
                pitch_degrees = std::clamp(pitch_degrees, -80.0f, 80.0f);

                last_mouse_x = x;
                last_mouse_y = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                camera_distance -= delta * 0.45f;
                camera_distance = std::clamp(camera_distance, 1.8f, 12.0f);
                return;
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
            for (mxvk::VKAbstractModel &pyramid : pyramids) {
                pyramid.resize(this);
            }
            if (star_sprite != nullptr) {
                star_sprite->resize(this);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const auto now = std::chrono::steady_clock::now();
            const float elapsed_seconds = std::chrono::duration<float>(now - start_time).count();
            const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
            last_frame_time = now;
            if (auto_spin_enabled) {
                auto_spin_radians += delta_seconds * auto_spin_speed;
            }

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            glm::mat4 moon_rotation = glm::rotate(glm::mat4(1.0f), glm::radians(pitch_degrees), glm::vec3(1.0f, 0.0f, 0.0f));
            moon_rotation = glm::rotate(moon_rotation, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
            moon_rotation = glm::rotate(moon_rotation, auto_spin_radians, glm::vec3(0.0f, 1.0f, 0.0f));

            mxvk::UniformBufferObject ubo{};
            ubo.model = moon_rotation;
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.1f, camera_distance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsed_seconds, 0.0f, 0.0f, 0.37f);

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);

            for (size_t i = 0; i < pyramids.size(); ++i) {
                mxvk::UniformBufferObject pyramid_ubo = ubo;
                pyramid_ubo.model = pyramidTransform(moon_rotation, pyramids[i], pyramid_placements[i]);
                pyramids[i].updateUBO(imageIndex, pyramid_ubo);
                pyramids[i].render(cmd, imageIndex, false);
            }

            if (star_sprite != nullptr) {
                updateStars(elapsed_seconds);
                star_sprite->updateCamera(imageIndex, ubo.view, ubo.proj);
                for (const StarParticle &star : stars) {
                    const float twinkle = 0.72f + 0.28f * std::sin((elapsed_seconds * star.twinkle_speed) + star.twinkle_phase);
                    const float depth_fade = std::clamp((-star.position.z - 8.0f) / 42.0f, 0.2f, 1.0f);
                    const float alpha = std::clamp(star.brightness * twinkle * depth_fade, 0.08f, 0.95f);
                    const float size = star.size * 1.8f * (0.72f + (1.0f - depth_fade) * 0.38f) * twinkle;
                    star_sprite->drawSprite(star.position, glm::vec2(size), glm::vec4(star.color, alpha));
                }
                star_sprite->render(cmd, imageIndex);
                star_sprite->clearQueue();
            }
        }

      private:
        void initStars() {
            stars.reserve(STAR_COUNT);
            for (size_t i = 0; i < STAR_COUNT; ++i) {
                stars.push_back(makeStar(true));
            }
        }

        void updateStars(float elapsed_seconds) {
            const float delta_seconds = std::clamp(elapsed_seconds - last_star_update_seconds, 0.0f, 0.05f);
            last_star_update_seconds = elapsed_seconds;

            for (StarParticle &star : stars) {
                star.position.z += star.speed * delta_seconds;
                star.position.x += std::sin(elapsed_seconds * 0.26f + star.twinkle_phase) * star.drift * delta_seconds;
                star.position.y += std::cos(elapsed_seconds * 0.19f + star.twinkle_phase) * star.drift * delta_seconds;
                if (star.position.z > -6.0f) {
                    star = makeStar(false);
                }
            }
        }

        [[nodiscard]] StarParticle makeStar(bool randomize_depth) {
            StarParticle star{};
            star.position.x = randomFloat(-26.0f, 26.0f);
            star.position.y = randomFloat(-16.0f, 16.0f);
            star.position.z = randomize_depth ? randomFloat(-66.0f, -8.0f) : randomFloat(-66.0f, -52.0f);
            star.speed = randomFloat(0.16f, 1.25f);
            star.brightness = randomFloat(0.22f, 1.0f);
            star.twinkle_speed = randomFloat(1.5f, 5.5f);
            star.twinkle_phase = randomFloat(0.0f, 6.28318530718f);
            star.drift = randomFloat(0.01f, 0.09f);

            const float density = randomFloat(0.0f, 1.0f);
            if (density < 0.58f) {
                star.size = randomFloat(0.030f, 0.075f);
                star.brightness *= randomFloat(0.55f, 0.85f);
            } else if (density < 0.88f) {
                star.size = randomFloat(0.075f, 0.150f);
                star.brightness *= randomFloat(0.75f, 1.0f);
            } else {
                star.size = randomFloat(0.150f, 0.320f);
                star.brightness *= randomFloat(0.95f, 1.20f);
            }

            const float tint = randomFloat(0.0f, 1.0f);
            if (tint < 0.24f) {
                star.color = glm::vec3(0.70f, 0.82f, 1.0f);
            } else if (tint < 0.48f) {
                star.color = glm::vec3(0.86f, 0.92f, 1.0f);
            } else if (tint < 0.72f) {
                star.color = glm::vec3(1.0f, 0.95f, 0.82f);
            } else if (tint < 0.90f) {
                star.color = glm::vec3(1.0f, 0.86f, 0.66f);
            } else {
                star.color = glm::vec3(1.0f, 1.0f, 1.0f);
            }
            return star;
        }

        [[nodiscard]] float randomFloat(float min_value, float max_value) {
            std::uniform_real_distribution<float> dist(min_value, max_value);
            return dist(random_engine);
        }

        [[nodiscard]] static glm::mat4 surfaceBasis(const glm::vec3 &surface_normal, float yaw_degrees) {
            const glm::vec3 normal = glm::normalize(surface_normal);
            const glm::vec3 reference = (std::abs(normal.y) > 0.92f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 tangent = glm::normalize(glm::cross(reference, normal));
            glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));

            const float yaw = glm::radians(yaw_degrees);
            tangent = glm::normalize((std::cos(yaw) * tangent) + (std::sin(yaw) * bitangent));
            bitangent = glm::normalize(glm::cross(normal, tangent));

            glm::mat4 basis(1.0f);
            basis[0] = glm::vec4(tangent, 0.0f);
            basis[1] = glm::vec4(normal, 0.0f);
            basis[2] = glm::vec4(bitangent, 0.0f);
            return basis;
        }

        [[nodiscard]] static glm::mat4 pyramidTransform(const glm::mat4 &moon_rotation,
                                                        const mxvk::VKAbstractModel &pyramid,
                                                        const PyramidPlacement &placement) {
            constexpr float MOON_RADIUS = 1.28f;
            const glm::vec3 normal = glm::normalize(placement.surface_normal);
            const float surface_offset = MOON_RADIUS + (placement.scale * 0.55f);

            glm::mat4 transform = moon_rotation;
            transform = glm::translate(transform, normal * surface_offset);
            transform *= surfaceBasis(normal, placement.yaw_degrees);
            transform = glm::scale(transform, glm::vec3(pyramid.modelRenderScale() * placement.scale));
            transform = glm::translate(transform, pyramid.modelCenterOffset());
            return transform;
        }

        std::string asset_root;
        mxvk::VKAbstractModel model{};
        std::array<mxvk::VKAbstractModel, 7> pyramids{};
        mxvk::VK_Sprite3D *star_sprite = nullptr;
        std::vector<StarParticle> stars{};
        std::default_random_engine random_engine{1337U};
        const std::array<PyramidPlacement, 7> pyramid_placements{{
            {glm::vec3(0.18f, 0.98f, 0.08f), 0.055f, 12.0f},
            {glm::vec3(-0.45f, 0.74f, 0.50f), 0.075f, 51.0f},
            {glm::vec3(0.58f, 0.62f, 0.53f), 0.048f, 138.0f},
            {glm::vec3(-0.76f, 0.43f, -0.20f), 0.062f, 204.0f},
            {glm::vec3(0.70f, 0.22f, -0.58f), 0.085f, 296.0f},
            {glm::vec3(-0.12f, -0.18f, 0.98f), 0.045f, 33.0f},
            {glm::vec3(0.30f, -0.50f, -0.81f), 0.070f, 250.0f},
        }};
        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        bool mouse_dragging = false;
        bool auto_spin_enabled = true;
        int last_mouse_x = 0;
        int last_mouse_y = 0;
        float yaw_degrees = 0.0f;
        float pitch_degrees = 8.0f;
        float camera_distance = 4.3f;
        float mouse_sensitivity = 0.35f;
        float auto_spin_speed = 0.45f;
        float auto_spin_radians = 0.0f;
        float last_star_update_seconds = 0.0f;
        std::chrono::steady_clock::time_point last_frame_time{std::chrono::steady_clock::now()};
        static constexpr size_t STAR_COUNT = 1800;
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::MoonWindow window(args.filename, args.path, args.fragmentPath, "MXVK Moon Example", args.width, args.height, args.fullscreen);
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
