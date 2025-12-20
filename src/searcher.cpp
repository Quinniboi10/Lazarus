#include "searcher.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

#include <algorithm>

void Searcher::start(const Board& board, Search::SearchParams sp) {
    if (!threadData)
        threadData = std::make_unique<Search::ThreadInfo>(Search::ThreadType::MAIN, stopFlag);

    stopFlag.store(false, std::memory_order_release);
    threadData->reset();

    time   = sp.time;
    thread = std::thread(Search::iterativeDeepening, board, std::ref(*threadData), sp, this);
}

void Searcher::stop() {
    stopFlag.store(true, std::memory_order_relaxed);

    if (thread.joinable())
        thread.join();
}

void Searcher::waitUntilFinished() const {
    while (!stopFlag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void Searcher::searchReport(const Board& board, const usize depth, const i32 score, const PvList& pv) {
    u64 nodes = threadData->nodes;

    fmt::print("info depth {} seldepth {} time {} nodes {}", depth, threadData->seldepth, time.elapsed(), nodes);
    if (time.elapsed() > 0)
        fmt::print(" nps {}", nodes * 1000 / time.elapsed());

    fmt::print(" score ");

    // Don't report mate scores if it's from the TB
    if (Search::isDecisive(score)) {
        fmt::print("mate {}", std::copysign((Search::MATE_SCORE - std::abs(score)) / 2 + 1, score));
    }
    else
        fmt::print("cp {}", scaleEval(score, board));

    fmt::print(" pv");
    for (const Move m : pv)
        cout << " " << m;

    cout << endl;
}
