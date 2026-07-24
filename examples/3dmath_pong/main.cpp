#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"
#if defined(MXVK_USE_EIGEN_MATH)
#include "mxvk/mxvk_math_eigen.hpp"
#else
#include "mxvk/mxvk_math.h"
#endif

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace {
    constexpr int DEFAULT_FRAME_WIDTH = 320;
    constexpr int DEFAULT_FRAME_HEIGHT = 240;
    constexpr int WINDOW_WIDTH = 1440;
    constexpr int WINDOW_HEIGHT = 1080;
    constexpr float COURT_HALF_WIDTH = 2.65f;
    constexpr float COURT_HALF_HEIGHT = 1.45f;
    constexpr float PADDLE_X = 2.25f;
    constexpr float PADDLE_HALF_WIDTH = 0.12f;
    constexpr float PADDLE_HALF_HEIGHT = 0.42f;
    constexpr float BALL_RADIUS = 0.14f;
    constexpr float CAMERA_DISTANCE = 7.0f;

    class SurfaceDeleter {
      public:
        void operator()(SDL_Surface *surface) const {
            SDL_DestroySurface(surface);
        }
    };

    using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

    struct Triangle {
        std::array<std::size_t, 3> indices{};
    };

    struct Mesh {
        std::vector<mxvk::vec4D> vertices;
        std::vector<Triangle> triangles;
        bool two_sided = false;

        [[nodiscard]] static Mesh cube() {
            return {
                {
                    {-1.0f, -1.0f, -1.0f, 1.0f},
                    {1.0f, -1.0f, -1.0f, 1.0f},
                    {1.0f, 1.0f, -1.0f, 1.0f},
                    {-1.0f, 1.0f, -1.0f, 1.0f},
                    {-1.0f, -1.0f, 1.0f, 1.0f},
                    {1.0f, -1.0f, 1.0f, 1.0f},
                    {1.0f, 1.0f, 1.0f, 1.0f},
                    {-1.0f, 1.0f, 1.0f, 1.0f},
                },
                {
                    {{0, 3, 2}},
                    {{0, 2, 1}},
                    {{4, 5, 6}},
                    {{4, 6, 7}},
                    {{0, 4, 7}},
                    {{0, 7, 3}},
                    {{1, 2, 6}},
                    {{1, 6, 5}},
                    {{3, 7, 6}},
                    {{3, 6, 2}},
                    {{0, 1, 5}},
                    {{0, 5, 4}},
                },
            };
        }

        [[nodiscard]] static Mesh sphere(int latitude_segments, int longitude_segments) {
            Mesh mesh;
            mesh.two_sided = true;
            for (int latitude = 0; latitude <= latitude_segments; ++latitude) {
                const float phi = static_cast<float>(latitude) * mxvk::PI / static_cast<float>(latitude_segments);
                const float ring_radius = std::sin(phi);
                const float y = std::cos(phi);
                for (int longitude = 0; longitude <= longitude_segments; ++longitude) {
                    const float theta = static_cast<float>(longitude) * 2.0f * mxvk::PI / static_cast<float>(longitude_segments);
                    mesh.vertices.emplace_back(
                        ring_radius * std::cos(theta),
                        y,
                        ring_radius * std::sin(theta),
                        1.0f);
                }
            }

            const std::size_t row_size = static_cast<std::size_t>(longitude_segments + 1);
            for (int latitude = 0; latitude < latitude_segments; ++latitude) {
                for (int longitude = 0; longitude < longitude_segments; ++longitude) {
                    const std::size_t first =
                        static_cast<std::size_t>(latitude) * row_size +
                        static_cast<std::size_t>(longitude);
                    const std::size_t second = first + row_size;
                    mesh.triangles.push_back({{first, second, first + 1}});
                    mesh.triangles.push_back({{first + 1, second, second + 1}});
                }
            }
            return mesh;
        }
    };

    struct MeshInstance {
        const Mesh &mesh;
        mxvk::vec4D position;
        mxvk::vec4D scale;
        mxvk::vec4D rotation;
        mxvk::MXCOLOR color;
    };

    class SoftwareRenderer {
      public:
        SoftwareRenderer(int width, int height)
            : frame_width(width),
              frame_height(height),
              depth_buffer(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
            frame_surface.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (frame_surface == nullptr) {
                throw mxvk::Exception(std::format("3dmath_pong: failed to create framebuffer: {}", SDL_GetError()));
            }
            frame_format = SDL_GetPixelFormatDetails(frame_surface->format);
            if (frame_format == nullptr) {
                throw mxvk::Exception(std::format("3dmath_pong: failed to query framebuffer format: {}", SDL_GetError()));
            }
            camera_rotation.BuildXYZ(17.0f, -8.0f, 0.0f);
        }

        [[nodiscard]] SDL_Surface *surface() const {
            return frame_surface.get();
        }

        [[nodiscard]] int width() const {
            return frame_width;
        }

        [[nodiscard]] int height() const {
            return frame_height;
        }

        void begin_frame() {
            std::ranges::fill(depth_buffer, std::numeric_limits<float>::infinity());
            for (int y = 0; y < frame_height; ++y) {
                const float fraction = static_cast<float>(y) / static_cast<float>(frame_height - 1);
                const int red = static_cast<int>(4.0f + fraction * 5.0f);
                const int green = static_cast<int>(10.0f + fraction * 12.0f);
                const int blue = static_cast<int>(24.0f + fraction * 20.0f);
                const mxvk::MXCOLOR color = mxvk::MXVK_RGB(red, green, blue);
                for (int x = 0; x < frame_width; ++x) {
                    put_pixel(x, y, color);
                }
            }
        }

        void draw_mesh(const MeshInstance &instance) {
            mxvk::Mat4D object_rotation;
            object_rotation.BuildXYZ(instance.rotation.x, instance.rotation.y, instance.rotation.z);

            std::vector<mxvk::vec4D> camera_vertices(instance.mesh.vertices.size());
            std::vector<mxvk::vec4D> projected_vertices(instance.mesh.vertices.size());
            for (std::size_t index = 0; index < instance.mesh.vertices.size(); ++index) {
                const mxvk::vec4D &vertex = instance.mesh.vertices[index];
                mxvk::vec4D transformed(
                    vertex.x * instance.scale.x,
                    vertex.y * instance.scale.y,
                    vertex.z * instance.scale.z,
                    1.0f);
                transformed = object_rotation.MulVec(transformed);
                transformed += instance.position;
                transformed = camera_rotation.MulVec(transformed);
                transformed.z += CAMERA_DISTANCE;
                camera_vertices[index] = transformed;
                projected_vertices[index] = project(transformed);
            }

            const mxvk::vec4D light_direction = normalized({-0.35f, -0.65f, -1.0f, 0.0f});
            for (const Triangle &triangle : instance.mesh.triangles) {
                const mxvk::vec4D &a = camera_vertices[triangle.indices[0]];
                const mxvk::vec4D &b = camera_vertices[triangle.indices[1]];
                const mxvk::vec4D &c = camera_vertices[triangle.indices[2]];
                mxvk::vec4D normal = mxvk::vec4D().Build(a, b).CrossProduct(mxvk::vec4D().Build(a, c));
                normal.Normalize();

                const mxvk::vec4D center = (a + b + c) * (1.0f / 3.0f);
                const mxvk::vec4D view_direction(-center.x, -center.y, -center.z, 0.0f);
                if (normal.DotProduct(view_direction) <= 0.0f) {
                    if (!instance.mesh.two_sided) {
                        continue;
                    }
                    normal = normal * -1.0f;
                }

                const float diffuse = std::max(0.0f, normal.DotProduct(light_direction));
                const float intensity = std::clamp(0.32f + diffuse * 0.68f, 0.0f, 1.0f);
                rasterize_triangle(
                    projected_vertices[triangle.indices[0]],
                    projected_vertices[triangle.indices[1]],
                    projected_vertices[triangle.indices[2]],
                    mxvk::shade_color(instance.color, intensity));
            }
        }

        void draw_digit(int x, int y, int digit, int scale, mxvk::MXCOLOR color) {
            static constexpr std::array<std::uint8_t, 10> SEGMENTS = {
                0b1111110,
                0b0110000,
                0b1101101,
                0b1111001,
                0b0110011,
                0b1011011,
                0b1011111,
                0b1110000,
                0b1111111,
                0b1111011,
            };
            const std::uint8_t segments = SEGMENTS[static_cast<std::size_t>(std::clamp(digit, 0, 9))];
            const auto horizontal = [this, scale, color](int left, int top) {
                fill_rectangle(left + scale, top, scale * 3, scale, color);
            };
            const auto vertical = [this, scale, color](int left, int top) {
                fill_rectangle(left, top + scale, scale, scale * 3, color);
            };
            if ((segments & 0b1000000U) != 0U)
                horizontal(x, y);
            if ((segments & 0b0100000U) != 0U)
                vertical(x + scale * 4, y);
            if ((segments & 0b0010000U) != 0U)
                vertical(x + scale * 4, y + scale * 4);
            if ((segments & 0b0001000U) != 0U)
                horizontal(x, y + scale * 7);
            if ((segments & 0b0000100U) != 0U)
                vertical(x, y + scale * 4);
            if ((segments & 0b0000010U) != 0U)
                vertical(x, y);
            if ((segments & 0b0000001U) != 0U)
                horizontal(x, y + scale * 3);
        }

        void draw_pause_indicator() {
            fill_rectangle(frame_width - 17, 8, 3, 12, mxvk::MXVK_RGB(255, 220, 92));
            fill_rectangle(frame_width - 10, 8, 3, 12, mxvk::MXVK_RGB(255, 220, 92));
        }

      private:
        SurfacePtr frame_surface;
        const SDL_PixelFormatDetails *frame_format = nullptr;
        int frame_width = 0;
        int frame_height = 0;
        std::vector<float> depth_buffer;
        mxvk::Mat4D camera_rotation;

        [[nodiscard]] static mxvk::vec4D normalized(mxvk::vec4D value) {
            value.Normalize();
            return value;
        }

        [[nodiscard]] std::uint32_t map_color(mxvk::MXCOLOR color) const {
            return SDL_MapRGBA(
                frame_format,
                nullptr,
                mxvk::color_r(color),
                mxvk::color_g(color),
                mxvk::color_b(color),
                mxvk::color_a(color));
        }

        void put_pixel(int x, int y, mxvk::MXCOLOR color) {
            if (x < 0 || y < 0 || x >= frame_width || y >= frame_height) {
                return;
            }
            auto *row =
                static_cast<std::uint8_t *>(frame_surface->pixels) +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_surface->pitch);
            *(reinterpret_cast<std::uint32_t *>(row) + x) = map_color(color);
        }

        void fill_rectangle(int x, int y, int width, int height, mxvk::MXCOLOR color) {
            for (int row = 0; row < height; ++row) {
                for (int column = 0; column < width; ++column) {
                    put_pixel(x + column, y + row, color);
                }
            }
        }

        [[nodiscard]] mxvk::vec4D project(const mxvk::vec4D &point) const {
            const float scale = static_cast<float>(std::min(frame_width, frame_height)) * 1.62f;
            const float z = std::max(point.z, 0.001f);
            return {
                static_cast<float>(frame_width) * 0.5f + point.x / z * scale,
                static_cast<float>(frame_height) * 0.54f - point.y / z * scale,
                point.z,
                1.0f,
            };
        }

        void rasterize_triangle(const mxvk::vec4D &a, const mxvk::vec4D &b, const mxvk::vec4D &c, mxvk::MXCOLOR color) {
            const auto edge = [](const mxvk::vec4D &first, const mxvk::vec4D &second, float x, float y) {
                return (x - first.x) * (second.y - first.y) - (y - first.y) * (second.x - first.x);
            };
            const float area = edge(b, c, a.x, a.y);
            if (std::abs(area) <= mxvk::EPSILON) {
                return;
            }

            const int min_x = std::clamp(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))), 0, frame_width - 1);
            const int max_x = std::clamp(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))), 0, frame_width - 1);
            const int min_y = std::clamp(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))), 0, frame_height - 1);
            const int max_y = std::clamp(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))), 0, frame_height - 1);
            for (int y = min_y; y <= max_y; ++y) {
                for (int x = min_x; x <= max_x; ++x) {
                    const float sample_x = static_cast<float>(x) + 0.5f;
                    const float sample_y = static_cast<float>(y) + 0.5f;
                    const float weight_a = edge(b, c, sample_x, sample_y) / area;
                    const float weight_b = edge(c, a, sample_x, sample_y) / area;
                    const float weight_c = edge(a, b, sample_x, sample_y) / area;
                    if (weight_a < 0.0f || weight_b < 0.0f || weight_c < 0.0f) {
                        continue;
                    }
                    const float reciprocal_depth = weight_a / a.z + weight_b / b.z + weight_c / c.z;
                    if (reciprocal_depth <= mxvk::EPSILON) {
                        continue;
                    }
                    const float depth = 1.0f / reciprocal_depth;
                    const std::size_t pixel_index =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_width) +
                        static_cast<std::size_t>(x);
                    if (depth >= depth_buffer[pixel_index]) {
                        continue;
                    }
                    depth_buffer[pixel_index] = depth;
                    put_pixel(x, y, color);
                }
            }
        }
    };

    class PongGame {
      public:
        PongGame()
            : random_engine(std::random_device{}()) {
            reset();
        }

        void reset() {
            player_y = 0.0f;
            computer_y = 0.0f;
            player_score = 0;
            computer_score = 0;
            paused = false;
            reset_ball(random_direction());
        }

        void toggle_pause() {
            paused = !paused;
        }

        void set_player_position(float position) {
            player_y = std::clamp(position, paddle_minimum_y(), paddle_maximum_y());
        }

        void move_player(float movement) {
            set_player_position(player_y + movement);
        }

        void update(float delta_seconds) {
            if (paused) {
                return;
            }

            const float target = ball_position.y;
            const float difference = target - computer_y;
            const float computer_movement = std::clamp(difference, -AI_SPEED * delta_seconds, AI_SPEED * delta_seconds);
            computer_y = std::clamp(computer_y + computer_movement, paddle_minimum_y(), paddle_maximum_y());

            ball_position += ball_velocity * delta_seconds;
            if (ball_position.y + BALL_RADIUS >= COURT_HALF_HEIGHT) {
                ball_position.y = COURT_HALF_HEIGHT - BALL_RADIUS;
                ball_velocity.y = -std::abs(ball_velocity.y);
            } else if (ball_position.y - BALL_RADIUS <= -COURT_HALF_HEIGHT) {
                ball_position.y = -COURT_HALF_HEIGHT + BALL_RADIUS;
                ball_velocity.y = std::abs(ball_velocity.y);
            }

            collide_with_paddle(-PADDLE_X, player_y, 1.0f);
            collide_with_paddle(PADDLE_X, computer_y, -1.0f);

            if (ball_position.x < -COURT_HALF_WIDTH - BALL_RADIUS) {
                ++computer_score;
                reset_ball(1.0f);
            } else if (ball_position.x > COURT_HALF_WIDTH + BALL_RADIUS) {
                ++player_score;
                reset_ball(-1.0f);
            }
        }

        [[nodiscard]] float player_position() const { return player_y; }
        [[nodiscard]] float computer_position() const { return computer_y; }
        [[nodiscard]] const mxvk::vec4D &ball() const { return ball_position; }
        [[nodiscard]] int left_score() const { return player_score; }
        [[nodiscard]] int right_score() const { return computer_score; }
        [[nodiscard]] bool is_paused() const { return paused; }

      private:
        static constexpr float AI_SPEED = 1.65f;
        float player_y = 0.0f;
        float computer_y = 0.0f;
        mxvk::vec4D ball_position{0.0f, 0.0f, -0.18f, 1.0f};
        mxvk::vec4D ball_velocity{1.8f, 0.35f, 0.0f, 0.0f};
        int player_score = 0;
        int computer_score = 0;
        bool paused = false;
        std::mt19937 random_engine;

        [[nodiscard]] static float paddle_minimum_y() {
            return -COURT_HALF_HEIGHT + PADDLE_HALF_HEIGHT + 0.08f;
        }

        [[nodiscard]] static float paddle_maximum_y() {
            return COURT_HALF_HEIGHT - PADDLE_HALF_HEIGHT - 0.08f;
        }

        [[nodiscard]] float random_direction() {
            std::uniform_int_distribution<int> distribution(0, 1);
            return distribution(random_engine) == 0 ? -1.0f : 1.0f;
        }

        void reset_ball(float horizontal_direction) {
            std::uniform_real_distribution<float> vertical_distribution(-0.62f, 0.62f);
            ball_position = {0.0f, 0.0f, -0.18f, 1.0f};
            ball_velocity = {horizontal_direction, vertical_distribution(random_engine), 0.0f, 0.0f};
            ball_velocity.Normalize();
            ball_velocity = ball_velocity * 1.8f;
        }

        void collide_with_paddle(float paddle_x, float paddle_y, float outgoing_direction) {
            if (ball_velocity.x * outgoing_direction >= 0.0f) {
                return;
            }
            const bool horizontal_overlap =
                std::abs(ball_position.x - paddle_x) <= PADDLE_HALF_WIDTH + BALL_RADIUS;
            const bool vertical_overlap =
                std::abs(ball_position.y - paddle_y) <= PADDLE_HALF_HEIGHT + BALL_RADIUS;
            if (!horizontal_overlap || !vertical_overlap) {
                return;
            }

            ball_position.x = paddle_x + outgoing_direction * (PADDLE_HALF_WIDTH + BALL_RADIUS);
            const float offset = (ball_position.y - paddle_y) / PADDLE_HALF_HEIGHT;
            const float current_speed = std::min(3.5f, ball_velocity.Length() * 1.045f);
            ball_velocity.x = outgoing_direction;
            ball_velocity.y += offset * 0.72f;
            ball_velocity.z = 0.0f;
            ball_velocity.Normalize();
            ball_velocity = ball_velocity * current_speed;
        }
    };
} // namespace

namespace example {
    class Math3DPongWindow final : public mxvk::VK_Window {
      public:
        Math3DPongWindow(bool fullscreen, bool enable_vsync, const FramebufferDimensions &framebuffer)
            : mxvk::VK_Window("MXVK 3D Math Pong", WINDOW_WIDTH, WINDOW_HEIGHT, fullscreen, MXVK_VALIDATION, enable_vsync),
              renderer(framebuffer.width, framebuffer.height),
              cube_mesh(Mesh::cube()),
              ball_mesh(Mesh::sphere(8, 12)) {
            setClearColor(0.01f, 0.02f, 0.04f, 1.0f);
            mxvk::BuildTables();
        }

        void event(SDL_Event &event) override {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                switch (event.key.key) {
                case SDLK_ESCAPE:
                    exit();
                    break;
                case SDLK_SPACE:
                    game.toggle_pause();
                    break;
                case SDLK_R:
                    game.reset();
                    break;
                default:
                    break;
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                const float normalized =
                    1.0f - 2.0f * event.motion.y / static_cast<float>(std::max(1, output_height));
                game.set_player_position(normalized * COURT_HALF_HEIGHT);
            }
        }

        void proc() override {
            output_width = swapchain_extent.width > 0U ? static_cast<int>(swapchain_extent.width) : WINDOW_WIDTH;
            output_height = swapchain_extent.height > 0U ? static_cast<int>(swapchain_extent.height) : WINDOW_HEIGHT;
            ensure_sprite();
            update_game();
            draw_game();
            frame_sprite->updateTexture(renderer.surface());
            frame_sprite->drawSpriteRect(0, 0, output_width, output_height);
        }

      private:
        SoftwareRenderer renderer;
        Mesh cube_mesh;
        Mesh ball_mesh;
        PongGame game;
        mxvk::VK_Sprite *frame_sprite = nullptr;
        std::chrono::steady_clock::time_point previous_frame_time = std::chrono::steady_clock::now();
        int output_width = WINDOW_WIDTH;
        int output_height = WINDOW_HEIGHT;

        void ensure_sprite() {
            if (frame_sprite != nullptr) {
                return;
            }
            frame_sprite = createSprite(renderer.surface());
            frame_sprite->setTextureFilter(VK_FILTER_NEAREST);
        }

        void update_game() {
            const auto now = std::chrono::steady_clock::now();
            const float delta_seconds = std::min(
                std::chrono::duration<float>(now - previous_frame_time).count(),
                0.05f);
            previous_frame_time = now;

            const bool *keyboard = SDL_GetKeyboardState(nullptr);
            if (keyboard != nullptr) {
                float movement = 0.0f;
                if (keyboard[SDL_SCANCODE_W] || keyboard[SDL_SCANCODE_UP]) {
                    movement += 2.7f * delta_seconds;
                }
                if (keyboard[SDL_SCANCODE_S] || keyboard[SDL_SCANCODE_DOWN]) {
                    movement -= 2.7f * delta_seconds;
                }
                game.move_player(movement);
            }
            game.update(delta_seconds);
        }

        void draw_game() {
            renderer.begin_frame();
            draw_cuboid({0.0f, 0.0f, 0.28f, 1.0f}, {2.72f, 1.52f, 0.10f, 0.0f}, mxvk::MXVK_RGB(12, 38, 65));
            draw_cuboid({0.0f, COURT_HALF_HEIGHT + 0.07f, 0.08f, 1.0f}, {2.72f, 0.07f, 0.12f, 0.0f}, mxvk::MXVK_RGB(45, 198, 255));
            draw_cuboid({0.0f, -COURT_HALF_HEIGHT - 0.07f, 0.08f, 1.0f}, {2.72f, 0.07f, 0.12f, 0.0f}, mxvk::MXVK_RGB(45, 198, 255));

            for (int dash = -4; dash <= 4; ++dash) {
                draw_cuboid(
                    {0.0f, static_cast<float>(dash) * 0.31f, 0.11f, 1.0f},
                    {0.025f, 0.09f, 0.025f, 0.0f},
                    mxvk::MXVK_RGB(112, 151, 180));
            }

            draw_cuboid(
                {-PADDLE_X, game.player_position(), -0.02f, 1.0f},
                {PADDLE_HALF_WIDTH, PADDLE_HALF_HEIGHT, 0.18f, 0.0f},
                mxvk::MXVK_RGB(30, 144, 255));
            draw_cuboid(
                {PADDLE_X, game.computer_position(), -0.02f, 1.0f},
                {PADDLE_HALF_WIDTH, PADDLE_HALF_HEIGHT, 0.18f, 0.0f},
                mxvk::MXVK_RGB(255, 65, 112));

            const float rotation = static_cast<float>(SDL_GetTicks()) * 0.18f;
            renderer.draw_mesh({
                ball_mesh,
                game.ball(),
                {BALL_RADIUS, BALL_RADIUS, BALL_RADIUS, 0.0f},
                {rotation, rotation * 0.7f, 0.0f, 0.0f},
                mxvk::MXVK_RGB(255, 236, 125),
            });

            const int score_scale = std::max(2, std::min(renderer.width(), renderer.height()) / 80);
            const int score_y = score_scale * 3;
            renderer.draw_digit(
                renderer.width() / 2 - score_scale * 10,
                score_y,
                game.left_score() % 10,
                score_scale,
                mxvk::MXVK_RGB(80, 183, 255));
            renderer.draw_digit(
                renderer.width() / 2 + score_scale * 4,
                score_y,
                game.right_score() % 10,
                score_scale,
                mxvk::MXVK_RGB(255, 91, 133));
            if (game.is_paused()) {
                renderer.draw_pause_indicator();
            }
        }

        void draw_cuboid(const mxvk::vec4D &position, const mxvk::vec4D &scale, mxvk::MXCOLOR color) {
            renderer.draw_mesh({
                cube_mesh,
                position,
                scale,
                {0.0f, 0.0f, 0.0f, 0.0f},
                color,
            });
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const FramebufferDimensions framebuffer = args.framebufferSpecified
                                                      ? args.framebuffer
                                                      : FramebufferDimensions{DEFAULT_FRAME_WIDTH, DEFAULT_FRAME_HEIGHT};
        example::Math3DPongWindow window(args.fullscreen, args.enable_vsync, framebuffer);
        window.loop();
    } catch (mxvk::Exception &exception) {
        std::cerr << std::format("mxvk: Exception: {}\n", exception.text());
        return EXIT_FAILURE;
    } catch (ArgException<std::string> &exception) {
        std::cerr << std::format("mxvk: Argument Exception: {}\n", exception.text());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
