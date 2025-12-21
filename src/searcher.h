#pragma once

#include "search.h"
#include "thread.h"

#include <thread>

struct Searcher {
    std::atomic<bool>                    stopFlag;
    Stopwatch<std::chrono::milliseconds> time;
    std::unique_ptr<Search::ThreadInfo>  threadData;
    std::thread                          thread;

    Searcher() {
        stopFlag.store(true, std::memory_order_relaxed);
        time.reset();
        threadData = std::make_unique<Search::ThreadInfo>(Search::ThreadType::MAIN, stopFlag);
    }

    void start(const Board& board, Search::SearchParams sp);
    void stop();
    void waitUntilFinished() const;

    void reset() {
        threadData->reset();
    }

    void searchReport(const Board& board, usize depth, i32 score, const PvList& pv);
};
