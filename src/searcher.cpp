#include "searcher.h"
#include "cursor.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

// Every worker will run this loop
void Searcher::runWorker(ThreadData& thisThread) {
    while (!killFlag.load(std::memory_order_relaxed)) {
        while (stopFlag.load(std::memory_order_relaxed)) {
            if (killFlag.load(std::memory_order_relaxed))
                return;
            std::this_thread::yield();
        }
        iterativeDeepening(thisThread, currentBoard);
    }
}

void Searcher::start(const Board& board, const SearchParams& newParams) {
    this->sp = newParams;
    searchLock.lock();
    this->currentBoard = board;
    this->depth        = 0;
    this->seldepth     = 0;
    this->score        = 0;
    this->pv.length    = 0;
    this->moveHistory.clear();
    searchLock.unlock();

    stopFlag.store(false, std::memory_order_release);
}

void Searcher::stop() {
    stopFlag.store(true, std::memory_order_relaxed);
}

void Searcher::waitUntilFinished() const {
    while (!stopFlag.load(std::memory_order_relaxed))
        std::this_thread::yield();
}

void Searcher::reportUci() {
    u64 nodes = 0;
    for (const ThreadData& t : threadData)
        nodes += t.nodes.load(std::memory_order_relaxed);

    searchLock.lock();

    fmt::print("info depth {} seldepth {} time {} nodes {} nps {} hashfull {}", depth, threadData[0].seldepth, sp.time.elapsed(), nodes, nodes * 1000 / (sp.time.elapsed() + 1), transpositionTable.hashfull());

    fmt::print(" score ");

    if (isDecisive(score)) {
        fmt::print("mate {}", std::copysign((MATE_SCORE - std::abs(score)) / 2 + 1, score));
    }
    else
        fmt::print("cp {}", scaleEval(score, currentBoard));

    fmt::print(" pv");
    for (const Move m : pv)
        cout << " " << m;

    cout << endl;
    searchLock.unlock();
}

void Searcher::reportPrettyPrint() {
    u64 nodes = 0;
    for (const ThreadData& t : threadData)
        nodes += t.nodes.load(std::memory_order_relaxed);

    searchLock.lock();

    cursor::cache();
    cursor::home();
    cout << currentBoard.toString(pv.moves[0]);
    cursor::load();

    // Depth
    fmt::print(fmt::fg(fmt::color::light_gray) | fmt::emphasis::bold, " {:<8} ", fmt::format("{}/{}", depth, seldepth));

    // Time
    fmt::print(fmt::fg(fmt::color::gray), "{:>10}    ", formatTime(sp.time.elapsed()));

    // Nodes
    fmt::print(fmt::fg(fmt::color::gray), "{:>20}    ", fmt::format("{} nodes", formatNum(nodes)));

    // Speed
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", fmt::format("{} knps", formatNum(nodes / (sp.time.elapsed() + 1))));

    // TT
    fmt::print(fmt::fg(fmt::rgb(105, 200, 215)), "TT: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>4}    ", fmt::format("{}%", transpositionTable.hashfull() / 10));

    // Score
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", getColoredScore(scaleEval(score, currentBoard)));

    // PV
    fmt::print("{}", getPrettyPV(pv));

    cout << endl;
    searchLock.unlock();
}