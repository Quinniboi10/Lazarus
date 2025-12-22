#pragma once

#include "search.h"
#include "thread.h"
#include "ttable.h"

#include <thread>

struct Searcher {
    TranspositionTable transpositionTable;

    std::atomic<bool>                    stopFlag;
    Stopwatch<std::chrono::milliseconds> time;
    std::unique_ptr<ThreadInfo>          threadData;
    std::thread                          thread;

    bool doReporting;

    explicit Searcher(const bool doReporting) {
        stopFlag.store(true, std::memory_order_relaxed);
        time.reset();

        threadData        = std::make_unique<ThreadInfo>(ThreadType::MAIN, stopFlag);
        this->doReporting = doReporting;

        reset();
    }

    void start(const Board& board, SearchParams sp);
    void stop();
    void waitUntilFinished();

    void reset() {
        threadData->reset();
    }

    MoveEvaluation iterativeDeepening(Board board, SearchParams sp);

    void searchReport(const Board& board, usize depth, i32 score, const PvList& pv);
};
