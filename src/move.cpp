#include "move.h"

#include "board.h"
#include "globals.h"

Move::Move(const string& str_in, const Board& board) {
    const Square from = parse_sq(str_in.substr(0, 2));
    Square to         = parse_sq(str_in.substr(2, 2));

    MoveType flags = STANDARD_MOVE;

    // Move must be promotion
    if (str_in.size() > 4) {
        switch (str_in.at(4)) {
            case 'q':
                *this = Move(from, to, QUEEN);
                return;
            case 'r':
                *this = Move(from, to, ROOK);
                return;
            case 'b':
                *this = Move(from, to, BISHOP);
                return;
            default:
                *this = Move(from, to, KNIGHT);
                return;
        }
    }

    if (!chess960
        && ((from == e1 && to == g1 && board.can_castle(WHITE, true)) || (from == e1 && to == c1 && board.can_castle(WHITE, false)) || (from == e8 && to == g8 && board.can_castle(BLACK, true))
            || (from == e8 && to == c8 && board.can_castle(BLACK, false)))) {
        const bool kingside = to > from;

        to = board.castle_sq(board.stm, kingside);

        flags = CASTLE;
    }
    else if (chess960 && board.get_piece(from) == KING && ((1ULL << to) & board.pieces(board.stm, ROOK))) {
        const bool kingside = to > from;
        if (board.can_castle(board.stm, kingside))
            flags = CASTLE;
    }
    else if (to == board.ep_sq && ((1ULL << from) & board.pieces(board.stm, PAWN)))
        flags = EN_PASSANT;

    *this = Move(from, to, flags);
}

string Move::str() const {
    const MoveType mt = type();

    string move_str = sq_to_algebraic(from());
    if (mt == CASTLE && !chess960)
        return move_str + (sq_to_algebraic(to() + (from() < to() ? WEST : EAST * 2)));

    move_str += sq_to_algebraic(to());

    if (mt != PROMOTION)
        return move_str;

    switch (promo()) {
        case KNIGHT:
            move_str += 'n';
            break;
        case BISHOP:
            move_str += 'b';
            break;
        case ROOK:
            move_str += 'r';
            break;
        case QUEEN:
            move_str += 'q';
            break;
        default:
            break;
    }

    return move_str;
}