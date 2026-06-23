#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace {
    using Clock = std::chrono::steady_clock;

    struct SurfaceDeleter {
        void operator()(SDL_Surface *surface) const {
            if (surface != nullptr) {
                SDL_DestroySurface(surface);
            }
        }
    };

    using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

    struct TtfDeleter {
        void operator()(TTF_Font *font) const {
            if (font != nullptr) {
                TTF_CloseFont(font);
            }
        }
    };

    using FontPtr = std::unique_ptr<TTF_Font, TtfDeleter>;

    struct Stream {
        float head = 0.0f;
        float speed = 0.0f;
        int length = 0;
        int bitSeed = 0;
        float phase = 0.0f;
        float depthPhase = 0.0f;
        float depthAmplitude = 0.0f;
        float depthBias = 0.0f;
    };

    SDL_Color matrixTrailColor(int level) {
        constexpr int maxLevel = 7;
        const float t = std::clamp(static_cast<float>(level) / static_cast<float>(maxLevel), 0.0f, 1.0f);
        const Uint8 r = static_cast<Uint8>(std::lerp(0.0f, 180.0f, std::pow(t, 2.8f)));
        const Uint8 g = static_cast<Uint8>(std::lerp(36.0f, 255.0f, std::pow(t, 0.72f)));
        const Uint8 b = static_cast<Uint8>(std::lerp(8.0f, 205.0f, std::pow(t, 3.2f)));
        const Uint8 a = static_cast<Uint8>(std::lerp(120.0f, 255.0f, t));
        return SDL_Color{r, g, b, a};
    }

    std::string trim_copy(const std::string &value) {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return {};
        }
        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    std::optional<Uint8> parse_u8_component(const std::string &value, int base) {
        int parsed = 0;
        const std::string trimmed = trim_copy(value);
        const char *begin = trimmed.data();
        const char *end = trimmed.data() + trimmed.size();
        const std::from_chars_result result = std::from_chars(begin, end, parsed, base);
        if (result.ec != std::errc{} || result.ptr != end || parsed < 0 || parsed > 255) {
            return std::nullopt;
        }
        return static_cast<Uint8>(parsed);
    }

    std::optional<SDL_Color> parse_color_spec(const std::string &spec) {
        const std::string value = trim_copy(spec);
        if (value.empty()) {
            return std::nullopt;
        }

        if (value.front() == '#') {
            if (value.size() != 7) {
                return std::nullopt;
            }
            const auto r = parse_u8_component(value.substr(1, 2), 16);
            const auto g = parse_u8_component(value.substr(3, 2), 16);
            const auto b = parse_u8_component(value.substr(5, 2), 16);
            if (!r || !g || !b) {
                return std::nullopt;
            }
            return SDL_Color{*r, *g, *b, 255};
        }

        const std::size_t first = value.find(',');
        if (first == std::string::npos) {
            return std::nullopt;
        }
        const std::size_t second = value.find(',', first + 1);
        if (second == std::string::npos || value.find(',', second + 1) != std::string::npos) {
            return std::nullopt;
        }

        const auto r = parse_u8_component(value.substr(0, first), 10);
        const auto g = parse_u8_component(value.substr(first + 1, second - first - 1), 10);
        const auto b = parse_u8_component(value.substr(second + 1), 10);
        if (!r || !g || !b) {
            return std::nullopt;
        }
        return SDL_Color{*r, *g, *b, 255};
    }

    SurfacePtr renderGlyph(TTF_Font *font, const std::string &glyph, const SDL_Color &color) {
        SDL_Surface *rendered = TTF_RenderText_Blended(font, glyph.c_str(), 0, color);
        if (rendered == nullptr) {
            return {};
        }

        SurfacePtr converted(SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32));
        SDL_DestroySurface(rendered);
        if (converted != nullptr) {
            SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
        }
        return converted;
    }
} // namespace

namespace example {
    class BinaryMatrixWindow final : public mxvk::VK_Window {
      public:
        BinaryMatrixWindow(const std::string &path,
                           const std::string &title,
                           const int width,
                           const int height,
                           const bool fullscreen,
                           const std::string &color)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(binary_matrix_ASSET_DIR) : path),
              rng(std::random_device{}()) {
            if (assetRoot == ".") {
                assetRoot = binary_matrix_ASSET_DIR;
            }

            if (!color.empty()) {
                const std::optional<SDL_Color> parsedColor = parse_color_spec(color);
                if (!parsedColor) {
                    throw mxvk::Exception("Invalid binary_matrix color: " + color + " (expected #RRGGBB or R,G,B)");
                }
                digitColor = *parsedColor;
            }

            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

            if (!TTF_Init()) {
                throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
            }

            font.reset(TTF_OpenFont((assetRoot + "/data/NotoSansCJK-Bold.ttc").c_str(), fontSize));
            if (!font) {
                throw mxvk::Exception("Failed to load binary matrix font: " + std::string(SDL_GetError()));
            }
            TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);

            const std::string spriteVertPath = assetRoot + "/data/sprite.vert.spv";
            const std::string backgroundFragPath = assetRoot + "/data/background.frag.spv";
            backgroundSprite = createSprite(assetRoot + "/data/bg.png", spriteVertPath, backgroundFragPath);
            if (backgroundSprite == nullptr) {
                throw mxvk::Exception("Failed to create binary matrix background sprite");
            }

            loadDigits();
            rebuildForExtent();
            lastFrame = Clock::now();
        }

        ~BinaryMatrixWindow() override {
            digitZeroSprite = nullptr;
            digitOneSprite = nullptr;
            backgroundSprite = nullptr;
            font.reset();
            TTF_Quit();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                } else if (e.key.key == SDLK_SPACE) {
                    randomizeStreams();
                }
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                mouseX = e.motion.x;
                mouseY = e.motion.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                mousePressed = true;
                mouseX = e.button.x;
                mouseY = e.button.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                mousePressed = false;
                mouseX = e.button.x;
                mouseY = e.button.y;
            }
        }

        void proc() override {
            rebuildForExtent();
            updateCameraInput();
        }

        void onSwapchainRecreated() override {
            if (digitZeroSprite != nullptr) {
                digitZeroSprite->resize(this);
            }
            if (digitOneSprite != nullptr) {
                digitOneSprite->resize(this);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (digitZeroSprite == nullptr || digitOneSprite == nullptr) {
                return;
            }
            const auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - lastFrame).count();
            lastFrame = now;
            dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);

            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            const float yaw = glm::radians(cameraYaw);
            const float pitch = glm::radians(cameraPitch);
            glm::mat4 cameraRotation(1.0f);
            cameraRotation = glm::rotate(cameraRotation, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            cameraRotation = glm::rotate(cameraRotation, pitch, glm::vec3(1.0f, 0.0f, 0.0f));

            const glm::vec3 cameraPosition = glm::vec3(cameraRotation * glm::vec4(0.0f, 0.0f, cameraDistance, 1.0f));
            const glm::vec3 cameraUp = glm::normalize(glm::vec3(cameraRotation * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            glm::mat4 view = glm::lookAt(cameraPosition, glm::vec3(0.0f), cameraUp);
            glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            updateBinaryRain(dt);
            backgroundTime += dt;

            drawBackground();
            backgroundSprite->renderSprites(cmd, sprite_pipeline_layout, extent.width, extent.height);
            backgroundSprite->clearQueue();

            digitZeroSprite->updateCamera(imageIndex, view, proj);
            digitOneSprite->updateCamera(imageIndex, view, proj);

            for (int column = 0; column < columns; ++column) {
                const Stream &stream = streams[column];
                drawStream(column, stream);
            }

            digitZeroSprite->render(cmd, imageIndex);
            digitZeroSprite->clearQueue();
            digitOneSprite->render(cmd, imageIndex);
            digitOneSprite->clearQueue();
        }

      private:
        void loadDigits() {
            SurfacePtr zero_surface = renderGlyph(font.get(), "0", digitColor);
            SurfacePtr one_surface = renderGlyph(font.get(), "1", digitColor);

            if (zero_surface == nullptr || one_surface == nullptr) {
                throw mxvk::Exception("Failed to render binary matrix glyphs");
            }

            digitZeroSprite = createSprite3D(zero_surface.get());
            digitOneSprite = createSprite3D(one_surface.get());

            if (digitZeroSprite == nullptr || digitOneSprite == nullptr) {
                throw mxvk::Exception("Failed to create binary matrix 3D sprites");
            }

            digitZeroSprite->setDepthTestEnabled(true);
            digitZeroSprite->setDepthWriteEnabled(false);
            digitZeroSprite->setAlphaDiscardThreshold(0.05f);

            digitOneSprite->setDepthTestEnabled(true);
            digitOneSprite->setDepthWriteEnabled(false);
            digitOneSprite->setAlphaDiscardThreshold(0.05f);
        }

        void rebuildForExtent() {
            const VkExtent2D extent = getSwapchainExtent();
            const int width = static_cast<int>(extent.width);
            const int height = static_cast<int>(extent.height);
            if (width <= 0 || height <= 0 || (extentWidth == width && extentHeight == height)) {
                return;
            }

            extentWidth = width;
            extentHeight = height;

            columns = std::max(48, width / 10);
            rows = std::max(72, height / 12);

            const float aspect = static_cast<float>(width) / static_cast<float>(std::max(1, height));
            horizontalSpan = 2.15f * aspect;
            verticalSpan = 3.6f;
            columnStep = (horizontalSpan * 2.0f) / static_cast<float>(columns);
            rowStep = (verticalSpan * 2.0f) / static_cast<float>(rows);
            baseGlyphSize = std::min(columnStep, rowStep) * 0.82f;

            streams.assign(static_cast<std::size_t>(columns), {});
            randomizeStreams();
        }

        void randomizeStreams() {
            std::uniform_real_distribution<float> headDist(-static_cast<float>(rows) * 1.2f, 0.0f);
            std::uniform_real_distribution<float> speedDist(4.0f, 12.0f);
            std::uniform_int_distribution<int> lengthDist(34, std::max(48, rows + rows / 2));
            std::uniform_int_distribution<int> seedDist(0, 4095);
            std::uniform_real_distribution<float> phaseDist(0.0f, 6.28318530717958647692f);
            std::uniform_real_distribution<float> amplitudeDist(0.15f, 0.60f);
            std::uniform_real_distribution<float> biasDist(-0.22f, 0.28f);

            for (Stream &stream : streams) {
                stream.head = headDist(rng);
                stream.speed = speedDist(rng);
                stream.length = std::min(lengthDist(rng), rows + rows / 2);
                stream.bitSeed = seedDist(rng);
                stream.phase = phaseDist(rng);
                stream.depthPhase = phaseDist(rng);
                stream.depthAmplitude = amplitudeDist(rng);
                stream.depthBias = biasDist(rng);
            }
        }

        void updateBinaryRain(float dt) {
            lastScrollPhase += dt * 0.85f;

            for (Stream &stream : streams) {
                stream.head += stream.speed * dt;
                if (stream.head - static_cast<float>(stream.length) > static_cast<float>(rows) + 2.0f) {
                    resetStream(stream);
                }
            }

            if (std::fmod(lastScrollPhase, 0.22f) < dt * 0.85f) {
                for (Stream &stream : streams) {
                    stream.bitSeed ^= (frameCounter & 3);
                }
            }

            ++frameCounter;
        }

        void resetStream(Stream &stream) {
            std::uniform_real_distribution<float> headDist(-static_cast<float>(rows) * 0.9f, -1.0f);
            std::uniform_real_distribution<float> speedDist(4.0f, 12.0f);
            std::uniform_int_distribution<int> lengthDist(34, std::max(48, rows + rows / 2));
            std::uniform_int_distribution<int> seedDist(0, 4095);
            std::uniform_real_distribution<float> phaseDist(0.0f, 6.28318530717958647692f);
            std::uniform_real_distribution<float> amplitudeDist(0.15f, 0.60f);
            std::uniform_real_distribution<float> biasDist(-0.22f, 0.28f);

            stream.head = headDist(rng);
            stream.speed = speedDist(rng);
            stream.length = std::min(lengthDist(rng), rows + rows / 2);
            stream.bitSeed = seedDist(rng);
            stream.phase = phaseDist(rng);
            stream.depthPhase = phaseDist(rng);
            stream.depthAmplitude = amplitudeDist(rng);
            stream.depthBias = biasDist(rng);
        }

        void drawStream(int column, const Stream &stream) {
            const float x = -horizontalSpan + (static_cast<float>(column) + 0.5f) * columnStep;

            const int headRow = static_cast<int>(std::floor(stream.head));
            for (int tail = stream.length; tail >= 0; --tail) {
                const int row = headRow - tail;
                if (row < -1 || row >= rows) {
                    continue;
                }

                const float age = static_cast<float>(tail) / static_cast<float>(std::max(1, stream.length));
                const float rowCenter = verticalSpan - (static_cast<float>(row) + 0.5f) * rowStep;
                const float z = 0.0f;
                const float size = baseGlyphSize;
                int level = trailLevels - static_cast<int>(std::round(age * static_cast<float>(trailLevels)));
                if (tail == 0) {
                    level = trailLevels;
                } else if (tail <= 2) {
                    level = trailLevels - 1;
                }
                level = std::clamp(level, 0, trailLevels);
                const SDL_Color tintColor = matrixTrailColor(level);
                const glm::vec4 tint(static_cast<float>(tintColor.r) / 255.0f,
                                     static_cast<float>(tintColor.g) / 255.0f,
                                     static_cast<float>(tintColor.b) / 255.0f,
                                     static_cast<float>(tintColor.a) / 255.0f);

                const int cell = static_cast<int>(std::floor(stream.head)) - tail;
                const bool drawOne = ((stream.bitSeed + column * 13 + cell * 17 + tail * 3 + frameCounter / 3) & 1) != 0;
                mxvk::VK_Sprite3D *sprite = drawOne ? digitOneSprite : digitZeroSprite;
                sprite->drawSprite(glm::vec3(x, rowCenter, z), glm::vec2(size, size), tint, 0.0f);
            }
        }

        void drawBackground() {
            if (backgroundSprite == nullptr) {
                return;
            }

            const VkExtent2D extent = getSwapchainExtent();
            if (extent.width == 0U || extent.height == 0U) {
                return;
            }

            backgroundSprite->setShaderParams(backgroundTime,
                                              static_cast<float>(mouseX),
                                              static_cast<float>(mouseY),
                                              mousePressed ? 1.0f : 0.0f);
            backgroundSprite->drawSpriteRect(0, 0, static_cast<int>(extent.width), static_cast<int>(extent.height));
        }

        void updateCameraInput() {
            const bool *keyboard = SDL_GetKeyboardState(nullptr);
            if (keyboard == nullptr) {
                return;
            }

            const auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - lastCameraInput).count();
            lastCameraInput = now;
            dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);

            const float orbitDelta = cameraOrbitSpeed * dt;
            const float zoomDelta = cameraZoomSpeed * dt;

            if (keyboard[SDL_SCANCODE_LEFT]) {
                cameraYaw -= orbitDelta;
            }
            if (keyboard[SDL_SCANCODE_RIGHT]) {
                cameraYaw += orbitDelta;
            }
            if (keyboard[SDL_SCANCODE_UP]) {
                cameraPitch += orbitDelta;
            }
            if (keyboard[SDL_SCANCODE_DOWN]) {
                cameraPitch -= orbitDelta;
            }
            if (keyboard[SDL_SCANCODE_PAGEUP]) {
                cameraDistance = std::max(1.5f, cameraDistance - zoomDelta);
            }
            if (keyboard[SDL_SCANCODE_PAGEDOWN]) {
                cameraDistance = std::min(12.0f, cameraDistance + zoomDelta);
            }

            cameraYaw = std::fmod(cameraYaw, 360.0f);
            if (cameraYaw < 0.0f) {
                cameraYaw += 360.0f;
            }

            cameraPitch = std::fmod(cameraPitch, 360.0f);
            if (cameraPitch < 0.0f) {
                cameraPitch += 360.0f;
            }
        }

        static constexpr int fontSize = 56;
        static constexpr int trailLevels = 7;

        std::string assetRoot;
        FontPtr font;
        mxvk::VK_Sprite *backgroundSprite = nullptr;
        mxvk::VK_Sprite3D *digitZeroSprite = nullptr;
        mxvk::VK_Sprite3D *digitOneSprite = nullptr;
        std::mt19937 rng;
        Clock::time_point lastFrame{Clock::now()};
        std::vector<Stream> streams;
        int columns = 0;
        int rows = 0;
        int extentWidth = 0;
        int extentHeight = 0;
        float horizontalSpan = 0.0f;
        float verticalSpan = 0.0f;
        float columnStep = 0.0f;
        float rowStep = 0.0f;
        float baseGlyphSize = 0.0f;
        float lastScrollPhase = 0.0f;
        float backgroundTime = 0.0f;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        bool mousePressed = false;
        int frameCounter = 0;
        float cameraYaw = 0.0f;
        float cameraPitch = 0.0f;
        float cameraDistance = 4.4f;
        Clock::time_point lastCameraInput{Clock::now()};
        static constexpr float cameraOrbitSpeed = 140.0f;
        static constexpr float cameraZoomSpeed = 2.2f;
        SDL_Color digitColor{255, 255, 255, 255};
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::BinaryMatrixWindow window(
            args.path, "-[ MXVK Binary Matrix ]-", args.width, args.height, args.fullscreen, args.color);
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
