#pragma once

#include "board.h"
#include "config.h"
#include "stopwatch.h"
#include "types.h"

#include <cstring>
#include <thread>

struct ThreadInfo;
struct ThreadStackManager;

struct SearchStack {
    PvList pv{};
    i16    staticEval{};

    SearchStack()                         = default;
    SearchStack(const SearchStack& other) = default;
    ~SearchStack()                        = default;
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

constexpr i16 MATE_SCORE       = 32500;
constexpr i16 MATE_IN_MAX_PLY  = MATE_SCORE - MAX_PLY;
constexpr i16 MATED_IN_MAX_PLY = -MATE_SCORE + static_cast<i32>(MAX_PLY);

inline bool isWin(const i16 score) {
    return score >= MATE_IN_MAX_PLY;
}
inline bool isLoss(const i16 score) {
    return score <= MATED_IN_MAX_PLY;
}
inline bool isDecisive(const i16 score) {
    return isWin(score) || isLoss(score);
}

void bench();