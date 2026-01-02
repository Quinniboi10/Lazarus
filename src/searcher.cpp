#include "searcher.h"
#include "cursor.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

void Searcher::start(const Board& board, const SearchParams sp) {
    stop();

    this->sp = sp;
    searchLock.lock();
    this->currentBoard = board;
    this->depth        = 0;
    this->seldepth     = 0;
    this->score        = 0;
    this->pv.length    = 0;
    searchLock.unlock();

    stopFlag.store(false, std::memory_order_relaxed);

    for (usize i = threadData.size(); i > 0; i--)
        threads.emplace_back(&Searcher::iterativeDeepening, this, std::ref(threadData[i - 1]), board, sp);
}

void Searcher::stop() {
    stopFlag.store(true, std::memory_order_relaxed);

    waitUntilFinished();

    threads.clear();
}

void Searcher::waitUntilFinished() {
    for (auto& t : threads)
        if (t.joinable())
            t.join();
}

void Searcher::setThreads(const usize numThreads) {
    threadData.clear();
    threadData.emplace_back(ThreadType::MAIN, stopFlag);

    for (usize i = 1; i < numThreads; i++)
        threadData.emplace_back(ThreadType::SECONDARY, stopFlag);
}

void Searcher::reportUci() {
    searchLock.lock();

    const u64 time = std::max<u64>(sp.time.elapsed(), 1);
    const u64 nodes = totalNodes();

    fmt::print("info depth {} seldepth {} time {} nodes {} nps {} hashfull {}", depth, threadData[0].seldepth, time, nodes, nodes * 1000 / time, transpositionTable.hashfull());

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

    const u64 time = std::max<u64>(sp.time.elapsed(), 1);
    const u64 nodes = totalNodes();

    cursor::cache();
    cursor::home();
    cout << currentBoard.toString(pv.moves[0]);
    cursor::load();

    // Depth
    fmt::print(fmt::fg(fmt::color::light_gray) | fmt::emphasis::bold, " {:<8} ", fmt::format("{}/{}", depth, seldepth));

    // Time
    fmt::print(fmt::fg(fmt::color::gray), "{:>10}    ", formatTime(time));

    // Nodes
    fmt::print(fmt::fg(fmt::color::gray), "{:>20}    ", fmt::format("{} nodes", formatNum(nodes)));

    // Speed
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", fmt::format("{} knps", formatNum(nodes / time)));

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