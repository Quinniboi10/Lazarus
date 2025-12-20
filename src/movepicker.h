#pragma once

#include "move.h"
#include "movegen.h"
#include "tunable.h"
#include "types.h"

inline int evaluateMove(const Board& board, const Move m) {
    const Square from = m.from();
    const Square to   = m.to();
    if (board.isCapture(m))
        return 500'000 + getPieceValue(board.getPiece(to)) * MO_VICTIM_SCALAR - getPieceValue(board.getPiece(from));
    return 0;
}

template<MovegenMode mode>
struct Movepicker {
    MoveList        moves;
    array<int, 256> moveScores;
    u16             seen;
    Move            TTMove;

    Movepicker(const Board& board) {
        moves = Movegen::generateMoves<mode>(board);
        seen  = 0;

        for (usize i = 0; i < moves.length; i++) {
            moveScores[i] = evaluateMove(board, moves.moves[i]);
        }
    }

    [[nodiscard]] usize findNext() {
        usize best      = seen;
        int   bestScore = moveScores[seen];

        for (usize i = seen; i < moves.length; i++) {
            if (moveScores[i] > bestScore) {
                best      = i;
                bestScore = moveScores[i];
            }
        }

        if (best != seen) {
            std::swap(moves.moves[seen], moves.moves[best]);
            std::swap(moveScores[seen], moveScores[best]);
        }

        return seen++;
    }


    bool hasNext() const {
        return seen < moves.length;
    }
    Move getNext() {
        assert(hasNext());
        return moves.moves[findNext()];
    }
};