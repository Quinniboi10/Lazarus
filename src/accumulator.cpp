#include "accumulator.h"
#include "board.h"
#include "globals.h"
#include "nnue.h"

void AccumulatorPair::recompute_all(const Board& board) {
    u64 white_pcs = board.pieces(WHITE);
    u64 black_pcs = board.pieces(BLACK);

    white_ = nnue.hl_bias;
    black_ = nnue.hl_bias;

    while (white_pcs) {
        const Square sq = pop_lsb(white_pcs);

        const usize white_input_ft = NNUE::feature(WHITE, WHITE, board.get_piece(sq), sq);
        const usize black_input_ft = NNUE::feature(BLACK, WHITE, board.get_piece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white_[i] += nnue.weights_to_hl[white_input_ft * HL_SIZE + i];
            black_[i] += nnue.weights_to_hl[black_input_ft * HL_SIZE + i];
        }
    }

    while (black_pcs) {
        const Square sq = pop_lsb(black_pcs);

        const usize white_input_ft = NNUE::feature(WHITE, BLACK, board.get_piece(sq), sq);
        const usize black_input_ft = NNUE::feature(BLACK, BLACK, board.get_piece(sq), sq);

        for (usize i = 0; i < HL_SIZE; i++) {
            white_[i] += nnue.weights_to_hl[white_input_ft * HL_SIZE + i];
            black_[i] += nnue.weights_to_hl[black_input_ft * HL_SIZE + i];
        }
    }
}

void AccumulatorPair::update(const Board& board, const Move m, const PieceType to_pt) {
    const Color stm        = ~board.stm;
    const Square from      = m.from();
    const Square to        = m.to();
    const MoveType mt      = m.type();
    const PieceType pt     = mt == PROMOTION ? PAWN : board.get_piece(to);
    const PieceType end_pt = mt == PROMOTION ? m.promo() : pt;

    if (mt == EN_PASSANT)
        add_sub_sub(stm, to, PAWN, from, PAWN, to + (stm == WHITE ? SOUTH : NORTH), PAWN);
    else if (mt == CASTLE) {
        const bool is_kingside = to > from;
        add_add_sub_sub(stm, KING_CASTLE_END_SQ[castle_idx(stm, is_kingside)], KING, ROOK_CASTLE_END_SQ[castle_idx(stm, is_kingside)], ROOK, from, KING, to, ROOK);
    }
    else if (to_pt != NO_PIECE_TYPE)
        add_sub_sub(stm, to, end_pt, from, pt, to, to_pt);
    else
        add_sub(stm, to, end_pt, from, pt);
}

// All friendly, for quiets
void AccumulatorPair::add_sub(const Color stm, const Square add, const PieceType add_pt, const Square sub, const PieceType sub_pt) {
    const usize add_w = NNUE::feature(WHITE, stm, add_pt, add);
    const usize add_b = NNUE::feature(BLACK, stm, add_pt, add);

    const usize sub_w = NNUE::feature(WHITE, stm, sub_pt, sub);
    const usize sub_b = NNUE::feature(BLACK, stm, sub_pt, sub);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weights_to_hl[add_w * HL_SIZE + i] - nnue.weights_to_hl[sub_w * HL_SIZE + i];
        black_[i] += nnue.weights_to_hl[add_b * HL_SIZE + i] - nnue.weights_to_hl[sub_b * HL_SIZE + i];
    }
}

// Captures
void AccumulatorPair::add_sub_sub(const Color stm, const Square add, const PieceType add_pt, const Square sub1, const PieceType sub_pt1, const Square sub2, const PieceType sub_pt2) {
    const usize add_w = NNUE::feature(WHITE, stm, add_pt, add);
    const usize add_b = NNUE::feature(BLACK, stm, add_pt, add);

    const usize sub_w1 = NNUE::feature(WHITE, stm, sub_pt1, sub1);
    const usize sub_b1 = NNUE::feature(BLACK, stm, sub_pt1, sub1);

    const usize sub_w2 = NNUE::feature(WHITE, ~stm, sub_pt2, sub2);
    const usize sub_b2 = NNUE::feature(BLACK, ~stm, sub_pt2, sub2);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weights_to_hl[add_w * HL_SIZE + i] - nnue.weights_to_hl[sub_w1 * HL_SIZE + i] - nnue.weights_to_hl[sub_w2 * HL_SIZE + i];
        black_[i] += nnue.weights_to_hl[add_b * HL_SIZE + i] - nnue.weights_to_hl[sub_b1 * HL_SIZE + i] - nnue.weights_to_hl[sub_b2 * HL_SIZE + i];
    }
}

// Castling
void AccumulatorPair::add_add_sub_sub(
  const Color stm, const Square add1, const PieceType add_pt1, const Square add2, const PieceType add_pt2, const Square sub1, const PieceType sub_pt1, const Square sub2, const PieceType sub_pt2) {
    const usize add_w1 = NNUE::feature(WHITE, stm, add_pt1, add1);
    const usize add_b1 = NNUE::feature(BLACK, stm, add_pt1, add1);

    const usize add_w2 = NNUE::feature(WHITE, stm, add_pt2, add2);
    const usize add_b2 = NNUE::feature(BLACK, stm, add_pt2, add2);

    const usize sub_w1 = NNUE::feature(WHITE, stm, sub_pt1, sub1);
    const usize sub_b1 = NNUE::feature(BLACK, stm, sub_pt1, sub1);

    const usize sub_w2 = NNUE::feature(WHITE, stm, sub_pt2, sub2);
    const usize sub_b2 = NNUE::feature(BLACK, stm, sub_pt2, sub2);

    for (usize i = 0; i < HL_SIZE; i++) {
        white_[i] += nnue.weights_to_hl[add_w1 * HL_SIZE + i] + nnue.weights_to_hl[add_w2 * HL_SIZE + i] - nnue.weights_to_hl[sub_w1 * HL_SIZE + i] - nnue.weights_to_hl[sub_w2 * HL_SIZE + i];
        black_[i] += nnue.weights_to_hl[add_b1 * HL_SIZE + i] + nnue.weights_to_hl[add_b2 * HL_SIZE + i] - nnue.weights_to_hl[sub_b1 * HL_SIZE + i] - nnue.weights_to_hl[sub_b2 * HL_SIZE + i];
    }
}