#pragma once

#include "accumulator.h"
#include "config.h"
#include "thread.h"
#include "types.h"

struct NNUE {
    alignas(64) array<i16, HL_SIZE * 768> weights_to_hl;
    alignas(64) array<i16, HL_SIZE> hl_bias;
    alignas(64) MultiArray<i16, OUTPUT_BUCKETS, HL_SIZE * 2> weights_to_out;
    array<i16, OUTPUT_BUCKETS> output_bias;

    static i16 ReLU(i16 x);
    static i16 CReLU(i16 x);
    static i32 SCReLU(i16 x);

    i32 vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, usize bucket) const;

    static usize feature(Color perspective, Color color, PieceType piece, Square square);

    void load_net(const string& filepath);

    int evaluate(const Board* board, const AccumulatorPair& accumulators) const;
    void print_buckets(const Board* board, const AccumulatorPair& accumulators) const;

    i16 evaluate(const Board& board, const ThreadData& this_thread) const;
};