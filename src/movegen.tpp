template<MovegenMode mode>
void movegen::pawn_moves(const Board& board, MoveList& moves) {
    const u64 pawns          = board.pieces(board.stm, PAWN);
    const Direction push_dir = board.stm == WHITE ? NORTH : SOUTH;
    u64 single_push          = shift(push_dir, pawns) & ~board.pieces();
    u64 push_promo           = single_push & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    single_push ^= push_promo;

    u64 double_push = shift(push_dir, single_push) & ~board.pieces();
    double_push &= board.stm == WHITE ? MASK_RANK[RANK4] : MASK_RANK[RANK5];

    u64 cap_e = shift(push_dir + EAST, pawns & ~MASK_FILE[HFILE]) & board.pieces(~board.stm);
    u64 cap_w = shift(push_dir + WEST, pawns & ~MASK_FILE[AFILE]) & board.pieces(~board.stm);

    u64 promo_e = cap_e & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    cap_e ^= promo_e;
    u64 promo_w = cap_w & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    cap_w ^= promo_w;

    if constexpr (mode == NOISY_ONLY) {
        single_push &= board.pieces(~board.stm);
        double_push &= board.pieces(~board.stm);
        cap_e &= board.pieces(~board.stm);
        cap_w &= board.pieces(~board.stm);
    }

    auto add_promos = [&](const Square from, const Square to) {
        assert(from >= 0);
        assert(from < 64);

        assert(to >= 0);
        assert(to < 64);

        moves.add(Move(from, to, QUEEN));
        if constexpr (mode != NOISY_ONLY) {
            moves.add(Move(from, to, ROOK));
            moves.add(Move(from, to, BISHOP));
            moves.add(Move(from, to, KNIGHT));
        }
    };

    Direction backshift = push_dir;

    while (single_push) {
        const Square to   = pop_lsb(single_push);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (push_promo) {
        const Square to   = pop_lsb(push_promo);
        const Square from = to - backshift;

        add_promos(from, to);
    }

    backshift = static_cast<Direction>(backshift + push_dir);

    while (double_push) {
        const Square to   = pop_lsb(double_push);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    backshift = static_cast<Direction>(push_dir + EAST);

    while (cap_e) {
        const Square to   = pop_lsb(cap_e);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (promo_e) {
        Square to         = pop_lsb(promo_e);
        const Square from = to - backshift;

        add_promos(from, to);
    }

    backshift = static_cast<Direction>(push_dir + WEST);

    while (cap_w) {
        const Square to   = pop_lsb(cap_w);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (promo_w) {
        const Square to   = pop_lsb(promo_w);
        const Square from = to - backshift;

        add_promos(from, to);
    }

    if (board.ep_sq != NO_SQUARE) {
        u64 ep_moves = pawn_attack_bb(~board.stm, board.ep_sq) & board.pieces(board.stm, PAWN);

        while (ep_moves) {
            const Square from = pop_lsb(ep_moves);

            moves.add(from, board.ep_sq, EN_PASSANT);
        }
    }
}

template<MovegenMode mode>
void movegen::knight_moves(const Board& board, MoveList& moves) {
    u64 knight_bb = board.pieces(board.stm, KNIGHT);

    const u64 friendly = board.pieces(board.stm);

    while (knight_bb > 0) {
        const Square curr_sq = pop_lsb(knight_bb);

        u64 knight_moves = KNIGHT_ATTACKS[curr_sq];
        knight_moves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            knight_moves &= board.pieces(~board.stm);

        while (knight_moves > 0) {
            const Square to = pop_lsb(knight_moves);
            moves.add(curr_sq, to);
        }
    }
}

template<MovegenMode mode>
void movegen::bishop_moves(const Board& board, MoveList& moves) {
    u64 bishop_bb = board.pieces(board.stm, BISHOP, QUEEN);

    const u64 occ      = board.pieces();
    const u64 friendly = board.pieces(board.stm);

    while (bishop_bb > 0) {
        const Square curr_sq = pop_lsb(bishop_bb);

        u64 bishop_moves = get_bishop_attacks(curr_sq, occ);
        bishop_moves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            bishop_moves &= board.pieces(~board.stm);

        while (bishop_moves > 0) {
            const Square to = pop_lsb(bishop_moves);
            moves.add(curr_sq, to);
        }
    }
}

template<MovegenMode mode>
void movegen::rook_moves(const Board& board, MoveList& moves) {
    u64 rook_bb = board.pieces(board.stm, ROOK, QUEEN);

    const u64 occ      = board.pieces();
    const u64 friendly = board.pieces(board.stm);

    while (rook_bb > 0) {
        const Square curr_sq = pop_lsb(rook_bb);

        u64 rook_moves = get_rook_attacks(curr_sq, occ);
        rook_moves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            rook_moves &= board.pieces(~board.stm);

        while (rook_moves > 0) {
            const Square to = pop_lsb(rook_moves);
            moves.add(curr_sq, to);
        }
    }
}

template<MovegenMode mode>
void movegen::king_moves(const Board& board, MoveList& moves) {
    const Square king_sq = get_lsb(board.pieces(board.stm, KING));

    assert(king_sq >= a1);
    assert(king_sq < NO_SQUARE);

    u64 king_moves = KING_ATTACKS[king_sq];
    king_moves &= ~board.pieces(board.stm);
    if constexpr (mode == NOISY_ONLY)
        king_moves &= board.pieces(~board.stm);

    while (king_moves > 0) {
        const Square to = pop_lsb(king_moves);
        moves.add(king_sq, to);
    }

    if (board.can_castle(board.stm, true))
        moves.add(king_sq, board.castle_sq(board.stm, true), CASTLE);
    if (board.can_castle(board.stm, false))
        moves.add(king_sq, board.castle_sq(board.stm, false), CASTLE);
}

template<MovegenMode mode>
MoveList movegen::gen_moves(const Board& board) {
    MoveList moves;
    king_moves<mode>(board, moves);
    if (board.double_check)
        return moves;

    pawn_moves<mode>(board, moves);
    knight_moves<mode>(board, moves);
    bishop_moves<mode>(board, moves);
    rook_moves<mode>(board, moves);
    // Note: Queen moves are done at the same time as bishop/rook moves

    return moves;
}