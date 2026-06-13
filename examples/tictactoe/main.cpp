#include "mxvk/argz.hpp"
#include "mxvk/mxvk.hpp"
#include "mxvk/mxvk_exception.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace example {
    class TicTacToeWindow : public mxvk::VK_Window {
      public:
        TicTacToeWindow(const std::string &assetPath, int width, int height, bool fullscreen)
            : mxvk::VK_Window("-[ MXVK Tic-Tac-Toe ]-", width, height, fullscreen, MXVK_VALIDATION),
              rng(std::random_device{}()) {
            assetRoot = assetPath.empty() ? std::string(tictactoe_ASSET_DIR) : assetPath;
            const std::string fontPath = assetPath.empty() ? std::string(tictactoe_FONT_PATH) : assetRoot + "/data/font.ttf";
            setFont(fontPath, 20);
            titleFont.reset(fontPath, 30);
            uiFont.reset(fontPath, 18);
            setClearColor(0.03f, 0.04f, 0.07f, 1.0f);
            background = createSprite(assetRoot + "/data/bg.png");
            makePixelSprite();
            resetGame();
        }

        void event(SDL_Event &e) override {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.key == SDLK_ESCAPE) {
                    exit();
                } else if (e.key.key == SDLK_R) {
                    resetGame();
                }
                return;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                handleClick(e.button.x, e.button.y);
            }
        }

        void proc() override {
            updateBoardLayout();
            drawBackground();
            drawBoard();
            drawMarks();
            drawText();
        }

      private:
        enum class Outcome {
            Running,
            UserWon,
            ComputerWon,
            Draw
        };

        static constexpr int gridSize = 360;
        static constexpr int cellSize = gridSize / 3;
        static constexpr int lineThickness = 6;
        static constexpr float pi = 3.14159265358979323846f;

        std::array<char, 9> board{};
        std::string assetRoot = ".";
        mxvk::Font titleFont{};
        mxvk::Font uiFont{};
        mxvk::VK_Sprite *background = nullptr;
        mxvk::VK_Sprite *boardPixel = nullptr;
        mxvk::VK_Sprite *xPixel = nullptr;
        mxvk::VK_Sprite *oPixel = nullptr;
        std::mt19937 rng;
        Outcome outcome = Outcome::Running;
        int boardX = 0;
        int boardY = 0;

        void makePixelSprite() {
            boardPixel = makeSolidPixel(235, 240, 255, 255);
            xPixel = makeSolidPixel(95, 205, 255, 255);
            oPixel = makeSolidPixel(255, 140, 140, 255);
        }

        mxvk::VK_Sprite *makeSolidPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::array<std::uint8_t, 4> pixel{r, g, b, a};
            mxvk::VK_Sprite *sprite = createSprite(1, 1);
            sprite->updateTexture(pixel.data(), 1, 1, 4);
            return sprite;
        }

        void resetGame() {
            board.fill(' ');
            outcome = Outcome::Running;
        }

        void updateBoardLayout() {
            const int windowWidth = static_cast<int>(swapchain_extent.width);
            const int windowHeight = static_cast<int>(swapchain_extent.height);
            boardX = std::max(24, (windowWidth - gridSize) / 2);
            boardY = std::max(100, (windowHeight - gridSize) / 2 + 20);
        }

        void handleClick(float windowMouseX, float windowMouseY) {
            if (outcome != Outcome::Running) {
                resetGame();
                return;
            }

            updateBoardLayout();
            const auto [mouseX, mouseY] = mouseToRenderCoordinates(windowMouseX, windowMouseY);
            const int localX = mouseX - boardX;
            const int localY = mouseY - boardY;
            if (localX < 0 || localY < 0 || localX >= gridSize || localY >= gridSize) {
                return;
            }

            const int col = localX / cellSize;
            const int row = localY / cellSize;
            const int index = row * 3 + col;
            if (board[static_cast<std::size_t>(index)] != ' ') {
                return;
            }

            board[static_cast<std::size_t>(index)] = 'X';
            updateOutcome();
            if (outcome == Outcome::Running) {
                makeComputerMove();
                updateOutcome();
            }
        }

        std::pair<int, int> mouseToRenderCoordinates(float mouseX, float mouseY) const {
            int logicalWidth = 0;
            int logicalHeight = 0;
            int pixelWidth = 0;
            int pixelHeight = 0;
            SDL_GetWindowSize(getSDLWindow(), &logicalWidth, &logicalHeight);
            SDL_GetWindowSizeInPixels(getSDLWindow(), &pixelWidth, &pixelHeight);

            if (logicalWidth <= 0 || logicalHeight <= 0 || pixelWidth <= 0 || pixelHeight <= 0 ||
                swapchain_extent.width == 0 || swapchain_extent.height == 0) {
                return {static_cast<int>(std::lround(mouseX)), static_cast<int>(std::lround(mouseY))};
            }

            const bool mouseLooksLogical =
                mouseX >= 0.0f && mouseY >= 0.0f &&
                mouseX <= static_cast<float>(logicalWidth) + 0.5f &&
                mouseY <= static_cast<float>(logicalHeight) + 0.5f;

            if (mouseLooksLogical && (pixelWidth != logicalWidth || pixelHeight != logicalHeight)) {
                mouseX *= static_cast<float>(swapchain_extent.width) / static_cast<float>(logicalWidth);
                mouseY *= static_cast<float>(swapchain_extent.height) / static_cast<float>(logicalHeight);
            }

            return {static_cast<int>(std::lround(mouseX)), static_cast<int>(std::lround(mouseY))};
        }

        void makeComputerMove() {
            if (playImmediateMove('O')) {
                return;
            }
            if (playImmediateMove('X')) {
                return;
            }
            if (board[4] == ' ') {
                board[4] = 'O';
                return;
            }

            std::vector<int> openCells;
            for (int i = 0; i < static_cast<int>(board.size()); ++i) {
                if (board[static_cast<std::size_t>(i)] == ' ') {
                    openCells.push_back(i);
                }
            }

            if (!openCells.empty()) {
                std::shuffle(openCells.begin(), openCells.end(), rng);
                board[static_cast<std::size_t>(openCells.front())] = 'O';
            }
        }

        bool playImmediateMove(char side) {
            for (int i = 0; i < static_cast<int>(board.size()); ++i) {
                if (board[static_cast<std::size_t>(i)] != ' ') {
                    continue;
                }

                board[static_cast<std::size_t>(i)] = side;
                const bool completesLine = winner() == side;
                board[static_cast<std::size_t>(i)] = ' ';
                if (completesLine) {
                    board[static_cast<std::size_t>(i)] = 'O';
                    return true;
                }
            }
            return false;
        }

        void updateOutcome() {
            const char winningSide = winner();
            if (winningSide == 'X') {
                outcome = Outcome::UserWon;
            } else if (winningSide == 'O') {
                outcome = Outcome::ComputerWon;
            } else if (std::ranges::none_of(board, [](char c) { return c == ' '; })) {
                outcome = Outcome::Draw;
            }
        }

        char winner() const {
            static constexpr std::array<std::array<int, 3>, 8> lines{{
                {{0, 1, 2}},
                {{3, 4, 5}},
                {{6, 7, 8}},
                {{0, 3, 6}},
                {{1, 4, 7}},
                {{2, 5, 8}},
                {{0, 4, 8}},
                {{2, 4, 6}},
            }};

            for (const auto &line : lines) {
                const char first = board[static_cast<std::size_t>(line[0])];
                if (first != ' ' &&
                    first == board[static_cast<std::size_t>(line[1])] &&
                    first == board[static_cast<std::size_t>(line[2])]) {
                    return first;
                }
            }
            return ' ';
        }

        void drawBoard() {
            if (boardPixel == nullptr) {
                return;
            }

            const int boardEnd = boardX + gridSize;
            for (int i = 1; i <= 2; ++i) {
                const int pos = boardX + i * cellSize - lineThickness / 2;
                boardPixel->drawSpriteRect(pos, boardY, lineThickness, gridSize);
                boardPixel->drawSpriteRect(boardX, boardY + i * cellSize - lineThickness / 2, gridSize, lineThickness);
            }

            boardPixel->drawSpriteRect(boardX - lineThickness, boardY - lineThickness, gridSize + lineThickness * 2, lineThickness);
            boardPixel->drawSpriteRect(boardX - lineThickness, boardY + gridSize, gridSize + lineThickness * 2, lineThickness);
            boardPixel->drawSpriteRect(boardX - lineThickness, boardY, lineThickness, gridSize);
            boardPixel->drawSpriteRect(boardEnd, boardY, lineThickness, gridSize);
        }

        void drawBackground() {
            if (background == nullptr) {
                return;
            }
            background->drawSpriteRect(0, 0, static_cast<int>(swapchain_extent.width), static_cast<int>(swapchain_extent.height));
        }

        void drawMarks() {
            if (xPixel == nullptr || oPixel == nullptr) {
                return;
            }

            const int markSize = std::max(24, cellSize * 58 / 100);
            const int thickness = std::max(4, cellSize / 18);

            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const int index = row * 3 + col;
                    const char mark = board[static_cast<std::size_t>(index)];
                    if (mark == ' ') {
                        continue;
                    }

                    const int centerX = boardX + col * cellSize + cellSize / 2;
                    const int centerY = boardY + row * cellSize + cellSize / 2;

                    if (mark == 'X') {
                        drawX(centerX, centerY, markSize, thickness);
                    } else {
                        drawO(centerX, centerY, markSize, thickness);
                    }
                }
            }
        }

        void drawX(int centerX, int centerY, int size, int thickness) {
            const int half = size / 2;
            drawLine(*xPixel, centerX - half, centerY - half, centerX + half, centerY + half, thickness);
            drawLine(*xPixel, centerX + half, centerY - half, centerX - half, centerY + half, thickness);
        }

        void drawO(int centerX, int centerY, int size, int thickness) {
            const int radius = size / 2;
            constexpr int segments = 96;
            int previousX = centerX + radius;
            int previousY = centerY;

            for (int i = 1; i <= segments; ++i) {
                const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
                const int x = centerX + static_cast<int>(std::lround(std::cos(angle) * static_cast<float>(radius)));
                const int y = centerY + static_cast<int>(std::lround(std::sin(angle) * static_cast<float>(radius)));
                drawLine(*oPixel, previousX, previousY, x, y, thickness);
                previousX = x;
                previousY = y;
            }
        }

        void drawLine(mxvk::VK_Sprite &sprite, int x0, int y0, int x1, int y1, int thickness) {
            const int dx = x1 - x0;
            const int dy = y1 - y0;
            const int steps = std::max(std::abs(dx), std::abs(dy));
            if (steps == 0) {
                drawDot(sprite, x0, y0, thickness);
                return;
            }

            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const int x = static_cast<int>(std::lround(static_cast<float>(x0) + static_cast<float>(dx) * t));
                const int y = static_cast<int>(std::lround(static_cast<float>(y0) + static_cast<float>(dy) * t));
                drawDot(sprite, x, y, thickness);
            }
        }

        void drawDot(mxvk::VK_Sprite &sprite, int x, int y, int size) {
            sprite.drawSpriteRect(x - size / 2, y - size / 2, size, size);
        }

        void drawText() {
            printText("Tic-Tac-Toe", boardX, 30, SDL_Color{235, 240, 255, 255}, titleFont);
            printText(statusText(), boardX, boardY - 135, SDL_Color{190, 210, 255, 255}, uiFont);
            printText("Click a square. R resets. Esc quits.", boardX, boardY + gridSize + 18, SDL_Color{180, 185, 200, 255}, uiFont);
        }

        std::string statusText() const {
            switch (outcome) {
            case Outcome::Running:
                return "You are X. The computer is O.";
            case Outcome::UserWon:
                return "You won. Click anywhere to play again.";
            case Outcome::ComputerWon:
                return "Computer won. Click anywhere to play again.";
            case Outcome::Draw:
                return "Draw. Click anywhere to play again.";
            }
            return {};
        }
    };
} // namespace example

int main(int argc, char **argv) {
    try {
        Arguments args = proc_args(argc, argv);
        example::TicTacToeWindow window(args.path, args.width, args.height, args.fullscreen);
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
