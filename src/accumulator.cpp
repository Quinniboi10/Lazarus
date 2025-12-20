#include "accumulator.h"
#include "board.h"
#include "globals.h"
#include "nnue.h"

void AccumulatorPair::resetAccumulators(const Board& board) {
    u64 whitePieces = board.pieces(WHITE);
    u64 blackPieces = board.pieces(BLACK);

    white_ = nnue.hiddenLayerBias;
    black_ = nnue.hiddenLayerBias;

    while (whitePieces) {
        const Square sq = popLSB(whitePieces);

        const usize whiteInputFeature = NNUE::feature(WHITE, WHITE, board.getPiece(sq), sq);
        const usize blackInputFeature = NNUE::feature(BLACK, WHITE, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white_[i] += nnue.weightsToHL[whiteInputFeature * HL_SIZE + i];
            black_[i] += nnue.weightsToHL[blackInputFeature * HL_SIZE + i];
        }
    }

    while (blackPieces) {
        const Square sq = popLSB(blackPieces);

        const usize whiteInputFeature = NNUE::feature(WHITE, BLACK, board.getPiece(sq), sq);
        const usize blackInputFeature = NNUE::feature(BLACK, BLACK, board.getPiece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white_[i] += nnue.weightsToHL[whiteInputFeature * HL_SIZE + i];
            black_[i] += nnue.weightsToHL[blackInputFeature * HL_SIZE + i];
        }
    }
}

void AccumulatorPair::update(const Board& board, const Move m, const PieceType toPT) {
    const Color     stm   = ~board.stm_;
    const Square    from  = m.from();
    const Square    to    = m.to();
    const MoveType  mt    = m.typeOf();
    const PieceType pt    = mt == PROMOTION ? PAWN : board.getPiece(to);
    const PieceType endPT = mt == PROMOTION ? m.promo() : pt;

    if (mt == EN_PASSANT)
        addSubSub(stm, to, PAWN, from, PAWN, to + (stm == WHITE ? SOUTH : NORTH), PAWN);
    else if (mt == CASTLE) {
        const bool isKingside = to > from;
        addAddSubSub(stm, KING_CASTLE_END_SQ[castleIndex(stm, isKingside)], KING, ROOK_CASTLE_END_SQ[castleIndex(stm, isKingside)], ROOK, from, KING, to, ROOK);
    }
    else if (toPT != NO_PIECE_TYPE)
        addSubSub(stm, to, endPT, from, pt, to, toPT);
    else
        addSub(stm, to, endPT, from, pt);
}

// All friendly, for quiets
void AccumulatorPair::addSub(const Color stm, const Square add, const PieceType addPT, const Square sub, const PieceType subPT) {
    const usize addW = NNUE::feature(WHITE, stm, addPT, add);
    const usize addB = NNUE::feature(BLACK, stm, addPT, add);

    const usize subW = NNUE::feature(WHITE, stm, subPT, sub);
    const usize subB = NNUE::feature(BLACK, stm, subPT, sub);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weightsToHL[addW * HL_SIZE + i] - nnue.weightsToHL[subW * HL_SIZE + i];
        black_[i] += nnue.weightsToHL[addB * HL_SIZE + i] - nnue.weightsToHL[subB * HL_SIZE + i];
    }
}

// Captures
void AccumulatorPair::addSubSub(const Color stm, const Square add, const PieceType addPT, const Square sub1, const PieceType subPT1, const Square sub2, const PieceType subPT2) {
    const usize addW = NNUE::feature(WHITE, stm, addPT, add);
    const usize addB = NNUE::feature(BLACK, stm, addPT, add);

    const usize subW1 = NNUE::feature(WHITE, stm, subPT1, sub1);
    const usize subB1 = NNUE::feature(BLACK, stm, subPT1, sub1);

    const usize subW2 = NNUE::feature(WHITE, ~stm, subPT2, sub2);
    const usize subB2 = NNUE::feature(BLACK, ~stm, subPT2, sub2);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weightsToHL[addW * HL_SIZE + i] - nnue.weightsToHL[subW1 * HL_SIZE + i] - nnue.weightsToHL[subW2 * HL_SIZE + i];
        black_[i] += nnue.weightsToHL[addB * HL_SIZE + i] - nnue.weightsToHL[subB1 * HL_SIZE + i] - nnue.weightsToHL[subB2 * HL_SIZE + i];
    }
}

// Castling
void AccumulatorPair::addAddSubSub(const Color stm, const Square add1, const PieceType addPT1, const Square add2, const PieceType addPT2, const Square sub1, const PieceType subPT1, const Square sub2, const PieceType subPT2) {
    const usize addW1 = NNUE::feature(WHITE, stm, addPT1, add1);
    const usize addB1 = NNUE::feature(BLACK, stm, addPT1, add1);

    const usize addW2 = NNUE::feature(WHITE, stm, addPT2, add2);
    const usize addB2 = NNUE::feature(BLACK, stm, addPT2, add2);

    const usize subW1 = NNUE::feature(WHITE, stm, subPT1, sub1);
    const usize subB1 = NNUE::feature(BLACK, stm, subPT1, sub1);

    const usize subW2 = NNUE::feature(WHITE, stm, subPT2, sub2);
    const usize subB2 = NNUE::feature(BLACK, stm, subPT2, sub2);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weightsToHL[addW1 * HL_SIZE + i] + nnue.weightsToHL[addW2 * HL_SIZE + i] - nnue.weightsToHL[subW1 * HL_SIZE + i] - nnue.weightsToHL[subW2 * HL_SIZE + i];
        black_[i] += nnue.weightsToHL[addB1 * HL_SIZE + i] + nnue.weightsToHL[addB2 * HL_SIZE + i] - nnue.weightsToHL[subB1 * HL_SIZE + i] - nnue.weightsToHL[subB2 * HL_SIZE + i];
    }
}