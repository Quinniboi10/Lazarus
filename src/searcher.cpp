#include "searcher.h"
#include "cursor.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

void Searcher::start(const Board& board, SearchParams sp) {
    searchLock.lock();
    this->currentBoard = board;
    this->depth        = 0;
    this->seldepth     = 0;
    this->score        = 0;
    this->pv.length    = 0;
    this->moveHistory.clear();
    searchLock.unlock();

    if (!threadData)
        threadData = std::make_unique<ThreadInfo>(ThreadType::MAIN, stopFlag);

    stopFlag.store(false, std::memory_order_release);
    threadData->reset();

    time   = sp.time;
    thread = std::jthread(&Searcher::iterativeDeepening, this, board, sp);
}

void Searcher::stop() {
    stopFlag.store(true, std::memory_order_relaxed);

    if (thread.joinable())
        thread.join();
}

void Searcher::waitUntilFinished() {
    if (thread.joinable())
        thread.join();

    while (!stopFlag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void Searcher::reportUci() {
    searchLock.lock();
    u64 nodes = threadData->nodes;

    fmt::print("info depth {} seldepth {} time {} nodes {} hashfull {}", depth, threadData->seldepth, time.elapsed(), nodes, transpositionTable.hashfull());
    if (time.elapsed() > 0)
        fmt::print(" nps {}", nodes * 1000 / time.elapsed());

    fmt::print(" score ");

    if (isDecisive(score)) {
        fmt::print("mate {}", std::copysign((MATE_SCORE - std::abs(score)) / 2 + 1, score));
    }
    else
        fmt::print("cp {}", scaleEval(score, currentBoard));

    const auto [ w, d, l ] = getWDL(currentBoard, score);
    fmt::print("wdl {} {} {}", w, d, l);

    fmt::print(" pv");
    for (const Move m : pv)
        cout << " " << m;

    cout << endl;
    searchLock.unlock();
}

void Searcher::reportPrettyPrint() {
    searchLock.lock();

    cursor::cache();
    cursor::home();
    cout << currentBoard.toString(pv.moves[0]);
    cursor::load();

    // Depth
    fmt::print(fmt::fg(fmt::color::light_gray) | fmt::emphasis::bold, " {:<8} ", fmt::format("{}/{}", depth, seldepth));

    // Time
    fmt::print(fmt::fg(fmt::color::gray), "{:>10}    ", formatTime(time.elapsed()));

    // Nodes
    fmt::print(fmt::fg(fmt::color::gray), "{:>20}    ", fmt::format("{} nodes", formatNum(threadData->nodes)));

    // Speed
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", fmt::format("{} knps", formatNum(threadData->nodes / (time.elapsed() + 1))));

    // TT
    fmt::print(fmt::fg(fmt::rgb(105, 200, 215)), "TT: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>4}    ", fmt::format("{}%", transpositionTable.hashfull() / 10));

    // WDL
    const auto [ w, d, l ] = getWDL(currentBoard, score);
    fmt::print(fmt::fg(fmt::rgb(105, 215, 105)), "W: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>4}    ", fmt::format("{:.1f}%", w / 10.0));
    fmt::print(fmt::fg(fmt::rgb(155, 155, 155)), "D: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>4}    ", fmt::format("{:.1f}%", d / 10.0));
    fmt::print(fmt::fg(fmt::rgb(215, 105, 105)), "L: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>4}    ", fmt::format("{:.1f}%", l / 10.0));

    // Score
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", getColoredScore(scaleEval(score, currentBoard)));

    // PV
    fmt::print("{}", getPrettyPV(pv));

    cout << endl;
    searchLock.unlock();
}