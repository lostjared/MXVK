#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_controller.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_png.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace knight {
    class Tour {
      public:
        Tour();

        void drawBoard(mxvk::VK_Sprite &whiteCell, mxvk::VK_Sprite &redCell, mxvk::VK_Sprite &visitedCell, float scaleX, float scaleY) const;
        void drawKnight(mxvk::VK_Sprite &texture, float scaleX, float scaleY) const;
        void nextMove();
        void resetTour();
        void resetTour(int startRow, int startCol);
        void resetTourFromPoint(float x, float y);

        [[nodiscard]] int getMoves() const { return moves; }
        [[nodiscard]] bool isTourOver() const { return tourOver; }

      private:
        struct Position {
            int row;
            int col;

            constexpr Position(int newRow = 0, int newCol = 0) : row(newRow), col(newCol) {}
        };

        void initializeBoard();
        void clearBoard();
        [[nodiscard]] bool isValidMove(const Position &position) const;
        [[nodiscard]] int getDegree(const Position &position) const;
        bool solveKnightsTour(Position position, int moveCount);

        static constexpr int BOARD_SIZE = 8;
        static constexpr int TOTAL_MOVES = BOARD_SIZE * BOARD_SIZE + 1;
        static constexpr int START_X = 100;
        static constexpr int START_Y = 30;
        static constexpr int CELL_SIZE = 55;
        static constexpr int CELL_DRAW_SIZE = 50;
        static constexpr int KNIGHT_SIZE = 35;

        std::vector<std::vector<int>> board;
        std::vector<Position> moveSequence;
        Position knightPos;
        int moves;
        bool tourOver;

        static constexpr std::array<int, 8> horizontal = {2, 1, -1, -2, -2, -1, 1, 2};
        static constexpr std::array<int, 8> vertical = {-1, -2, -2, -1, 1, 2, 2, 1};
    };

    class KnightsTourWindow : public mxvk::VK_Window {
      public:
        KnightsTourWindow(const std::string &path, int width, int height, bool fullscreen, bool enableVsync)
            : mxvk::VK_Window("Knights Tour", width, height, fullscreen, MXVK_VALIDATION, enableVsync),
              assetRoot((path.empty() || path == ".") ? std::string(KNIGHT_ASSET_DIR) : path),
              fontPath(assetRoot + "/data/font.ttf"),
              introStarted(SDL_GetTicks()) {
            setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            setFont(fontPath, TEXT_SIZE);
            loadWindowIcon();

            intro = createSprite(assetRoot + "/data/logo.png", "", assetRoot + "/data/fade.frag.spv");
            whiteCell = makeSolidSprite(255, 255, 255, 255);
            redCell = makeSolidSprite(255, 0, 0, 255);
            visitedCell = makeSolidSprite(0, 0, 0, 255);
            knightSprite = createSprite(assetRoot + "/data/knight.png", "", assetRoot + "/data/color_key.frag.spv");

            if (mxvk::Joystick::joysticks() > 0) {
                if (joystick.open(0)) {
                    std::cout << std::format("Joystick opened: {}\n", joystick.name());
                } else {
                    std::cout << "Could not open joystick..\n";
                }
            }
        }

        void event(SDL_Event &event) override {
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    exit();
                    return;
                }
                if (event.key.key == SDLK_S && !event.key.repeat) {
                    try {
                        saveSnapshot("screenshot.png");
                        std::cout << "mx: Screenshot captured..\n";
                    } catch (const mxvk::Exception &exception) {
                        std::cerr << std::format("mxvk: screenshot failed: {}\n", exception.text());
                    }
                    return;
                }
                if (screen != Screen::Tour) {
                    return;
                }
                if (event.key.key == SDLK_SPACE) {
                    tour.nextMove();
                } else if (event.key.key == SDLK_RETURN && !event.key.repeat) {
                    tour.resetTour();
                }
                return;
            }

            if (screen != Screen::Tour) {
                return;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    resetTourFromMousePosition(event.button.x, event.button.y);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    tour.resetTour();
                }
            } else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                if (event.jbutton.button == 1) {
                    tour.nextMove();
                } else if (event.jbutton.button == 2) {
                    tour.resetTour();
                }
            }
        }

        void proc() override {
            const float scaleX = swapchain_extent.width > 0U ? static_cast<float>(swapchain_extent.width) / DESIGN_WIDTH : 1.0f;
            const float scaleY = swapchain_extent.height > 0U ? static_cast<float>(swapchain_extent.height) / DESIGN_HEIGHT : 1.0f;

            if (screen == Screen::Intro) {
                drawIntro();
                return;
            }

            updateFont(scaleY);
            tour.drawBoard(*whiteCell, *redCell, *visitedCell, scaleX, scaleY);
            tour.drawKnight(*knightSprite, scaleX, scaleY);

            if (!tour.isTourOver()) {
                printScaledText("Knights Tour - Space to Move, Click a Square to Restart", TEXT_OFFSET_X, TEXT_OFFSET_Y, scaleX, scaleY);
                printMoveCount(scaleX, scaleY);
            } else {
                printScaledText("-[ Tour Complete ]- Press Return to Reset", TEXT_OFFSET_X, TEXT_OFFSET_Y, scaleX, scaleY);
            }
        }

      private:
        enum class Screen {
            Intro,
            Tour
        };

        static constexpr float DESIGN_WIDTH = 640.0f;
        static constexpr float DESIGN_HEIGHT = 480.0f;
        static constexpr int TEXT_OFFSET_X = 15;
        static constexpr int TEXT_OFFSET_Y = 5;
        static constexpr int TEXT_SIZE = 14;
        static constexpr Uint64 INTRO_STEP_MS = 15;
        static constexpr int INTRO_ALPHA_STEP = 3;

        std::string assetRoot;
        std::string fontPath;
        mxvk::VK_Sprite *intro = nullptr;
        mxvk::VK_Sprite *knightSprite = nullptr;
        mxvk::VK_Sprite *whiteCell = nullptr;
        mxvk::VK_Sprite *redCell = nullptr;
        mxvk::VK_Sprite *visitedCell = nullptr;
        mxvk::Joystick joystick;
        Tour tour;
        Screen screen = Screen::Intro;
        Uint64 introStarted = 0;
        int currentFontSize = TEXT_SIZE;

        mxvk::VK_Sprite *makeSolidSprite(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
            const std::array<std::uint8_t, 4> pixel = {red, green, blue, alpha};
            mxvk::VK_Sprite *sprite = createSprite(1, 1);
            sprite->updateTexture(pixel.data(), 1, 1, 4);
            return sprite;
        }

        void loadWindowIcon() {
            const char *videoDriver = SDL_GetCurrentVideoDriver();
            if (videoDriver != nullptr && std::string(videoDriver) == "wayland") {
                return;
            }

            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> icon(
                mxvk::LoadPNG((assetRoot + "/data/knight.png").c_str()), SDL_DestroySurface);
            if (icon != nullptr && !SDL_SetWindowIcon(getSDLWindow(), icon.get())) {
                std::cerr << std::format("knight: could not set window icon: {}\n", SDL_GetError());
            }
        }

        void drawIntro() {
            const Uint64 elapsed = SDL_GetTicks() - introStarted;
            const int fadeSteps = static_cast<int>(elapsed / INTRO_STEP_MS);
            const int alpha = std::max(0, 255 - fadeSteps * INTRO_ALPHA_STEP);
            if (alpha == 0) {
                screen = Screen::Tour;
                return;
            }

            intro->setShaderParams(static_cast<float>(alpha) / 255.0f);
            intro->drawSpriteRect(0, 0, static_cast<int>(swapchain_extent.width), static_cast<int>(swapchain_extent.height));
        }

        void updateFont(float scaleY) {
            const int desiredSize = std::max(1, static_cast<int>(std::lround(TEXT_SIZE * scaleY)));
            if (desiredSize == currentFontSize) {
                return;
            }
            setFont(fontPath, desiredSize);
            currentFontSize = desiredSize;
        }

        void printScaledText(const std::string &text, int x, int y, float scaleX, float scaleY) {
            printText(text,
                      static_cast<int>(std::lround(x * scaleX)),
                      static_cast<int>(std::lround(y * scaleY)),
                      SDL_Color{255, 255, 255, 255});
        }

        void printMoveCount(float scaleX, float scaleY) {
            const std::string text = std::format("Moves: {}", tour.getMoves());
            int textWidth = 0;
            int textHeight = 0;
            const int rightMargin = static_cast<int>(std::lround(TEXT_OFFSET_X * scaleX));
            int x = static_cast<int>(std::lround(400.0f * scaleX));
            if (getTextDimensions(text, textWidth, textHeight)) {
                x = std::max(0, static_cast<int>(swapchain_extent.width) - textWidth - rightMargin);
            }
            printText(text,
                      x,
                      static_cast<int>(std::lround(TEXT_OFFSET_Y * scaleY)),
                      SDL_Color{255, 255, 255, 255});
        }

        void resetTourFromMousePosition(float mouseX, float mouseY) {
            int windowWidth = 0;
            int windowHeight = 0;
            SDL_GetWindowSize(getSDLWindow(), &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                return;
            }

            const float designX = mouseX * DESIGN_WIDTH / static_cast<float>(windowWidth);
            const float designY = mouseY * DESIGN_HEIGHT / static_cast<float>(windowHeight);
            tour.resetTourFromPoint(designX, designY);
        }
    };

    Tour::Tour() : moves(1), tourOver(false) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        initializeBoard();
        resetTour();
    }

    void Tour::initializeBoard() {
        board.resize(BOARD_SIZE, std::vector<int>(BOARD_SIZE, 0));
    }

    void Tour::clearBoard() {
        for (auto &row : board) {
            std::fill(row.begin(), row.end(), 0);
        }
    }

    bool Tour::isValidMove(const Position &position) const {
        return position.row >= 0 && position.row < BOARD_SIZE &&
               position.col >= 0 && position.col < BOARD_SIZE &&
               board[position.row][position.col] == 0;
    }

    int Tour::getDegree(const Position &position) const {
        int count = 0;
        for (int index = 0; index < 8; ++index) {
            const int newRow = position.row + vertical[index];
            const int newCol = position.col + horizontal[index];
            if (isValidMove(Position(newRow, newCol))) {
                ++count;
            }
        }
        return count;
    }

    bool Tour::solveKnightsTour(Position position, int moveCount) {
        if (moveCount == TOTAL_MOVES) {
            return true;
        }

        std::vector<std::pair<int, Position>> nextMoves;
        for (int index = 0; index < 8; ++index) {
            Position nextPosition(position.row + vertical[index], position.col + horizontal[index]);
            if (isValidMove(nextPosition)) {
                nextMoves.emplace_back(getDegree(nextPosition), nextPosition);
            }
        }

        std::sort(nextMoves.begin(), nextMoves.end(), [](const auto &left, const auto &right) {
            return left.first < right.first;
        });

        for (const auto &[degree, nextPosition] : nextMoves) {
            [[maybe_unused]] const int moveDegree = degree;
            board[nextPosition.row][nextPosition.col] = moveCount;
            moveSequence.push_back(nextPosition);

            if (solveKnightsTour(nextPosition, moveCount + 1)) {
                return true;
            }

            board[nextPosition.row][nextPosition.col] = 0;
            moveSequence.pop_back();
        }
        return false;
    }

    void Tour::resetTour() {
        resetTour(std::rand() % BOARD_SIZE, std::rand() % BOARD_SIZE);
    }

    void Tour::resetTour(int startRow, int startCol) {
        if (startRow < 0 || startRow >= BOARD_SIZE || startCol < 0 || startCol >= BOARD_SIZE) {
            return;
        }

        clearBoard();
        knightPos = Position(startRow, startCol);
        board[knightPos.row][knightPos.col] = 1;
        moveSequence.clear();
        moveSequence.push_back(knightPos);
        solveKnightsTour(knightPos, 2);
        moves = 1;
        tourOver = false;
    }

    void Tour::resetTourFromPoint(float x, float y) {
        const int localX = static_cast<int>(std::floor(x)) - START_X;
        const int localY = static_cast<int>(std::floor(y)) - START_Y;
        if (localX < 0 || localY < 0) {
            return;
        }

        const int col = localX / CELL_SIZE;
        const int row = localY / CELL_SIZE;
        if (row >= BOARD_SIZE || col >= BOARD_SIZE ||
            localX % CELL_SIZE >= CELL_DRAW_SIZE || localY % CELL_SIZE >= CELL_DRAW_SIZE) {
            return;
        }

        resetTour(row, col);
    }

    void Tour::nextMove() {
        if (tourOver || static_cast<std::size_t>(moves) >= moveSequence.size()) {
            return;
        }

        const Position nextPosition = moveSequence[static_cast<std::size_t>(moves)];
        board[knightPos.row][knightPos.col] = -1;
        knightPos = nextPosition;
        ++moves;
        board[knightPos.row][knightPos.col] = moves;
        tourOver = static_cast<std::size_t>(moves) == moveSequence.size();
    }

    void Tour::drawBoard(mxvk::VK_Sprite &whiteCell, mxvk::VK_Sprite &redCell, mxvk::VK_Sprite &visitedCell, float scaleX, float scaleY) const {
        for (int row = 0; row < BOARD_SIZE; ++row) {
            for (int col = 0; col < BOARD_SIZE; ++col) {
                mxvk::VK_Sprite *cell = nullptr;
                if (board[row][col] == -1) {
                    cell = &visitedCell;
                } else if ((row + col) % 2 == 0) {
                    cell = &whiteCell;
                } else {
                    cell = &redCell;
                }

                cell->drawSpriteRect(
                    static_cast<int>(std::lround((START_X + col * CELL_SIZE) * scaleX)),
                    static_cast<int>(std::lround((START_Y + row * CELL_SIZE) * scaleY)),
                    static_cast<int>(std::lround(CELL_DRAW_SIZE * scaleX)),
                    static_cast<int>(std::lround(CELL_DRAW_SIZE * scaleY)));
            }
        }
    }

    void Tour::drawKnight(mxvk::VK_Sprite &texture, float scaleX, float scaleY) const {
        texture.drawSpriteRect(
            static_cast<int>(std::lround((START_X + knightPos.col * CELL_SIZE + 5) * scaleX)),
            static_cast<int>(std::lround((START_Y + knightPos.row * CELL_SIZE + 5) * scaleY)),
            static_cast<int>(std::lround(KNIGHT_SIZE * scaleX)),
            static_cast<int>(std::lround(KNIGHT_SIZE * scaleY)));
    }
} // namespace knight

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        if (!args.resolutionSpecified) {
            args.width = 960;
            args.height = 720;
        }
        knight::KnightsTourWindow window(args.path, args.width, args.height, args.fullscreen, args.enable_vsync);
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
