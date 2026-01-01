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

    MultiArray<u64, 2, 6, 64> pieceTable;
    array<u64, 65>            epTable;
    array<u64, 16>            castlingTable;

    // Fill Piece Table
    for (auto& stm : pieceTable)
        for (auto& pt : stm)
            for (u64& piece : pt)
                piece = engine();

    // Fill EP Table
    for (u64& ep : epTable)
        ep = engine();

    epTable[NO_SQUARE] = 0;

    // Fill STM Hash
    u64 stmHash = engine();

    // Fill Castling Table
    for (u64& right : castlingTable)
        right = engine();

    return std::make_tuple(pieceTable, epTable, stmHash, castlingTable);
}();

// Returns the piece on a square as a character
char Board::getPieceAsChar(const Square sq) const {
    if (getPiece(sq) == NO_PIECE_TYPE)
        return ' ';
    constexpr char whiteSymbols[] = { 'P', 'N', 'B', 'R', 'Q', 'K' };
    constexpr char blackSymbols[] = { 'p', 'n', 'b', 'r', 'q', 'k' };
    if (((1ULL << sq) & byColor[WHITE]) != 0)
        return whiteSymbols[getPiece(sq)];
    return blackSymbols[getPiece(sq)];
}

void Board::placePiece(const Color c, const PieceType pt, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& BB = byPieces[pt];

    assert(!readBit(BB, sq));

    fullHash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawnHash ^= PIECE_ZTABLE[c][PAWN][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = pt;
}

void Board::removePiece(const Color c, const PieceType pt, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& BB = byPieces[pt];

    assert(readBit(BB, sq));

    fullHash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawnHash ^= PIECE_ZTABLE[c][PAWN][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::removePiece(const Color c, const Square sq) {
    assert(sq >= 0);
    assert(sq < 64);

    const PieceType pt = getPiece(sq);

    auto& BB = byPieces[pt];

    assert(readBit(BB, sq));

    fullHash ^= PIECE_ZTABLE[c][pt][sq];
    if (pt == PAWN)
        pawnHash ^= PIECE_ZTABLE[c][PAWN][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::resetMailbox() {
    mailbox.fill(NO_PIECE_TYPE);
    for (u8 i = 0; i < 64; i++) {
        PieceType& sq   = mailbox[i];
        const u64  mask = 1ULL << i;
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

void Board::resetHashes() {
    fullHash = 0;
    pawnHash = 0;

    for (PieceType pt = PAWN; pt <= KING; pt = static_cast<PieceType>(pt + 1)) {
        u64 pcs = pieces(WHITE, pt);
        while (pcs) {
            const Square sq = popLSB(pcs);
            fullHash ^= PIECE_ZTABLE[WHITE][pt][sq];
            if (pt == PAWN)
                pawnHash ^= PIECE_ZTABLE[WHITE][PAWN][sq];
        }

        pcs = pieces(BLACK, pt);
        while (pcs) {
            const Square sq = popLSB(pcs);
            fullHash ^= PIECE_ZTABLE[BLACK][pt][sq];
            if (pt == PAWN)
                pawnHash ^= PIECE_ZTABLE[BLACK][PAWN][sq];
        }
    }

    fullHash ^= hashCastling();
    fullHash ^= EP_ZTABLE[epSquare];

    if (stm == BLACK)
        fullHash ^= STM_ZHASH;
}

// Updates checkers and pinners
void Board::updateCheckPin() {
    const u64 occ = pieces();

    const u64    kingBB = pieces(stm, KING);
    const Square kingSq = getLSB(kingBB);

    const u64 ourPieces         = pieces(stm);
    const u64 enemyRookQueens   = pieces(~stm, ROOK, QUEEN);
    const u64 enemyBishopQueens = pieces(~stm, BISHOP, QUEEN);

    // Direct attacks for potential checks
    const u64 rookChecks   = Movegen::getRookAttacks(kingSq, occ) & enemyRookQueens;
    const u64 bishopChecks = Movegen::getBishopAttacks(kingSq, occ) & enemyBishopQueens;
    u64       checks       = rookChecks | bishopChecks;
    checkMask              = 0;  // If no checks, will be set to all 1s later.

    // *** KNIGHT ATTACKS ***
    const u64 knightAttacks = Movegen::KNIGHT_ATTACKS[kingSq] & pieces(~stm, KNIGHT);
    checkMask |= knightAttacks;

    // *** PAWN ATTACKS ***
    if (stm == WHITE) {
        checkMask |= shift<NORTH_WEST>(kingBB & ~MASK_FILE[AFILE]) & pieces(BLACK, PAWN);
        checkMask |= shift<NORTH_EAST>(kingBB & ~MASK_FILE[HFILE]) & pieces(BLACK, PAWN);
    }
    else {
        checkMask |= shift<SOUTH_WEST>(kingBB & ~MASK_FILE[AFILE]) & pieces(WHITE, PAWN);
        checkMask |= shift<SOUTH_EAST>(kingBB & ~MASK_FILE[HFILE]) & pieces(WHITE, PAWN);
    }

    doubleCheck = popcount(checks | checkMask) > 1;

    while (checks) {
        checkMask |= LINESEG[kingSq][getLSB(checks)];
        checks &= checks - 1;
    }

    if (checkMask == 0)
        checkMask = ~checkMask;  // If no checks, set to all ones

    // ****** PIN STUFF HERE ******
    const u64 rookXrays   = Movegen::getXrayRookAttacks(kingSq, occ, ourPieces) & enemyRookQueens;
    const u64 bishopXrays = Movegen::getXrayBishopAttacks(kingSq, occ, ourPieces) & enemyBishopQueens;
    u64       pinners     = rookXrays | bishopXrays;
    pinnersPerC[stm]      = pinners;

    pinned = 0;
    while (pinners) {
        pinned |= LINESEG[getLSB(pinners)][kingSq] & ourPieces;
        pinners &= pinners - 1;
    }
}

void Board::setCastlingRights(const Color c, const Square sq, const bool value) {
    castling[castleIndex(c, getLSB(pieces(c, KING)) < sq)] = (value == false ? NO_SQUARE : sq);
}

void Board::unsetCastlingRights(const Color c) {
    castling[castleIndex(c, true)] = castling[castleIndex(c, false)] = NO_SQUARE;
}

u64 Board::hashCastling() const {
    constexpr usize blackQ = 0b1;
    constexpr usize blackK = 0b10;
    constexpr usize whiteQ = 0b100;
    constexpr usize whiteK = 0b1000;

    usize flags = 0;

    if (castling[castleIndex(WHITE, true)])
        flags |= whiteK;
    if (castling[castleIndex(WHITE, false)])
        flags |= whiteQ;
    if (castling[castleIndex(BLACK, true)])
        flags |= blackK;
    if (castling[castleIndex(BLACK, false)])
        flags |= blackQ;

    return CASTLING_ZTABLE[flags];
}

u8 Board::count(const PieceType pt) const {
    return popcount(pieces(pt));
}

u64 Board::pieces() const {
    return byColor[WHITE] | byColor[BLACK];
}
u64 Board::pieces(const Color c) const {
    return byColor[c];
}
u64 Board::pieces(const PieceType pt) const {
    return byPieces[pt];
}
u64 Board::pieces(const Color c, const PieceType pt) const {
    return byPieces[pt] & byColor[c];
}
u64 Board::pieces(const PieceType pt1, const PieceType pt2) const {
    return byPieces[pt1] | byPieces[pt2];
}
u64 Board::pieces(const Color c, const PieceType pt1, const PieceType pt2) const {
    return (byPieces[pt1] | byPieces[pt2]) & byColor[c];
}

u64 Board::attackersTo(const Square sq, const u64 occ) const {
    return (Movegen::getRookAttacks(sq, occ) & pieces(ROOK, QUEEN)) | (Movegen::getBishopAttacks(sq, occ) & pieces(BISHOP, QUEEN)) | (Movegen::pawnAttackBB(WHITE, sq) & pieces(BLACK, PAWN)) | (Movegen::pawnAttackBB(BLACK, sq) & pieces(WHITE, PAWN)) |
           (Movegen::KNIGHT_ATTACKS[sq] & pieces(KNIGHT)) | (Movegen::KING_ATTACKS[sq] & pieces(KING));
}

// Estimates the key after a move, ignores EP and castling
u64 Board::roughKeyAfter(const Move m) const {
    u64 key = fullHash ^ STM_ZHASH;

    if (m.isNull())
        return key;

    const Square    from     = m.from();
    const Square    to       = m.to();
    const MoveType  mt       = m.typeOf();
    const PieceType pt       = getPiece(from);
    const PieceType endPT    = mt == PROMOTION ? m.promo() : pt;
    const PieceType targetPT = getPiece(to);

    // Clear EP square
    key ^= EP_ZTABLE[epSquare] * (epSquare != NO_SQUARE);

    key ^= PIECE_ZTABLE[stm][pt][from];   // Piece on the from square
    key ^= PIECE_ZTABLE[stm][endPT][to];  // Piece on the end square

    // Double push
    if (pt == PAWN && (to + 16 == from || to - 16 == from) && (pieces(~stm, PAWN) & (shift<EAST>((1ULL << to) & ~MASK_FILE[HFILE]) | shift<WEST>((1ULL << to) & ~MASK_FILE[AFILE]))))
        key ^= EP_ZTABLE[stm == WHITE ? from + NORTH : from + SOUTH];

    // Capture
    if (targetPT != NO_PIECE_TYPE)
        key ^= PIECE_ZTABLE[~stm][targetPT][to];


    return key;
}

// Reset the board to startpos
void Board::reset() {
    byPieces[PAWN]   = 0xFF00ULL;
    byPieces[KNIGHT] = 0x42ULL;
    byPieces[BISHOP] = 0x24ULL;
    byPieces[ROOK]   = 0x81ULL;
    byPieces[QUEEN]  = 0x8ULL;
    byPieces[KING]   = 0x10ULL;
    byColor[WHITE]   = byPieces[PAWN] | byPieces[KNIGHT] | byPieces[BISHOP] | byPieces[ROOK] | byPieces[QUEEN] | byPieces[KING];

    byPieces[PAWN] |= 0xFF000000000000ULL;
    byPieces[KNIGHT] |= 0x4200000000000000ULL;
    byPieces[BISHOP] |= 0x2400000000000000ULL;
    byPieces[ROOK] |= 0x8100000000000000ULL;
    byPieces[QUEEN] |= 0x800000000000000ULL;
    byPieces[KING] |= 0x1000000000000000ULL;
    byColor[BLACK] = 0xFF000000000000ULL | 0x4200000000000000ULL | 0x2400000000000000ULL | 0x8100000000000000ULL | 0x800000000000000ULL | 0x1000000000000000ULL;


    stm      = WHITE;
    castling = { a8, h8, a1, h1 };

    epSquare = NO_SQUARE;

    halfMoveClock = 0;
    fromNull      = false;

    resetMailbox();
    resetHashes();
    updateCheckPin();

    posHistory = { fullHash };
}


// Load a board from the FEN
void Board::loadFromFEN(const string& fen) {
    reset();

    // Clear all squares
    byPieces.fill(0);
    byColor.fill(0);

    const std::vector<string> tokens = split(fen, ' ');

    const std::vector<string> rankTokens = split(tokens[0], '/');

    int currIdx = 56;

    constexpr char whitePieces[6] = { 'P', 'N', 'B', 'R', 'Q', 'K' };
    constexpr char blackPieces[6] = { 'p', 'n', 'b', 'r', 'q', 'k' };

    for (const string& rank : rankTokens) {
        for (const char c : rank) {
            if (isdigit(c)) {  // Empty squares
                currIdx += c - '0';
                continue;
            }
            for (int i = 0; i < 6; i++) {
                if (c == whitePieces[i]) {
                    setBit<1>(byPieces[i], currIdx);
                    setBit<1>(byColor[WHITE], currIdx);
                    break;
                }
                if (c == blackPieces[i]) {
                    setBit<1>(byPieces[i], currIdx);
                    setBit<1>(byColor[BLACK], currIdx);
                    break;
                }
            }
            currIdx++;
        }
        currIdx -= 16;
    }

    if (tokens[1] == "w")
        stm = WHITE;
    else
        stm = BLACK;

    castling.fill(NO_SQUARE);
    if (tokens[2].find('-') == string::npos) {
        // Standard FEN and maybe XFEN later
        if (tokens[2].find('K') != string::npos)
            castling[castleIndex(WHITE, true)] = h1;
        if (tokens[2].find('Q') != string::npos)
            castling[castleIndex(WHITE, false)] = a1;
        if (tokens[2].find('k') != string::npos)
            castling[castleIndex(BLACK, true)] = h8;
        if (tokens[2].find('q') != string::npos)
            castling[castleIndex(BLACK, false)] = a8;

        // FRC FEN
        if (std::tolower(tokens[2][0]) >= 'a' && std::tolower(tokens[2][0]) <= 'h') {
            chess960 = true;
            for (const char token : tokens[2]) {
                const File file = static_cast<File>(std::tolower(token) - 'a');

                if (std::isupper(token))
                    setCastlingRights(WHITE, toSquare(RANK1, file), true);
                else
                    setCastlingRights(BLACK, toSquare(RANK8, file), true);
            }
        }
    }

    if (tokens[3] != "-")
        epSquare = parseSquare(tokens[3]);
    else
        epSquare = NO_SQUARE;

    halfMoveClock = tokens.size() > 4 ? (stoi(tokens[4])) : 0;
    fullMoveClock = tokens.size() > 5 ? (stoi(tokens[5])) : 1;

    fromNull = false;

    resetMailbox();
    resetHashes();
    updateCheckPin();

    posHistory = { fullHash };
}

string Board::fen() const {
    std::ostringstream ss;

    // Pieces
    for (i32 rank = 7; rank >= 0; rank--) {
        usize empty = 0;
        for (usize file = 0; file < 8; file++) {
            const Square sq = toSquare(static_cast<Rank>(rank), static_cast<File>(file));
            const char   pc = getPieceAsChar(sq);
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
    if (castling[castleIndex(WHITE, true)] != NO_SQUARE)
        castle += 'K';
    if (castling[castleIndex(WHITE, false)] != NO_SQUARE)
        castle += 'Q';
    if (castling[castleIndex(BLACK, true)] != NO_SQUARE)
        castle += 'k';
    if (castling[castleIndex(BLACK, false)] != NO_SQUARE)
        castle += 'q';
    ss << ' ' << (castle.empty() ? "-" : castle);

    // En passant
    if (epSquare != NO_SQUARE)
        ss << ' ' << squareToAlgebraic(epSquare);
    else
        ss << " -";

    // Halfmove
    ss << ' ' << halfMoveClock;

    // Fullmove
    ss << ' ' << fullMoveClock;

    return ss.str();
}

// Return the type of the piece on the square
PieceType Board::getPiece(const Square sq) const {
    return mailbox[sq];
}

// This should return false if
// Move is a capture of any kind
// Move is a queen promotion
// Move is a knight promotion
bool Board::isQuiet(const Move m) const {
    return !isCapture(m) && (m.typeOf() != PROMOTION || m.promo() != QUEEN);
}

bool Board::isCapture(const Move m) const {
    return ((1ULL << m.to() & pieces(~stm)) || m.typeOf() == EN_PASSANT);
}

// Make a move from a string
void Board::move(const string& str) {
    move(Move(str, *this));
}

// Make a move
void Board::move(const Move m) {
    fullHash ^= hashCastling();
    fullHash ^= EP_ZTABLE[epSquare];

    epSquare             = NO_SQUARE;
    fromNull             = false;
    const Square    from = m.from();
    const Square    to   = m.to();
    const MoveType  mt   = m.typeOf();
    const PieceType pt   = getPiece(from);
    PieceType       toPT = NO_PIECE_TYPE;

    removePiece(stm, pt, from);
    if (isCapture(m)) {
        toPT          = getPiece(to);
        halfMoveClock = 0;
        posHistory.clear();
        if (mt != EN_PASSANT) {
            removePiece(~stm, toPT, to);
        }
    }
    else {
        if (pt == PAWN)
            halfMoveClock = 0;
        else
            halfMoveClock++;
    }

    switch (mt) {
    case STANDARD_MOVE:
        placePiece(stm, pt, to);
        if (pt == PAWN && (to + 16 == from || to - 16 == from) && (pieces(~stm, PAWN) & (shift<EAST>((1ULL << to) & ~MASK_FILE[HFILE]) | shift<WEST>((1ULL << to) & ~MASK_FILE[AFILE]))))  // Only set EP square if it could be taken
            epSquare = stm == WHITE ? from + NORTH : from + SOUTH;
        break;
    case EN_PASSANT:
        removePiece(~stm, PAWN, to + (stm == WHITE ? SOUTH : NORTH));
        placePiece(stm, pt, to);
        break;
    case CASTLE:
        assert(getPiece(to) == ROOK);
        removePiece(stm, ROOK, to);
        {
            const Rank r = rankOf(from);
            if (from < to) {  // Kingside
                placePiece(stm, KING, toSquare(r, GFILE));
                placePiece(stm, ROOK, toSquare(r, FFILE));
            }
            else {  // Queenside
                placePiece(stm, KING, toSquare(r, CFILE));
                placePiece(stm, ROOK, toSquare(r, DFILE));
            }
        }
        break;
    case PROMOTION: placePiece(stm, m.promo(), to); break;
    }

    assert(popcount(pieces(WHITE, KING)) == 1);
    assert(popcount(pieces(BLACK, KING)) == 1);

    if (pt == ROOK) {
        const Square sq = castleSq(stm, from > getLSB(pieces(stm, KING)));
        if (from == sq)
            setCastlingRights(stm, from, false);
    }
    else if (pt == KING)
        unsetCastlingRights(stm);
    if (toPT == ROOK) {
        const Square sq = castleSq(~stm, to > getLSB(pieces(~stm, KING)));
        if (to == sq)
            setCastlingRights(~stm, to, false);
    }

    stm = ~stm;

    fullHash ^= hashCastling();
    fullHash ^= EP_ZTABLE[epSquare];
    fullHash ^= STM_ZHASH;

    posHistory.push_back(fullHash);

    fullMoveClock += stm == WHITE;

    updateCheckPin();
}

bool Board::canNullMove() const {
    // Don't allow back-to-back null moves
    if (fromNull == true)
        return false;
    // Pawn + king only endgame
    if (popcount(pieces(stm)) - popcount(pieces(stm, PAWN)) == 1)
        return false;

    return true;
}

void Board::nullMove() {
    // En passant
    fullHash ^= EP_ZTABLE[epSquare];
    fullHash ^= EP_ZTABLE[NO_SQUARE];

    epSquare = NO_SQUARE;

    // Stm
    fullHash ^= STM_ZHASH;
    stm = ~stm;

    posHistory.push_back(fullHash);

    fromNull = true;
    updateCheckPin();
}

bool Board::canCastle(const Color c) const {
    return castleSq(c, true) != NO_SQUARE || castleSq(c, false) != NO_SQUARE;
}
bool Board::canCastle(const Color c, const bool kingside) const {
    return castleSq(c, kingside) != NO_SQUARE;
}

bool Board::isLegal(const Move m) {
    assert(!m.isNull());

    // Castling checks
    if (m.typeOf() == CASTLE) {
        if (inCheck())
            return false;

        const bool kingside = (m.from() < m.to());

        if (!canCastle(stm, kingside))
            return false;

        if (pinned & (1ULL << m.to()))
            return false;

        const Rank   r         = rankOf(m.from());
        const Square kingEndSq = toSquare(r, kingside ? GFILE : CFILE);
        const Square rookEndSq = toSquare(r, kingside ? FFILE : DFILE);

        u64 betweenBB = (LINESEG[m.from()][kingEndSq] | LINESEG[m.to()][rookEndSq]) ^ (1ULL << m.from()) ^ (1ULL << m.to());

        if (pieces() & betweenBB)
            return false;

        betweenBB = LINESEG[m.from()][kingEndSq] ^ (1ULL << m.from());

        while (betweenBB)
            if (isUnderAttack(stm, popLSB(betweenBB)))
                return false;

        return true;
    }

    u64&         king   = byPieces[KING];
    const Square kingSq = getLSB(king & byColor[stm]);

    // King moves
    if (king & (1ULL << m.from())) {
        u64& pieces = byColor[stm];

        pieces ^= king;
        king ^= 1ULL << kingSq;

        const bool ans = !isUnderAttack(stm, m.to());
        king ^= 1ULL << kingSq;
        pieces ^= king;
        return ans;
    }

    if (m.typeOf() == EN_PASSANT) {
        Board testBoard = *this;
        testBoard.move(m);
        return !testBoard.isUnderAttack(stm, getLSB(testBoard.pieces(stm, KING)));
    }

    // Direct checks
    if ((1ULL << m.to()) & ~checkMask)
        return false;

    // Pins
    return !(pinned & (1ULL << m.from())) || (LINE[m.from()][m.to()] & (king & byColor[stm]));
}

bool Board::inCheck() const {
    return ~checkMask;
}

bool Board::isUnderAttack(const Color c, const Square square) const {
    assert(square >= a1);
    assert(square < NO_SQUARE);
    // *** SLIDING PIECE ATTACKS ***
    // Straight Directions (Rooks and Queens)
    if (pieces(~c, ROOK, QUEEN) & Movegen::getRookAttacks(square, pieces()))
        return true;

    // Diagonal Directions (Bishops and Queens)
    if (pieces(~c, BISHOP, QUEEN) & Movegen::getBishopAttacks(square, pieces()))
        return true;

    // *** KNIGHT ATTACKS ***
    if (pieces(~c, KNIGHT) & Movegen::KNIGHT_ATTACKS[square])
        return true;

    // *** KING ATTACKS ***
    if (pieces(~c, KING) & Movegen::KING_ATTACKS[square])
        return true;


    // *** PAWN ATTACKS ***
    if (c == WHITE)
        return (Movegen::pawnAttackBB(WHITE, square) & pieces(BLACK, PAWN)) != 0;
    else
        return (Movegen::pawnAttackBB(BLACK, square) & pieces(WHITE, PAWN)) != 0;
}

bool Board::isDraw() {
    // 50 move rule
    if (halfMoveClock >= 100)
        return Movegen::generateLegalMoves(*this).length != 0;

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
    for (const u64 hash : posHistory) {
        seen += hash == fullHash;
        if (seen >= 3)
            return true;
    }
    return false;
}

bool Board::isGameOver() {
    if (isDraw())
        return true;
    const MoveList moves = Movegen::generateLegalMoves(*this);
    return moves.length == 0;
}

// Uses the swap-off algorithm, code from Stockfish
bool Board::see(const Move m, const int threshold) const {
    if (m.typeOf() != STANDARD_MOVE)
        return 0 >= threshold;

    const Square from = m.from();
    const Square to   = m.to();

    int swap = getPieceValue(getPiece(to)) - threshold;
    if (swap <= 0)
        return false;

    swap = getPieceValue(getPiece(from)) - swap;
    if (swap <= 0)
        return true;

    u64   occ       = pieces() ^ (1ULL << from) ^ (1ULL << to);
    Color stm       = this->stm;
    u64   attackers = attackersTo(to, occ);
    u64   bb;

    int res = 1;

    while (true) {
        stm = ~stm;
        attackers &= occ;

        u64 stmAttackers = attackers & pieces(stm);
        if (!stmAttackers)
            break;

        if (pinnersPerC[~stm] & occ) {
            stmAttackers &= ~pinned;
            if (!stmAttackers)
                break;
        }

        res ^= 1;

        if ((bb = stmAttackers & pieces(PAWN))) {
            swap = getPieceValue(PAWN) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << getLSB(bb);  // LSB as a bitboard

            attackers |= Movegen::getBishopAttacks(to, occ) & pieces(BISHOP, QUEEN);
        }

        else if ((bb = stmAttackers & pieces(KNIGHT))) {
            swap = getPieceValue(KNIGHT) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << getLSB(bb);
        }

        else if ((bb = stmAttackers & pieces(BISHOP))) {
            swap = getPieceValue(BISHOP) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << getLSB(bb);

            attackers |= Movegen::getBishopAttacks(to, occ) & pieces(BISHOP, QUEEN);
        }

        else if ((bb = stmAttackers & pieces(ROOK))) {
            swap = getPieceValue(ROOK) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << getLSB(bb);

            attackers |= Movegen::getRookAttacks(to, occ) & pieces(ROOK, QUEEN);
        }

        else if ((bb = stmAttackers & pieces(QUEEN))) {
            swap = getPieceValue(QUEEN) - swap;
            if (swap < res)
                break;
            occ ^= 1ULL << getLSB(bb);

            attackers |= (Movegen::getBishopAttacks(to, occ) & pieces(BISHOP, QUEEN)) | (Movegen::getRookAttacks(to, occ) & pieces(ROOK, QUEEN));
        }
        else
            return (attackers & ~pieces(stm)) ? res ^ 1 : res;  // King capture so flip side if enemy has attackers
    }

    return res;
}

// Print the board
string Board::toString(const Move m) const {
    std::ostringstream os;
    const auto         printInfo = [&](const usize line) {
        std::ostringstream ss;
        if (line == 1)
            ss << "FEN: " << fen();
        else if (line == 2)
            ss << "Hash: 0x" << std::hex << std::uppercase << fullHash << std::dec;
        else if (line == 3)
            ss << "Pawn hash: 0x" << std::hex << std::uppercase << pawnHash << std::dec;
        else if (line == 4)
            ss << "Side to move: " << (stm == WHITE ? "WHITE" : "BLACK");
        else if (line == 5)
            ss << "En passant: " << (epSquare == NO_SQUARE ? "-" : squareToAlgebraic(epSquare));
        return ss.str();
    };

    os << "\u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n";

    const auto from = m.isNull() ? NO_SQUARE : m.from();
    const auto to   = m.isNull() ? NO_SQUARE : m.to();

    const auto fromColor = fmt::color::dim_gray;
    const auto toColor   = isCapture(m) ? fmt::color::dark_red : fmt::color::dim_gray;

    usize line = 1;
    for (i32 rank = (stm == WHITE) * 7; (stm == WHITE) ? rank >= 0 : rank < 8; (stm == WHITE) ? rank-- : rank++) {
        os << "\u2502 ";
        for (i32 file = (stm != WHITE) * 7; (stm != WHITE) ? file >= 0 : file < 8; (stm != WHITE) ? file-- : file++) {
            const auto sq      = static_cast<Square>(rank * 8 + file);
            const auto fgColor = ((1ULL << sq) & pieces(WHITE)) ? fmt::color::orange : fmt::color::dark_blue;
            const auto bgColor = sq == to ? toColor : fromColor;

            if (from == sq || to == sq)
                os << fmt::format(fmt::fg(fgColor) | fmt::bg(bgColor), "{}", getPieceAsChar(sq)) << " ";
            else
                os << fmt::format(fmt::fg(fgColor), "{}", getPieceAsChar(sq)) << " ";
        }
        os << "\u2502 " << rank + 1 << "    " << printInfo(line++) << "\n";
    }
    os << "\u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n";
    if (stm == WHITE)
        os << "  a b c d e f g h\n";
    else
        os << "  h g f e d c b a\n";
    return os.str();
}