#include "piece.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "game_grid.hpp"

namespace mutatris {

    Piece::Piece(GameGrid *owner)
        : grid(owner) {
    }

    void Piece::reset() {
        x = grid->width() / 2;
        y = 0;
        do {
            for (Block &block : blocks) {
                block.color = 1 + (std::rand() % 7);
            }
        } while (blocks[0].color == blocks[1].color && blocks[0].color == blocks[2].color);
        direction = 0;
    }

    void Piece::shiftColors() {
        const std::array<Block, 3> previous = blocks;
        blocks[0] = previous[2];
        blocks[1] = previous[0];
        blocks[2] = previous[1];
    }

    bool Piece::checkLocation(int next_x, int next_y) const {
        std::array<const Block *, 3> target{};
        switch (direction) {
        case 0:
            target = {grid->at(next_x, next_y), grid->at(next_x, next_y + 1), grid->at(next_x, next_y + 2)};
            break;
        case 1:
            target = {grid->at(next_x, next_y), grid->at(next_x + 1, next_y), grid->at(next_x + 2, next_y)};
            break;
        case 2:
            target = {grid->at(next_x, next_y), grid->at(next_x - 1, next_y), grid->at(next_x - 2, next_y)};
            break;
        case 3:
            target = {grid->at(next_x, next_y), grid->at(next_x, next_y - 1), grid->at(next_x, next_y - 2)};
            break;
        default:
            break;
        }

        return std::all_of(target.begin(), target.end(), [](const Block *block) {
            return block != nullptr && block->color == 0;
        });
    }

    void Piece::moveLeft() {
        if (checkLocation(x - 1, y)) {
            --x;
        }
    }

    void Piece::moveRight() {
        if (checkLocation(x + 1, y)) {
            ++x;
        }
    }

    bool Piece::moveDown() {
        if (checkLocation(x, y + 1)) {
            ++y;
            return false;
        } else {
            return setBlock();
        }
    }

    bool Piece::drop() {
        while (checkLocation(x, y + 1)) {
            ++y;
        }
        return setBlock();
    }

    void Piece::shiftDirection() {
        const int oldDirection = direction;
        direction = (direction + 1) % 4;
        if (!checkLocation(x, y)) {
            direction = oldDirection;
        }
    }

    bool Piece::setBlock() {
        std::array<Block *, 3> target{};
        switch (direction) {
        case 0:
            target = {grid->at(x, y), grid->at(x, y + 1), grid->at(x, y + 2)};
            break;
        case 1:
            target = {grid->at(x, y), grid->at(x + 1, y), grid->at(x + 2, y)};
            break;
        case 2:
            target = {grid->at(x, y), grid->at(x - 1, y), grid->at(x - 2, y)};
            break;
        case 3:
            target = {grid->at(x, y), grid->at(x, y - 1), grid->at(x, y - 2)};
            break;
        default:
            break;
        }

        for (std::size_t i = 0; i < target.size(); ++i) {
            if (target[i] == nullptr) {
                return false;
            }
            target[i]->color = blocks[i].color;
            target[i]->clearElapsedMs = 0;
        }
        reset();
        return true;
    }

} // namespace mutatris
