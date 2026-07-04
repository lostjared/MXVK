#ifndef MUTATRIS_PUZZLE_GAME_HPP
#define MUTATRIS_PUZZLE_GAME_HPP

#include <array>
#include <functional>

#include "game_grid.hpp"

namespace mutatris {

    class PuzzleGame {
      public:
        explicit PuzzleGame(int difficulty, std::function<void()> lineSoundCallback);

        void newGame(int difficulty);
        void procBlocks();

        std::array<GameGrid, 4> grid{};
        int score = 0;
        unsigned int timeout = 1200;
        int clears = 0;
        int level = 0;

      private:
        struct CellRef {
            int gridIndex = 0;
            int x = 0;
            int y = 0;
        };

        struct CellBounds {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        [[nodiscard]] Block *blockAt(const CellRef &cell);
        [[nodiscard]] const Block *blockAt(const CellRef &cell) const;
        [[nodiscard]] static CellBounds cellBounds(const CellRef &cell);
        [[nodiscard]] bool sameCell(const CellRef &a, const CellRef &b) const;
        [[nodiscard]] bool findNextVisualMatch(const CellRef &current, int color, int dx, int dy, CellRef &next) const;
        [[nodiscard]] bool clearVisualRun();
        bool clearRun(GameGrid &focusGrid, int x, int y, int dx, int dy);
        void clearTopBottomSeams();
        bool clearSeam(std::array<Block *, 4> blocks);
        void moveDownBlocks();
        static void markClearing(Block &block);
        void recordClear();
        void playClearSound();

        std::function<void()> playLineSound;
    };

} // namespace mutatris

#endif
