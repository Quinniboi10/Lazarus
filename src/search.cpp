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

const auto lmrTable = []() {
    MultiArray<int, 2, MAX_PLY + 1, 219> lmrTable;
    for (int isQuiet = 0; isQuiet <= 1; isQuiet++) {
        for (usize depth = 0; depth <= MAX_PLY; depth++) {
            for (int movesSeen = 0; movesSeen <= 218; movesSeen++) {
                // Calculate reduction factor for late move reduction
                // Based on Weiss's formulas
                int& depthReduction = lmrTable[isQuiet][depth][movesSeen];
                if (depth == 0 || movesSeen == 0) {
                    depthReduction = 0;
                    continue;
                }
                if (isQuiet)
                    depthReduction = LMR_QUIET_CONST + std::log(depth) * std::log(movesSeen) / LMR_QUIET_DIVISOR;
                else
                    depthReduction = LMR_NOISY_CONST + std::log(depth) * std::log(movesSeen) / LMR_NOISY_DIVISOR;
            }
        }
    }
    return lmrTable;
}();

// Quiescence search
template<NodeType isPV>
i16 qsearch(Board& board, const usize ply, i16 alpha, const i16 beta, ThreadData& thisThread) {
    const i16 staticEval = nnue.evaluate(board, thisThread);
    if (ply >= MAX_PLY)
        return staticEval;

    i16 bestScore = staticEval;
    if (bestScore >= beta)
        return bestScore;
    if (bestScore > alpha)
        alpha = bestScore;

    i16 futilityScore = bestScore + QS_FUTILITY_MARGIN;

    Movepicker<NOISY_ONLY> picker(board, thisThread, Move::null());
    while (picker.hasNext()) {
        const Move m = picker.getNext();

        if (!board.isLegal(m))
            continue;

        if (!board.see(m, 0))
            continue;

        if (!board.inCheck() && board.isCapture(m) && futilityScore <= alpha && !board.see(m, 1)) {
            bestScore = std::max(bestScore, futilityScore);
            continue;
        }

        auto [newBoard, threadManager] = thisThread.makeMove(board, m);
        thisThread.nodes.fetch_add(1, std::memory_order_relaxed);

        const i16 score = -qsearch<isPV>(newBoard, ply + 1, -beta, -alpha, thisThread);

        if (score >= beta)
            return score;
        if (score > bestScore) {
            bestScore = score;
            if (score > alpha)
                alpha = score;
        }
    }

    return bestScore;
}
// Main search
template<NodeType isPV>
i16 search(Board& board, i16 depth, const usize ply, i16 alpha, i16 beta, SearchStack* ss, ThreadData& thisThread, TranspositionTable& tt, SearchLimit& sl) {
    if (depth + static_cast<i16>(ply) > static_cast<i16>(MAX_PLY))
        depth = MAX_PLY - ply;
    if constexpr (isPV)
        ss->pv.length = 0;
    if (ply > thisThread.seldepth)
        thisThread.seldepth = ply;
    if (board.isDraw() && ply > 0)
        return 0;
    if (depth <= 0)
        return qsearch<isPV>(board, ply, alpha, beta, thisThread);

    // Mate distance pruning
    if (ply > 0) {
        alpha = std::max<i16>(alpha, -MATE_SCORE + ply);
        beta  = std::min<i16>(beta, MATE_SCORE - ply - 1);

        if (alpha >= beta)
            return alpha;
    }

    Move bestMove  = Move::null();
    i16  bestScore = -INF_I16;

    i16 movesSeen     = 0;
    i16 movesSearched = 0;

    TTFlag ttFlag = FAIL_LOW;

    // TT probing
    Transposition& ttEntry = tt.getEntry(board.zobrist);
    const bool     ttHit   = ss->excluded.isNull() && ttEntry.key == board.zobrist;

    if (!isPV && ttHit && ttEntry.depth >= depth &&
        (ttEntry.flag == EXACT                                      // Exact score
         || (ttEntry.flag == BETA_CUTOFF && ttEntry.score >= beta)  // Lower bound, fail high
         || (ttEntry.flag == FAIL_LOW && ttEntry.score <= alpha)    // Upper bound, fail low
         )) {
        const i32& ttScore = ttEntry.score;
        if (isLoss(ttScore))
            return ttScore + ply;
        else if (isWin(ttScore))
            return ttScore - ply;
        return ttScore;
    }

    ss->staticEval = nnue.evaluate(board, thisThread);

    // Has the current position improving since last time stm played
    const bool improving = ss->staticEval > (ss - 2)->staticEval;

    // Pre-moveloop pruning
    if (!isPV && ply > 0 && !board.inCheck() && !isLoss(beta) && ss->excluded.isNull()) {
        // Reverse futility pruning
        const int rfpMargin = RFP_DEPTH_SCALAR * (depth - improving);
        if (ss->staticEval - rfpMargin >= beta && depth < 7)
            return ss->staticEval;

        // Null move pruning
        if (board.canNullMove() && ss->staticEval >= beta) {
            const i16 reduction = NMP_DEPTH_REDUCTION;

            auto [newBoard, threadManager] = thisThread.makeNullMove(board);
            const i16 score                = -search<NONPV>(newBoard, depth - reduction, ply + 1, -beta, -beta + 1, ss + 1, thisThread, tt, sl);

            if (score >= beta)
                return score;
        }
    }

    bool skipQuiets = false;

    MoveList badQuiets{};
    MoveList badNoisies{};

    Movepicker<ALL_MOVES> picker(board, thisThread, ttHit ? ttEntry.move : Move::null());
    while (picker.hasNext()) {
        // Check if the search has been aborted
        if (thisThread.breakFlag.load(std::memory_order_relaxed))
            return bestScore;
        if (sl.outOfNodes(thisThread.nodes)) {
            thisThread.breakFlag.store(true, std::memory_order_relaxed);
            return bestScore;
        }
        if (thisThread.nodes % 2048 == 0 && sl.outOfTime()) {
            thisThread.breakFlag.store(true, std::memory_order_relaxed);
            return bestScore;
        }

        const Move m = picker.getNext();

        if (m == ss->excluded)
            continue;

        if (!board.isLegal(m))
            continue;

        if (board.isQuiet(m) && skipQuiets)
            continue;

        movesSeen++;

        // TT prefetching
        tt.prefetch(board.roughKeyAfter(m));

        // Moveloop pruning
        if (ply > 0 && !isLoss(bestScore)) {
            // Futility pruning
            if (!board.inCheck() && depth < 6 && board.isQuiet(m) && ss->staticEval + FUTILITY_PRUNING_MARGIN + FUTILITY_PRUNING_SCALAR * depth < alpha) {
                skipQuiets = true;
                continue;
            }

            // SEE pruning
            const i32 seeThreshold = board.isQuiet(m) ? -SEE_QUIET_SCALAR * depth * depth : -SEE_NOISY_SCALAR * depth;
            if (!board.see(m, seeThreshold))
                continue;
        }

        movesSearched++;

        i32 extension = 0;
        // Singular extensions
        if (ply > 0 && depth >= SE_MIN_DEPTH && ttHit && m == ttEntry.move && ttEntry.depth >= depth - 3 && ttEntry.flag != FAIL_LOW) {
            const i32 sBeta  = std::max(-INF_INT + 1, ttEntry.score - depth * 2);
            const i32 sDepth = (depth - 1) / 2;

            ss->excluded    = m;
            const i32 score = search<NONPV>(board, sDepth, ply, sBeta - 1, sBeta, ss, thisThread, tt, sl);
            ss->excluded    = Move::null();

            if (score < sBeta) {
                if (!isPV && score < sBeta - SE_DOUBLE_MARGIN)
                    extension = 2;
                else
                    extension = 1;
            }
            // Negative extensions
            else if (ttEntry.score >= beta)
                extension = -2;
        }

        auto [newBoard, threadManager] = thisThread.makeMove(board, m);
        thisThread.nodes.fetch_add(1, std::memory_order_relaxed);

        const i16 newDepth = depth - 1 + extension;

        // Principal variation search (PVS)
        i16 score = -INF_I16;
        if (depth >= 2 && movesSearched >= 5 + 2 * (ply == 0) && !newBoard.inCheck()) {
            // Late move reduction (LMR)
            const i16 depthReduction = lmrTable[board.isQuiet(m)][depth][movesSearched] + !isPV * LMR_NONPV;

            score = -search<NONPV>(newBoard, newDepth - depthReduction / 1024, ply + 1, -alpha - 1, -alpha, ss + 1, thisThread, tt, sl);

            if (score > alpha)
                score = -search<NONPV>(newBoard, newDepth, ply + 1, -alpha - 1, -alpha, ss + 1, thisThread, tt, sl);
        }
        else if (!isPV || movesSearched > 1)
            score = -search<NONPV>(newBoard, newDepth, ply + 1, -alpha - 1, -alpha, ss + 1, thisThread, tt, sl);
        if (isPV && (movesSearched == 1 || score > alpha))
            score = -search<PV>(newBoard, newDepth, ply + 1, -beta, -alpha, ss + 1, thisThread, tt, sl);

        if (score > bestScore) {
            bestScore = score;
            if (bestScore > alpha) {
                bestMove = m;
                ttFlag   = EXACT;
                alpha    = bestScore;
                if constexpr (isPV)
                    ss->pv.update(m, (ss + 1)->pv);
            }
        }
        if (score >= beta) {
            ttFlag = BETA_CUTOFF;

            // Update histories
            const i32 historyBonus = (HIST_BONUS_A * depth * depth + HIST_BONUS_B * depth + HIST_BONUS_C) / 1024;
            if (board.isQuiet(m))
                thisThread.getHistory(board, m).update(historyBonus);
            else
                thisThread.getCaptureHistory(board, m).update(historyBonus);
            for (const Move badQuiet : badQuiets)
                thisThread.getHistory(board, badQuiet).update(-historyBonus);
            for (const Move badNoisy : badNoisies)
                thisThread.getCaptureHistory(board, badNoisy).update(-historyBonus);

            break;
        }

        if (bestMove != m) {
            if (board.isQuiet(m))
                badQuiets.add(m);
            else
                badNoisies.add(m);
        }
    }

    // Checkmate/stalemate detection
    if (!movesSeen) {
        if (board.inCheck()) {
            return -MATE_SCORE + static_cast<i16>(ply);
        }
        return 0;
    }

    // Adjust TT score for mates
    i16 ttScore = bestScore;
    if (isLoss(bestScore))
        ttScore = bestScore - static_cast<i16>(ply);
    else if (isWin(bestScore))
        ttScore = bestScore + static_cast<i16>(ply);

    if (ss->excluded.isNull() && !thisThread.breakFlag.load(std::memory_order_relaxed)) {
        const Transposition newEntry(board.zobrist, bestMove, ttFlag, ttScore, depth);

        if (tt.shouldReplace(ttEntry, newEntry))
            ttEntry = newEntry;
    }

    return bestScore;
}

MoveEvaluation Searcher::iterativeDeepening(ThreadData& thisThread, Board board) {
    thisThread.nodes    = 0;
    thisThread.seldepth = 0;
    thisThread.refresh(board);
    const bool isMain = thisThread.type == ThreadType::MAIN;

    // Time management
    const i64 time = board.stm == WHITE ? sp.wtime : sp.btime;
    const i64 inc  = board.stm == WHITE ? sp.winc : sp.binc;

    i64 searchTime = sp.mtime ? sp.mtime : (time / 20 + inc / 2);

    if (time != 0 || inc != 0)
        searchTime = std::max<i64>(searchTime - static_cast<i64>(MOVE_OVERHEAD), 1);

    const i64 softTime = searchTime * 0.6;

    // Create search limits, excluding time for depth 1
    SearchLimit depthOneSl(sp.time, 0, sp.nodes);
    SearchLimit mainSl(sp.time, searchTime, sp.nodes);

    // Create the search stack and clear it
    auto         stack = std::vector<SearchStack>(MAX_PLY + 3);
    SearchStack* ss    = &stack[2];

    for (auto& ss : stack) {
        ss = SearchStack();
    }

    const usize searchDepth = std::min(sp.depth, MAX_PLY);

    auto countNodes = [&]() -> u64 { return thisThread.nodes; };

    // Pretty printing
    if (isMain && doReporting && !doUci) {
        cursor::home();
        cursor::clearAll();

        cout << currentBoard.toString() << "\n" << endl;
    }

    for (usize currDepth = 1; currDepth <= searchDepth; currDepth++) {
        SearchLimit& sl = currDepth == 1 ? depthOneSl : mainSl;

        const auto searchCancelled = [&]() {
            if (thisThread.type == ThreadType::MAIN)
                return sl.outOfNodes(countNodes()) || sl.outOfTime() || thisThread.breakFlag.load(std::memory_order_relaxed);
            return thisThread.breakFlag.load(std::memory_order_relaxed) || (sp.softNodes > 0 && countNodes() > sp.softNodes);
        };

        const i16 score = search<PV>(board, currDepth, 0, -MATE_SCORE, MATE_SCORE, ss, thisThread, transpositionTable, sl);

        const bool cancelled = searchCancelled();

        if (currDepth > 1 && cancelled)
            break;

        if (isMain) {
            searchLock.lock();

            this->depth    = currDepth;
            this->seldepth = thisThread.seldepth;
            this->score    = score;
            this->pv       = ss->pv;

            const auto bestMove = ss->pv.moves[0];
            if (currDepth == 1 || this->moveHistory.back().second != bestMove)
                this->moveHistory.push({ sp.time.elapsed(), bestMove });
            searchLock.unlock();
        }

        if (currDepth == 1 && cancelled)
            break;

        if (isMain) {
            if (doReporting) {
                if (doUci)
                    reportUci();
                else
                    reportPrettyPrint();
            }

            // Soft nodes
            if (sp.softNodes > 0 && countNodes() > sp.softNodes)
                break;
            // Go mate
            if (sp.mate > 0 && MATE_SCORE - std::abs(score) / 2 + 1 <= sp.mate)
                break;
            // Soft TM
            if (softTime > 0 && static_cast<i64>(sp.time.elapsed()) >= softTime)
                break;
        }
    }

    if (isMain) {
        if (doReporting && doUci) {
            cout << "info nodes " << countNodes() << endl;
            cout << "bestmove " << this->pv.moves[0] << endl;
        }
        thisThread.breakFlag.store(true, std::memory_order_relaxed);
    }

    return { this->pv.moves[0], this->score };
}

void bench() {
    u64    totalNodes  = 0;
    double totalTimeMs = 0.0;

    cout << "Starting benchmark with depth " << BENCH_DEPTH << endl;

    const array<string, 50> fens = { "r3k2r/2pb1ppp/2pp1q2/p7/1nP1B3/1P2P3/P2N1PPP/R2QK2R w KQkq a6 0 14",
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
                                     "2r2b2/5p2/5k2/p1r1pP2/P2pB3/1P3P2/K1P3R1/7R w - - 23 93" };

    for (auto fen : fens) {
        if (fen.empty())
            continue;  // Skip empty lines

        Board board;
        board.reset();

        board.loadFromFEN(fen);

        // Set up for iterative deepening
        Stopwatch<std::chrono::milliseconds> time;

        Searcher searcher(false);
        searcher.start(board, SearchParams(time, BENCH_DEPTH, 0, 0, 0, 0, 0, 0, 0, 0));
        searcher.waitUntilFinished();

        const u64 durationMs = time.elapsed();

        totalNodes += searcher.threadData[0].nodes;
        totalTimeMs += durationMs;

        cout << "FEN: " << fen << endl;
        cout << "Nodes: " << formatNum(searcher.threadData[0].nodes) << ", Time: " << formatTime(durationMs) << endl;
        cout << "----------------------------------------" << endl;
    }

    cout << "Benchmark Completed." << endl;
    cout << "Total Nodes: " << formatNum(totalNodes) << endl;
    cout << "Total Time: " << formatTime(totalTimeMs) << endl;
    usize nps = 0;
    if (totalTimeMs > 0) {
        nps = totalNodes / totalTimeMs * 1000;
        cout << "Average NPS: " << formatNum(nps) << endl;
    }
    cout << totalNodes << " nodes " << nps << " nps" << endl;
}