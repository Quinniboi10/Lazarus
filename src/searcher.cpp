#include "searcher.h"
#include "cursor.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

void Searcher::start(const Board& board, SearchParams sp) {
    searchLock.lock();
    this->currentBoard = board;
    this->depth = 0;
    this->seldepth = 0;
    this->score = 0;
    this->pv.length = 0;
    this->moveHistory.clear();
    searchLock.unlock();

    if (!threadData)
        threadData = std::make_unique<ThreadInfo>(ThreadType::MAIN, stopFlag);

    stopFlag.store(false, std::memory_order_release);
    threadData->reset();

    time   = sp.time;
    thread = std::thread(&Searcher::iterativeDeepening, this, board, sp);
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

    fmt::print(" pv");
    for (const Move m : pv)
        cout << " " << m;

    cout << endl;
    searchLock.unlock();
}

void Searcher::searchReport() {
    searchLock.lock();
    cursor::home();

    const auto printStat = [&](const string& label, const auto& value, const string& suffix = "") { cout << Colors::GREY << label << Colors::WHITE << value << suffix << "   \n"; };

    const auto printBar = [&](const string& label, const float progress) {
        cout << Colors::GREY << label << Colors::WHITE;
        coloredProgBar(50, progress);
        cout << "  \n";
    };

    const u64 elapsedMs = std::max<u64>(time.elapsed(), 1);

    cout << currentBoard.toString(pv.moves[0]) << "\n";

    printStat(" TT Size:      ", (transpositionTable.size + 1) * sizeof(Transposition) / 1024 / 1024, "MiB");
    printBar(" TT Usage:     ", transpositionTable.hashfull() / 1000.0f);
    cout << "\n";

    printStat(" Nodes:            ", suffixNum(threadData->nodes));
    printStat(" Time:             ", formatTime(elapsedMs));
    printStat(" Nodes per second: ", suffixNum(threadData->nodes * 1000 / elapsedMs));
    cout << "\n";

    printStat(" Depth:     ", depth);
    printStat(" Max depth: ", seldepth);

    cout << endl;

    cursor::clear();
    cout << Colors::GREY << " Score:   ";
    if (isDecisive(score))
        fmt::print(fmt::fg(score > 0 ? fmt::rgb(0, 255, 0) : fmt::rgb(255, 0, 0)), "M {}", std::copysign((MATE_SCORE - std::abs(score)) / 2 + 1, score));
    else
        printColoredScore(score);

    cout << endl;

    cursor::clear();
    cout << Colors::GREY << " PV line: ";
    printPV(pv);
    cout << "\n";

    cout << "\n";
    cout << " Best move history:" << "\n";
    for (const auto& m : moveHistory) {
        cout << "    " << Colors::GREY << formatTime(m.first) << Colors::WHITE << " -> " << m.second << "     \n";
    }

    cout << endl;
    searchLock.unlock();
}