#pragma once

#include "search.h"
#include "thread.h"
#include "ttable.h"

#include <thread>

struct Searcher {
    TranspositionTable transpositionTable;

    std::atomic<bool> killFlag;
    alignas(64) std::atomic<bool> stopFlag;
    std::vector<std::jthread> workers{};
    std::vector<ThreadData>   threadData{};

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

   private:
    void killAllThreads() {
        stopFlag.store(true, std::memory_order_relaxed);
        killFlag.store(true, std::memory_order_relaxed);
        for (auto& w : workers)
            if (w.joinable())
                w.join();

        workers.clear();
        threadData.clear();
        killFlag.store(false, std::memory_order_relaxed);
    }

   public:
    explicit Searcher(const bool doReporting, const bool doUci = false) {
        killFlag.store(false, std::memory_order_relaxed);
        stopFlag.store(true, std::memory_order_relaxed);

        setThreads(1);

        this->doReporting = doReporting;
        this->doUci       = doUci;

        reset();
    }

    ~Searcher() {
        killAllThreads();
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
        killAllThreads();
        threadData.emplace_back(ThreadType::MAIN, stopFlag);

        for (usize i = 1; i < newThreads; i++)
            threadData.emplace_back(ThreadType::SECONDARY, stopFlag);

        for (usize i = 0; i < newThreads; i++)
            workers.emplace_back(&Searcher::runWorker, this, std::ref(threadData[i]));
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
