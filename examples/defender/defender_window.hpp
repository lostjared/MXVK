#ifndef DEFENDER_WINDOW_HPP
#define DEFENDER_WINDOW_HPP

#include "defender_assets.hpp"
#include "defender_entities.hpp"
#include "defender_types.hpp"

#include "asteroids3d_types.hpp"
#include "rain.hpp"
#include "ship.hpp"
#include "starfield.hpp"

#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_console.hpp"
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
#include "mxvk/mxvk_sound.hpp"
#endif

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace defender {

    class DefenderWindow : public mxvk::VK_Window {
      public:
        DefenderWindow(const std::string &path, int width, int height, bool fullscreen);

        ~DefenderWindow() override;

        void event(SDL_Event &e) override;

        void onSwapchainRecreated() override;

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t image_index) override;

      private:
        std::string asset_root;
        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        float elapsed_seconds = 0.0f;
        GameMode mode = GameMode::Intro;
        float intro_fade = 1.0f;
        Uint32 intro_last_update_ms = 0;
        Uint32 intro_fade_in_start_ms = 0;
        static constexpr Uint32 INTRO_FADE_IN_DURATION_MS = 500;
        static constexpr int INTRO_RAIN_TEXTURE_WIDTH = 1280;
        static constexpr int INTRO_RAIN_TEXTURE_HEIGHT = 720;
        static constexpr int ENEMY_SPAWN_ATTEMPTS = 16;
        static constexpr float ENEMY_SEPARATION_PADDING = 0.12f;
        Uint32 countdown_timer = 0;
        Uint32 countdown_duration = 1000;
        Uint32 countdown_start_ms = 0;
        int countdown_number = 3;
        Uint32 launch_timer = 0;
        Uint32 launch_duration = 1000;
        Uint32 launch_start_ms = 0;
        float ship_forward_direction = 1.0f;
        float camera_center_x = 0.0f;
        float playable_world_top = WORLD_TOP;
        float respawn_timer = 0.0f;
        int score = 0;
        int lives = 5;
        int level = 1;
        bool level_active = false;
        bool game_over = false;
        bool ship_respawning = false;
        bool reverse_pressed = false;
        bool propulsion_pressed = false;
        bool up_pressed = false;
        bool down_pressed = false;
        bool fire_pressed = false;
        bool roll_left_pressed = false;
        bool roll_right_pressed = false;
        bool show_fps_counter = false;
        bool crt_enabled = true;
        float fps_accumulator = 0.0f;
        int fps_frame_count = 0;
        std::string fps_text = "FPS: --";
        float barrel_roll_direction = 1.0f;
        float barrel_roll_progress = 0.0f;
        float barrel_roll_angle = 0.0f;
        static constexpr float BARREL_ROLL_DURATION = 0.65f;
        static constexpr int DEFAULT_FONT_SIZE = 24;
        static constexpr int HUD_FONT_SIZE = DEFAULT_FONT_SIZE * 2;
        static constexpr int COUNTDOWN_FONT_SIZE = HUD_FONT_SIZE;
        mxvk::Font hud_font{};
        mxvk::Font countdown_font{};

        space::Ship ship{};
        std::array<space::Projectile, MAX_PROJECTILES> projectiles{};
        std::array<Ufo, MAX_UFOS> ufos{};
        std::array<Asteroid, MAX_ASTEROIDS> asteroids{};
        std::array<space::Particle, space::MAX_PARTICLES> particles{};
        space::StarField star_field{};
        mxvk::VKAbstractModel ship_model{};
        std::array<mxvk::VKAbstractModel, MAX_ASTEROIDS> asteroid_models{};
        mxvk::VK_Sprite3D *star_sprite = nullptr;
        mxvk::VK_Sprite3D *terrain_sprite = nullptr;
        mxvk::VK_Sprite3D *projectile_sprite = nullptr;
        std::array<std::array<mxvk::VK_Sprite3D *, UFO_ANIMATION_FRAMES>, UFO_SPRITE_SET_COUNT> ufo_sprite_sets{};
        std::array<std::array<SpriteAlphaBounds, UFO_ANIMATION_FRAMES>, UFO_SPRITE_SET_COUNT> ufo_sprite_bounds{};
        mxvk::VK_Sprite3D *effect_sprite = nullptr;
        mxvk::VK_Sprite *intro_sprite = nullptr;
        mxvk::VK_Sprite *fade_overlay_sprite = nullptr;
        mxvk::VK_Sprite *crt_sprite = nullptr;
        std::unique_ptr<matrix::Rain> intro_rain{};
        mxvk::VK_Console console{};
        bool console_ready = false;
#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        std::unique_ptr<mxvk::VK_Mixer> background_music{};
        int background_music_track = -1;
        int crash_sound = -1;
        int shoot_sound = -1;
        int takeoff_sound = -1;
        int asteroid_explosion_sound = -1;
        int ufo_explosion_sound = -1;
#endif
        glm::vec3 camera_position{0.0f, CAMERA_HEIGHT, CAMERA_DISTANCE};
        glm::mat4 last_ship_model_matrix{1.0f};
        VkBuffer flame_vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory flame_vertex_buffer_memory = VK_NULL_HANDLE;
        VkPipeline flame_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout flame_pipeline_layout = VK_NULL_HANDLE;
        uint32_t flame_vertex_count = 0;

        void configure_console();

        bool handle_console_command(const std::vector<std::string> &args, std::ostream &out);

        void log_game(const std::string &message, SDL_Color color = SDL_Color{180, 220, 255, 255});

        [[nodiscard]] const char *mode_name() const;

        [[nodiscard]] static std::string format_vec3(const glm::vec3 &value);

        bool parse_int_arg(const std::vector<std::string> &args, std::size_t index, const char *name, int &value, std::ostream &out) const;

        bool parse_float_arg(const std::vector<std::string> &args, std::size_t index, const char *name, float &value, std::ostream &out) const;

        [[nodiscard]] int active_ufo_count() const;

        [[nodiscard]] int active_asteroid_count() const;

        [[nodiscard]] int active_projectile_count() const;

        Ufo *find_inactive_ufo();

        Asteroid *find_inactive_asteroid();

        void clear_input_state();

#if defined(MXVK_WITH_MIXER) || defined(WITH_MIXER)
        void ensure_background_music_playing();

        void play_sound(int sound_id);
#endif

        void update_ship(float dt);

        void update_barrel_roll(float dt);

        glm::mat4 build_ship_model_matrix();

        glm::mat4 build_asteroid_model_matrix(const Asteroid &asteroid, const mxvk::VKAbstractModel &model_resource) const;

        void update_camera_position(float dt);

        void update_playable_world_top(const VkExtent2D &extent);

        void reset_intro_screen();

        void start_launch_countdown();

        void start_intro_fade_in();

        void draw_intro(const VkExtent2D &extent);

        void draw_intro_fade_in(const VkExtent2D &extent);

        void draw_fade_overlay(const VkExtent2D &extent, float alpha);

        void update_camera_scroll(float aspect);

        void update_countdown();

        void draw_countdown(VkCommandBuffer cmd, uint32_t image_index, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection);

        void init_ufos();

        void init_asteroids();

        void start_level();

        void update_level_progress();

        void respawn_ufo(Ufo &ufo, bool initial_spawn = false);

        void update_ufos(float dt);

        void respawn_asteroid(Asteroid &asteroid, bool initial_spawn = false);

        void update_asteroids(float dt);

        [[nodiscard]] float ufo_collision_radius(const Ufo &ufo) const;

        [[nodiscard]] bool enemy_spawn_is_clear(const glm::vec3 &position, float radius, const Ufo *ignored_ufo, const Asteroid *ignored_asteroid) const;

        void clamp_enemy_to_world_y(glm::vec3 &position, glm::vec3 &velocity, float radius);

        void separate_enemies(glm::vec3 &first_position,
                              glm::vec3 &first_velocity,
                              float first_radius,
                              glm::vec3 &second_position,
                              glm::vec3 &second_velocity,
                              float second_radius,
                              float restitution);

        void resolve_enemy_overlaps();

        [[nodiscard]] int current_ufo_frame(const Ufo &ufo) const;

        [[nodiscard]] float current_ufo_pulse(const Ufo &ufo) const;

        [[nodiscard]] glm::vec2 ufo_draw_size(const Ufo &ufo, float pulse) const;

        void draw_ufos();

        void draw_terrain();

        void draw_asteroids(VkCommandBuffer cmd, uint32_t image_index, const glm::mat4 &view, const glm::mat4 &projection);

        void create_flame_resources();

        void cleanup_flame_resources();

        void cleanup_flame_swapchain_resources();

        void create_flame_swapchain_resources();

        void create_flame_mesh();

        void create_flame_pipeline();

        void draw_engine_flame(VkCommandBuffer cmd, const VkExtent2D &extent, const glm::mat4 &view, const glm::mat4 &projection);

        void create_buffer(VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties,
                           VkBuffer &buffer,
                           VkDeviceMemory &buffer_memory) const;

        [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

        void fire_projectile();

        void update_projectiles(float dt);

        void check_projectile_ufo_hits();

        void check_projectile_asteroid_hits();

        void check_ship_ufo_collisions();

        void check_ship_asteroid_collisions();

        void lose_life();

        void update_respawn(float dt);

        void reset_ship_to_origin();

        void reset_ufos_for_origin_start();

        void restart_game();

        void clear_projectiles();

        void draw_projectiles();

        void spawn_ufo_explosion(const glm::vec3 &position, float explosion_scale = 1.0f);

        void spawn_alien_explosion(const glm::vec3 &position, float explosion_scale = 1.0f);

        void spawn_ship_explosion(const glm::vec3 &position, float explosion_scale = 1.0f);

        void spawn_enemy_explosion(const glm::vec3 &position, float explosion_scale, const std::array<glm::vec3, 4> &wave_colors);

        space::Particle *find_free_particle();

        void update_particles(float dt);

        void draw_particles();

        void clear_particles();

        void update_fps_counter(float delta_seconds);

        void draw_hud(const VkExtent2D &extent);

        void draw_scanner(const VkExtent2D &extent);
    };

} // namespace defender

#endif
