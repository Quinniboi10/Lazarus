#pragma once

#include "config.h"
#include "move.h"
#include "types.h"

using Accumulator = array<i16, HL_SIZE>;

struct AccumulatorPair {
    alignas(64) Accumulator white_;
    alignas(64) Accumulator black_;

    void resetAccumulators(const Board& board);

    void update(const Board& board, Move m, PieceType toPT);

    void addSub(Color stm, Square add, PieceType addPT, Square sub, PieceType subPT);
    void addSubSub(Color stm, Square add, PieceType addPT, Square sub1, PieceType subPT1, Square sub2, PieceType subPT2);
    void addAddSubSub(Color stm, Square add1, PieceType addPT1, Square add2, PieceType addPT2, Square sub1, PieceType subPT1, Square sub2, PieceType subPT2);

    bool operator==(const AccumulatorPair& other) const = default;
};