#include "puzzle_game.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <utility>
#include <vector>

namespace mutatris {

    PuzzleGame::PuzzleGame(int difficulty, std::function<void()> lineSoundCallback)
        : playLineSound(std::move(lineSoundCallback)) {
        newGame(difficulty);
    }

    void PuzzleGame::newGame(int difficulty) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        score = 0;
        clears = 0;
        level = 0;
        timeout = difficulty == 0 ? 1200U : difficulty == 1 ? 900U
                                                            : 650U;
        grid[0].initGrid(GRID_WIDTH, TOP_GRID_HEIGHT);
        grid[1].initGrid(GRID_WIDTH, SIDE_GRID_HEIGHT);
        grid[2].initGrid(GRID_WIDTH, BOTTOM_GRID_HEIGHT);
        grid[3].initGrid(GRID_WIDTH, SIDE_GRID_HEIGHT);
    }

    void PuzzleGame::procBlocks() {
        level = std::clamp((1200 - static_cast<int>(timeout)) / 100, 0, 10);
        if (clearVisualRun()) {
            return;
        }
        for (GameGrid &focusGrid : grid) {
            for (int x = 0; x < focusGrid.width(); ++x) {
                for (int y = 0; y < focusGrid.height(); ++y) {
                    if (clearRun(focusGrid, x, y, 0, 1) ||
                        clearRun(focusGrid, x, y, 1, 0) ||
                        clearRun(focusGrid, x, y, 1, 1) ||
                        clearRun(focusGrid, x, y, -1, 1)) {
                        return;
                    }
                }
            }
        }
        clearTopBottomSeams();
        moveDownBlocks();
    }

    Block *PuzzleGame::blockAt(const CellRef &cell) {
        if (cell.gridIndex < 0 || cell.gridIndex >= static_cast<int>(grid.size())) {
            return nullptr;
        }
        return grid[static_cast<std::size_t>(cell.gridIndex)].at(cell.x, cell.y);
    }

    const Block *PuzzleGame::blockAt(const CellRef &cell) const {
        if (cell.gridIndex < 0 || cell.gridIndex >= static_cast<int>(grid.size())) {
            return nullptr;
        }
        return grid[static_cast<std::size_t>(cell.gridIndex)].at(cell.x, cell.y);
    }

    PuzzleGame::CellBounds PuzzleGame::cellBounds(const CellRef &cell) {
        if (cell.gridIndex == 0) {
            return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH, cell.y * BLOCK_HEIGHT, BLOCK_WIDTH, BLOCK_HEIGHT};
        }
        if (cell.gridIndex == 1) {
            return {cell.y * BLOCK_HEIGHT, (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH, BLOCK_HEIGHT, BLOCK_WIDTH};
        }
        if (cell.gridIndex == 2) {
            return {(DESIGN_WIDTH / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + cell.x * BLOCK_WIDTH,
                    (DESIGN_HEIGHT / 2) + 5 + (BOTTOM_GRID_HEIGHT - 1 - cell.y) * BLOCK_HEIGHT,
                    BLOCK_WIDTH,
                    BLOCK_HEIGHT};
        }
        return {DESIGN_WIDTH - (SIDE_GRID_HEIGHT * BLOCK_HEIGHT) + (SIDE_GRID_HEIGHT - 1 - cell.y) * BLOCK_HEIGHT,
                (DESIGN_HEIGHT / 2) - ((GRID_WIDTH * BLOCK_WIDTH) / 2) + (GRID_WIDTH - 1 - cell.x) * BLOCK_WIDTH,
                BLOCK_HEIGHT,
                BLOCK_WIDTH};
    }

    bool PuzzleGame::sameCell(const CellRef &a, const CellRef &b) const {
        return a.gridIndex == b.gridIndex && a.x == b.x && a.y == b.y;
    }

    bool PuzzleGame::findNextVisualMatch(const CellRef &current, int color, int dx, int dy, CellRef &next) const {
        const CellBounds currentBounds = cellBounds(current);
        const int currentCenterX2 = currentBounds.x * 2 + currentBounds.width;
        const int currentCenterY2 = currentBounds.y * 2 + currentBounds.height;
        constexpr int TOLERANCE2 = BLOCK_HEIGHT + 2;
        int bestError = TOLERANCE2 * TOLERANCE2 * 4 + 1;
        bool found = false;

        for (const int gridIndex : {0, 2}) {
            const GameGrid &focusGrid = grid[static_cast<std::size_t>(gridIndex)];
            for (int y = 0; y < focusGrid.height(); ++y) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    const CellRef candidate{gridIndex, x, y};
                    if (sameCell(current, candidate)) {
                        continue;
                    }
                    const Block *block = blockAt(candidate);
                    if (block == nullptr || block->color != color) {
                        continue;
                    }

                    const CellBounds candidateBounds = cellBounds(candidate);
                    const int candidateCenterX2 = candidateBounds.x * 2 + candidateBounds.width;
                    const int candidateCenterY2 = candidateBounds.y * 2 + candidateBounds.height;
                    const int expectedDeltaX2 = dx == 0 ? 0 : dx * (currentBounds.width + candidateBounds.width);
                    const int expectedDeltaY2 = dy == 0 ? 0 : dy * (currentBounds.height + candidateBounds.height);
                    const int errorX = std::abs((candidateCenterX2 - currentCenterX2) - expectedDeltaX2);
                    const int errorY = std::abs((candidateCenterY2 - currentCenterY2) - expectedDeltaY2);
                    if (errorX > TOLERANCE2 || errorY > TOLERANCE2) {
                        continue;
                    }

                    const int error = errorX * errorX + errorY * errorY;
                    if (error < bestError) {
                        bestError = error;
                        next = candidate;
                        found = true;
                    }
                }
            }
        }

        return found;
    }

    bool PuzzleGame::clearVisualRun() {
        constexpr std::array<std::pair<int, int>, 4> directions{{{1, 0}, {0, 1}, {1, 1}, {-1, 1}}};
        for (const int gridIndex : {0, 2}) {
            GameGrid &focusGrid = grid[static_cast<std::size_t>(gridIndex)];
            for (int y = 0; y < focusGrid.height(); ++y) {
                for (int x = 0; x < focusGrid.width(); ++x) {
                    const CellRef start{gridIndex, x, y};
                    const Block *startBlock = blockAt(start);
                    if (startBlock == nullptr || startBlock->color <= 0) {
                        continue;
                    }

                    for (const auto &[dx, dy] : directions) {
                        CellRef previous{};
                        if (findNextVisualMatch(start, startBlock->color, -dx, -dy, previous)) {
                            continue;
                        }

                        std::vector<CellRef> run{start};
                        CellRef current = start;
                        while (findNextVisualMatch(current, startBlock->color, dx, dy, current)) {
                            if (std::any_of(run.begin(), run.end(), [&](const CellRef &cell) {
                                    return sameCell(cell, current);
                                })) {
                                break;
                            }
                            run.push_back(current);
                        }

                        if (run.size() < 3U) {
                            continue;
                        }

                        for (const CellRef &cell : run) {
                            if (Block *block = blockAt(cell)) {
                                markClearing(*block);
                            }
                        }
                        ++score;
                        recordClear();
                        playClearSound();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool PuzzleGame::clearRun(GameGrid &focusGrid, int x, int y, int dx, int dy) {
        Block *start = focusGrid.at(x, y);
        if (start == nullptr || start->color <= 0) {
            return false;
        }

        const int color = start->color;
        const Block *previous = focusGrid.at(x - dx, y - dy);
        if (previous != nullptr && previous->color == color) {
            return false;
        }

        std::vector<Block *> blocks{};
        int cx = x;
        int cy = y;
        while (Block *block = focusGrid.at(cx, cy)) {
            if (block->color != color) {
                break;
            }
            blocks.push_back(block);
            cx += dx;
            cy += dy;
        }

        if (blocks.size() < 3U) {
            return false;
        }

        for (Block *block : blocks) {
            markClearing(*block);
        }
        ++score;
        recordClear();
        playClearSound();
        return true;
    }

    void PuzzleGame::clearTopBottomSeams() {
        for (int x = 0; x < grid[0].width(); ++x) {
            const int topY = grid[0].height() - 1;
            const int bottomY = grid[2].height() - 1;
            if (clearSeam({grid[0].at(x, topY), grid[0].at(x, topY - 1), grid[2].at(x, bottomY), grid[2].at(x, bottomY - 1)})) {
                return;
            }
            if (clearSeam({grid[2].at(x, bottomY), grid[0].at(x, topY), grid[2].at(x, bottomY - 1), grid[0].at(x, topY - 1)})) {
                return;
            }
        }
    }

    bool PuzzleGame::clearSeam(std::array<Block *, 4> blocks) {
        if (blocks[0] == nullptr || blocks[1] == nullptr || blocks[2] == nullptr || blocks[0]->color <= 0) {
            return false;
        }
        if (blocks[0]->color != blocks[1]->color || blocks[0]->color != blocks[2]->color) {
            return false;
        }
        if (blocks[3] != nullptr && blocks[3]->color == blocks[0]->color) {
            markClearing(*blocks[3]);
            score += 10;
            playClearSound();
        }
        markClearing(*blocks[0]);
        markClearing(*blocks[1]);
        markClearing(*blocks[2]);
        ++score;
        recordClear();
        playClearSound();
        return true;
    }

    void PuzzleGame::moveDownBlocks() {
        for (GameGrid &focusGrid : grid) {
            for (int x = 0; x < focusGrid.width(); ++x) {
                for (int y = focusGrid.height() - 2; y >= 0; --y) {
                    Block *current = focusGrid.at(x, y);
                    Block *below = focusGrid.at(x, y + 1);
                    if (current != nullptr && below != nullptr && current->color > 0 && below->color == 0) {
                        std::swap(current->color, below->color);
                        return;
                    }
                }
            }
        }
    }

    void PuzzleGame::markClearing(Block &block) {
        block.color = -1;
        block.clearElapsedMs = 0;
    }

    void PuzzleGame::recordClear() {
        ++clears;
        if ((clears % CLEARS_PER_LEVEL) == 0 && timeout > MIN_DROP_TIMEOUT_MS) {
            timeout = static_cast<unsigned int>(std::max(static_cast<int>(MIN_DROP_TIMEOUT_MS), static_cast<int>(timeout) - LEVEL_TIMEOUT_STEP_MS));
        }
    }

    void PuzzleGame::playClearSound() {
        if (playLineSound) {
            playLineSound();
        }
    }

} // namespace mutatris
