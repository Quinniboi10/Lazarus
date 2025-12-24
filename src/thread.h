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
    MultiArray<Move, MAX_PLY, 2>        killers;

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

    void updateKillers(usize ply, const Move m) {
        if (killers[ply][0] != m) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = m;
        }
    }

    void clearKillers() {
        for (usize i = 0; i < MAX_PLY; i++) {
            killers[i][0] = Move::null();
            killers[i][1] = Move::null();
        }
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