#pragma once

#include <algorithm>

#include "assert.h"
#include "config.h"

struct Board;

class Move {
    // See https://www.chessprogramming.org/Encoding_Moves
    // PROMO = 15
    // CAPTURE = 14
    // SPECIAL 1 = 13
    // SPECIAL 0 = 12
    u16 move;

   public:
    constexpr Move()                  = default;
    constexpr Move(const Move& other) = default;
    constexpr ~Move()                 = default;

    constexpr Move(const u8 start_sq, const u8 end_sq, const MoveType flags = STANDARD_MOVE) {
        move = start_sq | flags;
        move |= end_sq << 6;
        move |= flags << 12;
    }

    constexpr Move(const u8 start_sq, const u8 end_sq, const PieceType promo) {
        move = start_sq | PROMOTION;
        move |= end_sq << 6;
        move |= (promo - 1) << 12;
    }

    Move(const string& str_in, const Board& board);

    constexpr static Move null() {
        return {a1, a1, STANDARD_MOVE};
    }


    string str() const;

    Square from() const {
        return static_cast<Square>(move & 0b111111);
    }
    Square to() const {
        return static_cast<Square>((move >> 6) & 0b111111);
    }

    MoveType type() const {
        return static_cast<MoveType>(move & 0xC000);
    }

    PieceType promo() const {
        traced_assert(type() == PROMOTION);
        return static_cast<PieceType>(((move >> 12) & 0b11) + 1);
    }

    bool is_null() const {
        return *this == null();
    }

    bool operator==(const Move other) const {
        return move == other.move;
    }

    friend std::ostream& operator<<(std::ostream& os, const Move& m) {
        os << m.str();
        return os;
    }
};

struct MoveEvaluation {
    Move move;
    i16 eval;

    MoveEvaluation()                            = default;
    MoveEvaluation(const MoveEvaluation& other) = default;
    MoveEvaluation(const Move move, const i16 eval) {
        this->move = move;
        this->eval = eval;
    }
    ~MoveEvaluation() = default;
};

struct PvList {
    array<Move, MAX_PLY> moves;
    u32 length = 0;

    PvList()                    = default;
    PvList(const PvList& other) = default;
    ~PvList()                   = default;

    void update(const Move move, const PvList& child) {
        moves[0] = move;
        std::copy(child.moves.begin(), child.moves.begin() + child.length, moves.begin() + 1);

        length = child.length + 1;

        traced_assert(length == 1 || moves[0] != moves[1]);
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
    usize length = 0;

    MoveList() = default;

    void add(const Move m) {
        traced_assert(length < 256);
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
        traced_assert(has(m));
        const auto location = std::find(begin(), end(), m);
        if (location != end()) {
            *(location) = moves[length - 1];
            length--;
        }
    }
};