#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL3_ttf/SDL_ttf.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

namespace example {
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

    struct Glyph {
        Uint32 codepoint = 0;
        std::vector<SurfacePtr> levels{};
    };

    struct Stream {
        float head = 0.0f;
        float speed = 0.0f;
        int length = 0;
        int glyphOffset = 0;
        int shimmer = 0;
    };

    SDL_Color matrixColor(int level) {
        constexpr int maxLevel = 7;
        const float t = std::clamp(static_cast<float>(level) / static_cast<float>(maxLevel), 0.0f, 1.0f);
        const Uint8 r = static_cast<Uint8>(std::lerp(0.0f, 48.0f, std::pow(t, 2.9f)));
        const Uint8 g = static_cast<Uint8>(std::lerp(42.0f, 205.0f, std::pow(t, 0.82f)));
        const Uint8 b = static_cast<Uint8>(std::lerp(0.0f, 24.0f, std::pow(t, 3.1f)));
        const Uint8 a = static_cast<Uint8>(std::lerp(95.0f, 235.0f, t));
        return SDL_Color{r, g, b, a};
    }

    SDL_Color headColor() {
        return SDL_Color{208, 255, 200, 255};
    }

    std::vector<Uint32> fullSymbolSet() {
        return {
            0xFF66, 0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D,
            0xFF6E, 0xFF6F, 0xFF71, 0xFF72, 0xFF73, 0xFF74, 0xFF75, 0xFF76,
            0xFF77, 0xFF78, 0xFF79, 0xFF7A, 0xFF7B, 0xFF7C, 0xFF7D, 0xFF7E,
            0xFF7F, 0xFF80, 0xFF81, 0xFF82, 0xFF83, 0xFF84, 0xFF85, 0xFF86,
            0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C, 0xFF8D, 0xFF8E,
            0xFF8F, 0xFF90, 0xFF91, 0xFF92, 0xFF93, 0xFF94, 0xFF95, 0xFF96,
            0xFF97, 0xFF98, 0xFF99, 0xFF9A, 0xFF9B, 0xFF9C, 0xFF9D};
    }

    class PlanetWindow;

    class MatrixRainBackdrop {
      public:
        MatrixRainBackdrop(PlanetWindow &window, const std::string &assetRoot);
        ~MatrixRainBackdrop();

        void onSwapchainAboutToRecreate();
        void onSwapchainRecreated(PlanetWindow &window);
        void updateAndRender(PlanetWindow &window, VkCommandBuffer cmd);

      private:
        void loadGlyphs();
        void rebuildForExtent(PlanetWindow &window);
        void randomizeStreams();
        void updateRain(float dt);
        void resetStream(Stream &stream);
        void fadeCanvas(float dt);
        void drawStream(int column, const Stream &stream);

        static constexpr int fontSize = 28;
        static constexpr int glyphLevels = 7;

        std::string assetRoot;
        FontPtr font;
        SurfacePtr canvas;
        mxvk::VK_Sprite *rainSprite = nullptr;
        std::vector<Glyph> glyphs;
        std::vector<Stream> streams;
        std::mt19937 rng;
        Clock::time_point lastFrame{Clock::now()};
        int columns = 0;
        int rows = 0;
        int cellW = 20;
        int cellH = 28;
        int frameCounter = 0;
    };

    class PlanetWindow : public mxvk::VK_Window {
      public:
        PlanetWindow(const std::string &filename,
                     const std::string &path,
                     const std::string &title,
                     int width,
                     int height,
                     bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(PLANET_ASSET_DIR) : path) {
            const std::string modelPath = filename.empty() ? (assetRoot + "/data/saturn.mxmod.z") : filename;
            const std::string textureManifestPath = assetRoot + "/data/saturn.tex";
            const std::string textureBasePath = assetRoot + "/data";
            const std::string vertPath = std::string(PLANET_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = std::string(PLANET_SHADER_DIR) + "/model.frag.spv";

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setBackfaceCulling(false);
            model.setShaders(this, vertPath, fragPath);

            backdrop = std::make_unique<MatrixRainBackdrop>(*this, assetRoot);
        }

        ~PlanetWindow() override {
            if (device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device);
            }
            model.cleanup(this);
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                exit();
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = true;
                lastMouseX = static_cast<int>(e.button.x);
                lastMouseY = static_cast<int>(e.button.y);
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseDragging = false;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseDragging) {
                const int x = static_cast<int>(e.motion.x);
                const int y = static_cast<int>(e.motion.y);
                const int deltaX = x - lastMouseX;
                const int deltaY = y - lastMouseY;

                yawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                pitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                pitchDegrees = std::clamp(pitchDegrees, -80.0f, 80.0f);

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.45f;
                cameraDistance = std::clamp(cameraDistance, 1.8f, 12.0f);
                return;
            }
        }

        void onSwapchainAboutToRecreate() override {
            if (backdrop) {
                backdrop->onSwapchainAboutToRecreate();
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
            if (backdrop) {
                backdrop->onSwapchainRecreated(*this);
            }
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            if (backdrop) {
                backdrop->updateAndRender(*this, cmd);
            }

            const float elapsedSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            const VkExtent2D extent = getSwapchainExtent();
            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, glm::radians(-18.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            ubo.model = glm::rotate(ubo.model, elapsedSeconds * 0.45f, glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
            ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
            ubo.view = glm::lookAt(glm::vec3(0.0f, 0.15f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj[1][1] *= -1.0f;

            model.updateUBO(imageIndex, ubo);
            model.render(cmd, imageIndex, false);
        }

        void renderBackgroundSprite(mxvk::VK_Sprite &sprite, VkCommandBuffer cmd) {
            renderStandaloneSprite(sprite, cmd);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        std::unique_ptr<MatrixRainBackdrop> backdrop{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        bool mouseDragging = false;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float cameraDistance = 4.6f;
        float mouseSensitivity = 0.35f;
    };

    MatrixRainBackdrop::MatrixRainBackdrop(PlanetWindow &window, const std::string &assetRootPath)
        : assetRoot(assetRootPath),
          rng(std::random_device{}()) {
        if (!TTF_Init()) {
            throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
        }

        const std::string fontPath = assetRoot + "/data/keifont.ttf";
        font.reset(TTF_OpenFont(fontPath.c_str(), fontSize));
        if (!font) {
            throw mxvk::Exception("Failed to load matrix font: " + fontPath + ": " + std::string(SDL_GetError()));
        }
        TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);

        loadGlyphs();
        lastFrame = Clock::now();
    }

    MatrixRainBackdrop::~MatrixRainBackdrop() {
        glyphs.clear();
        font.reset();
        TTF_Quit();
    }

    void MatrixRainBackdrop::onSwapchainAboutToRecreate() {
        if (rainSprite) {
            rainSprite->releaseUploadResources();
        }
    }

    void MatrixRainBackdrop::onSwapchainRecreated(PlanetWindow &window) {
        rebuildForExtent(window);
    }

    void MatrixRainBackdrop::updateAndRender(PlanetWindow &window, VkCommandBuffer cmd) {
        rebuildForExtent(window);

        const auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);

        if (rainSprite == nullptr || canvas == nullptr || glyphs.empty()) {
            return;
        }

        updateRain(dt);
        rainSprite->updateTexture(canvas.get());
        rainSprite->drawSpriteRect(0, 0, canvas->w, canvas->h);
        window.renderBackgroundSprite(*rainSprite, cmd);
        rainSprite->clearQueue();
    }

    void MatrixRainBackdrop::loadGlyphs() {
        const std::vector<Uint32> symbols = fullSymbolSet();
        int maxGlyphW = 0;
        int maxGlyphH = 0;

        glyphs.reserve(symbols.size());
        for (const Uint32 codepoint : symbols) {
            Glyph glyph;
            glyph.codepoint = codepoint;
            glyph.levels.reserve(glyphLevels + 1);
            for (int level = 0; level <= glyphLevels; ++level) {
                SDL_Surface *rendered = TTF_RenderGlyph_Blended(font.get(), codepoint, matrixColor(level));
                if (rendered == nullptr) {
                    continue;
                }
                SurfacePtr converted(SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32));
                SDL_DestroySurface(rendered);
                if (converted != nullptr) {
                    SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
                    maxGlyphW = std::max(maxGlyphW, converted->w);
                    maxGlyphH = std::max(maxGlyphH, converted->h);
                    glyph.levels.emplace_back(std::move(converted));
                }
            }

            SDL_Surface *renderedHead = TTF_RenderGlyph_Blended(font.get(), codepoint, headColor());
            if (renderedHead != nullptr) {
                SurfacePtr converted(SDL_ConvertSurface(renderedHead, SDL_PIXELFORMAT_RGBA32));
                SDL_DestroySurface(renderedHead);
                if (converted != nullptr) {
                    SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
                    maxGlyphW = std::max(maxGlyphW, converted->w);
                    maxGlyphH = std::max(maxGlyphH, converted->h);
                    glyph.levels.emplace_back(std::move(converted));
                }
            }

            if (glyph.levels.size() == static_cast<std::size_t>(glyphLevels + 2)) {
                glyphs.emplace_back(std::move(glyph));
            }
        }

        if (glyphs.empty()) {
            throw mxvk::Exception("Planet example could not render any glyphs from keifont.ttf");
        }

        cellW = std::max(16, maxGlyphW + 3);
        cellH = std::max(24, maxGlyphH + 2);
    }

    void MatrixRainBackdrop::rebuildForExtent(PlanetWindow &window) {
        const VkExtent2D extent = window.getSwapchainExtent();
        const int width = static_cast<int>(extent.width);
        const int height = static_cast<int>(extent.height);
        if (width <= 0 || height <= 0 || (canvas != nullptr && canvas->w == width && canvas->h == height)) {
            return;
        }

        canvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (canvas == nullptr) {
            throw mxvk::Exception("Failed to create planet matrix canvas: " + std::string(SDL_GetError()));
        }
        SDL_FillSurfaceRect(canvas.get(), nullptr, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));

        if (rainSprite == nullptr) {
            rainSprite = window.createSprite(canvas.get());
        } else {
            rainSprite->updateTexture(canvas.get());
        }

        columns = std::max(1, (width + cellW - 1) / cellW);
        rows = std::max(1, (height + cellH - 1) / cellH);
        streams.assign(columns, {});
        randomizeStreams();
    }

    void MatrixRainBackdrop::randomizeStreams() {
        std::uniform_real_distribution<float> headDist(-static_cast<float>(rows) * 1.5f, 0.0f);
        std::uniform_real_distribution<float> speedDist(9.0f, 26.0f);
        std::uniform_int_distribution<int> lengthDist(12, 38);
        std::uniform_int_distribution<int> glyphDist(0, static_cast<int>(glyphs.size() - 1));
        std::uniform_int_distribution<int> shimmerDist(0, 1200);

        for (Stream &stream : streams) {
            stream.head = headDist(rng);
            stream.speed = speedDist(rng);
            stream.length = std::min(std::max(6, rows), lengthDist(rng));
            stream.glyphOffset = glyphDist(rng);
            stream.shimmer = shimmerDist(rng);
        }
    }

    void MatrixRainBackdrop::updateRain(float dt) {
        fadeCanvas(dt);

        for (int column = 0; column < columns; ++column) {
            Stream &stream = streams[column];
            stream.head += stream.speed * dt;
            if (stream.head - static_cast<float>(stream.length) > static_cast<float>(rows) + 3.0f) {
                resetStream(stream);
            }
            drawStream(column, stream);
        }
        ++frameCounter;
    }

    void MatrixRainBackdrop::resetStream(Stream &stream) {
        std::uniform_real_distribution<float> headDist(-static_cast<float>(rows) * 0.8f, -1.0f);
        std::uniform_real_distribution<float> speedDist(9.0f, 27.0f);
        std::uniform_int_distribution<int> lengthDist(10, 42);
        std::uniform_int_distribution<int> glyphDist(0, static_cast<int>(glyphs.size() - 1));
        std::uniform_int_distribution<int> shimmerDist(0, 1200);
        stream.head = headDist(rng);
        stream.speed = speedDist(rng);
        stream.length = std::min(std::max(8, rows + rows / 4), lengthDist(rng));
        stream.glyphOffset = glyphDist(rng);
        stream.shimmer = shimmerDist(rng);
    }

    void MatrixRainBackdrop::fadeCanvas(float dt) {
        const float fade = std::pow(0.50f, dt * 8.0f);
        if (!SDL_LockSurface(canvas.get())) {
            return;
        }
        auto *pixels = static_cast<Uint8 *>(canvas->pixels);
        for (int y = 0; y < canvas->h; ++y) {
            Uint8 *row = pixels + y * canvas->pitch;
            for (int x = 0; x < canvas->w; ++x) {
                Uint8 *p = row + x * 4;
                p[0] = static_cast<Uint8>(static_cast<float>(p[0]) * fade * 0.72f);
                p[1] = static_cast<Uint8>(static_cast<float>(p[1]) * fade);
                p[2] = static_cast<Uint8>(static_cast<float>(p[2]) * fade * 0.55f);
                p[3] = 255;
            }
        }
        SDL_UnlockSurface(canvas.get());
    }

    void MatrixRainBackdrop::drawStream(int column, const Stream &stream) {
        const int headRow = static_cast<int>(std::floor(stream.head));
        const int x = column * cellW;
        for (int tail = stream.length; tail >= 0; --tail) {
            const int row = headRow - tail;
            if (row < -1 || row >= rows) {
                continue;
            }

            const float age = static_cast<float>(tail) / static_cast<float>(std::max(1, stream.length));
            int level = glyphLevels - static_cast<int>(std::round(age * static_cast<float>(glyphLevels + 1)));
            if (tail == 0) {
                level = glyphLevels + 1;
            } else if (tail <= 2) {
                level = glyphLevels;
            }
            level = std::clamp(level, 0, glyphLevels + 1);

            const int glyphIndex =
                (stream.glyphOffset + row * 17 + column * 11 + stream.shimmer + frameCounter / 3 + tail * 5) %
                static_cast<int>(glyphs.size());
            const Glyph &glyph = glyphs[glyphIndex < 0 ? glyphIndex + static_cast<int>(glyphs.size()) : glyphIndex];
            if (glyph.levels.empty()) {
                continue;
            }

            const SDL_Rect clearRect{x, row * cellH, cellW, cellH};
            SDL_FillSurfaceRect(canvas.get(), &clearRect, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
            SDL_Surface *surface = glyph.levels[level].get();
            Uint8 previousAlpha = 255;
            SDL_GetSurfaceAlphaMod(surface, &previousAlpha);

            const int glyphX = x + (cellW - glyph.levels[level]->w) / 2;
            const int glyphY = row * cellH;
            const int glowAlpha = (tail == 0) ? 110 : (tail <= 2 ? 72 : 34);

            SDL_SetSurfaceAlphaMod(surface, static_cast<Uint8>(glowAlpha));
            SDL_Rect glowDst{glyphX - 1, glyphY, glyph.levels[level]->w, glyph.levels[level]->h};
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glowDst);
            glowDst.x = glyphX + 1;
            glowDst.y = glyphY;
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glowDst);
            glowDst.x = glyphX;
            glowDst.y = glyphY + 1;
            SDL_BlitSurface(surface, nullptr, canvas.get(), &glowDst);

            SDL_SetSurfaceAlphaMod(surface, previousAlpha);
            SDL_Rect dst{glyphX, glyphY, glyph.levels[level]->w, glyph.levels[level]->h};
            SDL_BlitSurface(surface, nullptr, canvas.get(), &dst);
        }
    }
} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        example::PlanetWindow window(args.filename, args.path, "MXVK Planet Example", args.width, args.height, args.fullscreen);
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
