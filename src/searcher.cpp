#include "searcher.h"
#include "cursor.h"
#include "globals.h"
#include "search.h"
#include "types.h"
#include "wdl.h"

void Searcher::start(const Board& board, const SearchParams sp) {
    stop();

    this->sp = sp;
    search_lock.lock();
    this->current_board = board;
    this->depth         = 0;
    this->seldepth      = 0;
    this->score         = 0;
    this->pv.length     = 0;
    search_lock.unlock();

    stop_flag.store(false, std::memory_order_relaxed);

    for (usize i = thread_data.size(); i > 0; i--)
        threads.emplace_back(&Searcher::iterative_deepening, this, std::ref(thread_data[i - 1]), board, sp);
}

void Searcher::stop() {
    stop_flag.store(true, std::memory_order_relaxed);

    wait_unit_done();

    threads.clear();
}

void Searcher::wait_unit_done() {
    for (auto& t : threads)
        if (t.joinable())
            t.join();
}

void Searcher::set_threads(const usize n_threads) {
    thread_data.clear();
    thread_data.emplace_back(ThreadType::MAIN, stop_flag);

    for (usize i = 1; i < n_threads; i++)
        thread_data.emplace_back(ThreadType::SECONDARY, stop_flag);
}

void Searcher::report_uci() {
    search_lock.lock();

    const u64 time  = std::max<u64>(sp.time.elapsed(), 1);
    const u64 nodes = total_nodes();

    fmt::print("info depth {} seldepth {} time {} nodes {} nps {} hashfull {}", depth, thread_data[0].seldepth, time, nodes, nodes * 1000 / time, transposition_table.hashfull());

    fmt::print(" score ");

    if (is_decisive(score))
        fmt::print("mate {}", std::copysign((MATE_SCORE - std::abs(score)) / 2 + 1, score));
    else
        fmt::print("cp {}", scale_eval(score, current_board));

    const auto [w, d, l] = get_wdl(current_board, score);
    fmt::print(" wdl {} {} {}", w, d, l);

    fmt::print(" pv");
    for (const Move m : pv)
        cout << " " << m;

    cout << endl;
    search_lock.unlock();
}

void Searcher::report_pretty() {
    search_lock.lock();

    const u64 time  = std::max<u64>(sp.time.elapsed(), 1);
    const u64 nodes = total_nodes();

    cursor::cache();
    cursor::home();
    cout << current_board.str(pv.moves[0]);
    cursor::load();

    // Depth
    fmt::print(fmt::fg(fmt::color::light_gray) | fmt::emphasis::bold, " {:<8} ", fmt::format("{}/{}", depth, seldepth));

    // Time
    fmt::print(fmt::fg(fmt::color::gray), "{:>10}    ", format_time(time));

    // Nodes
    fmt::print(fmt::fg(fmt::color::gray), "{:>20}    ", fmt::format("{} nodes", format_num(nodes)));

    // Speed
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", fmt::format("{} knps", format_num(nodes / time)));

    // TT
    fmt::print(fmt::fg(fmt::rgb(105, 200, 215)), "TT: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>6}    ", fmt::format("{:.1f}%", transposition_table.hashfull() / 10.0));

    // WDL
    const auto [w, d, l] = get_wdl(current_board, score);
    fmt::print(fmt::fg(fmt::rgb(105, 215, 105)), "W: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>6}    ", fmt::format("{:.1f}%", w / 10.0));
    fmt::print(fmt::fg(fmt::rgb(155, 155, 155)), "D: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>6}    ", fmt::format("{:.1f}%", d / 10.0));
    fmt::print(fmt::fg(fmt::rgb(215, 105, 105)), "L: ");
    fmt::print(fmt::fg(fmt::color::gray), "{:>6}    ", fmt::format("{:.1f}%", l / 10.0));

    // Score
    fmt::print(fmt::fg(fmt::color::gray), "{:>12}    ", get_colored_score(scale_eval(score, current_board)));

    // PV
    fmt::print("{}", get_pretty_pv(pv));

    cout << endl;
    search_lock.unlock();
}