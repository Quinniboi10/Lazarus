#include "nnue.h"
#include "accumulator.h"
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
        const Vector<i16> stmAccumValues  = load_ep<i16>(&stm[i]);
        const Vector<i16> nstmAccumValues = load_ep<i16>(&nstm[i]);

        // Clamp values
        const Vector<i16> stmClamped  = clamp_ep<i16>(stmAccumValues, 0, QA);
        const Vector<i16> nstmClamped = clamp_ep<i16>(nstmAccumValues, 0, QA);

        // Load weights
        const Vector<i16> stmWeights  = load_ep<i16>(&weightsToOut[bucket][i]);
        const Vector<i16> nstmWeights = load_ep<i16>(&weightsToOut[bucket][i + HL_SIZE]);

        // SCReLU it
        const Vector<i32> stmActivated  = madd_epi16(stmClamped, mullo_ep(stmClamped, stmWeights));
        const Vector<i32> nstmActivated = madd_epi16(nstmClamped, mullo_ep(nstmClamped, nstmWeights));

        accumulator = add_ep<i32>(accumulator, stmActivated);
        accumulator = add_ep<i32>(accumulator, nstmActivated);
    }

    return reduce_ep<i32>(accumulator);
}
#else
    #pragma message("Using compiler optimized NNUE inference")
i32 NNUE::vectorizedSCReLU(const Accumulator& stm, const Accumulator& nstm, const usize bucket) {
    i32 res = 0;

    #pragma unroll
    for (usize i = 0; i < HL_SIZE; i++) {
        res += (i32) SCReLU(stm[i]) * weightsToOut[bucket][i];
        res += (i32) SCReLU(nstm[i]) * weightsToOut[bucket][i + HL_SIZE];
    }
    return res;
}
#endif

// Finds the input feature
usize NNUE::feature(const Color perspective, const Color color, const PieceType piece, const Square square) {
    const usize colorIndex  = (perspective == color) ? 0 : 1;
    const usize squareIndex = (perspective == BLACK) ? flipRank(square) : static_cast<int>(square);

    return colorIndex * 64 * 6 + piece * 64 + squareIndex;
}

void NNUE::loadNetwork(const string& filepath) {
    std::ifstream stream(filepath, std::ios::binary);
    if (!stream.is_open()) {
        cerr << "Failed to open file: " + filepath << endl;
        cerr << "Expect engine to not work as intended with bad evaluation" << endl;
    }

    // Load weightsToHL
    for (i16& weight : weightsToHL) {
        weight = readLittleEndian<i16>(stream);
    }

    // Load hiddenLayerBias
    for (i16& bias : hiddenLayerBias) {
        bias = readLittleEndian<i16>(stream);
    }

    // Load weightsToOut
    for (auto& i : weightsToOut) {
        for (i16& w : i) {
            w = readLittleEndian<i16>(stream);
        }
    }

    // Load outputBias
    for (i16& b : outputBias) {
        b = readLittleEndian<i16>(stream);
    }
}

// Returns the output of the NN
int NNUE::forwardPass(const Board* board, const AccumulatorPair& accumulators) const {
    const usize divisor      = 32 / OUTPUT_BUCKETS;
    const usize outputBucket = (popcount(board->pieces()) - 2) / divisor;

    const Accumulator& accumulatorSTM = board->stm == WHITE ? accumulators.white_ : accumulators.black_;
    const Accumulator& accumulatorOPP = ~board->stm == WHITE ? accumulators.white_ : accumulators.black_;

    // Accumulate output for STM and OPP using separate weight segments
    i64 eval = 0;

    if constexpr (ACTIVATION != ::SCReLU) {
        for (usize i = 0; i < HL_SIZE; i++) {
            // First HL_SIZE weights are for STM
            if constexpr (ACTIVATION == ::ReLU)
                eval += ReLU(accumulatorSTM[i]) * weightsToOut[outputBucket][i];
            if constexpr (ACTIVATION == ::CReLU)
                eval += CReLU(accumulatorSTM[i]) * weightsToOut[outputBucket][i];

            // Last HL_SIZE weights are for OPP
            if constexpr (ACTIVATION == ::ReLU)
                eval += ReLU(accumulatorOPP[i]) * weightsToOut[outputBucket][HL_SIZE + i];
            if constexpr (ACTIVATION == ::CReLU)
                eval += CReLU(accumulatorOPP[i]) * weightsToOut[outputBucket][HL_SIZE + i];
        }
    }
    else
        eval = vectorizedSCReLU(accumulatorSTM, accumulatorOPP, outputBucket);


    // Dequantization
    if constexpr (ACTIVATION == ::SCReLU)
        eval /= QA;

    eval += outputBias[outputBucket];

    // Apply output bias and scale the result
    return (eval * EVAL_SCALE) / (QA * QB);
}

// Debug feature based on SF
void NNUE::showBuckets(const Board* board, const AccumulatorPair& accumulators) const {
    const usize divisor     = 32 / OUTPUT_BUCKETS;
    const usize usingBucket = (popcount(board->pieces()) - 2) / divisor;

    int staticEval = 0;

    cout << "+------------+------------+" << endl;
    cout << "|   Bucket   | Evaluation |" << endl;
    cout << "+------------+------------+" << endl;

    const Accumulator& accumulatorSTM = board->stm == WHITE ? accumulators.white_ : accumulators.black_;
    const Accumulator& accumulatorOPP = ~board->stm == WHITE ? accumulators.white_ : accumulators.black_;

    for (usize outputBucket = 0; outputBucket < OUTPUT_BUCKETS; outputBucket++) {
        // Accumulate output for STM and OPP using separate weight segments
        i64 eval = 0;

        if constexpr (ACTIVATION != ::SCReLU) {
            for (usize i = 0; i < HL_SIZE; i++) {
                // First HL_SIZE weights are for STM
                if constexpr (ACTIVATION == ::ReLU)
                    eval += ReLU(accumulatorSTM[i]) * weightsToOut[outputBucket][i];
                if constexpr (ACTIVATION == ::CReLU)
                    eval += CReLU(accumulatorSTM[i]) * weightsToOut[outputBucket][i];

                // Last HL_SIZE weights are for OPP
                if constexpr (ACTIVATION == ::ReLU)
                    eval += ReLU(accumulatorOPP[i]) * weightsToOut[outputBucket][HL_SIZE + i];
                if constexpr (ACTIVATION == ::CReLU)
                    eval += CReLU(accumulatorOPP[i]) * weightsToOut[outputBucket][HL_SIZE + i];
            }
        }
        else
            eval = vectorizedSCReLU(accumulatorSTM, accumulatorOPP, outputBucket);


        // Dequantization
        if constexpr (ACTIVATION == ::SCReLU)
            eval /= QA;

        eval += outputBias[outputBucket];

        // Apply output bias and scale the result
        staticEval = (eval * EVAL_SCALE) / (QA * QB);

        fmt::print("| {:<10} |  {:<+8.2f}  |", outputBucket, staticEval / 100.0);
        if (outputBucket == usingBucket)
            cout << " <- Current bucket";
        cout << endl;
        if (outputBucket == OUTPUT_BUCKETS - 1)
            cout << "+------------+------------+" << endl;
    }
}

i16 NNUE::evaluate(const Board& board, const ThreadData& thisThread) const {
#ifndef NDEBUG
    AccumulatorPair verifAccumulator;
    verifAccumulator.resetAccumulators(board);
    if (verifAccumulator != thisThread.accumulatorStack.top())
        cout << board.toString() << endl;
    assert(verifAccumulator == thisThread.accumulatorStack.top());
#endif
    return std::clamp<i32>(forwardPass(&board, thisThread.accumulatorStack.top()), MATED_IN_MAX_PLY, MATE_IN_MAX_PLY);
}