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
              rng_(std::random_device{}()) {
            assetRoot_ = assetPath.empty() ? std::string(tictactoe_ASSET_DIR) : assetPath;
            const std::string fontPath = assetPath.empty() ? std::string(tictactoe_FONT_PATH) : assetRoot_ + "/data/font.ttf";
            setFont(fontPath, 20);
            titleFont_.reset(fontPath, 30);
            uiFont_.reset(fontPath, 18);
            setClearColor(0.03f, 0.04f, 0.07f, 1.0f);
            background_ = createSprite(assetRoot_ + "/data/bg.png");
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

        static constexpr int gridSize_ = 360;
        static constexpr int cellSize_ = gridSize_ / 3;
        static constexpr int lineThickness_ = 6;
        static constexpr float pi_ = 3.14159265358979323846f;

        std::array<char, 9> board_{};
        std::string assetRoot_ = ".";
        mxvk::Font titleFont_{};
        mxvk::Font uiFont_{};
        mxvk::VK_Sprite *background_ = nullptr;
        mxvk::VK_Sprite *boardPixel_ = nullptr;
        mxvk::VK_Sprite *xPixel_ = nullptr;
        mxvk::VK_Sprite *oPixel_ = nullptr;
        std::mt19937 rng_;
        Outcome outcome_ = Outcome::Running;
        int boardX_ = 0;
        int boardY_ = 0;

        void makePixelSprite() {
            boardPixel_ = makeSolidPixel(235, 240, 255, 255);
            xPixel_ = makeSolidPixel(95, 205, 255, 255);
            oPixel_ = makeSolidPixel(255, 140, 140, 255);
        }

        mxvk::VK_Sprite *makeSolidPixel(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::array<std::uint8_t, 4> pixel{r, g, b, a};
            mxvk::VK_Sprite *sprite = createSprite(1, 1);
            sprite->updateTexture(pixel.data(), 1, 1, 4);
            return sprite;
        }

        void resetGame() {
            board_.fill(' ');
            outcome_ = Outcome::Running;
        }

        void updateBoardLayout() {
            const int windowWidth = static_cast<int>(swapchain_extent.width);
            const int windowHeight = static_cast<int>(swapchain_extent.height);
            boardX_ = std::max(24, (windowWidth - gridSize_) / 2);
            boardY_ = std::max(100, (windowHeight - gridSize_) / 2 + 20);
        }

        void handleClick(float windowMouseX, float windowMouseY) {
            if (outcome_ != Outcome::Running) {
                resetGame();
                return;
            }

            updateBoardLayout();
            const auto [mouseX, mouseY] = mouseToRenderCoordinates(windowMouseX, windowMouseY);
            const int localX = mouseX - boardX_;
            const int localY = mouseY - boardY_;
            if (localX < 0 || localY < 0 || localX >= gridSize_ || localY >= gridSize_) {
                return;
            }

            const int col = localX / cellSize_;
            const int row = localY / cellSize_;
            const int index = row * 3 + col;
            if (board_[static_cast<std::size_t>(index)] != ' ') {
                return;
            }

            board_[static_cast<std::size_t>(index)] = 'X';
            updateOutcome();
            if (outcome_ == Outcome::Running) {
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
            if (board_[4] == ' ') {
                board_[4] = 'O';
                return;
            }

            std::vector<int> openCells;
            for (int i = 0; i < static_cast<int>(board_.size()); ++i) {
                if (board_[static_cast<std::size_t>(i)] == ' ') {
                    openCells.push_back(i);
                }
            }

            if (!openCells.empty()) {
                std::shuffle(openCells.begin(), openCells.end(), rng_);
                board_[static_cast<std::size_t>(openCells.front())] = 'O';
            }
        }

        bool playImmediateMove(char side) {
            for (int i = 0; i < static_cast<int>(board_.size()); ++i) {
                if (board_[static_cast<std::size_t>(i)] != ' ') {
                    continue;
                }

                board_[static_cast<std::size_t>(i)] = side;
                const bool completesLine = winner() == side;
                board_[static_cast<std::size_t>(i)] = ' ';
                if (completesLine) {
                    board_[static_cast<std::size_t>(i)] = 'O';
                    return true;
                }
            }
            return false;
        }

        void updateOutcome() {
            const char winningSide = winner();
            if (winningSide == 'X') {
                outcome_ = Outcome::UserWon;
            } else if (winningSide == 'O') {
                outcome_ = Outcome::ComputerWon;
            } else if (std::ranges::none_of(board_, [](char c) { return c == ' '; })) {
                outcome_ = Outcome::Draw;
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
                const char first = board_[static_cast<std::size_t>(line[0])];
                if (first != ' ' &&
                    first == board_[static_cast<std::size_t>(line[1])] &&
                    first == board_[static_cast<std::size_t>(line[2])]) {
                    return first;
                }
            }
            return ' ';
        }

        void drawBoard() {
            if (boardPixel_ == nullptr) {
                return;
            }

            const int boardEnd = boardX_ + gridSize_;
            for (int i = 1; i <= 2; ++i) {
                const int pos = boardX_ + i * cellSize_ - lineThickness_ / 2;
                boardPixel_->drawSpriteRect(pos, boardY_, lineThickness_, gridSize_);
                boardPixel_->drawSpriteRect(boardX_, boardY_ + i * cellSize_ - lineThickness_ / 2, gridSize_, lineThickness_);
            }

            boardPixel_->drawSpriteRect(boardX_ - lineThickness_, boardY_ - lineThickness_, gridSize_ + lineThickness_ * 2, lineThickness_);
            boardPixel_->drawSpriteRect(boardX_ - lineThickness_, boardY_ + gridSize_, gridSize_ + lineThickness_ * 2, lineThickness_);
            boardPixel_->drawSpriteRect(boardX_ - lineThickness_, boardY_, lineThickness_, gridSize_);
            boardPixel_->drawSpriteRect(boardEnd, boardY_, lineThickness_, gridSize_);
        }

        void drawBackground() {
            if (background_ == nullptr) {
                return;
            }
            background_->drawSpriteRect(0, 0, static_cast<int>(swapchain_extent.width), static_cast<int>(swapchain_extent.height));
        }

        void drawMarks() {
            if (xPixel_ == nullptr || oPixel_ == nullptr) {
                return;
            }

            const int markSize = std::max(24, cellSize_ * 58 / 100);
            const int thickness = std::max(4, cellSize_ / 18);

            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const int index = row * 3 + col;
                    const char mark = board_[static_cast<std::size_t>(index)];
                    if (mark == ' ') {
                        continue;
                    }

                    const int centerX = boardX_ + col * cellSize_ + cellSize_ / 2;
                    const int centerY = boardY_ + row * cellSize_ + cellSize_ / 2;

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
            drawLine(*xPixel_, centerX - half, centerY - half, centerX + half, centerY + half, thickness);
            drawLine(*xPixel_, centerX + half, centerY - half, centerX - half, centerY + half, thickness);
        }

        void drawO(int centerX, int centerY, int size, int thickness) {
            const int radius = size / 2;
            constexpr int segments = 96;
            int previousX = centerX + radius;
            int previousY = centerY;

            for (int i = 1; i <= segments; ++i) {
                const float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi_;
                const int x = centerX + static_cast<int>(std::lround(std::cos(angle) * static_cast<float>(radius)));
                const int y = centerY + static_cast<int>(std::lround(std::sin(angle) * static_cast<float>(radius)));
                drawLine(*oPixel_, previousX, previousY, x, y, thickness);
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
            printText("Tic-Tac-Toe", boardX_, 30, SDL_Color{235, 240, 255, 255}, titleFont_);
            printText(statusText(), boardX_, boardY_ - 135, SDL_Color{190, 210, 255, 255}, uiFont_);
            printText("Click a square. R resets. Esc quits.", boardX_, boardY_ + gridSize_ + 18, SDL_Color{180, 185, 200, 255}, uiFont_);
        }

        std::string statusText() const {
            switch (outcome_) {
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
