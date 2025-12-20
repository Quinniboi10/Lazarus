#pragma once

#include "search.h"
#include "tunable.h"
#include "types.h"

#include <utility>

namespace Search {
struct ThreadInfo {
    Stack<AccumulatorPair, MAX_PLY + 1> accumulatorStack;

    ThreadType type;

    std::atomic<bool>& breakFlag;

    std::atomic<u64> nodes;
    usize            seldepth;

    usize minNmpPly;

    ThreadInfo(ThreadType type, std::atomic<bool>& breakFlag);

    // Copy constructor
    ThreadInfo(const ThreadInfo& other);

    std::pair<Board, ThreadStackManager> makeMove(const Board& board, Move m);

    void refresh(const Board& b);
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
}