#pragma once

#include "config.h"
#include "move.h"
#include "types.h"

using Accumulator = array<i16, HL_SIZE>;

struct AccumulatorPair {
    alignas(64) Accumulator white_;
    alignas(64) Accumulator black_;

    void recompute_all(const Board& board);

    void update(const Board& board, Move m, PieceType to_pt);

    void add_sub(Color stm, Square add, PieceType add_pt, Square sub, PieceType sub_pt);
    void add_sub_sub(Color stm, Square add, PieceType add_pt, Square sub1, PieceType sub_pt1, Square sub2, PieceType sub_pt2);
    void add_add_sub_sub(Color stm, Square add1, PieceType add_pt1, Square add2, PieceType add_pt2, Square sub1, PieceType sub_pt1, Square sub2, PieceType sub_pt2);

    bool operator==(const AccumulatorPair& other) const = default;
};