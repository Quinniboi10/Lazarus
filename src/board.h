#pragma once

#include "move.h"
#include "types.h"
#include "util.h"

constexpr array<Square, 4> ROOK_CASTLE_END_SQ = {d8, f8, d1, f1};
constexpr array<Square, 4> KING_CASTLE_END_SQ = {c8, g8, c1, g1};

struct Board {
    // Index is based on square, returns the piece type
    array<PieceType, 64> mailbox;
    // Indexed pawns, knights, bishops, rooks, queens, king
    array<u64, 6> by_pieces;
    // Index is based on color
    array<u64, 2> by_color;
    // Board hash
    u64 full_hash;   // The entire board, for TT, threefold, etc
    u64 pawn_hash;   // Just the pawns
    u64 major_hash;  // King + queen + rook

    // History of positions
    std::vector<u64> pos_history;

    bool double_check;
    u64 check_mask;
    u64 pinned;
    array<u64, 2> pinners_by_color;


    Square ep_sq;
    // Index KQkq
    array<Square, 4> castling;

    Color stm;

    usize halfmove_ctr;
    usize fullmove_ctr;

   private:
    bool from_null;

    char get_piece_char(Square sq) const;

    void place_piece(Color c, PieceType pt, Square sq);
    void remove_piece(Color c, PieceType pt, Square sq);
    void remove_piece(Color c, Square sq);
    void reset_mailbox();
    void reset_hashes();
    void update_check_pin();

    void set_castling_rights(Color c, Square sq, bool value);
    void unset_castling_rights(Color c);

    u64 hash_castling() const;

    template<bool minimal>
    void move(Move m);

   public:
    Board() = default;

    constexpr Square castle_sq(const Color c, const bool kingside) const {
        return castling[castle_idx(c, kingside)];
    }

    u8 count(PieceType pt) const;

    u64 pieces() const;
    u64 pieces(Color c) const;
    u64 pieces(PieceType pt) const;
    u64 pieces(Color c, PieceType pt) const;
    u64 pieces(PieceType pt1, PieceType pt2) const;
    u64 pieces(Color c, PieceType pt1, PieceType pt2) const;

    u64 attackers_to(Square sq, u64 occ) const;

    u64 approx_key_after(Move m) const;

    void reset();

    void load_fen(const string& fen);
    string fen() const;

    PieceType get_piece(Square sq) const;
    bool is_capture(Move m) const;
    bool is_quiet(Move m) const;

    void move(Move m);
    void move(const string& str);

    bool can_null_move() const;
    void make_null_move();

    bool can_castle(Color c) const;
    bool can_castle(Color c, bool kingside) const;

    bool is_legal(Move m);

    bool in_check() const;
    bool is_under_attack(Color c, Square square) const;

    bool is_draw();
    bool is_game_over();

    bool see(Move m, int threshold) const;

    string str(Move m = Move::null()) const;

    friend u64 perft(Board& board, usize depth);
};