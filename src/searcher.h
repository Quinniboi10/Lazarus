#pragma once

#include "search.h"
#include "thread.h"
#include "ttable.h"
#include "threadpool.h"

struct Searcher {
    TranspositionTable transpositionTable;

    alignas(64) std::atomic<bool> stopFlag;
    std::vector<ThreadData> threadData{};
    std::unique_ptr<ThreadPool> threads;

    SearchParams sp;

    // Thread-safe probes to get information from the search
    std::mutex                          searchLock{};
    Board                               currentBoard{};
    usize                               depth{};
    usize                               seldepth{};
    i16                                 score{};
    PvList                              pv{};
    RollingWindow<std::pair<u64, Move>> moveHistory{ static_cast<usize>(std::max<int>(getTerminalRows() - 26, 1)) };

    bool doReporting;

    // Dictates if uci/pretty printing should be used, false by default
    bool doUci;

    explicit Searcher(const bool doReporting, const bool doUci = false) {
        stopFlag.store(true, std::memory_order_relaxed);

        setThreads(1);

        this->doReporting = doReporting;
        this->doUci       = doUci;

        reset();
    }

    void runWorker(ThreadData& thisThread);

    void start(const Board& board, const SearchParams& sp);
    void stop();
    void waitUntilFinished() const;

    void resizeTT(const u64 newSizeMiB) {
        transpositionTable.reserve(newSizeMiB);
        transpositionTable.clear();
    }
    void setThreads(const usize newThreads) {
        threads = std::make_unique<ThreadPool>(newThreads);

        threadData.emplace_back(ThreadType::MAIN, stopFlag);
        for (usize i = 1; i < newThreads; i++)
            threadData.emplace_back(ThreadType::SECONDARY, stopFlag);
    }

    void reset() {
        for (auto& t : threadData)
            t.reset();
        transpositionTable.clear();
    }

    MoveEvaluation iterativeDeepening(ThreadData& thisThread, Board board);

    void reportUci();
    void reportPrettyPrint();
};
