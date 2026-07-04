#ifndef MUTATRIS_GAME_GRID_HPP
#define MUTATRIS_GAME_GRID_HPP

#include <vector>

#include "common.hpp"
#include "piece.hpp"

namespace mutatris {

    class GameGrid {
      public:
        GameGrid();

        void initGrid(int width, int height);
        [[nodiscard]] Block *at(int x, int y);
        [[nodiscard]] const Block *at(int x, int y) const;
        [[nodiscard]] int width() const { return gridWidth; }
        [[nodiscard]] int height() const { return gridHeight; }
        [[nodiscard]] bool canMoveDown() const;

        Piece gamePiece;

      private:
        int gridWidth = 0;
        int gridHeight = 0;
        std::vector<Block> cells{};
    };

} // namespace mutatris

#endif
