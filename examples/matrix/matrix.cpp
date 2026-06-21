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
        std::string symbol;
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
        const Uint8 r = static_cast<Uint8>(std::lerp(0.0f, 180.0f, std::pow(t, 2.8f)));
        const Uint8 g = static_cast<Uint8>(std::lerp(36.0f, 255.0f, std::pow(t, 0.72f)));
        const Uint8 b = static_cast<Uint8>(std::lerp(8.0f, 205.0f, std::pow(t, 3.2f)));
        const Uint8 a = static_cast<Uint8>(std::lerp(120.0f, 255.0f, t));
        return SDL_Color{r, g, b, a};
    }

    SDL_Color headColor() {
        return SDL_Color{230, 255, 235, 255};
    }
} // namespace

namespace example {
    class MatrixWindow : public mxvk::VK_Window {
      public:
        MatrixWindow(const std::string &path,
                     const std::string &title,
                     const int width,
                     const int height,
                     const bool fullscreen)
            : mxvk::VK_Window(title, width, height, fullscreen, MXVK_VALIDATION),
              assetRoot(path.empty() ? std::string(matrix_ASSET_DIR) : path),
              rng(std::random_device{}()) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (!TTF_Init()) {
                throw mxvk::Exception("Failed to initialize SDL_ttf: " + std::string(SDL_GetError()));
            }
            font.reset(TTF_OpenFont((assetRoot + "/data/keifont.ttf").c_str(), fontSize));
            if (!font) {
                throw mxvk::Exception("Failed to load matrix font: " + std::string(SDL_GetError()));
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
            static const std::vector<std::string> symbols = {
                "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                "A", "B", "C", "D", "E", "F", "G", "H", "K", "M",
                "N", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
                "@", "#", "$", "%", "&", "*", "+", "-", ":", ";",
                "\xEF\xBD\xA6", "\xEF\xBD\xA7", "\xEF\xBD\xA8", "\xEF\xBD\xA9",
                "\xEF\xBD\xAA", "\xEF\xBD\xAB", "\xEF\xBD\xAC", "\xEF\xBD\xAD",
                "\xEF\xBD\xAE", "\xEF\xBD\xAF", "\xEF\xBD\xB1", "\xEF\xBD\xB2",
                "\xEF\xBD\xB3", "\xEF\xBD\xB4", "\xEF\xBD\xB5", "\xEF\xBD\xB6",
                "\xEF\xBD\xB7", "\xEF\xBD\xB8", "\xEF\xBD\xB9", "\xEF\xBD\xBA",
                "\xEF\xBD\xBB", "\xEF\xBD\xBC", "\xEF\xBD\xBD", "\xEF\xBD\xBE",
                "\xEF\xBD\xBF", "\xEF\xBE\x80", "\xEF\xBE\x81", "\xEF\xBE\x82",
                "\xEF\xBE\x83", "\xEF\xBE\x84", "\xEF\xBE\x85", "\xEF\xBE\x86",
                "\xEF\xBE\x87", "\xEF\xBE\x88", "\xEF\xBE\x89", "\xEF\xBE\x8A",
                "\xEF\xBE\x8B", "\xEF\xBE\x8C", "\xEF\xBE\x8D", "\xEF\xBE\x8E",
                "\xEF\xBE\x8F", "\xEF\xBE\x90", "\xEF\xBE\x91", "\xEF\xBE\x92",
                "\xEF\xBE\x93", "\xEF\xBE\x94", "\xEF\xBE\x95", "\xEF\xBE\x96",
                "\xEF\xBE\x97", "\xEF\xBE\x98", "\xEF\xBE\x99", "\xEF\xBE\x9A",
                "\xEF\xBE\x9B", "\xEF\xBE\x9C", "\xEF\xBE\x9D"};

            glyphs.reserve(symbols.size());
            for (const std::string &symbol : symbols) {
                Glyph glyph;
                glyph.symbol = symbol;
                glyph.levels.reserve(glyphLevels + 1);
                for (int level = 0; level <= glyphLevels; ++level) {
                    SDL_Surface *rendered = TTF_RenderText_Blended(font.get(), symbol.c_str(), 0, matrixColor(level));
                    if (rendered == nullptr) {
                        continue;
                    }
                    SurfacePtr converted(SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_RGBA32));
                    SDL_DestroySurface(rendered);
                    if (converted != nullptr) {
                        SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
                        glyph.levels.emplace_back(std::move(converted));
                    }
                }
                SDL_Surface *renderedHead = TTF_RenderText_Blended(font.get(), symbol.c_str(), 0, headColor());
                if (renderedHead != nullptr) {
                    SurfacePtr converted(SDL_ConvertSurface(renderedHead, SDL_PIXELFORMAT_RGBA32));
                    SDL_DestroySurface(renderedHead);
                    if (converted != nullptr) {
                        SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_BLEND);
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

            int textW = 0;
            int textH = 0;
            TTF_GetStringSize(font.get(), "\xEF\xBE\x8F", 0, &textW, &textH);
            cellW = std::max(14, textW + 2);
            cellH = std::max(22, textH - 1);
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
                    p[0] = static_cast<Uint8>(static_cast<float>(p[0]) * fade * 0.80f);
                    p[1] = static_cast<Uint8>(static_cast<float>(p[1]) * fade);
                    p[2] = static_cast<Uint8>(static_cast<float>(p[2]) * fade * 0.68f);
                    p[3] = 255;
                }
            }
            SDL_UnlockSurface(canvas.get());
        }

        void drawStream(int column, const Stream &stream) {
            const int headRow = static_cast<int>(std::floor(stream.head));
            const int x = column * cellW + ((column % 5 == 0) ? -1 : 0);
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

                SDL_Rect dst{x + (cellW - glyph.levels[level]->w) / 2, row * cellH, glyph.levels[level]->w, glyph.levels[level]->h};
                SDL_BlitSurface(glyph.levels[level].get(), nullptr, canvas.get(), &dst);
            }
        }

        static constexpr int fontSize = 24;
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
        int cellW = 18;
        int cellH = 24;
        int frameCounter = 0;
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::MatrixWindow window(args.path, "-[ MXVK Matrix Digital Rain ]-", args.width, args.height, args.fullscreen);
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
