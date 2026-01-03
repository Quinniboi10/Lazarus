#pragma once

#include "search.h"
#include "thread.h"
#include "ttable.h"

#include <barrier>
#include <mutex>
#include <thread>

struct Searcher {
    TranspositionTable transpositionTable;

    std::atomic<bool>        stopFlag{ true };
    std::vector<ThreadData>  threadData;
    std::vector<std::thread> threads;

    SearchParams sp;

    // Atomic probes to get information from the search
    std::mutex searchLock{};
    Board      currentBoard{};
    usize      depth{};
    usize      seldepth{};
    i16        score{};
    PvList     pv{};

    bool doReporting;

    // Dictates if uci/pretty printing should be used, false by default
    bool doUci;

    explicit Searcher(const bool doReporting, const bool doUci = false) {
        setThreads(1);
        this->doReporting = doReporting;
        this->doUci       = doUci;

        reset();
    }

    u64 totalNodes() const {
        u64 nodes = 0;
        for (const ThreadData& t : threadData)
            nodes += t.nodes.load(std::memory_order_relaxed);
        return nodes;
    }

    void start(const Board& board, SearchParams sp);
    void stop();
    void waitUntilFinished();

    void setThreads(usize numThreads);

    void resizeTT(const u64 newSizeMiB) {
        transpositionTable.reserve(newSizeMiB);
        transpositionTable.clear();
    }

    void reset() {
        transpositionTable.clear();
        for (auto& t : threadData)
            t.reset();
    }

    MoveEvaluation iterativeDeepening(ThreadData& thisThread, Board board, SearchParams sp);

    void reportUci();
    void reportPrettyPrint();
};
