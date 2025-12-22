#pragma once

#include "search.h"
#include "thread.h"

#include <thread>

struct Searcher {
    std::atomic<bool>                    stopFlag;
    Stopwatch<std::chrono::milliseconds> time;
    std::unique_ptr<ThreadInfo>          threadData;
    std::thread                          thread;

    bool doReporting;

    Searcher(const bool doReporting) {
        stopFlag.store(true, std::memory_order_relaxed);
        time.reset();
        threadData        = std::make_unique<ThreadInfo>(ThreadType::MAIN, stopFlag);
        this->doReporting = doReporting;
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
