#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

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

    std::vector<Uint32> binarySymbolSet() {
        return {U'0', U'1'};
    }
} // namespace

namespace example {
    class MatrixWindow : public mxvk::VK_Window {
      public:
        MatrixWindow(const std::string &path,
                     const std::string &title,
                     const int width,
                     const int height,
                     const bool fullscreen,
                     const bool binary)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(matrix_ASSET_DIR) : path),
              binaryGlyphMode(binary),
              rng(std::random_device{}()) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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
            rebuildForExtent();
            lastFrame = Clock::now();
        }

        ~MatrixWindow() override {
            glyphs.clear();
            font.reset();
            TTF_Quit();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                } else if (e.key.key == SDLK_SPACE) {
                    randomizeStreams();
                    if (canvas != nullptr) {
                        SDL_FillSurfaceRect(canvas.get(), nullptr, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));
                    }
                }
            }
        }

        void proc() override {
            rebuildForExtent();
            const auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - lastFrame).count();
            lastFrame = now;
            dt = std::clamp(dt, 0.0f, 1.0f / 15.0f);

            if (rainSprite == nullptr || canvas == nullptr || glyphs.empty()) {
                return;
            }

            updateRain(dt);
            rainSprite->updateTexture(canvas->pixels, canvas->w, canvas->h, canvas->pitch);
            rainSprite->drawSpriteRect(0, 0, canvas->w, canvas->h);
        }

      private:
        void loadGlyphs() {
            const std::vector<Uint32> symbols = binaryGlyphMode ? binarySymbolSet() : fullSymbolSet();
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
                throw mxvk::Exception("Matrix demo could not render any glyphs from keifont.ttf");
            }

            cellW = std::max(16, maxGlyphW + 3);
            cellH = std::max(24, maxGlyphH + 2);
        }

        void rebuildForExtent() {
            const VkExtent2D extent = getSwapchainExtent();
            const int width = static_cast<int>(extent.width);
            const int height = static_cast<int>(extent.height);
            if (width <= 0 || height <= 0 || (canvas != nullptr && canvas->w == width && canvas->h == height)) {
                return;
            }

            canvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
            if (canvas == nullptr) {
                throw mxvk::Exception("Failed to create matrix canvas: " + std::string(SDL_GetError()));
            }
            SDL_FillSurfaceRect(canvas.get(), nullptr, SDL_MapSurfaceRGBA(canvas.get(), 0, 0, 0, 255));

            if (rainSprite == nullptr) {
                rainSprite = createSprite(canvas.get());
            } else {
                rainSprite->updateTexture(canvas.get());
            }

            columns = std::max(1, (width + cellW - 1) / cellW);
            rows = std::max(1, (height + cellH - 1) / cellH);
            streams.assign(columns, {});
            randomizeStreams();
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

        static constexpr int fontSize = 28;
        static constexpr int glyphLevels = 7;

        std::string assetRoot;
        FontPtr font;
        SurfacePtr canvas;
        mxvk::VK_Sprite *rainSprite = nullptr;
        bool binaryGlyphMode = false;
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
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::MatrixWindow window(
            args.path, "-[ MXVK Matrix Digital Rain ]-", args.width, args.height, args.fullscreen, args.binary);
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
