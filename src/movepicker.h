#pragma once

#include "move.h"
#include "movegen.h"
#include "tunable.h"
#include "types.h"

inline int eval_move(const Board& board, const ThreadData& this_thread, const Move m) {
    const Square from = m.from();
    const Square to   = m.to();
    if (board.is_capture(m))
        return (board.see(m, -MO_CAPTURE_SEE_THRESHOLD) ? 500'000 : -200'000)          // Constant bonus for captures
             + get_piece_value(board.get_piece(to)) * MO_VICTIM_SCALAR                 // Prioritize capturing stronger pieces
             + this_thread.get_capture_history(board, m) * MO_CAPTHIST_WEIGHT / 1024;  // Probe the capture history

    return this_thread.get_history(board, m);
}

template<MovegenMode mode>
struct Movepicker {
    MoveList moves;
    array<int, 256> move_scores;
    u16 seen;

    Movepicker(const Board& board, const ThreadData& this_thread, const Move tt_move) {
        moves = movegen::gen_moves<mode>(board);
        seen  = 0;

        for (usize i = 0; i < moves.length; i++) {
            const Move m   = moves.moves[i];
            move_scores[i] = eval_move(board, this_thread, m) + 900'000 * (m == tt_move);
        }
    }

    [[nodiscard]] usize find_next() {
        usize best     = seen;
        int best_score = move_scores[seen];

        for (usize i = seen + 1; i < moves.length; i++) {
            if (move_scores[i] > best_score) {
                best       = i;
                best_score = move_scores[i];
            }
        }

        if (best != seen) {
            std::swap(moves.moves[seen], moves.moves[best]);
            std::swap(move_scores[seen], move_scores[best]);
        }

        return seen++;
    }


    bool has_next() const {
        return seen < moves.length;
    }
    Move get_next() {
        assert(has_next());
        return moves.moves[find_next()];
    }
};