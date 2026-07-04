#include "game_grid.hpp"

#include <cstddef>

namespace mutatris {

    GameGrid::GameGrid()
        : gamePiece(this) {
    }

    void GameGrid::initGrid(int width, int height) {
        gridWidth = width;
        gridHeight = height;
        cells.assign(static_cast<std::size_t>(width * height), {});
        gamePiece.reset();
    }

    Block *GameGrid::at(int x, int y) {
        if (x < 0 || x >= gridWidth || y < 0 || y >= gridHeight) {
            return nullptr;
        }
        return &cells[static_cast<std::size_t>(y * gridWidth + x)];
    }

    const Block *GameGrid::at(int x, int y) const {
        if (x < 0 || x >= gridWidth || y < 0 || y >= gridHeight) {
            return nullptr;
        }
        return &cells[static_cast<std::size_t>(y * gridWidth + x)];
    }

    bool GameGrid::canMoveDown() const {
        return gamePiece.getDirection() == 3 || gamePiece.checkLocation(gamePiece.getX(), gamePiece.getY()) || gamePiece.getY() != 0;
    }

} // namespace mutatris
