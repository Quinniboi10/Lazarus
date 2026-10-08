#include "search.h"

#include "config.h"
#include "constants.h"
#include "cursor.h"
#include "globals.h"
#include "movegen.h"
#include "movepicker.h"
#include "searcher.h"
#include "thread.h"

#include <cmath>

const auto lmr_table = []() {
    MultiArray<int, 2, MAX_PLY + 1, 219> lmr_table;
    for (int is_quiet = 0; is_quiet <= 1; is_quiet++) {
        for (usize depth = 0; depth <= MAX_PLY; depth++) {
            for (int moves_seen = 0; moves_seen <= 218; moves_seen++) {
                // Calculate reduction factor for late move reduction
                // Based on Weiss's formulas
                int& depth_reduction = lmr_table[is_quiet][depth][moves_seen];
                if (depth == 0 || moves_seen == 0) {
                    depth_reduction = 0;
                    continue;
                }
                if (is_quiet)
                    depth_reduction = LMR_QUIET_CONST + std::log(depth) * std::log(moves_seen) / LMR_QUIET_DIVISOR;
                else
                    depth_reduction = LMR_NOISY_CONST + std::log(depth) * std::log(moves_seen) / LMR_NOISY_DIVISOR;
            }
        }
    }
    return lmr_table;
}();

// Quiescence search
template<NodeType is_pv>
i16 qsearch(Board& board, const usize ply, i16 alpha, const i16 beta, ThreadData& this_thread) {
    const i16 static_eval = nnue.evaluate(board, this_thread);
    if (ply >= MAX_PLY)
        return static_eval;

    i16 best_score = static_eval;
    if (best_score >= beta)
        return best_score;
    if (best_score > alpha)
        alpha = best_score;

    i16 futility_score = best_score + QS_FUTILITY_MARGIN;

    Movepicker<NOISY_ONLY> picker(board, this_thread, Move::null());
    while (picker.has_next()) {
        const Move m = picker.get_next();

        if (!board.is_legal(m))
            continue;

        if (!board.see(m, 0))
            continue;

        if (!board.in_check() && board.is_capture(m) && futility_score <= alpha && !board.see(m, 1)) {
            best_score = std::max(best_score, futility_score);
            continue;
        }

        auto [new_board, thread_manager] = this_thread.make_move(board, m);
        this_thread.nodes.fetch_add(1, std::memory_order_relaxed);

        const i16 score = -qsearch<is_pv>(new_board, ply + 1, -beta, -alpha, this_thread);

        if (score >= beta)
            return score;
        if (score > best_score) {
            best_score = score;
            if (score > alpha)
                alpha = score;
        }
    }

    return best_score;
}
// Main search
template<NodeType is_pv>
i16 search(Board& board, i16 depth, const usize ply, i16 alpha, i16 beta, SearchStack* ss, ThreadData& this_thread, TranspositionTable& tt, SearchLimit& sl) {
    if (depth + static_cast<i16>(ply) > static_cast<i16>(MAX_PLY))
        depth = MAX_PLY - ply;
    if constexpr (is_pv)
        ss->pv.length = 0;
    if (ply > this_thread.seldepth)
        this_thread.seldepth = ply;
    if (board.is_draw() && ply > 0)
        return 0;
    if (depth <= 0)
        return qsearch<is_pv>(board, ply, alpha, beta, this_thread);

    // Mate distance pruning
    if (ply > 0) {
        alpha = std::max<i16>(alpha, -MATE_SCORE + ply);
        beta  = std::min<i16>(beta, MATE_SCORE - ply - 1);

        if (alpha >= beta)
            return alpha;
    }

    Move best_move = Move::null();
    i16 best_score = -INF_I16;

    i16 moves_seen     = 0;
    i16 moves_searched = 0;

    TTFlag tt_flag = FAIL_LOW;

    // TT probing
    Transposition& tt_entry = tt.get(board.full_hash);
    const bool tt_hit       = ss->excluded.is_null() && tt_entry.key == board.full_hash;

    if (!is_pv && tt_hit && tt_entry.depth >= depth
        && (tt_entry.flag == EXACT                                       // Exact score
            || (tt_entry.flag == BETA_CUTOFF && tt_entry.score >= beta)  // Lower bound, fail high
            || (tt_entry.flag == FAIL_LOW && tt_entry.score <= alpha)    // Upper bound, fail low
            )) {
        const i32& tt_score = tt_entry.score;
        if (is_loss(tt_score))
            return tt_score + ply;
        if (is_win(tt_score))
            return tt_score - ply;
        return tt_score;
    }

    ss->static_eval = this_thread.correct_static_eval(board, nnue.evaluate(board, this_thread));

    // Has the current position improving since last time stm played
    const bool improving = ss->static_eval > (ss - 2)->static_eval;

    // Pre-moveloop pruning
    if (!is_pv && ply > 0 && !board.in_check() && !is_loss(beta) && ss->excluded.is_null()) {
        // Reverse futility pruning
        const int rfp_margin = RFP_DEPTH_SCALAR * (depth - improving);
        if (ss->static_eval - rfp_margin >= beta && depth < 7)
            return ss->static_eval;

        // Null move pruning
        if (board.can_null_move() && ss->static_eval >= beta) {
            const i16 reduction = NMP_DEPTH_REDUCTION;

            auto [new_board, thread_manager] = this_thread.make_null_move(board);
            const i16 score                  = -search<NONPV>(new_board, depth - reduction, ply + 1, -beta, -beta + 1, ss + 1, this_thread, tt, sl);

            if (score >= beta)
                return score;
        }
    }

    bool skip_quiets = false;

    MoveList bad_quiets;
    MoveList bad_noises;

    Movepicker<ALL_MOVES> picker(board, this_thread, tt_hit ? tt_entry.move : Move::null());
    while (picker.has_next()) {
        // Check if the search has been aborted
        if (this_thread.break_flag.load(std::memory_order_relaxed))
            return best_score;
        if (sl.out_of_nodes(this_thread.nodes)) {
            this_thread.break_flag.store(true, std::memory_order_relaxed);
            return best_score;
        }
        if (this_thread.nodes % 2048 == 0 && sl.out_of_time()) {
            this_thread.break_flag.store(true, std::memory_order_relaxed);
            return best_score;
        }

        const Move m = picker.get_next();

        if (m == ss->excluded)
            continue;

        if (!board.is_legal(m))
            continue;

        if (board.is_quiet(m) && skip_quiets)
            continue;

        moves_seen++;

        // TT prefetching
        tt.prefetch(board.approx_key_after(m));

        // Moveloop pruning
        if (ply > 0 && !is_loss(best_score)) {
            // Futility pruning
            if (!board.in_check() && depth < 6 && board.is_quiet(m) && ss->static_eval + FUTILITY_PRUNING_MARGIN + FUTILITY_PRUNING_SCALAR * depth < alpha) {
                skip_quiets = true;
                continue;
            }

            // Late move pruning (LMP)
            if (!is_pv && !board.in_check() && moves_searched >= LMP_MIN_MOVES + depth * depth && depth <= LMP_MAX_DEPTH && board.is_quiet(m)) {
                skip_quiets = true;
                continue;
            }

            // SEE pruning
            const i32 see_threshold = board.is_quiet(m) ? -SEE_QUIET_SCALAR * depth * depth : -SEE_NOISY_SCALAR * depth;
            if (!board.see(m, see_threshold))
                continue;
        }

        moves_searched++;

        i32 extension = 0;
        // Singular extensions
        if (ply > 0 && depth >= SE_MIN_DEPTH && tt_hit && m == tt_entry.move && tt_entry.depth >= depth - 3 && tt_entry.flag != FAIL_LOW) {
            const i32 s_beta  = std::max(-INF_INT + 1, tt_entry.score - depth * 2);
            const i32 s_depth = (depth - 1) / 2;

            ss->excluded    = m;
            const i32 score = search<NONPV>(board, s_depth, ply, s_beta - 1, s_beta, ss, this_thread, tt, sl);
            ss->excluded    = Move::null();

            if (score < s_beta) {
                if (!is_pv && score < s_beta - SE_DOUBLE_MARGIN)
                    extension = 2;
                else
                    extension = 1;
            }
            // Negative extensions
            else if (tt_entry.score >= beta)
                extension = -2;
        }

        auto [new_board, thread_manager] = this_thread.make_move(board, m);
        this_thread.nodes.fetch_add(1, std::memory_order_relaxed);

        const i16 new_depth = depth - 1 + extension;

        // Principal variation search (PVS)
        i16 score = -INF_I16;
        if (depth >= 2 && moves_searched >= 5 + 2 * (ply == 0) && !new_board.in_check()) {
            // Late move reduction (LMR)
            const i16 depth_reduction = lmr_table[board.is_quiet(m)][depth][moves_searched] + !is_pv * LMR_NONPV;

            score = -search<NONPV>(new_board, new_depth - depth_reduction / 1024, ply + 1, -alpha - 1, -alpha, ss + 1, this_thread, tt, sl);

            if (score > alpha)
                score = -search<NONPV>(new_board, new_depth, ply + 1, -alpha - 1, -alpha, ss + 1, this_thread, tt, sl);
        }
        else if (!is_pv || moves_searched > 1)
            score = -search<NONPV>(new_board, new_depth, ply + 1, -alpha - 1, -alpha, ss + 1, this_thread, tt, sl);
        if (is_pv && (moves_searched == 1 || score > alpha))
            score = -search<PV>(new_board, new_depth, ply + 1, -beta, -alpha, ss + 1, this_thread, tt, sl);

        if (score > best_score) {
            best_score = score;
            if (best_score > alpha) {
                best_move = m;
                tt_flag   = EXACT;
                alpha     = best_score;
                if constexpr (is_pv)
                    ss->pv.update(m, (ss + 1)->pv);
            }
        }
        if (score >= beta) {
            tt_flag = BETA_CUTOFF;

            // Update histories
            const i32 history_bonus = (HIST_BONUS_A * depth * depth + HIST_BONUS_B * depth + HIST_BONUS_C) / 1024;
            if (board.is_quiet(m))
                this_thread.get_history(board, m).update(history_bonus);
            else
                this_thread.get_capture_history(board, m).update(history_bonus);
            for (const Move m : bad_quiets)
                this_thread.get_history(board, m).update(-history_bonus);
            for (const Move m : bad_noises)
                this_thread.get_capture_history(board, m).update(-history_bonus);

            break;
        }

        if (best_move != m) {
            if (board.is_quiet(m))
                bad_quiets.add(m);
            else
                bad_noises.add(m);
        }
    }

    // Checkmate/stalemate detection
    if (!moves_seen) {
        if (board.in_check()) {
            return -MATE_SCORE + static_cast<i16>(ply);
        }
        return 0;
    }

    // Adjust TT score for mates
    i16 tt_score = best_score;
    if (is_loss(best_score))
        tt_score = best_score - static_cast<i16>(ply);
    else if (is_win(best_score))
        tt_score = best_score + static_cast<i16>(ply);

    if (ss->excluded.is_null() && !this_thread.break_flag.load(std::memory_order_relaxed)) {
        // Update correction histories
        if (!board.in_check() && (board.is_quiet(best_move) || best_move.is_null())
            && (tt_flag == EXACT || tt_flag == BETA_CUTOFF && best_score > ss->static_eval || tt_flag == FAIL_LOW && best_score < ss->static_eval))
            this_thread.update_corrhist(board, depth, best_score, ss->static_eval);

        // Update TT
        const Transposition new_entry(board.full_hash, best_move, tt_flag, tt_score, depth);

        if (tt.should_replace(tt_entry, new_entry))
            tt_entry = new_entry;
    }

    return best_score;
}

MoveEvaluation Searcher::iterative_deepening(ThreadData& this_thread, Board board, SearchParams sp) {
    this_thread.break_flag.store(false);
    this_thread.nodes    = 0;
    this_thread.seldepth = 0;
    this_thread.refresh(board);
    const bool is_main = this_thread.type == ThreadType::MAIN;

    // Time management
    const i64 time = board.stm == WHITE ? sp.wtime : sp.btime;
    const i64 inc  = board.stm == WHITE ? sp.winc : sp.binc;

    i64 search_time = sp.mtime ? sp.mtime : (time / 20 + inc / 2);

    if (time != 0 || inc != 0)
        search_time = std::max<i64>(search_time - static_cast<i64>(MOVE_OVERHEAD), 1);

    const i64 soft_time = search_time * 0.6;

    // Create search limits, excluding time for depth 1
    SearchLimit depth_one_sl(sp.time, 0, sp.nodes);
    SearchLimit main_sl(sp.time, search_time, sp.nodes);

    // Create the search stack and clear it
    auto stack      = std::vector<SearchStack>(MAX_PLY + 3);
    SearchStack* ss = &stack[2];

    for (auto& ss : stack) {
        ss = SearchStack();
    }

    const usize search_depth = std::min(sp.depth, MAX_PLY);

    // Pretty printing
    if (is_main && do_reporting && !do_uci) {
        cursor::home();
        cursor::clear_all();

        cout << current_board.str() << "\n" << endl;
    }

    for (usize curr_depth = 1; curr_depth <= search_depth; curr_depth++) {
        SearchLimit& sl = curr_depth == 1 ? depth_one_sl : main_sl;

        const auto search_cancelled = [&]() {
            if (this_thread.type == ThreadType::MAIN)
                return sl.out_of_nodes(total_nodes()) || sl.out_of_time() || this_thread.break_flag.load(std::memory_order_relaxed);
            return this_thread.break_flag.load(std::memory_order_relaxed) || (sp.soft_nodes > 0 && total_nodes() > sp.soft_nodes);
        };

        i16 score;
        if (curr_depth < MIN_ASP_WINDOW_DEPTH)
            score = search<PV>(board, curr_depth, 0, -INF_I16, INF_I16, ss, this_thread, transposition_table, sl);
        else {
            int delta = INITIAL_ASP_WINDOW;

            while (!search_cancelled()) {
                const i16 alpha = std::max<i32>(this->score - delta, -INF_I16);
                const i16 beta  = std::min<i32>(this->score + delta, INF_I16);
                score           = search<PV>(board, curr_depth, 0, alpha, beta, ss, this_thread, transposition_table, sl);
                if (score <= alpha || score >= beta)
                    delta = ASP_WIDENING_FACTOR / 1024.0 * delta;
                else
                    break;
            }
        }


        // If depth 1 was searched, save its results
        if (curr_depth == 1) {
            search_lock.lock();
            this->depth    = 1;
            this->seldepth = this_thread.seldepth;
            this->score    = score;
            this->pv       = ss->pv;
            search_lock.unlock();
        }

        // If the search has been canceled, exit here to prevent saving partial data
        if (search_cancelled())
            break;

        search_lock.lock();
        this->depth    = curr_depth;
        this->seldepth = this_thread.seldepth;
        this->score    = score;
        this->pv       = ss->pv;
        search_lock.unlock();


        if (is_main && do_reporting) {
            if (do_uci)
                report_uci();
            else
                report_pretty();
        }

        if (is_main) {
            // Soft nodes
            if (sp.soft_nodes > 0 && total_nodes() > sp.soft_nodes)
                break;
            // Go mate
            if (sp.mate > 0 && MATE_SCORE - std::abs(score) / 2 + 1 <= sp.mate)
                break;
            // Soft TM
            if (soft_time > 0 && static_cast<i64>(sp.time.elapsed()) >= soft_time)
                break;
        }
    }

    if (is_main && do_reporting && do_uci) {
        cout << "info nodes " << total_nodes() << endl;
        cout << "bestmove " << this->pv.moves[0] << endl;
    }

    this_thread.break_flag.store(true, std::memory_order_relaxed);

    return {this->pv.moves[0], this->score};
}

void bench() {
    u64 total_nodes      = 0;
    double total_time_ms = 0.0;

    cout << "Starting benchmark with depth " << BENCH_DEPTH << endl;

    const array<string, 50> fens = {"r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
                                    "4rrk1/2p1b1p1/p1p3q1/4p3/2P2n1p/1P1NR2P/PB3PP1/3R1QK1 b - - 2 24",
                                    "r3qbrk/6p1/2b2pPp/p3pP1Q/PpPpP2P/3P1B2/2PB3K/R5R1 w - - 16 42",
                                    "6k1/1R3p2/6p1/2Bp3p/3P2q1/P7/1P2rQ1K/5R2 b - - 4 44",
                                    "8/8/1p2k1p1/3p3p/1p1P1P1P/1P2PK2/8/8 w - - 3 54",
                                    "7r/2p3k1/1p1p1qp1/1P1Bp3/p1P2r1P/P7/4R3/Q4RK1 w - - 0 36",
                                    "r1bq1rk1/pp2b1pp/n1pp1n2/3P1p2/2P1p3/2N1P2N/PP2BPPP/R1BQ1RK1 b - - 2 10",
                                    "3r3k/2r4p/1p1b3q/p4P2/P2Pp3/1B2P3/3BQ1RP/6K1 w - - 3 87",
                                    "2r4r/1p4k1/1Pnp4/3Qb1pq/8/4BpPp/5P2/2RR1BK1 w - - 0 42",
                                    "4q1bk/6b1/7p/p1p4p/PNPpP2P/KN4P1/3Q4/4R3 b - - 0 37",
                                    "2q3r1/1r2pk2/pp3pp1/2pP3p/P1Pb1BbP/1P4Q1/R3NPP1/4R1K1 w - - 2 34",
                                    "1r2r2k/1b4q1/pp5p/2pPp1p1/P3Pn2/1P1B1Q1P/2R3P1/4BR1K b - - 1 37",
                                    "r3kbbr/pp1n1p1P/3ppnp1/q5N1/1P1pP3/P1N1B3/2P1QP2/R3KB1R b KQkq b3 0 17",
                                    "8/6pk/2b1Rp2/3r4/1R1B2PP/P5K1/8/2r5 b - - 16 42",
                                    "1r4k1/4ppb1/2n1b1qp/pB4p1/1n1BP1P1/7P/2PNQPK1/3RN3 w - - 8 29",
                                    "8/p2B4/PkP5/4p1pK/4Pb1p/5P2/8/8 w - - 29 68",
                                    "3r4/ppq1ppkp/4bnp1/2pN4/2P1P3/1P4P1/PQ3PBP/R4K2 b - - 2 20",
                                    "5rr1/4n2k/4q2P/P1P2n2/3B1p2/4pP2/2N1P3/1RR1K2Q w - - 1 49",
                                    "1r5k/2pq2p1/3p3p/p1pP4/4QP2/PP1R3P/6PK/8 w - - 1 51",
                                    "q5k1/5ppp/1r3bn1/1B6/P1N2P2/BQ2P1P1/5K1P/8 b - - 2 34",
                                    "r1b2k1r/5n2/p4q2/1ppn1Pp1/3pp1p1/NP2P3/P1PPBK2/1RQN2R1 w - - 0 22",
                                    "r1bqk2r/pppp1ppp/5n2/4b3/4P3/P1N5/1PP2PPP/R1BQKB1R w KQkq - 0 5",
                                    "r1bqr1k1/pp1p1ppp/2p5/8/3N1Q2/P2BB3/1PP2PPP/R3K2n b Q - 1 12",
                                    "r1bq2k1/p4r1p/1pp2pp1/3p4/1P1B3Q/P2B1N2/2P3PP/4R1K1 b - - 2 19",
                                    "r4qk1/6r1/1p4p1/2ppBbN1/1p5Q/P7/2P3PP/5RK1 w - - 2 25",
                                    "r7/6k1/1p6/2pp1p2/7Q/8/p1P2K1P/8 w - - 0 32",
                                    "r3k2r/ppp1pp1p/2nqb1pn/3p4/4P3/2PP4/PP1NBPPP/R2QK1NR w KQkq - 1 5",
                                    "3r1rk1/1pp1pn1p/p1n1q1p1/3p4/Q3P3/2P5/PP1NBPPP/4RRK1 w - - 0 12",
                                    "5rk1/1pp1pn1p/p3Brp1/8/1n6/5N2/PP3PPP/2R2RK1 w - - 2 20",
                                    "8/1p2pk1p/p1p1r1p1/3n4/8/5R2/PP3PPP/4R1K1 b - - 3 27",
                                    "8/4pk2/1p1r2p1/p1p4p/Pn5P/3R4/1P3PP1/4RK2 w - - 1 33",
                                    "8/5k2/1pnrp1p1/p1p4p/P6P/4R1PK/1P3P2/4R3 b - - 1 38",
                                    "8/8/1p1kp1p1/p1pr1n1p/P6P/1R4P1/1P3PK1/1R6 b - - 15 45",
                                    "8/8/1p1k2p1/p1prp2p/P2n3P/6P1/1P1R1PK1/4R3 b - - 5 49",
                                    "8/8/1p4p1/p1p2k1p/P2n1P1P/4K1P1/1P6/3R4 w - - 6 54",
                                    "8/8/1p4p1/p1p2k1p/P2n1P1P/4K1P1/1P6/6R1 b - - 6 59",
                                    "8/5k2/1p4p1/p1pK3p/P2n1P1P/6P1/1P6/4R3 b - - 14 63",
                                    "8/1R6/1p1K1kp1/p6p/P1p2P1P/6P1/1Pn5/8 w - - 0 67",
                                    "1rb1rn1k/p3q1bp/2p3p1/2p1p3/2P1P2N/PP1RQNP1/1B3P2/4R1K1 b - - 4 23",
                                    "4rrk1/pp1n1pp1/q5p1/P1pP4/2n3P1/7P/1P3PB1/R1BQ1RK1 w - - 3 22",
                                    "r2qr1k1/pb1nbppp/1pn1p3/2ppP3/3P4/2PB1NN1/PP3PPP/R1BQR1K1 w - - 4 12",
                                    "2r2k2/8/4P1R1/1p6/8/P4K1N/7b/2B5 b - - 0 55",
                                    "6k1/5pp1/8/2bKP2P/2P5/p4PNb/B7/8 b - - 1 44",
                                    "2rqr1k1/1p3p1p/p2p2p1/P1nPb3/2B1P3/5P2/1PQ2NPP/R1R4K w - - 3 25",
                                    "r1b2rk1/p1q1ppbp/6p1/2Q5/8/4BP2/PPP3PP/2KR1B1R b - - 2 14",
                                    "6r1/5k2/p1b1r2p/1pB1p1p1/1Pp3PP/2P1R1K1/2P2P2/3R4 w - - 1 36",
                                    "rnbqkb1r/pppppppp/5n2/8/2PP4/8/PP2PPPP/RNBQKBNR b KQkq c3 0 2",
                                    "2rr2k1/1p4bp/p1q1p1p1/4Pp1n/2PB4/1PN3P1/P3Q2P/2RR2K1 w - f6 0 20",
                                    "3br1k1/p1pn3p/1p3n2/5pNq/2P1p3/1PN3PP/P2Q1PB1/4R1K1 w - - 0 23",
                                    "2r2b2/5p2/5k2/p1r1pP2/P2pB3/1P3P2/K1P3R1/7R w - - 23 93"};

    for (auto fen : fens) {
        if (fen.empty())
            continue;  // Skip empty lines

        Board board;
        board.reset();

        board.load_fen(fen);

        // Set up for iterative deepening
        Stopwatch<std::chrono::milliseconds> time;

        Searcher searcher(false);
        searcher.start(board, SearchParams(time, BENCH_DEPTH, 0, 0, 0, 0, 0, 0, 0, 0));
        searcher.wait_unit_done();

        const u64 duration_ms = time.elapsed();

        total_nodes += searcher.total_nodes();
        total_time_ms += duration_ms;

        cout << "FEN: " << fen << endl;
        cout << "Nodes: " << format_num(searcher.total_nodes()) << ", Time: " << format_time(duration_ms) << endl;
        cout << "----------------------------------------" << endl;
    }

    cout << "Benchmark Completed." << endl;
    cout << "Total Nodes: " << format_num(total_nodes) << endl;
    cout << "Total Time: " << format_time(total_time_ms) << endl;
    usize nps = 0;
    if (total_time_ms > 0) {
        nps = total_nodes / total_time_ms * 1000;
        cout << "Average NPS: " << format_num(nps) << endl;
    }
    cout << total_nodes << " nodes " << nps << " nps" << endl;
}