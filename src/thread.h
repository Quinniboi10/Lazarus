#pragma once

#include "accumulator.h"
#include "search.h"
#include "types.h"

#include <utility>

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
        const i32 clampedBonus = std::clamp<i32>(bonus, -MAX_HISTORY, MAX_HISTORY);
        value += clampedBonus - value * abs(clampedBonus) / MAX_HISTORY;
    }
};

struct ThreadInfo {
    // History is indexed [stm][from][to]
    MultiArray<HistoryEntry, 2, 64, 64> history;

    // Capthist is indexed [stm][pt][captured pt][to]
    MultiArray<HistoryEntry, 2, 6, 6, 64> capthist;

    // All the accumulators for each thread's search
    Stack<AccumulatorPair, MAX_PLY + 1> accumulatorStack;

    ThreadType type;

    std::atomic<bool>& breakFlag;

    std::atomic<u64> nodes;
    usize            seldepth;

    ThreadInfo(ThreadType type, std::atomic<bool>& breakFlag);

    // Copy constructor
    ThreadInfo(const ThreadInfo& other);

    // Accessors for the histories
    HistoryEntry& getHistory(const Board& b, const Move m) {
        return history[b.stm][m.from()][m.to()];
    }
    const HistoryEntry& getHistory(const Board& b, const Move m) const {
        return history[b.stm][m.from()][m.to()];
    }
    HistoryEntry& getCaptureHistory(const Board& b, const Move m) {
        return capthist[b.stm][b.getPiece(m.from())][b.getPiece(m.to())][m.to()];
    }
    const HistoryEntry& getCaptureHistory(const Board& b, const Move m) const {
        return capthist[b.stm][b.getPiece(m.from())][b.getPiece(m.to())][m.to()];
    }

    std::pair<Board, ThreadStackManager> makeMove(const Board& board, Move m);
    std::pair<Board, ThreadStackManager> makeNullMove(const Board& board);

    // Reset the accumulator stack for a given position
    void refresh(const Board& b);
    // Reset data that lasts between searches
    void reset();
};

struct ThreadStackManager {
    ThreadInfo& thisThread;

    explicit ThreadStackManager(ThreadInfo& thisThread) :
        thisThread(thisThread) {}

    ThreadStackManager(const ThreadStackManager& other) = delete;

    ~ThreadStackManager() {
        thisThread.accumulatorStack.pop();
    }
};