#ifndef MUTATRIS_PIECE_HPP
#define MUTATRIS_PIECE_HPP

#include <array>
#include <cstddef>

#include "common.hpp"

namespace mutatris {

    class GameGrid;

    class Piece {
      public:
        explicit Piece(GameGrid *owner);

        void reset();
        void shiftColors();
        void moveLeft();
        void moveRight();
        [[nodiscard]] bool moveDown();
        [[nodiscard]] bool drop();
        void shiftDirection();
        [[nodiscard]] bool setBlock();
        [[nodiscard]] bool checkLocation(int next_x, int next_y) const;

        [[nodiscard]] int getX() const { return x; }
        [[nodiscard]] int getY() const { return y; }
        [[nodiscard]] int getDirection() const { return direction; }
        [[nodiscard]] const Block *at(int index) const {
            return index >= 0 && index < static_cast<int>(blocks.size()) ? &blocks[static_cast<std::size_t>(index)] : nullptr;
        }

      private:
        std::array<Block, 3> blocks{};
        int x = 0;
        int y = 0;
        int direction = 0;
        GameGrid *grid = nullptr;
    };

} // namespace mutatris

#endif
