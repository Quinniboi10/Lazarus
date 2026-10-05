#pragma once

#include "board.h"
#include "config.h"
#include "stopwatch.h"
#include "types.h"

#include <cstring>
#include <thread>

struct ThreadData;
struct ThreadStackManager;

struct SearchStack {
    PvList pv{};
    Move excluded = Move::null();
    i16 static_eval{};

    SearchStack()                         = default;
    SearchStack(const SearchStack& other) = default;
    ~SearchStack()                        = default;
};

enum class ThreadType {
    MAIN      = 1,
    SECONDARY = 0
};
enum NodeType {
    NONPV,
    PV
};

struct SearchParams {
    Stopwatch<std::chrono::milliseconds> time;

    usize depth;
    u64 nodes;
    u64 soft_nodes;
    u64 mtime;
    u64 wtime;
    u64 btime;
    u64 winc;
    u64 binc;
    usize mate;

    SearchParams() = default;

    SearchParams(const Stopwatch<std::chrono::milliseconds>& time,
                 const usize depth,
                 const u64 nodes,
                 const u64 soft_nodes,
                 const u64 mtime,
                 const u64 wtime,
                 const u64 btime,
                 const u64 winc,
                 const u64 binc,
                 const usize mate) :
        time(time),
        depth(depth),
        nodes(nodes),
        soft_nodes(soft_nodes),
        mtime(mtime),
        wtime(wtime),
        btime(btime),
        winc(winc),
        binc(binc),
        mate(mate) {
    }
};

struct SearchLimit {
    Stopwatch<std::chrono::milliseconds>& time;
    u64 max_nodes;
    i64 search_time;

    SearchLimit(auto& time, auto search_time, auto max_nodes) :
        time(time),
        max_nodes(max_nodes),
        search_time(search_time) {
    }

    bool out_of_nodes(const u64 nodes) const {
        return nodes >= max_nodes && max_nodes > 0;
    }

    bool out_of_time() const {
        if (search_time == 0)
            return false;
        return static_cast<i64>(time.elapsed()) >= search_time;
    }
};

constexpr i16 MATE_SCORE       = 32500;
constexpr i16 MATE_IN_MAX_PLY  = MATE_SCORE - MAX_PLY;
constexpr i16 MATED_IN_MAX_PLY = -MATE_SCORE + static_cast<i32>(MAX_PLY);

inline bool is_win(const i16 score) {
    return score >= MATE_IN_MAX_PLY;
}
inline bool is_loss(const i16 score) {
    return score <= MATED_IN_MAX_PLY;
}
inline bool is_decisive(const i16 score) {
    return is_win(score) || is_loss(score);
}

void bench();