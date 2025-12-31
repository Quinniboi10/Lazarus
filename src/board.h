#pragma once

#include "move.h"
#include "types.h"
#include "util.h"

constexpr array<Square, 4> ROOK_CASTLE_END_SQ = { d8, f8, d1, f1 };
constexpr array<Square, 4> KING_CASTLE_END_SQ = { c8, g8, c1, g1 };

struct Board {
    // Index is based on square, returns the piece type
    array<PieceType, 64> mailbox;
    // Indexed pawns, knights, bishops, rooks, queens, king
    array<u64, 6> byPieces;
    // Index is based on color
    array<u64, 2> byColor;
    // Board hash
    u64 fullHash;   // The entire board, for TT, threefold, etc
    u64 pawnHash;   // Just the pawns
    u64 majorHash;  // King + queen + rook

    // History of positions
    std::vector<u64> posHistory;

    bool          doubleCheck;
    u64           checkMask;
    u64           pinned;
    array<u64, 2> pinnersPerC;


    Square epSquare;
    // Index KQkq
    array<Square, 4> castling;

    Color stm;

    usize halfMoveClock;
    usize fullMoveClock;

   private:
    bool fromNull;

    char getPieceAsChar(Square sq) const;

    void placePiece(Color c, PieceType pt, Square sq);
    void removePiece(Color c, PieceType pt, Square sq);
    void removePiece(Color c, Square sq);
    void resetMailbox();
    void resetHashes();
    void updateCheckPin();

    void setCastlingRights(Color c, Square sq, bool value);
    void unsetCastlingRights(Color c);

    u64 hashCastling() const;

    template<bool minimal>
    void move(Move m);

   public:
    Board() = default;

    constexpr Square castleSq(const Color c, const bool kingside) const {
        return castling[castleIndex(c, kingside)];
    }

    u8 count(PieceType pt) const;

    u64 pieces() const;
    u64 pieces(Color c) const;
    u64 pieces(PieceType pt) const;
    u64 pieces(Color c, PieceType pt) const;
    u64 pieces(PieceType pt1, PieceType pt2) const;
    u64 pieces(Color c, PieceType pt1, PieceType pt2) const;

    u64 attackersTo(Square sq, u64 occ) const;

    u64 roughKeyAfter(Move m) const;

    void reset();

    void   loadFromFEN(const string& fen);
    string fen() const;

    PieceType getPiece(Square sq) const;
    bool      isCapture(Move m) const;
    bool      isQuiet(Move m) const;

    void move(Move m);
    void move(const string& str);

    bool canNullMove() const;
    void nullMove();

    bool canCastle(Color c) const;
    bool canCastle(Color c, bool kingside) const;

    bool isLegal(Move m);

    bool inCheck() const;
    bool isUnderAttack(Color c, Square square) const;

    bool isDraw();
    bool isGameOver();

    bool see(Move m, int threshold) const;

    string toString(Move m = Move::null()) const;

    friend u64 perft(Board& board, usize depth);
};