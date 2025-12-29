#include "thread.h"
#include <tuple>

ThreadData::ThreadData(const ThreadType type, std::atomic<bool>& breakFlag) :
    type(type),
    breakFlag(breakFlag) {
    deepFill(history, 0);
    nodes    = 0;
    seldepth = 0;
}
ThreadData::ThreadData(const ThreadData& other) :
    history(other.history),
    accumulatorStack(other.accumulatorStack),
    type(other.type),
    breakFlag(other.breakFlag),
    seldepth(other.seldepth) {
    nodes.store(other.nodes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

std::pair<Board, ThreadStackManager> ThreadData::makeMove(const Board& board, const Move m) {
    Board newBoard = board;
    newBoard.move(m);

    accumulatorStack.push(accumulatorStack.top());
    accumulatorStack.topAsReference().update(newBoard, m, board.getPiece(m.to()));

    return { std::piecewise_construct, std::forward_as_tuple(std::move(newBoard)), std::forward_as_tuple(*this) };
}

std::pair<Board, ThreadStackManager> ThreadData::makeNullMove(const Board& board) {
    Board newBoard = board;
    newBoard.nullMove();

    accumulatorStack.push(accumulatorStack.top());

    return { std::piecewise_construct, std::forward_as_tuple(std::move(newBoard)), std::forward_as_tuple(*this) };
}

void ThreadData::refresh(const Board& b) {
    accumulatorStack.clear();

    AccumulatorPair accumulators{};
    accumulators.resetAccumulators(b);
    accumulatorStack.push(accumulators);
}

void ThreadData::reset() {
    deepFill(history, 0);
}