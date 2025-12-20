#pragma once

#include "accumulator.h"
#include "move.h"
#include "types.h"
#include "util.h"

constexpr array<Square, 4> ROOK_CASTLE_END_SQ = { d8, f8, d1, f1 };
constexpr array<Square, 4> KING_CASTLE_END_SQ = { c8, g8, c1, g1 };

struct Board {
    // Index is based on square, returns the piece type
    array<PieceType, 64> mailbox_;
    // Indexed pawns, knights, bishops, rooks, queens, king
    array<u64, 6> byPieces_;
    // Index is based on color
    array<u64, 2> byColor_;
    // Board hash
    u64 zobrist_;

    // History of positions
    std::vector<u64> posHistory_;

    bool          doubleCheck_;
    u64           checkMask_;
    u64           pinned_;
    array<u64, 2> pinnersPerC_;


    Square epSquare_;
    // Index KQkq
    array<Square, 4> castling_;

    Color stm_;

    usize halfMoveClock_;
    usize fullMoveClock_;

   private:
    bool fromNull_;

    char getPieceAsStr(Square sq) const;

    void placePiece(Color c, PieceType pt, Square sq);
    void removePiece(Color c, PieceType pt, Square sq);
    void removePiece(Color c, Square sq);
    void resetMailbox();
    void resetZobrist();
    void updateCheckPin();

    void setCastlingRights(Color c, Square sq, bool value);
    void unsetCastlingRights(Color c);

    u64 hashCastling() const;

    template<bool minimal>
    void move(Move m);

   public:
    Board() = default;

    static void fillZobristTable();

    constexpr Square castleSq(const Color c, const bool kingside) const {
        return castling_[castleIndex(c, kingside)];
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

    void display() const;

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
};