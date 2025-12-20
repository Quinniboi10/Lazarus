#pragma once

#include <algorithm>
#include <cassert>

#include "config.h"
#include "types.h"

struct Board;

class Move {
    // See https://www.chessprogramming.org/Encoding_Moves
    // PROMO = 15
    // CAPTURE = 14
    // SPECIAL 1 = 13
    // SPECIAL 0 = 12
   private:
    u16 move;

   public:
    constexpr Move()  = default;
    constexpr ~Move() = default;

    constexpr Move(const u8 startSquare, const u8 endSquare, const MoveType flags = STANDARD_MOVE) {
        move = startSquare | flags;
        move |= endSquare << 6;
        move |= flags << 12;
    }

    constexpr Move(const u8 startSquare, const u8 endSquare, const PieceType promo) {
        move = startSquare | PROMOTION;
        move |= endSquare << 6;
        move |= (promo - 1) << 12;
    }

    Move(const string& strIn, const Board& board);

    constexpr static Move null() {
        return { a1, a1, STANDARD_MOVE };
    }


    string toString() const;

    Square from() const {
        return static_cast<Square>(move & 0b111111);
    }
    Square to() const {
        return static_cast<Square>((move >> 6) & 0b111111);
    }

    MoveType typeOf() const {
        return static_cast<MoveType>(move & 0xC000);
    }

    PieceType promo() const {
        assert(typeOf() == PROMOTION);
        return static_cast<PieceType>(((move >> 12) & 0b11) + 1);
    }

    bool isNull() const {
        return *this == null();
    }

    bool operator==(const Move other) const {
        return move == other.move;
    }

    friend std::ostream& operator<<(std::ostream& os, const Move& m) {
        os << m.toString();
        return os;
    }
};

struct MoveEvaluation {
    Move move;
    i16  eval;

    MoveEvaluation(const Move move, const i16 eval) {
        this->move = move;
        this->eval = eval;
    }
};

struct PvList {
    array<Move, MAX_PLY> moves;
    u32                  length;

    PvList() {
        length = 0;
    }

    void update(const Move move, const PvList& child) {
        moves[0] = move;
        std::copy(child.moves.begin(), child.moves.begin() + child.length, moves.begin() + 1);

        length = child.length + 1;

        assert(length == 1 || moves[0] != moves[1]);
    }

    auto begin() {
        return moves.begin();
    }
    auto end() {
        return moves.begin() + length;
    }
    auto begin() const {
        return moves.begin();
    }
    auto end() const {
        return moves.begin() + length;
    }

    auto& operator=(const PvList& other) {
        std::copy(other.moves.begin(), other.moves.begin() + other.length, moves.begin());
        length = other.length;

        return *this;
    }
};

struct MoveList {
    array<Move, 256> moves;
    usize            length;

    constexpr MoveList() {
        length = 0;
    }

    void add(const Move m) {
        assert(length < 256);
        moves[length++] = m;
    }

    void add(const u8 from, const u8 to, const MoveType flags = STANDARD_MOVE) {
        add(Move(from, to, flags));
    }
    void add(const u8 from, const u8 to, const PieceType promo) {
        add(Move(from, to, promo));
    }

    auto begin() {
        return moves.begin();
    }
    auto end() {
        return moves.begin() + length;
    }
    auto begin() const {
        return moves.begin();
    }
    auto end() const {
        return moves.begin() + length;
    }

    bool has(const Move m) const {
        return std::find(begin(), end(), m) != end();
    }
    void remove(const Move m) {
        assert(has(m));
        const auto location = std::find(begin(), end(), m);
        if (location != end()) {
            *(location) = moves[length - 1];
            length--;
        }
    }
};