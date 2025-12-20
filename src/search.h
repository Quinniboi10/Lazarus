#pragma once

#include "board.h"
#include "config.h"
#include "stopwatch.h"
#include "types.h"

#include <cstring>
#include <thread>

struct Searcher;

namespace Search {
struct ThreadInfo;
struct ThreadStackManager;

struct SearchStack {
    PvList pv;
};

enum class ThreadType { MAIN = 1, SECONDARY = 0 };
enum NodeType { NONPV, PV };

struct SearchParams {
    Stopwatch<std::chrono::milliseconds> time;

    usize depth;
    u64   nodes;
    u64   softNodes;
    u64   mtime;
    u64   wtime;
    u64   btime;
    u64   winc;
    u64   binc;
    usize mate;

    SearchParams(const Stopwatch<std::chrono::milliseconds>& time, const usize depth, const u64 nodes, const u64 softNodes, const u64 mtime, const u64 wtime, const u64 btime, const u64 winc, const u64 binc, const usize mate) :
        time(time),
        depth(depth),
        nodes(nodes),
        softNodes(softNodes),
        mtime(mtime),
        wtime(wtime),
        btime(btime),
        winc(winc),
        binc(binc),
        mate(mate) {}
};

struct SearchLimit {
    Stopwatch<std::chrono::milliseconds>& time;
    u64                                   maxNodes;
    i64                                   searchTime;

    SearchLimit(auto& time, auto searchTime, auto maxNodes) :
        time(time),
        maxNodes(maxNodes),
        searchTime(searchTime) {}

    bool outOfNodes(const u64 nodes) const {
        return nodes >= maxNodes && maxNodes > 0;
    }

    bool outOfTime() const {
        if (searchTime == 0)
            return false;
        return static_cast<i64>(time.elapsed()) >= searchTime;
    }
};

constexpr i32 MATE_SCORE       = 32767;
constexpr i32 MATE_IN_MAX_PLY  = MATE_SCORE - MAX_PLY;
constexpr i32 MATED_IN_MAX_PLY = -MATE_SCORE + static_cast<i32>(MAX_PLY);

inline bool isWin(const i32 score) {
    return score >= MATE_IN_MAX_PLY;
}
inline bool isLoss(const i32 score) {
    return score <= MATED_IN_MAX_PLY;
}
inline bool isDecisive(const i32 score) {
    return isWin(score) || isLoss(score);
}

MoveEvaluation iterativeDeepening(Board board, ThreadInfo& thisThread, SearchParams sp, Searcher* searcher = nullptr);

void bench();
}