#pragma once

#include "accumulator.h"
#include "config.h"
#include "thread.h"
#include "types.h"

struct NNUE {
    alignas(64) array<i16, HL_SIZE * 768> weightsToHL;
    alignas(64) array<i16, HL_SIZE> hiddenLayerBias;
    alignas(64) MultiArray<i16, OUTPUT_BUCKETS, HL_SIZE * 2> weightsToOut;
    array<i16, OUTPUT_BUCKETS> outputBias;

    static i16 ReLU(i16 x);
    static i16 CReLU(i16 x);
    static i32 SCReLU(i16 x);

    i32 vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, usize bucket) const;

    static usize feature(Color perspective, Color color, PieceType piece, Square square);

    void loadNetwork(const string& filepath);

    int  forwardPass(const Board* board, const AccumulatorPair& accumulators) const;
    void showBuckets(const Board* board, const AccumulatorPair& accumulators) const;

    i16 evaluate(const Board& board, const ThreadData& thisThread) const;
};