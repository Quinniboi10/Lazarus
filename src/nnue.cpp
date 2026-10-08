#include "nnue.h"

#include "accumulator.h"
#include "assert.h"
#include "board.h"
#include "config.h"
#include "search.h"
#include "simd.h"
#include "thread.h"
#include "util.h"

#include "../external/fmt/fmt/format.h"

#include <algorithm>
#include <cstring>
#include <fstream>

i16 NNUE::ReLU(const i16 x) {
    if (x < 0)
        return 0;
    return x;
}

i16 NNUE::CReLU(const i16 x) {
    if (x < 0)
        return 0;
    if (x > QA)
        return QA;
    return x;
}

i32 NNUE::SCReLU(const i16 x) {
    if (x < 0)
        return 0;
    if (x > QA)
        return QA * QA;
    return x * x;
}

#if defined(__x86_64__) || defined(__amd64__) || (defined(_WIN64) && (defined(_M_X64) || defined(_M_AMD64)) || defined(__ARM_NEON))
i32 NNUE::vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, const usize bucket) const {
    using namespace simd;
    static_assert(HL_SIZE % VECTOR_SIZE<i16> == 0, "HL size is not compatible with the size of this CPU's native register");

    Vector<i32> accumulator{};

    for (usize i = 0; i < HL_SIZE; i += VECTOR_SIZE<i16>) {
        // Load accumulators
        const Vector<i16> stm_accum_vals  = load_ep<i16>(&stm[i]);
        const Vector<i16> nstm_accum_vals = load_ep<i16>(&nstm[i]);

        // Clamp values
        const Vector<i16> stm_clamped  = clamp_ep<i16>(stm_accum_vals, 0, QA);
        const Vector<i16> nstm_clamped = clamp_ep<i16>(nstm_accum_vals, 0, QA);

        // Load weights
        const Vector<i16> stm_weights  = load_ep<i16>(&weights_to_out[bucket][i]);
        const Vector<i16> nstm_weights = load_ep<i16>(&weights_to_out[bucket][i + HL_SIZE]);

        // SCReLU it
        const Vector<i32> stm_act  = madd_epi16(stm_clamped, mullo_ep(stm_clamped, stm_weights));
        const Vector<i32> nstm_act = madd_epi16(nstm_clamped, mullo_ep(nstm_clamped, nstm_weights));

        accumulator = add_ep<i32>(accumulator, stm_act);
        accumulator = add_ep<i32>(accumulator, nstm_act);
    }

    return reduce_ep<i32>(accumulator);
}
#else
    #pragma message("Using compiler optimized NNUE inference")
i32 NNUE::vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, const usize bucket) {
    i32 res = 0;

    #pragma unroll
    for (usize i = 0; i < HL_SIZE; i++) {
        res += (i32) SCReLU(stm[i]) * weights_to_out[bucket][i];
        res += (i32) SCReLU(nstm[i]) * weights_to_out[bucket][i + HL_SIZE];
    }
    return res;
}
#endif

// Finds the input feature
usize NNUE::feature(const Color perspective, const Color color, const PieceType piece, const Square square) {
    const usize color_idx = (perspective == color) ? 0 : 1;
    const usize sq_idx    = (perspective == BLACK) ? flip_rank(square) : static_cast<int>(square);

    return color_idx * 64 * 6 + piece * 64 + sq_idx;
}

void NNUE::load_net(const string& filepath) {
    std::ifstream stream(filepath, std::ios::binary);
    if (!stream.is_open()) {
        cerr << "Failed to open file: " + filepath << endl;
        cerr << "Expect engine to not work as intended with bad evaluation" << endl;
    }

    // Load weightsToHL
    for (i16& weight : weights_to_hl) {
        weight = read_little_endian<i16>(stream);
    }

    // Load hl_bias
    for (i16& bias : hl_bias) {
        bias = read_little_endian<i16>(stream);
    }

    // Load weights_to_out
    for (auto& i : weights_to_out) {
        for (i16& w : i) {
            w = read_little_endian<i16>(stream);
        }
    }

    // Load output_bias
    for (i16& b : output_bias) {
        b = read_little_endian<i16>(stream);
    }
}

// Returns the output of the NN
int NNUE::evaluate(const Board* board, const AccumulatorPair& accumulators) const {
    const usize divisor       = 32 / OUTPUT_BUCKETS;
    const usize output_bucket = (popcount(board->pieces()) - 2) / divisor;

    const Accumulator& accum_stm = board->stm == WHITE ? accumulators.white_ : accumulators.black_;
    const Accumulator& accum_opp = ~board->stm == WHITE ? accumulators.white_ : accumulators.black_;

    // Accumulate output for STM and OPP using separate weight segments
    i64 eval = 0;

    if constexpr (ACTIVATION != ::SCReLU) {
        for (usize i = 0; i < HL_SIZE; i++) {
            // First HL_SIZE weights are for STM
            if constexpr (ACTIVATION == ::ReLU)
                eval += ReLU(accum_stm[i]) * weights_to_out[output_bucket][i];
            if constexpr (ACTIVATION == ::CReLU)
                eval += CReLU(accum_stm[i]) * weights_to_out[output_bucket][i];

            // Last HL_SIZE weights are for OPP
            if constexpr (ACTIVATION == ::ReLU)
                eval += ReLU(accum_opp[i]) * weights_to_out[output_bucket][HL_SIZE + i];
            if constexpr (ACTIVATION == ::CReLU)
                eval += CReLU(accum_opp[i]) * weights_to_out[output_bucket][HL_SIZE + i];
        }
    }
    else
        eval = vectorizedSCReLU(accum_stm, accum_opp, output_bucket);


    // Dequantization
    if constexpr (ACTIVATION == ::SCReLU)
        eval /= QA;

    eval += output_bias[output_bucket];

    // Apply output bias and scale the result
    return (eval * EVAL_SCALE) / (QA * QB);
}

// Debug feature based on SF
void NNUE::print_buckets(const Board* board, const AccumulatorPair& accumulators) const {
    const usize divisor      = 32 / OUTPUT_BUCKETS;
    const usize using_bucket = (popcount(board->pieces()) - 2) / divisor;

    int static_eval = 0;

    cout << "+------------+------------+" << endl;
    cout << "|   Bucket   | Evaluation |" << endl;
    cout << "+------------+------------+" << endl;

    const Accumulator& accum_stm = board->stm == WHITE ? accumulators.white_ : accumulators.black_;
    const Accumulator& accum_opp = ~board->stm == WHITE ? accumulators.white_ : accumulators.black_;

    for (usize output_bucket = 0; output_bucket < OUTPUT_BUCKETS; output_bucket++) {
        // Accumulate output for STM and OPP using separate weight segments
        i64 eval = 0;

        if constexpr (ACTIVATION != ::SCReLU) {
            for (usize i = 0; i < HL_SIZE; i++) {
                // First HL_SIZE weights are for STM
                if constexpr (ACTIVATION == ::ReLU)
                    eval += ReLU(accum_stm[i]) * weights_to_out[output_bucket][i];
                if constexpr (ACTIVATION == ::CReLU)
                    eval += CReLU(accum_stm[i]) * weights_to_out[output_bucket][i];

                // Last HL_SIZE weights are for OPP
                if constexpr (ACTIVATION == ::ReLU)
                    eval += ReLU(accum_opp[i]) * weights_to_out[output_bucket][HL_SIZE + i];
                if constexpr (ACTIVATION == ::CReLU)
                    eval += CReLU(accum_opp[i]) * weights_to_out[output_bucket][HL_SIZE + i];
            }
        }
        else
            eval = vectorizedSCReLU(accum_stm, accum_opp, output_bucket);


        // Dequantization
        if constexpr (ACTIVATION == ::SCReLU)
            eval /= QA;

        eval += output_bias[output_bucket];

        // Apply output bias and scale the result
        static_eval = (eval * EVAL_SCALE) / (QA * QB);

        fmt::print("| {:<10} |  {:<+8.2f}  |", output_bucket, static_eval / 100.0);
        if (output_bucket == using_bucket)
            cout << " <- Current bucket";
        cout << endl;
        if (output_bucket == OUTPUT_BUCKETS - 1)
            cout << "+------------+------------+" << endl;
    }
}

i16 NNUE::evaluate(const Board& board, const ThreadData& this_thread) const {
#ifndef NDEBUG
    AccumulatorPair verif_accum;
    verif_accum.recompute_all(board);
    if (verif_accum != this_thread.accum_stack.top())
        cout << board.str() << endl;
    traced_assert(verif_accum == this_thread.accum_stack.top());
#endif
    return std::clamp<i32>(evaluate(&board, this_thread.accum_stack.top()), MATED_IN_MAX_PLY, MATE_IN_MAX_PLY);
}