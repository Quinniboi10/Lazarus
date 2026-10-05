#include "board.h"
#include "constants.h"
#include "globals.h"
#include "movegen.h"
#include "search.h"
#include "types.h"
#include "util.h"

#include "../external/fmt/fmt/color.h"

#include <cassert>
#include <random>

const auto [PIECE_ZTABLE, EP_ZTABLE, STM_ZHASH, CASTLING_ZTABLE] = []() {
    // Initialize the generator
    std::mt19937_64 engine(69420);

    MultiArray<u64, 2, 6, 64> piece_table;
    array<u64, 65> ep_table;
    array<u64, 16> castling_table;

    // Fill Piece Table
    for (auto& stm : piece_table)
        for (auto& pt : stm)
            for (u64& piece : pt)
                piece = engine();

    // Fill EP Table
    for (u64& ep : ep_table)
        ep = engine();

    ep_table[NO_SQUARE] = 0;

    // Fill STM Hash
    u64 stm_hash = engine();

    // Fill Castling Table
    for (u64& right : castling_table)
        right = engine();

    return std::make_tuple(piece_table, ep_table, stm_hash, castling_table);
}();

// Returns the piece on a square as a character
char Board::get_piece_char(const Square sq) const {
    if (get_piece(sq) == NO_PIECE_TYPE)
        return ' ';
    constexpr char white_symbols[] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    constexpr char black_symbols[] = {'p', 'n', 'b', 'r', 'q', 'k'};
    if (((1ULL << sq) & by_color[WHITE]) != 0)
        return white_symbols[get_piece(sq)];
    return black_symbols[get_piece(sq)];
}

void Board::place_piece(const Color c, const PieceType pt, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& bb = by_pieces[pt];

    assert(!read_bit(bb, sq));

    full_hash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawn_hash ^= PIECE_ZTABLE[c][PAWN][sq];
    else if (pt == KING || pt == QUEEN || pt == ROOK)
        major_hash ^= PIECE_ZTABLE[c][pt][sq];

    bb ^= 1ULL << sq;
    by_color[c] ^= 1ULL << sq;

    mailbox[sq] = pt;
}

void Board::remove_piece(const Color c, const PieceType pt, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& bb = by_pieces[pt];

    assert(read_bit(bb, sq));

    full_hash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawn_hash ^= PIECE_ZTABLE[c][PAWN][sq];
    else if (pt == KING || pt == QUEEN || pt == ROOK)
        major_hash ^= PIECE_ZTABLE[c][pt][sq];

    bb ^= 1ULL << sq;
    by_color[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::remove_piece(const Color c, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    const PieceType pt = get_piece(sq);

    auto& bb = by_pieces[pt];

    assert(read_bit(bb, sq));

    full_hash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawn_hash ^= PIECE_ZTABLE[c][PAWN][sq];
    else if (pt == KING || pt == QUEEN || pt == ROOK)
        major_hash ^= PIECE_ZTABLE[c][pt][sq];

    bb ^= 1ULL << sq;
    by_color[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::reset_mailbox() {
    mailbox.fill(NO_PIECE_TYPE);
    for (u8 i = 0; i < 64; i++) {
        PieceType& sq  = mailbox[i];
        const u64 mask = 1ULL << i;
        if (mask & pieces(PAWN))
            sq = PAWN;
        else if (mask & pieces(KNIGHT))
            sq = KNIGHT;
        else if (mask & pieces(BISHOP))
            sq = BISHOP;
        else if (mask & pieces(ROOK))
            sq = ROOK;
        else if (mask & pieces(QUEEN))
            sq = QUEEN;
        else if (mask & pieces(KING))
            sq = KING;
    }
}

void Board::reset_hashes() {
    full_hash  = 0;
    pawn_hash  = 0;
    major_hash = 0;

    for (PieceType pt = PAWN; pt <= KING; pt = static_cast<PieceType>(pt + 1)) {
        u64 pcs = pieces(WHITE, pt);
        while (pcs) {
            const Square sq = pop_lsb(pcs);
            full_hash ^= PIECE_ZTABLE[WHITE][pt][sq];
            if (pt == PAWN)
                pawn_hash ^= PIECE_ZTABLE[WHITE][PAWN][sq];
            else if (pt == KING || pt == QUEEN || pt == ROOK)
                major_hash ^= PIECE_ZTABLE[WHITE][pt][sq];
        }

        pcs = pieces(BLACK, pt);
        while (pcs) {
            const Square sq = pop_lsb(pcs);
            full_hash ^= PIECE_ZTABLE[BLACK][pt][sq];
            if (pt == PAWN)
                pawn_hash ^= PIECE_ZTABLE[BLACK][PAWN][sq];
            else if (pt == KING || pt == QUEEN || pt == ROOK)
                major_hash ^= PIECE_ZTABLE[BLACK][pt][sq];
        }
    }

    full_hash ^= hash_castling();
    full_hash ^= EP_ZTABLE[ep_sq];

    if (stm == BLACK)
        full_hash ^= STM_ZHASH;
}

// Updates checkers and pinners
void Board::update_check_pin() {
    const u64 occ = pieces();

    const u64 king_bb    = pieces(stm, KING);
    const Square king_sq = get_lsb(king_bb);

    const u64 our_pcs     = pieces(stm);
    const u64 enemy_ortho = pieces(~stm, ROOK, QUEEN);
    const u64 enemy_diag  = pieces(~stm, BISHOP, QUEEN);

    // Direct attacks for potential checks
    const u64 ortho_checks = movegen::get_rook_attacks(king_sq, occ) & enemy_ortho;
    const u64 diag_checks  = movegen::get_bishop_attacks(king_sq, occ) & enemy_diag;
    u64 checks             = ortho_checks | diag_checks;
    check_mask             = 0;  // If no checks, will be set to all 1s later.

    // *** KNIGHT ATTACKS ***
    const u64 knight_attacks = movegen::KNIGHT_ATTACKS[king_sq] & pieces(~stm, KNIGHT);
    check_mask |= knight_attacks;

    // *** PAWN ATTACKS ***
    if (stm == WHITE) {
        check_mask |= shift<NORTH_WEST>(king_bb & ~MASK_FILE[AFILE]) & pieces(BLACK, PAWN);
        check_mask |= shift<NORTH_EAST>(king_bb & ~MASK_FILE[HFILE]) & pieces(BLACK, PAWN);
    }
    else {
        check_mask |= shift<SOUTH_WEST>(king_bb & ~MASK_FILE[AFILE]) & pieces(WHITE, PAWN);
        check_mask |= shift<SOUTH_EAST>(king_bb & ~MASK_FILE[HFILE]) & pieces(WHITE, PAWN);
    }

    double_check = popcount(checks | check_mask) > 1;

    while (checks) {
        check_mask |= LINESEG[king_sq][get_lsb(checks)];
        checks &= checks - 1;
    }

    if (check_mask == 0)
        check_mask = ~check_mask;  // If no checks, set to all ones

    // ****** PIN STUFF HERE ******
    const u64 ortho_xrays = movegen::get_xray_rook_attacks(king_sq, occ, our_pcs) & enemy_ortho;
    const u64 diag_xrays  = movegen::get_xray_bishop_attacks(king_sq, occ, our_pcs) & enemy_diag;
    u64 pinners           = ortho_xrays | diag_xrays;
    pinners_by_color[stm] = pinners;

    pinned = 0;
    while (pinners) {
        pinned |= LINESEG[get_lsb(pinners)][king_sq] & our_pcs;
        pinners &= pinners - 1;
    }
}

void Board::set_castling_rights(const Color c, const Square sq, const bool value) {
    castling[castle_idx(c, get_lsb(pieces(c, KING)) < sq)] = (value == false ? NO_SQUARE : sq);
}

void Board::unset_castling_rights(const Color c) {
    castling[castle_idx(c, true)] = castling[castle_idx(c, false)] = NO_SQUARE;
}

u64 Board::hash_castling() const {
    constexpr usize black_q = 0b1;
    constexpr usize black_k = 0b10;
    constexpr usize white_q = 0b100;
    constexpr usize white_k = 0b1000;

    usize flags = 0;

    if (castling[castle_idx(WHITE, true)])
        flags |= white_k;
    if (castling[castle_idx(WHITE, false)])
        flags |= white_q;
    if (castling[castle_idx(BLACK, true)])
        flags |= black_k;
    if (castling[castle_idx(BLACK, false)])
        flags |= black_q;

    return CASTLING_ZTABLE[flags];
}

u8 Board::count(const PieceType pt) const {
    return popcount(pieces(pt));
}

u64 Board::pieces() const {
    return by_color[WHITE] | by_color[BLACK];
}
u64 Board::pieces(const Color c) const {
    return by_color[c];
}
u64 Board::pieces(const PieceType pt) const {
    return by_pieces[pt];
}
u64 Board::pieces(const Color c, const PieceType pt) const {
    return by_pieces[pt] & by_color[c];
}
u64 Board::pieces(const PieceType pt1, const PieceType pt2) const {
    return by_pieces[pt1] | by_pieces[pt2];
}
u64 Board::pieces(const Color c, const PieceType pt1, const PieceType pt2) const {
    return (by_pieces[pt1] | by_pieces[pt2]) & by_color[c];
}

u64 Board::attackers_to(const Square sq, const u64 occ) const {
    return (movegen::get_rook_attacks(sq, occ) & pieces(ROOK, QUEEN)) | (movegen::get_bishop_attacks(sq, occ) & pieces(BISHOP, QUEEN)) | (movegen::pawn_attack_bb(WHITE, sq) & pieces(BLACK, PAWN))
         | (movegen::pawn_attack_bb(BLACK, sq) & pieces(WHITE, PAWN)) | (movegen::KNIGHT_ATTACKS[sq] & pieces(KNIGHT)) | (movegen::KING_ATTACKS[sq] & pieces(KING));
}

// Estimates the key after a move, ignores EP and castling
u64 Board::approx_key_after(const Move m) const {
    u64 key = full_hash ^ STM_ZHASH;

    if (m.is_null())
        return key;

    const Square from         = m.from();
    const Square to           = m.to();
    const MoveType mt         = m.type();
    const PieceType pt        = get_piece(from);
    const PieceType end_pt    = mt == PROMOTION ? m.promo() : pt;
    const PieceType target_pt = get_piece(to);

    // Clear EP square
    key ^= EP_ZTABLE[ep_sq] * (ep_sq != NO_SQUARE);

    key ^= PIECE_ZTABLE[stm][pt][from];    // Piece on the from square
    key ^= PIECE_ZTABLE[stm][end_pt][to];  // Piece on the end square

    // Double push
    if (pt == PAWN && (to + 16 == from || to - 16 == from) && (pieces(~stm, PAWN) & (shift<EAST>((1ULL << to) & ~MASK_FILE[HFILE]) | shift<WEST>((1ULL << to) & ~MASK_FILE[AFILE]))))
        key ^= EP_ZTABLE[stm == WHITE ? from + NORTH : from + SOUTH];

    // Capture
    if (target_pt != NO_PIECE_TYPE)
        key ^= PIECE_ZTABLE[~stm][target_pt][to];


    return key;
}

// Reset the board to startpos
void Board::reset() {
    by_pieces[PAWN]   = 0xFF00ULL;
    by_pieces[KNIGHT] = 0x42ULL;
    by_pieces[BISHOP] = 0x24ULL;
    by_pieces[ROOK]   = 0x81ULL;
    by_pieces[QUEEN]  = 0x8ULL;
    by_pieces[KING]   = 0x10ULL;
    by_color[WHITE]   = by_pieces[PAWN] | by_pieces[KNIGHT] | by_pieces[BISHOP] | by_pieces[ROOK] | by_pieces[QUEEN] | by_pieces[KING];

    by_pieces[PAWN] |= 0xFF000000000000ULL;
    by_pieces[KNIGHT] |= 0x4200000000000000ULL;
    by_pieces[BISHOP] |= 0x2400000000000000ULL;
    by_pieces[ROOK] |= 0x8100000000000000ULL;
    by_pieces[QUEEN] |= 0x800000000000000ULL;
    by_pieces[KING] |= 0x1000000000000000ULL;
    by_color[BLACK] = 0xFF000000000000ULL | 0x4200000000000000ULL | 0x2400000000000000ULL | 0x8100000000000000ULL | 0x800000000000000ULL | 0x1000000000000000ULL;


    stm      = WHITE;
    castling = {a8, h8, a1, h1};

    ep_sq = NO_SQUARE;

    halfmove_ctr = 0;
    fullmove_ctr = 1;

    from_null = false;

    reset_mailbox();
    reset_hashes();
    update_check_pin();

    pos_history = {full_hash};
}


// Load a board from the FEN
void Board::load_fen(const string& fen) {
    reset();

    // Clear all squares
    by_pieces.fill(0);
    by_color.fill(0);

    const std::vector<string> tokens = split(fen, ' ');

    const std::vector<string> rank_tokens = split(tokens[0], '/');

    int curr_idx = 56;

    constexpr char white_pieces[6] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    constexpr char black_pieces[6] = {'p', 'n', 'b', 'r', 'q', 'k'};

    for (const string& rank : rank_tokens) {
        for (const char c : rank) {
            if (isdigit(c)) {  // Empty squares
                curr_idx += c - '0';
                continue;
            }
            for (int i = 0; i < 6; i++) {
                if (c == white_pieces[i]) {
                    set_bit<1>(by_pieces[i], curr_idx);
                    set_bit<1>(by_color[WHITE], curr_idx);
                    break;
                }
                if (c == black_pieces[i]) {
                    set_bit<1>(by_pieces[i], curr_idx);
                    set_bit<1>(by_color[BLACK], curr_idx);
                    break;
                }
            }
            curr_idx++;
        }
        curr_idx -= 16;
    }

    if (tokens[1] == "w")
        stm = WHITE;
    else
        stm = BLACK;

    castling.fill(NO_SQUARE);
    if (tokens[2].find('-') == string::npos) {
        // Standard FEN and maybe XFEN later
        if (tokens[2].find('K') != string::npos)
            castling[castle_idx(WHITE, true)] = h1;
        if (tokens[2].find('Q') != string::npos)
            castling[castle_idx(WHITE, false)] = a1;
        if (tokens[2].find('k') != string::npos)
            castling[castle_idx(BLACK, true)] = h8;
        if (tokens[2].find('q') != string::npos)
            castling[castle_idx(BLACK, false)] = a8;

        // FRC FEN
        if (std::tolower(tokens[2][0]) >= 'a' && std::tolower(tokens[2][0]) <= 'h') {
            chess960 = true;
            for (const char token : tokens[2]) {
                const File file = static_cast<File>(std::tolower(token) - 'a');

                if (std::isupper(token))
                    set_castling_rights(WHITE, to_sq(RANK1, file), true);
                else
                    set_castling_rights(BLACK, to_sq(RANK8, file), true);
            }
        }
    }

    if (tokens[3] != "-")
        ep_sq = parse_sq(tokens[3]);
    else
        ep_sq = NO_SQUARE;

    halfmove_ctr = tokens.size() > 4 ? (stoi(tokens[4])) : 0;
    fullmove_ctr = tokens.size() > 5 ? (stoi(tokens[5])) : 1;

    from_null = false;

    reset_mailbox();
    reset_hashes();
    update_check_pin();

    pos_history = {full_hash};
}

string Board::fen() const {
    std::ostringstream ss;

    // Pieces
    for (i32 rank = 7; rank >= 0; rank--) {
        usize empty = 0;
        for (usize file = 0; file < 8; file++) {
            const Square sq = to_sq(static_cast<Rank>(rank), static_cast<File>(file));
            const char pc   = get_piece_char(sq);
            if (pc == ' ')
                empty++;
            else {
                if (empty) {
                    ss << empty;
                    empty = 0;
                }
                ss << pc;
            }
        }
        if (empty)
            ss << empty;
        if (rank != 0)
            ss << '/';
    }

    // Stm
    ss << ' ' << (stm == WHITE ? 'w' : 'b');

    // Castling
    string castle;
    if (castling[castle_idx(WHITE, true)] != NO_SQUARE)
        castle += 'K';
    if (castling[castle_idx(WHITE, false)] != NO_SQUARE)
        castle += 'Q';
    if (castling[castle_idx(BLACK, true)] != NO_SQUARE)
        castle += 'k';
    if (castling[castle_idx(BLACK, false)] != NO_SQUARE)
        castle += 'q';
    ss << ' ' << (castle.empty() ? "-" : castle);

    // En passant
    if (ep_sq != NO_SQUARE)
        ss << ' ' << sq_to_algebraic(ep_sq);
    else
        ss << " -";

    // Halfmove
    ss << ' ' << halfmove_ctr;

    // Fullmove
    ss << ' ' << fullmove_ctr;

    return ss.str();
}

// Return the type of the piece on the square
PieceType Board::get_piece(const Square sq) const {
    return mailbox[sq];
}

// This should return false if
// Move is a capture of any kind
// Move is a queen promotion
// Move is a knight promotion
bool Board::is_quiet(const Move m) const {
    return !is_capture(m) && (m.type() != PROMOTION || m.promo() != QUEEN);
}

bool Board::is_capture(const Move m) const {
    return ((1ULL << m.to() & pieces(~stm)) || m.type() == EN_PASSANT);
}

// Make a move from a string
void Board::move(const string& str) {
    move(Move(str, *this));
}

// Make a move
void Board::move(const Move m) {
    full_hash ^= hash_castling();
    full_hash ^= EP_ZTABLE[ep_sq];

    ep_sq              = NO_SQUARE;
    from_null          = false;
    const Square from  = m.from();
    const Square to    = m.to();
    const MoveType mt  = m.type();
    const PieceType pt = get_piece(from);
    PieceType to_pt    = NO_PIECE_TYPE;

    remove_piece(stm, pt, from);
    if (is_capture(m)) {
        to_pt        = get_piece(to);
        halfmove_ctr = 0;
        pos_history.clear();
        if (mt != EN_PASSANT) {
            remove_piece(~stm, to_pt, to);
        }
    }
    else {
        if (pt == PAWN)
            halfmove_ctr = 0;
        else
            halfmove_ctr++;
    }

    switch (mt) {
        case STANDARD_MOVE:
            place_piece(stm, pt, to);
            if (pt == PAWN && (to + 16 == from || to - 16 == from)
                && (pieces(~stm, PAWN) & (shift<EAST>((1ULL << to) & ~MASK_FILE[HFILE]) | shift<WEST>((1ULL << to) & ~MASK_FILE[AFILE]))))  // Only set EP square if it could be taken
                ep_sq = stm == WHITE ? from + NORTH : from + SOUTH;
            break;
        case EN_PASSANT:
            remove_piece(~stm, PAWN, to + (stm == WHITE ? SOUTH : NORTH));
            place_piece(stm, pt, to);
            break;
        case CASTLE:
            assert(get_piece(to) == ROOK);
            remove_piece(stm, ROOK, to);
            {
                const Rank r = rank_of(from);
                if (from < to) {  // Kingside
                    place_piece(stm, KING, to_sq(r, GFILE));
                    place_piece(stm, ROOK, to_sq(r, FFILE));
                }
                else {  // Queenside
                    place_piece(stm, KING, to_sq(r, CFILE));
                    place_piece(stm, ROOK, to_sq(r, DFILE));
                }
            }
            break;
        case PROMOTION:
            place_piece(stm, m.promo(), to);
            break;
    }

    assert(popcount(pieces(WHITE, KING)) == 1);
    assert(popcount(pieces(BLACK, KING)) == 1);

    if (pt == ROOK) {
        const Square sq = castle_sq(stm, from > get_lsb(pieces(stm, KING)));
        if (from == sq)
            set_castling_rights(stm, from, false);
    }
    else if (pt == KING)
        unset_castling_rights(stm);
    if (to_pt == ROOK) {
        const Square sq = castle_sq(~stm, to > get_lsb(pieces(~stm, KING)));
        if (to == sq)
            set_castling_rights(~stm, to, false);
    }

    stm = ~stm;

    full_hash ^= hash_castling();
    full_hash ^= EP_ZTABLE[ep_sq];
    full_hash ^= STM_ZHASH;

    pos_history.push_back(full_hash);

    fullmove_ctr += stm == WHITE;

    update_check_pin();
}

bool Board::can_null_move() const {
    // Don't allow back-to-back null moves
    if (from_null == true)
        return false;
    // Pawn + king only endgame
    if (popcount(pieces(stm)) - popcount(pieces(stm, PAWN)) == 1)
        return false;

    return true;
}

void Board::make_null_move() {
    // En passant
    full_hash ^= EP_ZTABLE[ep_sq];
    full_hash ^= EP_ZTABLE[NO_SQUARE];

    ep_sq = NO_SQUARE;

    // Stm
    full_hash ^= STM_ZHASH;
    stm = ~stm;

    pos_history.push_back(full_hash);

    from_null = true;
    update_check_pin();
}

bool Board::can_castle(const Color c) const {
    return castle_sq(c, true) != NO_SQUARE || castle_sq(c, false) != NO_SQUARE;
}
bool Board::can_castle(const Color c, const bool kingside) const {
    return castle_sq(c, kingside) != NO_SQUARE;
}

bool Board::is_legal(const Move m) {
    assert(!m.is_null());

    // Castling checks
    if (m.type() == CASTLE) {
        if (in_check())
            return false;

        const bool kingside = (m.from() < m.to());

        if (!can_castle(stm, kingside))
            return false;

        if (pinned & (1ULL << m.to()))
            return false;

        const Rank r             = rank_of(m.from());
        const Square king_end_sq = to_sq(r, kingside ? GFILE : CFILE);
        const Square rook_end_sq = to_sq(r, kingside ? FFILE : DFILE);

        u64 between_bb = (LINESEG[m.from()][king_end_sq] | LINESEG[m.to()][rook_end_sq]) ^ (1ULL << m.from()) ^ (1ULL << m.to());

        if (pieces() & between_bb)
            return false;

        between_bb = LINESEG[m.from()][king_end_sq] ^ (1ULL << m.from());

        while (between_bb)
            if (is_under_attack(stm, pop_lsb(between_bb)))
                return false;

        return true;
    }

    u64& king            = by_pieces[KING];
    const Square king_sq = get_lsb(king & by_color[stm]);

    // King moves
    if (king & (1ULL << m.from())) {
        u64& pieces = by_color[stm];

        pieces ^= king;
        king ^= 1ULL << king_sq;

        const bool ans = !is_under_attack(stm, m.to());
        king ^= 1ULL << king_sq;
        pieces ^= king;
        return ans;
    }

    if (m.type() == EN_PASSANT) {
        Board test_board = *this;
        test_board.move(m);
        return !test_board.is_under_attack(stm, get_lsb(test_board.pieces(stm, KING)));
    }

    // Direct checks
    if ((1ULL << m.to()) & ~check_mask)
        return false;

    // Pins
    return !(pinned & (1ULL << m.from())) || (LINE[m.from()][m.to()] & (king & by_color[stm]));
}

bool Board::in_check() const {
    return ~check_mask;
}

bool Board::is_under_attack(const Color c, const Square square) const {
    assert(square >= a1);
    assert(square < NO_SQUARE);
    // *** SLIDING PIECE ATTACKS ***
    // Straight Directions (Rooks and Queens)
    if (pieces(~c, ROOK, QUEEN) & movegen::get_rook_attacks(square, pieces()))
        return true;

    // Diagonal Directions (Bishops and Queens)
    if (pieces(~c, BISHOP, QUEEN) & movegen::get_bishop_attacks(square, pieces()))
        return true;

    // *** KNIGHT ATTACKS ***
    if (pieces(~c, KNIGHT) & movegen::KNIGHT_ATTACKS[square])
        return true;

    // *** KING ATTACKS ***
    if (pieces(~c, KING) & movegen::KING_ATTACKS[square])
        return true;


    // *** PAWN ATTACKS ***
    if (c == WHITE)
        return (movegen::pawn_attack_bb(WHITE, square) & pieces(BLACK, PAWN)) != 0;
    else
        return (movegen::pawn_attack_bb(BLACK, square) & pieces(WHITE, PAWN)) != 0;
}

bool Board::is_draw() {
    // 50 move rule
    if (halfmove_ctr >= 100)
        return movegen::gen_legal_moves(*this).length != 0;

    // Insufficient material
    if (pieces(PAWN) == 0                                // No pawns
        && pieces(QUEEN) == 0                            // No queens
        && pieces(ROOK) == 0                             // No rooks
        && ((pieces(BISHOP) & LIGHT_SQ_BB) == 0          // No light sq bishops
            || (pieces(BISHOP) & DARK_SQ_BB) == 0)       // OR no dark sq bishops
        && (pieces(BISHOP) == 0 || pieces(KNIGHT) == 0)  // Not bishop + knight
        && popcount(pieces(KNIGHT)) < 2)                 // Under 2 knights
        return true;

    // Threefold
    u8 seen = 0;
    for (const u64 hash : pos_history) {
        seen += hash == full_hash;
        if (seen >= 3)
            return true;
    }
    return false;
}

bool Board::is_game_over() {
    if (is_draw())
        return true;
    const MoveList moves = movegen::gen_legal_moves(*this);
    return moves.length == 0;
}

// Uses the swap-off algorithm, code from Stockfish
bool Board::see(const Move m, const int threshold) const {
    if (m.type() != STANDARD_MOVE)
        return 0 >= threshold;

    const Square from = m.from();
    const Square to   = m.to();

    int swap = get_piece_value(get_piece(to)) - threshold;
    if (swap <= 0)
        return false;

    swap = get_piece_value(get_piece(from)) - swap;
    if (swap <= 0)
        return true;

    u64 occ       = pieces() ^ (1ULL << from) ^ (1ULL << to);
    Color stm     = this->stm;
    u64 attackers = attackers_to(to, occ);
    u64 bb;

    int res = 1;

    while (true) {
        stm = ~stm;
        attackers &= occ;

        u64 stm_attackers = attackers & pieces(stm);
        if (!stm_attackers)
            break;

        if (pinners_by_color[~stm] & occ) {
            stm_attackers &= ~pinned;
            if (!stm_attackers)
                break;
        }

        res ^= 1;

        if ((bb = stm_attackers & pieces(PAWN))) {
            swap = get_piece_value(PAWN) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << get_lsb(bb);  // LSB as a bitboard

            attackers |= movegen::get_bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        }

        else if ((bb = stm_attackers & pieces(KNIGHT))) {
            swap = get_piece_value(KNIGHT) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << get_lsb(bb);
        }

        else if ((bb = stm_attackers & pieces(BISHOP))) {
            swap = get_piece_value(BISHOP) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << get_lsb(bb);

            attackers |= movegen::get_bishop_attacks(to, occ) & pieces(BISHOP, QUEEN);
        }

        else if ((bb = stm_attackers & pieces(ROOK))) {
            swap = get_piece_value(ROOK) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << get_lsb(bb);

            attackers |= movegen::get_rook_attacks(to, occ) & pieces(ROOK, QUEEN);
        }

        else if ((bb = stm_attackers & pieces(QUEEN))) {
            swap = get_piece_value(QUEEN) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << get_lsb(bb);

            attackers |= (movegen::get_bishop_attacks(to, occ) & pieces(BISHOP, QUEEN)) | (movegen::get_rook_attacks(to, occ) & pieces(ROOK, QUEEN));
        }
        else
            return (attackers & ~pieces(stm)) ? res ^ 1 : res;  // King capture so flip side if enemy has attackers
    }

    return res;
}

// Print the board
string Board::str(const Move m) const {
    std::ostringstream os;
    const auto print_info = [&](const usize line) {
        std::ostringstream ss;
        if (line == 1)
            ss << "FEN: " << fen();
        else if (line == 2)
            ss << "Hash: 0x" << std::hex << std::uppercase << full_hash << std::dec;
        else if (line == 3)
            ss << "Side to move: " << (stm == WHITE ? "WHITE" : "BLACK");
        else if (line == 4)
            ss << "En passant: " << (ep_sq == NO_SQUARE ? "-" : sq_to_algebraic(ep_sq));
        return ss.str();
    };

    os << "\u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n";

    const auto from = m.is_null() ? NO_SQUARE : m.from();
    const auto to   = m.is_null() ? NO_SQUARE : m.to();

    const auto from_color = fmt::color::dim_gray;
    const auto to_color   = is_capture(m) ? fmt::color::dark_red : fmt::color::dim_gray;

    usize line = 1;
    for (i32 rank = (stm == WHITE) * 7; (stm == WHITE) ? rank >= 0 : rank < 8; (stm == WHITE) ? rank-- : rank++) {
        os << "\u2502 ";
        for (i32 file = (stm != WHITE) * 7; (stm != WHITE) ? file >= 0 : file < 8; (stm != WHITE) ? file-- : file++) {
            const auto sq       = static_cast<Square>(rank * 8 + file);
            const auto fg_color = ((1ULL << sq) & pieces(WHITE)) ? fmt::color::orange : fmt::color::dark_blue;
            const auto bg_color = sq == to ? to_color : from_color;

            if (from == sq || to == sq)
                os << fmt::format(fmt::fg(fg_color) | fmt::bg(bg_color), "{}", get_piece_char(sq)) << " ";
            else
                os << fmt::format(fmt::fg(fg_color), "{}", get_piece_char(sq)) << " ";
        }
        os << "\u2502 " << rank + 1 << "    " << print_info(line++) << "\n";
    }
    os << "\u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n";
    if (stm == WHITE)
        os << "  a b c d e f g h\n";
    else
        os << "  h g f e d c b a\n";
    return os.str();
}