template<MovegenMode mode>
void Movegen::pawnMoves(const Board& board, MoveList& moves) {
    const u64       pawns        = board.pieces(board.stm_, PAWN);
    const Direction pushDir      = board.stm_ == WHITE ? NORTH : SOUTH;
    u64             singlePushes = shift(pushDir, pawns) & ~board.pieces();
    u64             pushPromo    = singlePushes & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    singlePushes ^= pushPromo;

    u64 doublePushes = shift(pushDir, singlePushes) & ~board.pieces();
    doublePushes &= board.stm_ == WHITE ? MASK_RANK[RANK4] : MASK_RANK[RANK5];

    u64 captureEast = shift(pushDir + EAST, pawns & ~MASK_FILE[HFILE]) & board.pieces(~board.stm_);
    u64 captureWest = shift(pushDir + WEST, pawns & ~MASK_FILE[AFILE]) & board.pieces(~board.stm_);

    u64 eastPromo = captureEast & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    captureEast ^= eastPromo;
    u64 westPromo = captureWest & (MASK_RANK[RANK1] | MASK_RANK[RANK8]);
    captureWest ^= westPromo;

    if constexpr (mode == NOISY_ONLY) {
        singlePushes &= board.pieces(~board.stm_);
        doublePushes &= board.pieces(~board.stm_);
        captureEast &= board.pieces(~board.stm_);
        captureWest &= board.pieces(~board.stm_);
    }

    auto addPromos = [&](const Square from, const Square to) {
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

    Direction backshift = pushDir;

    while (singlePushes) {
        const Square to   = popLSB(singlePushes);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (pushPromo) {
        const Square to   = popLSB(pushPromo);
        const Square from = to - backshift;

        addPromos(from, to);
    }

    backshift = static_cast<Direction>(backshift + pushDir);

    while (doublePushes) {
        const Square to   = popLSB(doublePushes);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    backshift = static_cast<Direction>(pushDir + EAST);

    while (captureEast) {
        const Square to   = popLSB(captureEast);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (eastPromo) {
        Square       to   = popLSB(eastPromo);
        const Square from = to - backshift;

        addPromos(from, to);
    }

    backshift = static_cast<Direction>(pushDir + WEST);

    while (captureWest) {
        const Square to   = popLSB(captureWest);
        const Square from = to - backshift;

        moves.add(from, to);
    }

    while (westPromo) {
        const Square to   = popLSB(westPromo);
        const Square from = to - backshift;

        addPromos(from, to);
    }

    if (board.epSquare_ != NO_SQUARE) {
        u64 epMoves = pawnAttackBB(~board.stm_, board.epSquare_) & board.pieces(board.stm_, PAWN);

        while (epMoves) {
            const Square from = popLSB(epMoves);

            moves.add(from, board.epSquare_, EN_PASSANT);
        }
    }
}

template<MovegenMode mode>
void Movegen::knightMoves(const Board& board, MoveList& moves) {
    u64 knightBB = board.pieces(board.stm_, KNIGHT);

    const u64 friendly = board.pieces(board.stm_);

    while (knightBB > 0) {
        const Square currentSquare = popLSB(knightBB);

        u64 knightMoves = KNIGHT_ATTACKS[currentSquare];
        knightMoves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            knightMoves &= board.pieces(~board.stm_);

        while (knightMoves > 0) {
            const Square to = popLSB(knightMoves);
            moves.add(currentSquare, to);
        }
    }
}

template<MovegenMode mode>
void Movegen::bishopMoves(const Board& board, MoveList& moves) {
    u64 bishopBB = board.pieces(board.stm_, BISHOP, QUEEN);

    const u64 occ      = board.pieces();
    const u64 friendly = board.pieces(board.stm_);

    while (bishopBB > 0) {
        const Square currentSquare = popLSB(bishopBB);

        u64 bishopMoves = getBishopAttacks(currentSquare, occ);
        bishopMoves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            bishopMoves &= board.pieces(~board.stm_);

        while (bishopMoves > 0) {
            const Square to = popLSB(bishopMoves);
            moves.add(currentSquare, to);
        }
    }
}

template<MovegenMode mode>
void Movegen::rookMoves(const Board& board, MoveList& moves) {
    u64 rookBB = board.pieces(board.stm_, ROOK, QUEEN);

    const u64 occ      = board.pieces();
    const u64 friendly = board.pieces(board.stm_);

    while (rookBB > 0) {
        const Square currentSquare = popLSB(rookBB);

        u64 rookMoves = getRookAttacks(currentSquare, occ);
        rookMoves &= ~friendly;
        if constexpr (mode == NOISY_ONLY)
            rookMoves &= board.pieces(~board.stm_);

        while (rookMoves > 0) {
            const Square to = popLSB(rookMoves);
            moves.add(currentSquare, to);
        }
    }
}

template<MovegenMode mode>
void Movegen::kingMoves(const Board& board, MoveList& moves) {
    const Square kingSq = getLSB(board.pieces(board.stm_, KING));

    assert(kingSq >= a1);
    assert(kingSq < NO_SQUARE);

    u64 kingMoves = KING_ATTACKS[kingSq];
    kingMoves &= ~board.pieces(board.stm_);
    if constexpr (mode == NOISY_ONLY)
        kingMoves &= board.pieces(~board.stm_);

    while (kingMoves > 0) {
        const Square to = popLSB(kingMoves);
        moves.add(kingSq, to);
    }

    if (board.canCastle(board.stm_, true))
        moves.add(kingSq, board.castleSq(board.stm_, true), CASTLE);
    if (board.canCastle(board.stm_, false))
        moves.add(kingSq, board.castleSq(board.stm_, false), CASTLE);
}

template<MovegenMode mode>
MoveList Movegen::generateMoves(const Board& board) {
    MoveList moves;
    kingMoves<mode>(board, moves);
    if (board.doubleCheck_)
        return moves;

    pawnMoves<mode>(board, moves);
    knightMoves<mode>(board, moves);
    bishopMoves<mode>(board, moves);
    rookMoves<mode>(board, moves);
    // Note: Queen moves are done at the same time as bishop/rook moves

    return moves;
}