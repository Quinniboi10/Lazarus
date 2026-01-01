#pragma once

#include "accumulator.h"
#include "search.h"
#include "types.h"

#include <utility>

template<i32 MAX_VALUE>
struct HistoryEntry {
    i32 value;

    HistoryEntry() :
        value(0) {}
    HistoryEntry(const i32 v) :
        value(v) {}

    operator i32() const {
        return value;
    }

    void update(const i32 bonus) {
        const i32 clampedBonus = std::clamp<i32>(bonus, -MAX_VALUE, MAX_VALUE);
        value += clampedBonus - value * abs(clampedBonus) / MAX_VALUE;
    }
};

struct ThreadData {
    // History is indexed [stm][from][to]
    MultiArray<HistoryEntry<MAX_HISTORY>, 2, 64, 64> history;

    // Capthist is indexed [stm][pt][captured pt][to]
    // En passant is a possible capture with no targeted type
    MultiArray<HistoryEntry<MAX_HISTORY>, 2, 6, 7, 64> capthist;

    // Pawn correction history indexed [stm][pawn key % size]
    MultiArray<HistoryEntry<MAX_CORRHIST>, 2, CORRHIST_SIZE> pawnCorrhist;

    // All the accumulators for each thread's search
    Stack<AccumulatorPair, MAX_PLY + 1> accumulatorStack;

    ThreadType type;

    std::atomic<bool>& breakFlag;

    std::atomic<u64> nodes;
    usize            seldepth;

    ThreadData(ThreadType type, std::atomic<bool>& breakFlag);

    // Copy constructor
    ThreadData(const ThreadData& other);

    // Accessors for the histories
    auto& getHistory(const Board& b, const Move m) {
        return history[b.stm][m.from()][m.to()];
    }
    auto& getHistory(const Board& b, const Move m) const {
        return history[b.stm][m.from()][m.to()];
    }
    auto& getCaptureHistory(const Board& b, const Move m) {
        return capthist[b.stm][b.getPiece(m.from())][b.getPiece(m.to())][m.to()];
    }
    auto& getCaptureHistory(const Board& b, const Move m) const {
        return capthist[b.stm][b.getPiece(m.from())][b.getPiece(m.to())][m.to()];
    }
    void updateCorrhist(const Board& b, const i16 depth, const i16 score, const i16 eval) {
        const i32 bonus = std::clamp<i32>((score - eval) * depth / 8, -MAX_CORRHIST / 4, MAX_CORRHIST / 4);
        pawnCorrhist[b.stm][b.pawnHash % CORRHIST_SIZE].update(bonus);
    }
    i16 correctStaticEval(const Board& b, const i16 staticEval) const {
        return std::clamp<i16>(staticEval + pawnCorrhist[b.stm][b.pawnHash % CORRHIST_SIZE] * PAWN_CORRHIST_WEIGHT / 512, MATED_IN_MAX_PLY, MATE_IN_MAX_PLY);
    }

    std::pair<Board, ThreadStackManager> makeMove(const Board& board, Move m);
    std::pair<Board, ThreadStackManager> makeNullMove(const Board& board);

    // Reset the accumulator stack for a given position
    void refresh(const Board& b);
    // Reset data that lasts between searches
    void reset();
};

struct ThreadStackManager {
    ThreadData& thisThread;

    explicit ThreadStackManager(ThreadData& thisThread) :
        thisThread(thisThread) {}

    ThreadStackManager(const ThreadStackManager& other) = delete;

    ~ThreadStackManager() {
        thisThread.accumulatorStack.pop();
    }
};