#pragma once

#include "move.h"
#include "movegen.h"
#include "tunable.h"
#include "types.h"

inline int evaluateMove(const Board& board, const ThreadInfo& thisThread, const Move m) {
    const Square from = m.from();
    const Square to   = m.to();
    if (board.isCapture(m))
        return (board.see(m, -MO_CAPTURE_SEE_THRESHOLD) ? 500'000 : -200'000) + getPieceValue(board.getPiece(to)) * MO_VICTIM_SCALAR - getPieceValue(board.getPiece(from));

    return thisThread.getHistory(board, m);
}

template<MovegenMode mode>
struct Movepicker {
    MoveList        moves;
    array<int, 256> moveScores;
    u16             seen;

    Movepicker(const Board& board, const ThreadInfo& thisThread, const Move ttMove) {
        moves = Movegen::generateMoves<mode>(board);
        seen  = 0;

        for (usize i = 0; i < moves.length; i++) {
            const Move m  = moves.moves[i];
            moveScores[i] = evaluateMove(board, thisThread, m) + 900'000 * (m == ttMove);
        }
    }

    [[nodiscard]] usize findNext() {
        usize best      = seen;
        int   bestScore = moveScores[seen];

        for (usize i = seen + 1; i < moves.length; i++) {
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