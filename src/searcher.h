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
    std::jthread                         thread;

    // Atomic probes to get information from the search
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
        time.reset();

        threadData        = std::make_unique<ThreadInfo>(ThreadType::MAIN, stopFlag);
        this->doReporting = doReporting;
        this->doUci       = doUci;

        reset();
    }

    void start(const Board& board, SearchParams sp);
    void stop();
    void waitUntilFinished();

    void resizeTT(const u64 newSizeMiB) {
        fmt::println("Trying for {} MiB", newSizeMiB);
        transpositionTable.reserve(newSizeMiB);
        transpositionTable.clear();
    }

    void reset() {
        threadData->reset();
        transpositionTable.clear();
    }

    MoveEvaluation iterativeDeepening(Board board, SearchParams sp);

    void reportUci();
    void reportPrettyPrint();
};
