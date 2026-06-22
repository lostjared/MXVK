#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_abstract_model.hpp"
#include "mxvk/mxvk_exception.hpp"

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

    struct Glyph {
        Uint32 codepoint;
        std::vector<SurfacePtr> levels;
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

    std::vector<Uint32> matrixSymbolSet() {
        return {
            0xFF66, 0xFF67, 0xFF68, 0xFF69, 0xFF6A, 0xFF6B, 0xFF6C, 0xFF6D,
            0xFF6E, 0xFF6F, 0xFF71, 0xFF72, 0xFF73, 0xFF74, 0xFF75, 0xFF76,
            0xFF77, 0xFF78, 0xFF79, 0xFF7A, 0xFF7B, 0xFF7C, 0xFF7D, 0xFF7E,
            0xFF7F, 0xFF80, 0xFF81, 0xFF82, 0xFF83, 0xFF84, 0xFF85, 0xFF86,
            0xFF87, 0xFF88, 0xFF89, 0xFF8A, 0xFF8B, 0xFF8C, 0xFF8D, 0xFF8E,
            0xFF8F, 0xFF90, 0xFF91, 0xFF92, 0xFF93, 0xFF94, 0xFF95, 0xFF96,
            0xFF97, 0xFF98, 0xFF99, 0xFF9A, 0xFF9B, 0xFF9C, 0xFF9D};
    }

    class MatrixTexture {
      public:
        explicit MatrixTexture(const std::string &assetRoot)
            : assetRoot(assetRoot.empty() ? std::string(MODEL_EXAMPLE_ASSET_DIR) : assetRoot),
              rng(std::random_device{}()) {
            if (!TTF_Init()) {
                throw mxvk::Exception("Failed to initialize SDL_ttf for matrix texture: " + std::string(SDL_GetError()));
            }

            const std::string fontPath = this->assetRoot + "/data/keifont.ttf";
            font.reset(TTF_OpenFont(fontPath.c_str(), fontSize));
            if (!font) {
                throw mxvk::Exception("Failed to load matrix texture font: " + fontPath + ": " + std::string(SDL_GetError()));
            }
            TTF_SetFontHinting(font.get(), TTF_HINTING_LIGHT);

            loadGlyphs();

            canvas.reset(SDL_CreateSurface(textureWidth, textureHeight, SDL_PIXELFORMAT_RGBA32));
            if (canvas == nullptr) {
                throw mxvk::Exception("Failed to create matrix texture canvas: " + std::string(SDL_GetError()));
            }
            uploadCanvas.reset(SDL_CreateSurface(textureWidth, textureHeight, SDL_PIXELFORMAT_RGBA32));
            if (uploadCanvas == nullptr) {
                throw mxvk::Exception("Failed to create matrix texture upload canvas: " + std::string(SDL_GetError()));
            }
            SDL_FillSurfaceRect(canvas.get(), nullptr, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
            SDL_FillSurfaceRect(uploadCanvas.get(), nullptr, SDL_MapSurfaceRGBA(uploadCanvas.get(), 0, 0, 0, 255));

            columns = std::max(1, (textureWidth + cellW - 1) / cellW);
            rows = std::max(1, (textureHeight + cellH - 1) / cellH);
            streams.assign(columns, {});
            randomizeStreams();
            lastFrame = Clock::now();
        }

        ~MatrixTexture() {
            glyphs.clear();
            font.reset();
            TTF_Quit();
        }

        void update(float dt) {
            if (canvas == nullptr || glyphs.empty()) {
                return;
            }

            dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);
            updateRain(dt);
            flipForUpload();
        }

        [[nodiscard]] const void *pixels() const {
            return uploadCanvas != nullptr ? uploadCanvas->pixels : nullptr;
        }

        [[nodiscard]] int width() const {
            return uploadCanvas != nullptr ? uploadCanvas->w : 0;
        }

        [[nodiscard]] int height() const {
            return uploadCanvas != nullptr ? uploadCanvas->h : 0;
        }

        [[nodiscard]] int pitch() const {
            return uploadCanvas != nullptr ? uploadCanvas->pitch : 0;
        }

      private:
        void loadGlyphs() {
            const std::vector<Uint32> symbols = matrixSymbolSet();
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
                throw mxvk::Exception("Matrix texture could not render any glyphs from keifont.ttf");
            }

            cellW = std::max(16, maxGlyphW + 3);
            cellH = std::max(24, maxGlyphH + 2);
        }

        void randomizeStreams() {
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

        void updateRain(float dt) {
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
            lastFrame = Clock::now();
        }

        void resetStream(Stream &stream) {
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

        void fadeCanvas(float dt) {
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

        void drawStream(int column, const Stream &stream) {
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

                const int glyphIndex = (stream.glyphOffset + row * 17 + column * 11 + stream.shimmer + frameCounter / 3 + tail * 5) %
                                       static_cast<int>(glyphs.size());
                const Glyph &glyph = glyphs[glyphIndex < 0 ? glyphIndex + glyphs.size() : glyphIndex];
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

        void flipForUpload() {
            if (canvas == nullptr || uploadCanvas == nullptr) {
                return;
            }
            if (!SDL_LockSurface(canvas.get()) || !SDL_LockSurface(uploadCanvas.get())) {
                if (canvas != nullptr) {
                    SDL_UnlockSurface(canvas.get());
                }
                if (uploadCanvas != nullptr) {
                    SDL_UnlockSurface(uploadCanvas.get());
                }
                return;
            }

            const auto *srcPixels = static_cast<const Uint8 *>(canvas->pixels);
            auto *dstPixels = static_cast<Uint8 *>(uploadCanvas->pixels);
            const int rowBytes = std::min(canvas->pitch, uploadCanvas->pitch);
            for (int y = 0; y < canvas->h; ++y) {
                const int srcRow = y * canvas->pitch;
                const int dstRow = (uploadCanvas->h - 1 - y) * uploadCanvas->pitch;
                std::memcpy(dstPixels + dstRow, srcPixels + srcRow, static_cast<std::size_t>(rowBytes));
            }

            SDL_UnlockSurface(uploadCanvas.get());
            SDL_UnlockSurface(canvas.get());
        }

        static constexpr int fontSize = 28;
        static constexpr int glyphLevels = 7;
        static constexpr int textureWidth = 1024;
        static constexpr int textureHeight = 1024;

        std::string assetRoot;
        FontPtr font;
        SurfacePtr canvas;
        SurfacePtr uploadCanvas;
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
} // namespace

namespace example {

    class ModelWindow : public mxvk::VK_Window {
      public:
        ModelWindow(const std::string filename,
                    bool usingDefaultModel,
                    const std::string &path,
                    const std::string &resource,
                    const std::string &resource_path,
                    const std::string &fragmentShaderPath,
                    const std::string &title,
                    int width,
                    int height,
                    bool fullscreen,
                    bool binaryTextureMode)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(MODEL_EXAMPLE_ASSET_DIR) : path),
              binaryTextureMode(binaryTextureMode) {
            const std::string modelPath = filename;
            const std::string textureManifestPath = resource.empty() && usingDefaultModel ? assetRoot + "/data/texture_manifest.txt" : resource;
            const bool useDefaultTextureBase = resource_path.empty() && (usingDefaultModel || !resource.empty());
            const std::string textureBasePath = resource_path.empty() ? (useDefaultTextureBase ? assetRoot + "/data" : "") : resource_path;
            const std::string vertPath = std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.vert.spv";
            const std::string fragPath = fragmentShaderPath.empty() ? (std::string(MODEL_EXAMPLE_SHADER_DIR) + "/model.frag.spv") : fragmentShaderPath;

            model.load(this, modelPath, textureManifestPath, textureBasePath, 1.0f);
            model.setShaders(this, vertPath, fragPath);
            model.setBackfaceCulling(!binaryTextureMode);
            if (binaryTextureMode) {
                binaryMatrixTexture = std::make_unique<MatrixTexture>(assetRoot);
            }
        }

        ~ModelWindow() override {
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

            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_SPACE) {
                if (e.key.repeat) {
                    return;
                }
                autoSpinEnabled = !autoSpinEnabled;
                return;
            }

            if (binaryTextureMode && e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                if (e.key.repeat) {
                    return;
                }
                skyboxMode = !skyboxMode;
                if (skyboxMode) {
                    resetSkyboxCamera();
                }
                return;
            }

            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                const bool pressed = (e.type == SDL_EVENT_KEY_DOWN);
                if (e.key.key == SDLK_W) {
                    skyboxLookUpKey = pressed;
                } else if (e.key.key == SDLK_S) {
                    skyboxLookDownKey = pressed;
                } else if (e.key.key == SDLK_A) {
                    skyboxLookLeftKey = pressed;
                } else if (e.key.key == SDLK_D) {
                    skyboxLookRightKey = pressed;
                }
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

                if (skyboxMode) {
                    skyboxYawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                    skyboxPitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                    skyboxPitchDegrees = std::clamp(skyboxPitchDegrees, -85.0f, 85.0f);
                } else {
                    yawDegrees += static_cast<float>(deltaX) * mouseSensitivity;
                    pitchDegrees += static_cast<float>(deltaY) * mouseSensitivity;
                    pitchDegrees = std::clamp(pitchDegrees, -80.0f, 80.0f);
                }

                lastMouseX = x;
                lastMouseY = y;
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL && !skyboxMode) {
                const float delta = (e.wheel.y != 0.0f) ? e.wheel.y : static_cast<float>(e.wheel.integer_y);
                cameraDistance -= delta * 0.45f;
                cameraDistance = std::clamp(cameraDistance, 1.8f, 12.0f);
                return;
            }
        }

        void onSwapchainRecreated() override {
            model.resize(this);
        }

        void onRecordCustomRendering(VkCommandBuffer cmd, uint32_t imageIndex) override {
            const auto now = std::chrono::steady_clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
            const float elapsedSeconds = std::chrono::duration<float>(now - startTime).count();
            lastFrameTime = now;
            if (autoSpinEnabled) {
                autoSpinRadians += deltaSeconds * autoSpinSpeed;
            }

            if (skyboxMode) {
                updateSkyboxCamera(deltaSeconds);
            }

            const VkExtent2D extent = getSwapchainExtent();

            const float aspect = (extent.height > 0U)
                                     ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                                     : 1.0f;

            mxvk::UniformBufferObject ubo{};
            if (skyboxMode) {
                const glm::vec3 front = skyboxForwardVector();
                const glm::vec3 target = skyboxCameraPosition + front;
                ubo.model = glm::scale(glm::mat4(1.0f), glm::vec3(model.modelRenderScale() * skyboxScaleMultiplier));
                ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
                ubo.view = glm::lookAt(skyboxCameraPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.proj = glm::perspective(glm::radians(70.0f), aspect, 0.02f, 100.0f);
            } else {
                ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
                ubo.model = glm::rotate(ubo.model, glm::radians(yawDegrees) + autoSpinRadians, glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.model = glm::scale(ubo.model, glm::vec3(model.modelRenderScale()));
                ubo.model = glm::translate(ubo.model, model.modelCenterOffset());
                ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraDistance), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
            }
            ubo.proj[1][1] *= -1.0f;
            ubo.fx = glm::vec4(elapsedSeconds, 0.0f, 0.0f, 0.37f);

            model.updateUBO(imageIndex, ubo);
            if (binaryTextureMode && binaryMatrixTexture != nullptr) {
                binaryMatrixTexture->update(deltaSeconds);
                [[maybe_unused]] const bool textureUpdated = model.updatePrimaryTexture(
                    binaryMatrixTexture->pixels(),
                    binaryMatrixTexture->width(),
                    binaryMatrixTexture->height(),
                    binaryMatrixTexture->pitch());
            }
            model.render(cmd, imageIndex, false);
        }

      private:
        std::string assetRoot;
        mxvk::VKAbstractModel model{};
        bool binaryTextureMode = false;
        std::unique_ptr<MatrixTexture> binaryMatrixTexture{};
        bool mouseDragging = false;
        bool skyboxMode = false;
        bool skyboxLookUpKey = false;
        bool skyboxLookDownKey = false;
        bool skyboxLookLeftKey = false;
        bool skyboxLookRightKey = false;
        bool autoSpinEnabled = true;
        int lastMouseX = 0;
        int lastMouseY = 0;
        float yawDegrees = 0.0f;
        float pitchDegrees = 12.0f;
        float cameraDistance = 4.2f;
        glm::vec3 skyboxCameraPosition{0.0f, 0.0f, 0.0f};
        float skyboxYawDegrees = 180.0f;
        float skyboxPitchDegrees = 0.0f;
        float skyboxScaleMultiplier = 1.6f;
        float mouseSensitivity = 0.35f;
        float autoSpinSpeed = 0.65f;
        float autoSpinRadians = 0.0f;
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();

        void resetSkyboxCamera() {
            skyboxCameraPosition = glm::vec3(0.0f);
            skyboxYawDegrees = 180.0f;
            skyboxPitchDegrees = 0.0f;
        }

        [[nodiscard]] glm::vec3 skyboxForwardVector() const {
            const float yawRadians = glm::radians(skyboxYawDegrees);
            const float pitchRadians = glm::radians(skyboxPitchDegrees);
            return glm::normalize(glm::vec3{
                std::sin(yawRadians) * std::cos(pitchRadians),
                std::sin(pitchRadians),
                -std::cos(yawRadians) * std::cos(pitchRadians)});
        }

        [[nodiscard]] glm::vec3 skyboxRightVector() const {
            const glm::vec3 forward = skyboxForwardVector();
            return glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        }

        void updateSkyboxCamera(float deltaSeconds) {
            const float lookSpeed = 85.0f;
            if (skyboxLookLeftKey) {
                skyboxYawDegrees -= lookSpeed * deltaSeconds;
            }
            if (skyboxLookRightKey) {
                skyboxYawDegrees += lookSpeed * deltaSeconds;
            }
            if (skyboxLookUpKey) {
                skyboxPitchDegrees += lookSpeed * deltaSeconds;
            }
            if (skyboxLookDownKey) {
                skyboxPitchDegrees -= lookSpeed * deltaSeconds;
            }
            skyboxPitchDegrees = std::clamp(skyboxPitchDegrees, -85.0f, 85.0f);
        }
    };

} // namespace example

int main(int argc, char **argv) {
    try {
        const Arguments args = proc_args(argc, argv);
        const bool usingDefaultModel = args.filename.empty();
        std::string filename = args.filename;
        if (args.filename.empty()) {
            filename = args.path + "/data/pyramid.obj";
        }
        example::ModelWindow window(
            filename,
            usingDefaultModel,
            args.path,
            args.resource,
            args.resource_path,
            args.fragmentPath,
            "MXVK Model Example",
            args.width,
            args.height,
            args.fullscreen,
            args.binary);
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
