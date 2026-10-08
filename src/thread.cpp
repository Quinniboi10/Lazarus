#include "thread.h"

#include <tuple>

ThreadData::ThreadData(const ThreadType type, std::atomic<bool>& break_flag) :
    type(type),
    break_flag(break_flag) {
    break_flag.store(false, std::memory_order_relaxed);

    deepfill(history, 0);
    nodes    = 0;
    seldepth = 0;
}
ThreadData::ThreadData(const ThreadData& other) :
    history(other.history),
    accum_stack(other.accum_stack),
    type(other.type),
    break_flag(other.break_flag),
    seldepth(other.seldepth) {
    nodes.store(other.nodes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

std::pair<Board, ThreadStackManager> ThreadData::make_move(const Board& board, const Move m) {
    Board new_board = board;
    new_board.move(m);

    accum_stack.push(accum_stack.top());
    accum_stack.top_as_ref().update(new_board, m, board.get_piece(m.to()));

    return {std::piecewise_construct, std::forward_as_tuple(std::move(new_board)), std::forward_as_tuple(*this)};
}

std::pair<Board, ThreadStackManager> ThreadData::make_null_move(const Board& board) {
    Board new_board = board;
    new_board.make_null_move();

    accum_stack.push(accum_stack.top());

    return {std::piecewise_construct, std::forward_as_tuple(std::move(new_board)), std::forward_as_tuple(*this)};
}

void ThreadData::refresh(const Board& b) {
    accum_stack.clear();

    AccumulatorPair accumulators{};
    accumulators.recompute_all(b);
    accum_stack.push(accumulators);
}

void ThreadData::reset() {
    deepfill(history, 0);
    deepfill(capthist, 0);
    deepfill(pawn_corrhist, 0);
    deepfill(major_corrhist, 0);
}