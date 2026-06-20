#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_console.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <ostream>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr int GAME_STARS = 22000;
constexpr int MAX_PROJECTILES = 64;
constexpr int MAX_ASTEROIDS = 49;
constexpr int MAX_PARTICLES = 6000;
constexpr int MAX_GENERATIONS = 1;
constexpr int CHILDREN_PER_SPAWN = 2;
constexpr int LARGE_ASTEROID_POINTS = 20;
constexpr int MEDIUM_ASTEROID_POINTS = 50;
constexpr int SMALL_ASTEROID_POINTS = 100;
constexpr float PROJECTILE_SPEED = 52.0f;
constexpr float PROJECTILE_LIFETIME = 3.0f;
constexpr int FIRE_COOLDOWN = 5;
constexpr int SHOTS_PER_BURST = 5;
constexpr int FIRE_DELAY = 3;
constexpr int EXPLOSION_DURATION_FRAMES = 90;
constexpr float SHIP_RADIUS = 3.0f;
constexpr float BOUNDARY_X_MIN = -150.0f;
constexpr float BOUNDARY_X_MAX = 150.0f;
constexpr float BOUNDARY_Y_MIN = -100.0f;
constexpr float BOUNDARY_Y_MAX = 100.0f;
constexpr float BOUNDARY_Z_MIN = -150.0f;
constexpr float BOUNDARY_Z_MAX = 150.0f;
constexpr float BOUNDARY_BOUNCE_FACTOR = 1.2f;

enum class GameMode {
    Intro,
    Loading,
    Playing
};

std::default_random_engine &rng() {
    static thread_local std::default_random_engine engine{std::random_device{}()};
    return engine;
}

float random_float(float min_value, float max_value) {
    std::uniform_real_distribution<float> dist(min_value, max_value);
    return dist(rng());
}

int random_int(int min_value, int max_value) {
    std::uniform_int_distribution<int> dist(min_value, max_value);
    return dist(rng());
}

SDL_Surface *load_color_keyed_png(const std::string &path, std::uint8_t threshold = 12, std::uint8_t softness = 48) {
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

    struct KeyedPixel {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 0;
    };

    std::vector<KeyedPixel> keyed(static_cast<std::size_t>(pixel_count));
    for (int i = 0; i < pixel_count; ++i) {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 0;
        SDL_GetRGBA(pixels[i], format_details, nullptr, &r, &g, &b, &a);
        const int brightness = std::max({static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)});
        if (brightness <= threshold) {
            keyed[static_cast<std::size_t>(i)] = {r, g, b, 0};
            continue;
        }
        const int soft_end = static_cast<int>(threshold) + static_cast<int>(softness);
        if (brightness < soft_end) {
            const float t = static_cast<float>(brightness - threshold) / static_cast<float>(std::max<int>(1, softness));
            a = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<float>(a) * t)), 0, 255));
        }
        keyed[static_cast<std::size_t>(i)] = {r, g, b, a};
    }

    constexpr int COLOR_BLEED_PASSES = 5;
    for (int pass = 0; pass < COLOR_BLEED_PASSES; ++pass) {
        std::vector<KeyedPixel> next = keyed;
        for (int y = 0; y < surface->h; ++y) {
            for (int x = 0; x < surface->w; ++x) {
                const int index = y * surface->w + x;
                if (keyed[static_cast<std::size_t>(index)].a != 0) {
                    continue;
                }

                int red = 0;
                int green = 0;
                int blue = 0;
                int count = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (nx < 0 || ny < 0 || nx >= surface->w || ny >= surface->h) {
                            continue;
                        }
                        const KeyedPixel &neighbor = keyed[static_cast<std::size_t>(ny * surface->w + nx)];
                        if (neighbor.a == 0) {
                            continue;
                        }
                        red += neighbor.r;
                        green += neighbor.g;
                        blue += neighbor.b;
                        ++count;
                    }
                }
                if (count > 0) {
                    KeyedPixel &out = next[static_cast<std::size_t>(index)];
                    out.r = static_cast<std::uint8_t>(red / count);
                    out.g = static_cast<std::uint8_t>(green / count);
                    out.b = static_cast<std::uint8_t>(blue / count);
                }
            }
        }
        keyed = std::move(next);
    }

    for (int i = 0; i < pixel_count; ++i) {
        const KeyedPixel &px = keyed[static_cast<std::size_t>(i)];
        pixels[i] = SDL_MapRGBA(format_details, nullptr, px.r, px.g, px.b, px.a);
    }

    SDL_UnlockSurface(surface);
    return surface;
}

glm::vec3 normalize_or_zero(const glm::vec3 &value) {
    const float len = glm::length(value);
    if (len <= 1e-6f) {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return value / len;
}

glm::mat4 build_model_matrix(const glm::vec3 &position,
                             const glm::vec3 &rotation_degrees,
                             float scale,
                             const glm::vec3 &center_offset) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(scale));
    model = glm::translate(model, center_offset);
    return model;
}

struct Projectile {
    glm::vec3 position{0.0f};
    glm::vec3 prev_position{0.0f};
    glm::vec3 velocity{0.0f};
    float lifetime = 0.0f;
    bool active = false;
};

struct Asteroid {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 rotation_speed{0.0f};
    float radius = 0.0f;
    bool active = false;
    int generation = 0;
    int model_index = 0;
};

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f};
    float size = 0.0f;
    float lifetime = 0.0f;
    float max_lifetime = 0.0f;
    bool active = false;
};

struct FlameVertex {
    glm::vec3 pos{};
    glm::vec4 color{};
};

struct FlamePushConstants {
    glm::mat4 mvp{1.0f};
    glm::vec4 params{0.0f};
};

struct Star {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 base_color{1.0f};
    glm::vec4 color{1.0f};
    float size = 1.0f;
    float brightness = 1.0f;
    float twinkle_phase = 0.0f;
    float twinkle_speed = 1.0f;
    int layer = 0;
};

struct Ship {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 rotation{0.0f};
    float current_speed = 1.0f;
    float min_speed = 0.1f;
    float max_speed = 25.0f;
    float turn_speed = 140.0f;
    float pitch_speed_multiplier = 0.55f;
    float camera_distance = 6.0f;
    float camera_height = 1.6f;
    bool visible = true;
    bool exploding = false;
    int explosion_timer = 0;
    int lives = 5;
    int score = 0;
    int fire_cooldown = 0;
    int burst_count = 0;
    int continuous_fire_timer = 0;
    bool overheated = false;
    int overheat_cooldown = 0;

    glm::vec3 forward() const {
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        return normalize_or_zero(glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    }
};

class StarField {
  public:
    StarField() = default;

    void init(int star_count, float min_radius, float max_radius) {
        if (initialized) {
            return;
        }

        stars.resize(static_cast<size_t>(star_count));
        this->min_radius = min_radius;
        this->max_radius = max_radius;
        for (auto &star : stars) {
            respawn(star, glm::vec3(0.0f));
        }
        initialized = true;
    }

    void setSprite(mxvk::VK_Sprite3D *sprite_batch) {
        sprite = sprite_batch;
    }

    void resize(mxvk::VK_Window *window) {
        if (sprite != nullptr) {
            sprite->resize(window);
        }
    }

    void update(float delta_time, const glm::vec3 &camera_position, float elapsed_time) {
        for (auto &star : stars) {
            star.position += star.velocity * delta_time;
            const float distance = glm::length(star.position - camera_position);
            if (distance > max_radius * 1.2f || distance < min_radius * 0.35f) {
                respawn(star, camera_position);
            }

        const float twinkle = 0.65f + 0.35f * std::sin(elapsed_time * star.twinkle_speed + star.twinkle_phase);
        const float fade = std::clamp(1.0f - ((distance - min_radius) / (max_radius - min_radius)), 0.2f, 0.7f);
        const float brightness = star.brightness * twinkle * fade;
        star.color = glm::vec4(
            std::clamp(star.base_color.r * brightness, 0.0f, 1.0f),
            std::clamp(star.base_color.g * brightness, 0.0f, 1.0f),
            std::clamp(star.base_color.b * brightness, 0.0f, 1.0f),
            std::clamp(0.2f + brightness, 0.0f, 1.0f));
        }
    }

    void draw() {
        if (sprite == nullptr) {
            return;
        }
        for (const auto &star : stars) {
            sprite->drawSprite(star.position, glm::vec2(star.size), star.color);
        }
    }

  private:
    std::vector<Star> stars{};
    mxvk::VK_Sprite3D *sprite = nullptr;
    bool initialized = false;
    float min_radius = 50.0f;
    float max_radius = 200.0f;

    void respawn(Star &star, const glm::vec3 &center) {
        const float theta = random_float(0.0f, 2.0f * PI);
        const float phi = std::acos(random_float(-1.0f, 1.0f));
        const float radius = random_float(min_radius, max_radius);

        star.position.x = center.x + radius * std::sin(phi) * std::cos(theta);
        star.position.y = center.y + radius * std::sin(phi) * std::sin(theta);
        star.position.z = center.z + radius * std::cos(phi);
        star.velocity = glm::vec3(
            random_float(-0.06f, 0.06f),
            random_float(-0.06f, 0.06f),
            random_float(-0.06f, 0.06f));

        const float roll = random_float(0.0f, 1.0f);
        if (roll < 0.5f) {
            star.base_color = {0.90f, 0.90f, 1.00f, 1.0f};
            star.size = random_float(0.026f, 0.056f);
        } else if (roll < 0.65f) {
            star.base_color = {0.60f, 0.78f, 1.00f, 1.0f};
            star.size = random_float(0.034f, 0.073f);
        } else if (roll < 0.75f) {
            star.base_color = {1.00f, 0.95f, 0.65f, 1.0f};
            star.size = random_float(0.035f, 0.078f);
        } else if (roll < 0.85f) {
            star.base_color = {1.00f, 0.66f, 0.32f, 1.0f};
            star.size = random_float(0.042f, 0.090f);
        } else if (roll < 0.92f) {
            star.base_color = {1.00f, 0.40f, 0.40f, 1.0f};
            star.size = random_float(0.045f, 0.095f);
        } else {
            star.base_color = {1.00f, 1.00f, 1.00f, 1.0f};
            star.size = random_float(0.070f, 0.133f);
        }

        star.brightness = random_float(0.25f, 1.0f);
        star.color = star.base_color;
        star.twinkle_phase = random_float(0.0f, 2.0f * PI);
        star.twinkle_speed = random_float(0.4f, 3.2f);
        star.layer = roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2);
    }
};

} // namespace

namespace example {

class Asteroids3DWindow : public mxvk::VK_Window {
  public:
    Asteroids3DWindow(const std::string &path, int width, int height, bool fullscreen)
        : mxvk::VK_Window("3D Asteroids", width, height, fullscreen, MXVK_VALIDATION),
          asset_root((path.empty() || path == ".") ? std::string(ASTEROIDS3D_ASSET_DIR) : path) {
        if (asset_root == ".") {
            asset_root = ASTEROIDS3D_ASSET_DIR;
        }

        setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        load_loading_screen_resources();
        configure_console();
    }

    ~Asteroids3DWindow() override {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        cleanup_flame_resources();
        ship_model.cleanup(this);
        for (auto &model : asteroid_models) {
            model.cleanup(this);
        }
        if (star_sprite != nullptr) {
            star_sprite->cleanup();
        }
        if (projectile_sprite != nullptr) {
            projectile_sprite->cleanup();
        }
        if (effect_sprite != nullptr) {
            effect_sprite->cleanup();
        }
    }

    void event(SDL_Event &e) override {
        const bool was_console_visible = console.isVisible();
        const bool is_console_toggle = e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F3;
        console.handleEvent(e);
        if (is_console_toggle) {
            log_game(console.isVisible() ? "Console opened." : "Console closed.");
            return;
        }
        if (was_console_visible) {
            return;
        }

        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
            if (mode == GameMode::Playing) {
                mode = GameMode::Intro;
                intro_fade = 1.0f;
                intro_last_update_ms = SDL_GetTicks();
                log_game("Returned to intro screen.");
            } else {
                log_game("Exit requested from intro screen.");
                exit();
            }
            return;
        }
        if (mode == GameMode::Intro &&
            e.type == SDL_EVENT_KEY_DOWN &&
            (e.key.key == SDLK_SPACE || e.key.key == SDLK_RETURN)) {
            intro_fade = 0.01f;
            log_game("Intro skipped. Starting game.");
            return;
        }
        if (mode == GameMode::Playing && e.type == SDL_EVENT_KEY_DOWN) {
            if (e.key.key == SDLK_F1) {
                debug_menu = !debug_menu;
                log_game(std::string("Debug HUD ") + (debug_menu ? "enabled." : "disabled."));
                return;
            }
            if (e.key.key == SDLK_F2) {
                inverted_controls = !inverted_controls;
                log_game(std::string("Controls set to ") + (inverted_controls ? "inverted." : "arcade."));
                return;
            }
            if (e.key.key == SDLK_RETURN) {
                restart_game();
                log_game("Game restarted from keyboard.");
                return;
            }
        }
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE && ship.lives <= 0) {
            restart_game();
            log_game("Game restarted after game over.");
        }
    }

    void onSwapchainRecreated() override {
        ship_model.resize(this);
        for (auto &model : asteroid_models) {
            model.resize(this);
        }
        if (star_sprite != nullptr) {
            star_sprite->resize(this);
        }
        if (projectile_sprite != nullptr) {
            projectile_sprite->resize(this);
        }
        if (effect_sprite != nullptr) {
            effect_sprite->resize(this);
        }
        if (intro_sprite != nullptr) {
            intro_sprite->rebuildPipeline();
        }
        cleanup_flame_swapchain_resources();
        create_flame_swapchain_resources();
    }

    void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override {
        current_command_buffer = cmd;
        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
        last_frame_time = now;
        const float dt = std::min(delta_seconds, 0.1f);
        last_delta_time = dt;
        elapsed_seconds += dt;

        const VkExtent2D extent = getSwapchainExtent();
        const float aspect = (extent.height > 0U) ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

        if (mode == GameMode::Intro) {
            draw_intro(extent);
            console.draw();
            return;
        }

        if (mode == GameMode::Loading) {
            draw_loading(extent);
            console.draw();
            return;
        }

        if (!console.isVisible()) {
            handle_input(dt);
        }
        update_ship(dt);
        update_projectiles(dt);
        update_asteroids(dt);
        update_particles(dt);

        if (ship.lives <= 0 && !ship.exploding) {
            if (star_sprite != nullptr) {
                star_sprite->clearQueue();
            }
            if (projectile_sprite != nullptr) {
                projectile_sprite->clearQueue();
            }
            if (effect_sprite != nullptr) {
                effect_sprite->clearQueue();
            }
            draw_game_over(image_index, aspect);
            console.draw();
            return;
        }

        const glm::vec3 ship_forward = ship.forward();
        const glm::vec3 camera_target = ship.position + ship_forward * 6.0f;
        const glm::vec3 ideal_camera = ship.position - ship_forward * ship.camera_distance + glm::vec3(0.0f, ship.camera_height, 0.0f);
        camera_position = glm::mix(camera_position, ideal_camera, 1.0f - std::exp(-dt * 10.0f));
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        view_matrix = glm::lookAt(camera_position, camera_target, up);
        projection_matrix = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 500.0f);
        projection_matrix[1][1] *= -1.0f;

        star_field.update(dt, camera_position, elapsed_seconds);
        star_field.setSprite(star_sprite);

        star_sprite->updateCamera(image_index, view_matrix, projection_matrix);
        projectile_sprite->updateCamera(image_index, view_matrix, projection_matrix);
        effect_sprite->updateCamera(image_index, view_matrix, projection_matrix);

        star_field.draw();
        star_sprite->render(cmd, image_index);
        star_sprite->clearQueue();

        draw_asteroids(image_index);
        draw_ship(image_index);
        draw_engine_flame(cmd, extent);
        draw_projectiles();
        draw_particles();
        projectile_sprite->render(cmd, image_index);
        projectile_sprite->clearQueue();
        effect_sprite->render(cmd, image_index);
        effect_sprite->clearQueue();

        if (!console.isVisible()) {
            draw_hud(aspect);
        }
        console.draw();
    }

  private:
    std::string asset_root;
    std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
    float elapsed_seconds = 0.0f;
    GameMode mode = GameMode::Intro;
    float intro_fade = 1.0f;
    Uint32 intro_last_update_ms = 0;
    bool debug_menu = false;
    bool inverted_controls = false;
    float keyboard_yaw = 0.0f;
    float keyboard_pitch = 0.0f;
    float keyboard_roll = 0.0f;
    float smooth_yaw = 0.0f;
    float smooth_pitch = 0.0f;
    float smooth_roll = 0.0f;

    Ship ship{};
    std::array<Projectile, MAX_PROJECTILES> projectiles{};
    std::array<Asteroid, MAX_ASTEROIDS> asteroids{};
    std::array<Particle, MAX_PARTICLES> particles{};
    StarField star_field{};
    glm::vec3 camera_position{0.0f, 1.6f, 6.0f};
    glm::mat4 view_matrix{1.0f};
    glm::mat4 projection_matrix{1.0f};

    mxvk::VKAbstractModel ship_model{};
    std::array<mxvk::VKAbstractModel, MAX_ASTEROIDS> asteroid_models{};
    mxvk::VK_Sprite3D *star_sprite = nullptr;
    mxvk::VK_Sprite3D *projectile_sprite = nullptr;
    mxvk::VK_Sprite3D *effect_sprite = nullptr;
    mxvk::VK_Sprite *intro_sprite = nullptr;
    mxvk::VK_Console console;
    bool console_ready = false;
    bool game_resources_loaded = false;
    int loading_step_index = 0;
    static constexpr int loading_step_count = 11;
    glm::mat4 last_ship_model_matrix{1.0f};
    VkBuffer flame_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory flame_vertex_buffer_memory = VK_NULL_HANDLE;
    VkPipeline flame_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout flame_pipeline_layout = VK_NULL_HANDLE;
    uint32_t flame_vertex_count = 0;

    void log_game(const std::string &message, SDL_Color color = SDL_Color{180, 220, 255, 255}) {
        if (!console_ready) {
            return;
        }
        console.printLine("[game] " + message, color);
    }

    void configure_console() {
        console.attach(*this, asset_root + "/data/font.ttf", 20);
        console.setSpriteYOriginTopLeft(true);
        console.setPrompt("asteroids> ");
        console_ready = true;
        console.printLine("Press F3 to open/close the console.");
        console.printLine("Type 'help' for asteroids3d commands.");
        log_game("Console attached.");
        log_game("asteroids3d initialized.");
        console.setCommandCallback([this](mxvk::VK_Window &, const std::vector<std::string> &args, std::ostream &out) {
            if (args.empty()) {
                return true;
            }

            const std::string &cmd = args.front();
            if (cmd == "help") {
                out << "asteroids3d commands:\n"
                    << "  clear              Clear console output\n"
                    << "  echo <text>        Print text to the console\n"
                    << "  status             Print score, lives, mode, and asteroid count\n"
                    << "  restart            Restart the game\n"
                    << "  intro              Return to the intro screen\n"
                    << "  play               Start or resume play\n"
                    << "  debug              Toggle debug HUD\n"
                    << "  controls           Toggle arcade/inverted pitch controls\n"
                    << "  about              Print program banner\n"
                    << "  quit / exit        Close the window\n";
                return true;
            }

            if (cmd == "echo") {
                for (std::size_t i = 1; i < args.size(); ++i) {
                    if (i > 1) {
                        out << ' ';
                    }
                    out << args[i];
                }
                return true;
            }

            if (cmd == "status") {
                out << "Mode: " << ((mode == GameMode::Intro) ? "intro" : "playing") << '\n'
                    << "Score: " << ship.score << '\n'
                    << "Lives: " << std::max(0, ship.lives) << '\n'
                    << "Asteroids: " << active_asteroids() << '\n'
                    << "Speed: " << ship.current_speed << " / " << ship.max_speed << '\n'
                    << "Controls: " << (inverted_controls ? "inverted" : "arcade") << '\n'
                    << "Debug HUD: " << (debug_menu ? "on" : "off") << '\n';
                return true;
            }

            if (cmd == "restart") {
                restart_game();
                mode = GameMode::Playing;
                log_game("Game restarted from console.");
                out << "Game restarted.";
                return true;
            }

            if (cmd == "intro") {
                mode = GameMode::Intro;
                intro_fade = 1.0f;
                intro_last_update_ms = SDL_GetTicks();
                log_game("Returned to intro screen from console.");
                out << "Intro screen active.";
                return true;
            }

            if (cmd == "play") {
                mode = GameMode::Playing;
                log_game("Play mode activated from console.");
                out << "Playing.";
                return true;
            }

            if (cmd == "debug") {
                debug_menu = !debug_menu;
                log_game(std::string("Debug HUD ") + (debug_menu ? "enabled from console." : "disabled from console."));
                out << "Debug HUD " << (debug_menu ? "enabled." : "disabled.");
                return true;
            }

            if (cmd == "controls") {
                inverted_controls = !inverted_controls;
                log_game(std::string("Controls set to ") + (inverted_controls ? "inverted from console." : "arcade from console."));
                out << "Controls set to " << (inverted_controls ? "inverted." : "arcade.");
                return true;
            }

            if (cmd == "about") {
                out << "asteroids3d: MXVK port of gl_asteroids.\n";
                return true;
            }

            if (cmd == "quit" || cmd == "exit") {
                log_game("Exit requested from console.");
                out << "Closing window...";
                exit();
                return true;
            }

            return false;
        });
    }

    void load_loading_screen_resources() {
        setFont(asset_root + "/data/font.ttf", 18);

        intro_sprite = createSprite(
            asset_root + "/data/intro.png",
            asset_root + "/data/sprite.vert.spv",
            std::string(ASTEROIDS3D_SHADER_DIR) + "/intro.frag.spv");
        intro_last_update_ms = SDL_GetTicks();
        loading_step_index = 0;
        game_resources_loaded = false;
    }

    void draw_intro(const VkExtent2D &extent) {
        if (intro_sprite == nullptr) {
            mode = GameMode::Loading;
            return;
        }

        const Uint32 current_ms = SDL_GetTicks();
        if ((current_ms - intro_last_update_ms) > 35U) {
            intro_last_update_ms = current_ms;
            intro_fade -= 0.01f;
        }

        if (intro_fade <= 0.0f) {
            mode = GameMode::Loading;
            intro_fade = 1.0f;
            loading_step_index = 0;
            game_resources_loaded = false;
            log_game("Intro finished. Loading game resources.");
            return;
        }

        intro_sprite->setShaderParams(static_cast<float>(current_ms) / 1000.0f, 0.0f, 0.0f, intro_fade);
        intro_sprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
    }

    void draw_loading([[maybe_unused]] const VkExtent2D &extent) {
        if (!game_resources_loaded) {
            const int progress_percent = std::clamp((loading_step_index * 100) / loading_step_count, 0, 100);
            printText("Loading " + std::to_string(progress_percent) + "%", 25, 25, {255, 255, 255, 255});
            load_next_game_resource_step();
            return;
        }

        printText("Loading 100%", 25, 25, {255, 255, 255, 255});
    }

    void load_next_game_resource_step() {
        const std::string model_vert = std::string(ASTEROIDS3D_SHADER_DIR) + "/model.vert.spv";
        const std::string model_frag = std::string(ASTEROIDS3D_SHADER_DIR) + "/model.frag.spv";

        switch (loading_step_index) {
        case 0: {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> star_surface(load_color_keyed_png(asset_root + "/data/particle_star.png", 12), SDL_DestroySurface);
            star_sprite = createSprite3D(star_surface.get());
            if (star_sprite == nullptr) {
                throw mxvk::Exception("Failed to create star sprite batch");
            }
            star_sprite->setDepthTestEnabled(false);
            star_sprite->setDepthWriteEnabled(false);
            star_sprite->setAlphaDiscardThreshold(0.01f);
            break;
        }
        case 1: {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> fire_surface(load_color_keyed_png(asset_root + "/data/particle_explosion.png", 12), SDL_DestroySurface);
            projectile_sprite = createSprite3D(fire_surface.get());
            if (projectile_sprite == nullptr) {
                throw mxvk::Exception("Failed to create projectile sprite batch");
            }
            projectile_sprite->setDepthTestEnabled(true);
            projectile_sprite->setDepthWriteEnabled(false);
            projectile_sprite->setAlphaDiscardThreshold(0.05f);
            break;
        }
        case 2: {
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> explosion_surface(load_color_keyed_png(asset_root + "/data/particle_explosion.png", 12), SDL_DestroySurface);
            effect_sprite = createSprite3D(explosion_surface.get());
            if (effect_sprite == nullptr) {
                throw mxvk::Exception("Failed to create effect sprite batch");
            }
            effect_sprite->setDepthTestEnabled(true);
            effect_sprite->setDepthWriteEnabled(false);
            effect_sprite->setAlphaDiscardThreshold(0.05f);
            break;
        }
        case 3: {
            ship_model.load(this, asset_root + "/data/starship.obj", "", asset_root + "/data", 1.0f);
            break;
        }
        case 4: {
            ship_model.setShaders(this, model_vert, model_frag);
            ship_model.setBackfaceCulling(false);
            break;
        }
        case 5: {
            create_flame_resources();
            break;
        }
        case 6: {
            const int model_index = 0;
            asteroids[0].model_index = model_index;
            asteroid_models[0].load(this, asset_root + "/data/asteroid.mxmod", asset_root + "/data/rock.tex", asset_root + "/data", 1.0f);
            asteroid_models[0].setShaders(this, model_vert, model_frag);
            asteroid_models[0].setBackfaceCulling(false);
            break;
        }
        case 7: {
            const int model_index = 1;
            asteroids[1].model_index = model_index;
            asteroid_models[1].load(this, asset_root + "/data/asteroid2.mxmod", asset_root + "/data/rock2.tex", asset_root + "/data", 1.0f);
            asteroid_models[1].setShaders(this, model_vert, model_frag);
            asteroid_models[1].setBackfaceCulling(false);
            break;
        }
        case 8: {
            const int model_index = 2;
            asteroids[2].model_index = model_index;
            const std::string asteroid_texture = (random_int(0, 1) == 0) ? asset_root + "/data/rock.tex" : asset_root + "/data/rock2.tex";
            asteroid_models[2].load(this, asset_root + "/data/asteroid3.mxmod", asteroid_texture, asset_root + "/data", 1.0f);
            asteroid_models[2].setShaders(this, model_vert, model_frag);
            asteroid_models[2].setBackfaceCulling(false);
            break;
        }
        case 9: {
            star_field.init(GAME_STARS, 4.0f, 30.0f);
            break;
        }
        case 10: {
            restart_game();
            break;
        }
        default:
            game_resources_loaded = true;
            intro_last_update_ms = SDL_GetTicks();
            mode = GameMode::Playing;
            log_game("Loading complete. Game is now playing.");
            return;
        }

        ++loading_step_index;
        if (loading_step_index >= loading_step_count) {
            game_resources_loaded = true;
            intro_last_update_ms = SDL_GetTicks();
            mode = GameMode::Playing;
            log_game("Loading complete. Game is now playing.");
        }
    }

    void restart_game() {
        ship.position = glm::vec3(0.0f);
        ship.velocity = glm::vec3(0.0f);
        ship.rotation = glm::vec3(0.0f);
        ship.current_speed = 1.0f;
        ship.visible = true;
        ship.exploding = false;
        ship.explosion_timer = 0;
        ship.lives = 5;
        ship.score = 0;
        ship.fire_cooldown = 0;
        ship.burst_count = 0;
        ship.continuous_fire_timer = 0;
        ship.overheated = false;
        ship.overheat_cooldown = 0;
        for (auto &projectile : projectiles) {
            projectile.active = false;
        }
        for (auto &particle : particles) {
            particle.active = false;
        }
        for (auto &asteroid : asteroids) {
            asteroid.active = false;
        }
        spawn_initial_asteroids();
        log_game("Game state reset: score=0 lives=5.");
    }

    void spawn_initial_asteroids() {
        for (int i = 0; i < 7; ++i) {
            glm::vec3 position{0.0f};
            do {
                position = glm::vec3(
                    random_float(-90.0f, 90.0f),
                    random_float(-50.0f, 50.0f),
                    random_float(-90.0f, 90.0f));
            } while (glm::length(position - ship.position) < 28.0f);

            spawn_asteroid(position,
                           glm::vec3(random_float(-0.8f, 0.8f), random_float(-0.8f, 0.8f), random_float(-0.8f, 0.8f)),
                           random_float(2.8f, 7.0f),
                           0,
                           random_int(0, 2));
        }
        log_game("Initial asteroid field spawned.");
    }

    void spawn_asteroid(const glm::vec3 &position, const glm::vec3 &velocity, float radius, int generation, int preferred_model_index = -1) {
        Asteroid *free_asteroid = find_free_asteroid(preferred_model_index);
        if (free_asteroid == nullptr && preferred_model_index >= 0) {
            free_asteroid = find_free_asteroid();
        }

        if (free_asteroid == nullptr) {
            log_game("Asteroid spawn skipped: no free asteroid slots.", SDL_Color{255, 190, 90, 255});
            return;
        }

        const int slot_model_index = free_asteroid->model_index;
        free_asteroid->position = position;
        free_asteroid->velocity = velocity;
        free_asteroid->radius = radius;
        free_asteroid->generation = generation;
        free_asteroid->rotation = glm::vec3(random_float(0.0f, 360.0f), random_float(0.0f, 360.0f), random_float(0.0f, 360.0f));
        free_asteroid->rotation_speed = glm::vec3(random_float(-45.0f, 45.0f), random_float(-45.0f, 45.0f), random_float(-45.0f, 45.0f));
        free_asteroid->model_index = slot_model_index;
        free_asteroid->active = true;
        if (generation == 0) {
            log_game(std::format("Asteroid spawned at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}.", position.x, position.y, position.z, radius));
        }
    }

    void handle_input(float dt) {
        const bool *keys = SDL_GetKeyboardState(nullptr);
        if (keys == nullptr) {
            return;
        }

        if (ship.lives <= 0) {
            return;
        }

        auto ramp_axis = [dt](float &value, float target, float rise_rate, float fall_rate) {
            const float rate = (std::fabs(target) > std::fabs(value)) ? rise_rate : fall_rate;
            const float step = rate * dt;
            if (value < target) {
                value = std::min(value + step, target);
            } else if (value > target) {
                value = std::max(value - step, target);
            }
        };

        float yaw_amount = 0.0f;
        float pitch_amount = 0.0f;
        float roll_amount = 0.0f;
        bool manual_roll_input = false;

        float keyboard_yaw_target = 0.0f;
        if (keys[SDL_SCANCODE_LEFT]) {
            keyboard_yaw_target = 1.0f;
        } else if (keys[SDL_SCANCODE_RIGHT]) {
            keyboard_yaw_target = -1.0f;
        }
        ramp_axis(keyboard_yaw, keyboard_yaw_target, 3.2f, 8.0f);
        if (std::fabs(keyboard_yaw) > 0.001f) {
            yaw_amount = keyboard_yaw;
        }

        float keyboard_pitch_target = 0.0f;
        if (inverted_controls) {
            if (keys[SDL_SCANCODE_W]) {
                keyboard_pitch_target = -1.0f;
            }
            if (keys[SDL_SCANCODE_S]) {
                keyboard_pitch_target = 1.0f;
            }
        } else {
            if (keys[SDL_SCANCODE_W]) {
                keyboard_pitch_target = 1.0f;
            }
            if (keys[SDL_SCANCODE_S]) {
                keyboard_pitch_target = -1.0f;
            }
        }
        ramp_axis(keyboard_pitch, keyboard_pitch_target, 2.8f, 8.0f);
        if (std::fabs(keyboard_pitch) > 0.001f) {
            pitch_amount = keyboard_pitch;
        }

        float keyboard_roll_target = 0.0f;
        if (keys[SDL_SCANCODE_A]) {
            keyboard_roll_target = -1.0f;
        } else if (keys[SDL_SCANCODE_D]) {
            keyboard_roll_target = 1.0f;
        }
        ramp_axis(keyboard_roll, keyboard_roll_target, 3.0f, 8.0f);
        if (std::fabs(keyboard_roll) > 0.001f) {
            roll_amount = keyboard_roll;
            manual_roll_input = true;
        }

        const float smoothing = std::clamp(dt * 8.0f, 0.0f, 1.0f);
        smooth_yaw = glm::mix(smooth_yaw, yaw_amount, smoothing);
        smooth_pitch = glm::mix(smooth_pitch, pitch_amount, smoothing);
        smooth_roll = glm::mix(smooth_roll, roll_amount, smoothing);

        if (std::fabs(smooth_yaw) > 0.01f) {
            ship.rotation.y += smooth_yaw * ship.turn_speed * dt;
        }
        if (std::fabs(smooth_pitch) > 0.01f) {
            ship.rotation.x += smooth_pitch * ship.turn_speed * ship.pitch_speed_multiplier * dt;
        }
        if (manual_roll_input && std::fabs(smooth_roll) > 0.01f) {
            ship.rotation.z += smooth_roll * ship.turn_speed * dt;
        }
        if (!manual_roll_input && std::fabs(smooth_yaw) > 0.01f) {
            const float target_roll = -smooth_yaw * 35.0f;
            const float roll_diff = target_roll - ship.rotation.z;
            ship.rotation.z += roll_diff * 5.0f * dt;
        }
        if (!manual_roll_input && std::fabs(smooth_yaw) < 0.01f) {
            while (ship.rotation.z > 180.0f) {
                ship.rotation.z -= 360.0f;
            }
            while (ship.rotation.z < -180.0f) {
                ship.rotation.z += 360.0f;
            }
            ship.rotation.z = glm::mix(ship.rotation.z, 0.0f, 3.0f * dt);
        }

        if (keys[SDL_SCANCODE_UP]) {
            increase_speed(dt);
        } else if (keys[SDL_SCANCODE_DOWN]) {
            decrease_speed(dt);
        } else {
            if (ship.current_speed > 5.0f) {
                decrease_speed(dt * 0.5f);
            } else if (ship.current_speed < 5.0f) {
                increase_speed(dt * 0.5f);
            }
        }

        const bool firing = keys[SDL_SCANCODE_SPACE];
        if (firing) {
            if (can_fire()) {
                fire_projectile();
            }
        } else {
            update_fire_timer(false);
        }
    }

    void increase_speed(float dt) {
        ship.current_speed += ship.turn_speed * dt * 0.2f;
        ship.current_speed = std::min(ship.current_speed, ship.max_speed);
    }

    void decrease_speed(float dt) {
        ship.current_speed -= ship.turn_speed * dt * 0.2f;
        ship.current_speed = std::max(ship.current_speed, ship.min_speed);
    }

    bool can_fire() {
        if (ship.overheated) {
            return false;
        }
        if (ship.fire_cooldown <= 0) {
            if (ship.burst_count < SHOTS_PER_BURST) {
                ship.fire_cooldown = FIRE_DELAY;
                ship.burst_count++;
                return true;
            }
            ship.fire_cooldown = FIRE_COOLDOWN;
            ship.burst_count = 0;
            return false;
        }
        return false;
    }

    void update_fire_timer(bool firing) {
        if (firing && !ship.overheated) {
            ship.continuous_fire_timer++;
            ship.overheat_cooldown = 0;
            if (ship.continuous_fire_timer >= 180) {
                ship.overheated = true;
                ship.overheat_cooldown = 0;
                ship.continuous_fire_timer = 0;
                ship.burst_count = 0;
                log_game("Weapons overheated.", SDL_Color{255, 150, 80, 255});
            }
        } else if (firing && ship.overheated) {
            ship.overheat_cooldown = 0;
        } else {
            if (ship.overheated) {
                ship.overheat_cooldown++;
                if (ship.overheat_cooldown >= 180) {
                    ship.overheated = false;
                    ship.overheat_cooldown = 0;
                    ship.continuous_fire_timer = 0;
                    log_game("Weapons cooled down.");
                }
            } else if (ship.continuous_fire_timer > 0) {
                ship.continuous_fire_timer--;
            }
        }
    }

    void fire_projectile() {
        const glm::vec3 forward = ship.forward();
        const glm::vec3 muzzle = ship.position + forward * 2.5f;
        for (auto &projectile : projectiles) {
            if (projectile.active) {
                continue;
            }
            projectile.position = muzzle;
            projectile.prev_position = muzzle;
            projectile.velocity = forward * PROJECTILE_SPEED;
            projectile.lifetime = 0.0f;
            projectile.active = true;
            log_game(std::format("Projectile fired from ({:.1f}, {:.1f}, {:.1f}).", muzzle.x, muzzle.y, muzzle.z));
            return;
        }
        log_game("Projectile fire skipped: projectile pool full.", SDL_Color{255, 190, 90, 255});
    }

    void update_ship(float dt) {
        if (ship.exploding) {
            ship.explosion_timer--;
            if (ship.explosion_timer <= 0) {
                ship.exploding = false;
                ship.visible = true;
                ship.position = glm::vec3(0.0f);
                ship.velocity = glm::vec3(0.0f);
                ship.rotation = glm::vec3(0.0f);
                ship.current_speed = 1.0f;
                clear_particles();
                log_game("Ship respawned at origin.");
            }
            return;
        }

        const glm::vec3 forward = ship.forward();
        ship.velocity = forward * ship.current_speed;
        ship.position += ship.velocity * dt;
        ship.rotation.x = std::clamp(ship.rotation.x, -75.0f, 75.0f);
        if (ship.rotation.z > 180.0f) {
            ship.rotation.z -= 360.0f;
        } else if (ship.rotation.z < -180.0f) {
            ship.rotation.z += 360.0f;
        }
        if (ship.fire_cooldown > 0) {
            ship.fire_cooldown--;
        }
    }

    void update_projectiles(float dt) {
        for (auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }
            projectile.prev_position = projectile.position;
            projectile.position += projectile.velocity * dt;
            projectile.lifetime += dt;
            if (projectile.lifetime >= PROJECTILE_LIFETIME) {
                projectile.active = false;
            }
        }

        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                continue;
            }
            for (auto &projectile : projectiles) {
                if (!projectile.active) {
                    continue;
                }

                const glm::vec3 segment = projectile.position - projectile.prev_position;
                const float segment_length_sq = glm::dot(segment, segment);

                glm::vec3 closest_point = projectile.prev_position;

                if (segment_length_sq > 1e-6f) {
                    const glm::vec3 to_asteroid = asteroid.position - projectile.prev_position;
                    const float t = std::clamp(glm::dot(to_asteroid, segment) / segment_length_sq, 0.0f, 1.0f);
                    closest_point = projectile.prev_position + segment * t;
                }

                const float dist = glm::length(closest_point - asteroid.position);
                const float projectile_hit_radius = asteroid.radius * 0.97f;

                if (dist < projectile_hit_radius) {
                    projectile.active = false;
                    log_game(std::format("Projectile hit asteroid at ({:.1f}, {:.1f}, {:.1f}).", asteroid.position.x, asteroid.position.y, asteroid.position.z));
                    split_asteroid(asteroid);
                    break;
                }
            }
        }
    }

    void split_asteroid(Asteroid &asteroid) {
        const glm::vec3 hit_position = asteroid.position;
        const int generation = asteroid.generation;
        const float radius = asteroid.radius * 0.5f;

        spawn_asteroid_explosion(hit_position);

        if (generation >= MAX_GENERATIONS) {
            ship.score += SMALL_ASTEROID_POINTS;
            asteroid.active = false;
            log_game(std::format("Small asteroid destroyed. Score={}.", ship.score));
            return;
        }

        const int child_count = CHILDREN_PER_SPAWN;
        const float child_radius = asteroid.radius * 0.18f;
        const glm::vec3 view_forward = normalize_or_zero(hit_position - camera_position);
        glm::vec3 split_axis = glm::cross(view_forward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(split_axis) <= 1e-4f) {
            split_axis = glm::cross(view_forward, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        if (glm::length(split_axis) <= 1e-4f) {
            split_axis = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        split_axis = normalize_or_zero(split_axis);
        const glm::vec3 toward_camera = normalize_or_zero(camera_position - hit_position);
        const float child_separation = std::max(asteroid.radius * 0.9f, child_radius * 4.0f);
        const glm::vec3 depth_bias = toward_camera * (child_separation * 0.25f);
        std::array<glm::vec3, CHILDREN_PER_SPAWN> child_positions = {
            hit_position - split_axis * child_separation + depth_bias,
            hit_position + split_axis * child_separation + depth_bias,
        };

        for (int i = 0; i < child_count; ++i) {
            Asteroid *child = find_free_asteroid(asteroid.model_index);
            if (child == nullptr) {
                log_game("Asteroid split skipped: asteroid pool exhausted.", SDL_Color{255, 190, 90, 255});
                break;
            }

            const glm::vec3 child_offset = child_positions[static_cast<std::size_t>(i)] - hit_position;
            const glm::vec3 child_velocity = normalize_or_zero(child_offset) * random_float(5.0f, 9.0f);
            child->active = true;
            child->position = child_positions[static_cast<std::size_t>(i)];
            child->radius = child_radius;
            child->generation = generation + 1;
            child->rotation = glm::vec3(random_float(0.0f, 360.0f), random_float(0.0f, 360.0f), random_float(0.0f, 360.0f));
            child->rotation_speed = asteroid.rotation_speed * random_float(0.8f, 1.5f);
            child->model_index = asteroid.model_index;
            child->velocity = child_velocity;
            log_game(std::format(
                "Asteroid child {} spawned at ({:.1f}, {:.1f}, {:.1f}) radius {:.1f}.",
                i + 1,
                child->position.x,
                child->position.y,
                child->position.z,
                child->radius));
        }

        if (radius >= 25.0f) {
            ship.score += LARGE_ASTEROID_POINTS;
            log_game(std::format("Large asteroid split into {} pieces. Score={}.", child_count, ship.score));
        } else {
            ship.score += MEDIUM_ASTEROID_POINTS;
            log_game(std::format("Medium asteroid split into {} pieces. Score={}.", child_count, ship.score));
        }

        asteroid.active = false;
    }

    Asteroid *find_free_asteroid(int preferred_model_index = -1) {
        for (auto &asteroid : asteroids) {
            if (!asteroid.active && (preferred_model_index < 0 || asteroid.model_index == preferred_model_index)) {
                return &asteroid;
            }
        }
        return nullptr;
    }

    void update_asteroids(float dt) {
        int active_count = 0;
        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                continue;
            }
            ++active_count;
            asteroid.position += asteroid.velocity * dt;
            asteroid.rotation += asteroid.rotation_speed * dt;
            bool bounced = false;
            if (asteroid.position.x < BOUNDARY_X_MIN) {
                asteroid.position.x = BOUNDARY_X_MIN;
                asteroid.velocity.x = -asteroid.velocity.x * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            } else if (asteroid.position.x > BOUNDARY_X_MAX) {
                asteroid.position.x = BOUNDARY_X_MAX;
                asteroid.velocity.x = -asteroid.velocity.x * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            }
            if (asteroid.position.y < BOUNDARY_Y_MIN) {
                asteroid.position.y = BOUNDARY_Y_MIN;
                asteroid.velocity.y = -asteroid.velocity.y * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            } else if (asteroid.position.y > BOUNDARY_Y_MAX) {
                asteroid.position.y = BOUNDARY_Y_MAX;
                asteroid.velocity.y = -asteroid.velocity.y * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            }
            if (asteroid.position.z < BOUNDARY_Z_MIN) {
                asteroid.position.z = BOUNDARY_Z_MIN;
                asteroid.velocity.z = -asteroid.velocity.z * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            } else if (asteroid.position.z > BOUNDARY_Z_MAX) {
                asteroid.position.z = BOUNDARY_Z_MAX;
                asteroid.velocity.z = -asteroid.velocity.z * BOUNDARY_BOUNCE_FACTOR;
                bounced = true;
            }
            if (bounced) {
                asteroid.velocity += glm::vec3(random_float(-0.5f, 0.5f), random_float(-0.5f, 0.5f), random_float(-0.5f, 0.5f));
            }
            if (glm::length(asteroid.velocity) > 0.01f) {
                asteroid.velocity *= 0.995f;
            }
            if (asteroid.rotation.x > 360.0f) asteroid.rotation.x -= 360.0f;
            if (asteroid.rotation.y > 360.0f) asteroid.rotation.y -= 360.0f;
            if (asteroid.rotation.z > 360.0f) asteroid.rotation.z -= 360.0f;
        }

        if (active_count == 0) {
            spawn_initial_asteroids();
            log_game("Asteroid field reset.");
        }

        for (auto &asteroid : asteroids) {
            if (!asteroid.active) {
                continue;
            }
            const float ship_distance = glm::length(ship.position - asteroid.position);
            const float ship_collision_radius = asteroid.radius * 0.65f + 1.0f;
            if (ship_distance < ship_collision_radius) {
                log_game(std::format("Ship collision with asteroid at distance {:.1f}.", ship_distance), SDL_Color{255, 130, 90, 255});
                start_ship_explosion();
                break;
            }
        }
    }

    void start_ship_explosion() {
        if (ship.exploding) {
            return;
        }
        ship.exploding = true;
        ship.visible = false;
        ship.explosion_timer = EXPLOSION_DURATION_FRAMES;
        ship.lives--;
        ship.overheated = false;
        ship.overheat_cooldown = 0;
        ship.continuous_fire_timer = 0;
        ship.burst_count = 0;
        log_game(std::format("Ship destroyed. Lives remaining: {}.", std::max(0, ship.lives)), SDL_Color{255, 120, 80, 255});
        if (ship.lives <= 0) {
            log_game(std::format("Game over. Final score: {}.", ship.score), SDL_Color{255, 90, 90, 255});
        }
        spawn_ship_explosion(ship.position);
    }

    void spawn_asteroid_explosion(const glm::vec3 &position) {
        spawn_gl_explosion(position);
    }

    void spawn_ship_explosion(const glm::vec3 &position) {
        spawn_gl_explosion(position);
    }

    void spawn_gl_explosion(const glm::vec3 &position) {
        struct ExplosionWave {
            float min_speed;
            float max_speed;
            float min_size;
            float max_size;
            float min_lifetime;
            float max_lifetime;
            glm::vec3 color;
        };

        constexpr int WAVE_COUNT = 4;
        constexpr int MAX_GL_EXPLOSIONS = 5;
        constexpr std::array<ExplosionWave, WAVE_COUNT> waves = {
            ExplosionWave{40.0f, 60.0f, 0.72f, 1.14f, 1.5f, 2.5f, {1.0f, 1.0f, 1.0f}},
            ExplosionWave{30.0f, 45.0f, 0.58f, 0.86f, 2.0f, 3.0f, {1.0f, 1.0f, 1.0f}},
            ExplosionWave{20.0f, 35.0f, 0.43f, 0.72f, 2.5f, 3.5f, {1.0f, 1.0f, 1.0f}},
            ExplosionWave{10.0f, 25.0f, 0.14f, 0.43f, 3.0f, 4.0f, {1.0f, 1.0f, 1.0f}},
        };

        const int particles_per_wave = MAX_PARTICLES / (WAVE_COUNT * MAX_GL_EXPLOSIONS);
        int spawned = 0;
        for (int wave_index = 0; wave_index < WAVE_COUNT; ++wave_index) {
            const ExplosionWave &wave = waves[static_cast<std::size_t>(wave_index)];
            for (int i = 0; i < particles_per_wave; ++i) {
                Particle *particle = find_free_particle();
                if (particle == nullptr) {
                    return;
                }

                const float theta = random_float(0.0f, 2.0f * PI);
                const float phi = random_float(0.0f, PI);
                const glm::vec3 dir{
                    std::sin(phi) * std::cos(theta),
                    std::sin(phi) * std::sin(theta),
                    std::cos(phi),
                };

                const float offset = 0.8f + 0.2f * static_cast<float>(wave_index) / static_cast<float>(WAVE_COUNT);
                const float speed = random_float(wave.min_speed, wave.max_speed);
                particle->position = position + dir * offset;
                particle->velocity = dir * speed + glm::vec3(
                                                       random_float(-5.0f, 5.0f),
                                                       random_float(-5.0f, 5.0f),
                                                       random_float(-5.0f, 5.0f));
                particle->color = glm::vec4(
                    wave.color.r * random_float(0.9f, 1.1f),
                    wave.color.g * random_float(0.9f, 1.1f),
                    wave.color.b * random_float(0.9f, 1.1f),
                    0.1f);
                particle->size = random_float(wave.min_size, wave.max_size);
                particle->lifetime = 0.0f;
                particle->max_lifetime = random_float(wave.min_lifetime, wave.max_lifetime);
                particle->active = true;
                ++spawned;
            }
        }
        log_game(std::format("Explosion spawned {} particles.", spawned));
    }

    void spawn_particles(const glm::vec3 &position,
                         const glm::vec4 &color,
                         int count,
                         float min_speed,
                         float max_speed,
                         float min_size,
                         float max_size,
                         float min_lifetime,
                         float max_lifetime) {
        for (int i = 0; i < count; ++i) {
            Particle *particle = find_free_particle();
            if (particle == nullptr) {
                return;
            }
            const glm::vec3 dir = normalize_or_zero(glm::vec3(
                random_float(-1.0f, 1.0f),
                random_float(-1.0f, 1.0f),
                random_float(-1.0f, 1.0f)));
            particle->position = position;
            particle->velocity = dir * random_float(min_speed, max_speed);
            particle->color = color;
            particle->size = random_float(min_size, max_size);
            particle->lifetime = 0.0f;
            particle->max_lifetime = random_float(min_lifetime, max_lifetime);
            particle->active = true;
        }
    }

    Particle *find_free_particle() {
        for (auto &particle : particles) {
            if (!particle.active) {
                return &particle;
            }
        }
        return nullptr;
    }

    void update_particles(float dt) {
        for (auto &particle : particles) {
            if (!particle.active) {
                continue;
            }
            particle.position += particle.velocity * dt;
            particle.velocity *= 0.98f;
            particle.velocity.y -= 0.5f * dt;
            particle.lifetime += dt;

            const float life_ratio = particle.lifetime / particle.max_lifetime;
            if (life_ratio >= 1.0f) {
                particle.active = false;
                continue;
            }
            if (life_ratio < 0.2f) {
                particle.color.a = life_ratio / 0.2f;
            } else if (life_ratio > 0.8f) {
                particle.color.a = (1.0f - life_ratio) / 0.2f;
            } else {
                particle.color.a = 1.0f;
            }
            if (life_ratio < 0.3f) {
                particle.size *= 1.01f;
            } else {
                particle.size *= 0.99f;
            }
            if (particle.color.a < 0.01f) {
                particle.active = false;
            }
        }
    }

    void clear_particles() {
        for (auto &particle : particles) {
            particle.active = false;
        }
    }

    void draw_ship(uint32_t image_index) {
        if (!ship.visible) {
            return;
        }

        const float ship_scale = 1.55f;
        mxvk::UniformBufferObject ubo{};
        ubo.model = build_model_matrix(ship.position, ship.rotation, ship_scale * ship_model.modelRenderScale(), ship_model.modelCenterOffset());
        last_ship_model_matrix = ubo.model;
        ubo.view = view_matrix;
        ubo.proj = projection_matrix;
        ubo.fx = glm::vec4(camera_position, elapsed_seconds);
        ship_model.updateUBO(image_index, ubo);
        ship_model.render(current_command_buffer, image_index, false);
    }

    void draw_asteroids(uint32_t image_index) {
        for (std::size_t i = 0; i < asteroids.size(); ++i) {
            const Asteroid &asteroid = asteroids[i];
            if (!asteroid.active) {
                continue;
            }
            mxvk::UniformBufferObject ubo{};
            const float scale = asteroid.radius;
            mxvk::VKAbstractModel &asteroid_model = asteroid_models[i];
            ubo.model = build_model_matrix(asteroid.position, asteroid.rotation, scale * asteroid_model.modelRenderScale(),
                                            asteroid_model.modelCenterOffset());
            ubo.view = view_matrix;
            ubo.proj = projection_matrix;
            ubo.fx = glm::vec4(camera_position, elapsed_seconds);
            asteroid_model.updateUBO(image_index, ubo);
            asteroid_model.render(current_command_buffer, image_index, false);
        }
    }

    void draw_projectiles() {
        for (const auto &projectile : projectiles) {
            if (!projectile.active) {
                continue;
            }
            const float life_factor = 1.0f - (projectile.lifetime / PROJECTILE_LIFETIME);
            const float pulse = (0.55f + 0.22f * (1.0f - life_factor)) * (0.9f + 0.1f * std::sin(elapsed_seconds * 12.0f));
            projectile_sprite->drawSprite(projectile.position, glm::vec2(pulse), glm::vec4(1.0f, 0.95f, 0.80f, 1.0f));
        }
    }

    void draw_particles() {
        for (const auto &particle : particles) {
            if (!particle.active) {
                continue;
            }
            effect_sprite->drawSprite(particle.position,
                                      glm::vec2(particle.size),
                                      particle.color);
        }
    }

    void create_flame_resources() {
        create_flame_mesh();
        create_flame_swapchain_resources();
    }

    void cleanup_flame_resources() {
        cleanup_flame_swapchain_resources();
        if (flame_vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, flame_vertex_buffer, nullptr);
            flame_vertex_buffer = VK_NULL_HANDLE;
        }
        if (flame_vertex_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, flame_vertex_buffer_memory, nullptr);
            flame_vertex_buffer_memory = VK_NULL_HANDLE;
        }
        flame_vertex_count = 0;
    }

    void cleanup_flame_swapchain_resources() {
        if (flame_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, flame_pipeline, nullptr);
            flame_pipeline = VK_NULL_HANDLE;
        }
        if (flame_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, flame_pipeline_layout, nullptr);
            flame_pipeline_layout = VK_NULL_HANDLE;
        }
    }

    void create_flame_swapchain_resources() {
        if (flame_vertex_count == 0 || device == VK_NULL_HANDLE) {
            return;
        }
        create_flame_pipeline();
    }

    void create_flame_mesh() {
        constexpr int segments = 40;
        constexpr float base_z = 0.555f;
        constexpr float tip_z = 1.02f;
        constexpr float base_y = 0.040f;
        constexpr float outer_radius = 0.052f;
        constexpr float inner_radius = 0.026f;

        std::vector<FlameVertex> vertices{};
        vertices.reserve(static_cast<std::size_t>(segments) * 6U);

        const glm::vec4 outer_base_color{1.0f, 0.42f, 0.08f, 0.50f};
        const glm::vec4 outer_tip_color{0.7f, 0.08f, 0.0f, 0.0f};
        const glm::vec4 inner_base_color{1.0f, 0.92f, 0.45f, 0.72f};
        const glm::vec4 inner_tip_color{1.0f, 0.32f, 0.04f, 0.0f};

        auto add_cone = [&](float radius, const glm::vec4 &base_color, const glm::vec4 &tip_color) {
            const glm::vec3 tip{0.0f, base_y, tip_z};
            for (int i = 0; i < segments; ++i) {
                const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * PI;
                const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * PI;
                const glm::vec3 p0{std::cos(a0) * radius, base_y + std::sin(a0) * radius, base_z};
                const glm::vec3 p1{std::cos(a1) * radius, base_y + std::sin(a1) * radius, base_z};
                vertices.push_back({p0, base_color});
                vertices.push_back({p1, base_color});
                vertices.push_back({tip, tip_color});
            }
        };

        add_cone(outer_radius, outer_base_color, outer_tip_color);
        add_cone(inner_radius, inner_base_color, inner_tip_color);

        flame_vertex_count = static_cast<uint32_t>(vertices.size());
        const VkDeviceSize buffer_size = sizeof(FlameVertex) * static_cast<VkDeviceSize>(vertices.size());
        create_buffer(buffer_size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      flame_vertex_buffer,
                      flame_vertex_buffer_memory);

        void *data = nullptr;
        if (vkMapMemory(device, flame_vertex_buffer_memory, 0, buffer_size, 0, &data) != VK_SUCCESS || data == nullptr) {
            throw mxvk::Exception("Failed to map asteroids3d flame vertex buffer");
        }
        std::memcpy(data, vertices.data(), static_cast<std::size_t>(buffer_size));
        vkUnmapMemory(device, flame_vertex_buffer_memory);
    }

    void create_flame_pipeline() {
        cleanup_flame_swapchain_resources();

        const std::vector<char> vert_shader_code = loadSpv(std::string(ASTEROIDS3D_SHADER_DIR) + "/flame.vert.spv");
        const std::vector<char> frag_shader_code = loadSpv(std::string(ASTEROIDS3D_SHADER_DIR) + "/flame.frag.spv");

        VkShaderModule vert_shader_module = createShaderModule(device, vert_shader_code);
        VkShaderModule frag_shader_module = VK_NULL_HANDLE;

        try {
            frag_shader_module = createShaderModule(device, frag_shader_code);

            VkPipelineShaderStageCreateInfo vert_stage{};
            vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vert_stage.module = vert_shader_module;
            vert_stage.pName = "main";

            VkPipelineShaderStageCreateInfo frag_stage{};
            frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_stage.module = frag_shader_module;
            frag_stage.pName = "main";

            std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages = {vert_stage, frag_stage};

            VkVertexInputBindingDescription binding_description{};
            binding_description.binding = 0;
            binding_description.stride = sizeof(FlameVertex);
            binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 2> attributes{};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = offsetof(FlameVertex, pos);
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes[1].offset = offsetof(FlameVertex, color);

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &binding_description;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
            vertex_input.pVertexAttributeDescriptions = attributes.data();

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            const std::array<VkDynamicState, 2> dynamic_states = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };
            VkPipelineDynamicStateCreateInfo dynamic_info{};
            dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_info.pDynamicStates = dynamic_states.data();

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = VK_TRUE;
            depth_stencil.depthWriteEnable = VK_FALSE;
            depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            depth_stencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            color_blend_attachment.blendEnable = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_blend_attachment;

            VkPushConstantRange push_range{};
            push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push_range.offset = 0;
            push_range.size = sizeof(FlamePushConstants);

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_range;

            if (vkCreatePipelineLayout(device, &layout_info, nullptr, &flame_pipeline_layout) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create asteroids3d flame pipeline layout");
            }

            const VkFormat color_format = getSwapchainFormat();
            const VkFormat depth_format = getDepthFormat();

            VkPipelineRenderingCreateInfo rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &color_format;
            if (depth_format != VK_FORMAT_UNDEFINED) {
                rendering_info.depthAttachmentFormat = depth_format;
            }

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &rendering_info;
            pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
            pipeline_info.pStages = shader_stages.data();
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_info;
            pipeline_info.layout = flame_pipeline_layout;
            pipeline_info.renderPass = VK_NULL_HANDLE;
            pipeline_info.subpass = 0;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &flame_pipeline) != VK_SUCCESS) {
                throw mxvk::Exception("Failed to create asteroids3d flame pipeline");
            }
        } catch (...) {
            if (frag_shader_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, frag_shader_module, nullptr);
            }
            vkDestroyShaderModule(device, vert_shader_module, nullptr);
            cleanup_flame_swapchain_resources();
            throw;
        }

        vkDestroyShaderModule(device, frag_shader_module, nullptr);
        vkDestroyShaderModule(device, vert_shader_module, nullptr);
    }

    void draw_engine_flame(VkCommandBuffer cmd, const VkExtent2D &extent) {
        if (!ship.visible || ship.current_speed <= ship.min_speed * 1.2f) {
            return;
        }
        if (flame_pipeline == VK_NULL_HANDLE || flame_vertex_buffer == VK_NULL_HANDLE || flame_vertex_count == 0) {
            return;
        }

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        FlamePushConstants pc{};
        pc.mvp = projection_matrix * view_matrix * last_ship_model_matrix;
        pc.params = glm::vec4(elapsed_seconds, ship.current_speed / ship.max_speed, 0.0f, 0.0f);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, flame_pipeline);
        vkCmdPushConstants(cmd,
                           flame_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(pc),
                           &pc);

        VkBuffer vertex_buffers[] = {flame_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
        vkCmdDraw(cmd, flame_vertex_count, 1, 0, 0);
    }

    void create_buffer(VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties,
                       VkBuffer &buffer,
                       VkDeviceMemory &buffer_memory) const {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
            throw mxvk::Exception("Failed to create asteroids3d buffer");
        }

        VkMemoryRequirements mem_requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS) {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw mxvk::Exception("Failed to allocate asteroids3d buffer memory");
        }
        if (vkBindBufferMemory(device, buffer, buffer_memory, 0) != VK_SUCCESS) {
            vkFreeMemory(device, buffer_memory, nullptr);
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            buffer_memory = VK_NULL_HANDLE;
            throw mxvk::Exception("Failed to bind asteroids3d buffer memory");
        }
    }

    [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties mem_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
            if ((type_filter & (1U << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        throw mxvk::Exception("Failed to find asteroids3d memory type");
    }

    void draw_hud([[maybe_unused]] float aspect) {
        const SDL_Color white{255, 255, 255, 255};
        const SDL_Color red{220, 60, 60, 255};
        const VkExtent2D extent = getSwapchainExtent();
        const int right_x = std::max(25, static_cast<int>(extent.width) - 250);
        printText("MXVK Asteroids v1.0", right_x, 25, red);
        printText("Score: " + std::to_string(ship.score), right_x, 50, white);
        printText("Lives: " + std::to_string(std::max(0, ship.lives)), right_x, 75, white);
        printText("Asteroids: " + std::to_string(active_asteroids()), right_x, 100, white);
        printText("[F1 for Debug]", right_x, 125, white);
        printText(inverted_controls ? "[Inverted] F2/Y" : "[Arcade] F2/Y", right_x, 150, white);
        printText("[F3 for Console]", right_x, 175, white);

        if (!debug_menu) {
            return;
        }

        const float fps = (last_delta_time > 0.0001f) ? (1.0f / last_delta_time) : 0.0f;
        printText("Ship X,Y,Z: " + vec3_string(ship.position), 25, 25, white);
        printText("Velocity X,Y,Z: " + vec3_string(ship.velocity), 25, 50, white);
        printText("FPS: " + std::to_string(fps), 25, 75, white);
        printText("Aseroids destroyed: " + std::to_string(MAX_ASTEROIDS - active_asteroids()), 25, 100, white);
        printText("Controls: Arrows to Move, W,S Tilt Up/Down - SPACE to shoot", 25, 125, white);
        printText("Nearest Object: " + std::to_string(nearest_asteroid_distance()), 25, 150, white);
        printText("Farthest Object: " + std::to_string(farthest_asteroid_distance()), 25, 175, white);
        printText("Speed: " + std::to_string(ship.current_speed) + " / " + std::to_string(ship.max_speed), 25, 200, white);
        printText("Controller: Disconnected", 25, 225, white);
        printText("Press ENTER to randomize asteroids", 25, 250, white);
    }

    int active_asteroids() const {
        int count = 0;
        for (const auto &asteroid : asteroids) {
            if (asteroid.active) {
                ++count;
            }
        }
        return count;
    }

    void draw_game_over([[maybe_unused]] uint32_t image_index, [[maybe_unused]] float aspect) {
        const SDL_Color red{235, 60, 60, 255};
        const SDL_Color white{255, 255, 255, 255};
        printText("GAME OVER", 24, 20, red);
        printText("Final Score: " + std::to_string(ship.score), 24, 52, white);
        printText("Press SPACE to restart", 24, 84, white);
    }

    VkCommandBuffer current_command_buffer = VK_NULL_HANDLE;
    float last_delta_time = 1.0f / 60.0f;

    std::string vec3_string(const glm::vec3 &value) const {
        return std::to_string(value.x) + ", " + std::to_string(value.y) + ", " + std::to_string(value.z);
    }

    float nearest_asteroid_distance() const {
        float nearest = 999999.0f;
        for (const auto &asteroid : asteroids) {
            if (asteroid.active) {
                nearest = std::min(nearest, glm::length(ship.position - asteroid.position));
            }
        }
        return nearest;
    }

    float farthest_asteroid_distance() const {
        float farthest = 0.0f;
        for (const auto &asteroid : asteroids) {
            if (asteroid.active) {
                farthest = std::max(farthest, glm::length(ship.position - asteroid.position));
            }
        }
        return farthest;
    }
};

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::Asteroids3DWindow window(args.path, args.width, args.height, args.fullscreen);
        window.loop();
    } catch (const mxvk::Exception &e) {
        std::cerr << std::format("mxvk: Exception: {}\n", e.text());
        return EXIT_FAILURE;
    } catch (const ArgException<std::string> &e) {
        std::cerr << "Argument Exception: " << e.text() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
