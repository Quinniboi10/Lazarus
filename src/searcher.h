#pragma once

#include "search.h"
#include "thread.h"
#include "ttable.h"

#include <barrier>
#include <mutex>
#include <thread>

struct Searcher {
    TranspositionTable transposition_table;

    std::atomic<bool> stop_flag{true};
    std::vector<ThreadData> thread_data;
    std::vector<std::thread> threads;

    SearchParams sp;

    // Atomic probes to get information from the search
    std::mutex search_lock{};
    Board current_board{};
    usize depth{};
    usize seldepth{};
    i16 score{};
    PvList pv{};

    bool do_reporting;

    // Dictates if uci/pretty printing should be used, false by default
    bool do_uci;

    explicit Searcher(const bool do_reporting, const bool do_uci = false) {
        set_threads(1);
        this->do_reporting = do_reporting;
        this->do_uci       = do_uci;

        reset();
    }

    u64 total_nodes() const {
        u64 nodes = 0;
        for (const ThreadData& t : thread_data)
            nodes += t.nodes.load(std::memory_order_relaxed);
        return nodes;
    }

    void start(const Board& board, SearchParams sp);
    void stop();
    void wait_unit_done();

    void set_threads(usize n_threads);

    void resize_tt(const u64 new_size_mib) {
        transposition_table.reserve(new_size_mib);
        transposition_table.clear();
    }

    void reset() {
        transposition_table.clear();
        for (auto& t : thread_data)
            t.reset();
    }

    MoveEvaluation iterative_deepening(ThreadData& this_thread, Board board, SearchParams sp);

    void report_uci();
    void report_pretty();
};
