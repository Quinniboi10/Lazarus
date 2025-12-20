#include "thread.h"
#include <tuple>

namespace Search {
ThreadInfo::ThreadInfo(const ThreadType type, std::atomic<bool>& breakFlag) :
    type(type),
    breakFlag(breakFlag) {
    breakFlag.store(false, std::memory_order_relaxed);

    nodes     = 0;
    seldepth  = 0;
    minNmpPly = 0;
}
ThreadInfo::ThreadInfo(const ThreadInfo& other) :
    accumulatorStack(other.accumulatorStack),
    type(other.type),
    breakFlag(other.breakFlag),
    seldepth(other.seldepth),
    minNmpPly(other.minNmpPly) {
    nodes.store(other.nodes.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

std::pair<Board, ThreadStackManager> ThreadInfo::makeMove(const Board& board, const Move m) {
    Board newBoard = board;
    newBoard.move(m);

    accumulatorStack.push(accumulatorStack.top());
    accumulatorStack.topAsReference().update(newBoard, m, board.getPiece(m.to()));

    return { std::piecewise_construct, std::forward_as_tuple(std::move(newBoard)), std::forward_as_tuple(*this) };
}

void ThreadInfo::refresh(const Board& b) {
    accumulatorStack.clear();

    AccumulatorPair accumulators{};
    accumulators.resetAccumulators(b);
    accumulatorStack.push(accumulators);
}

void ThreadInfo::reset() {
    nodes.store(0, std::memory_order_relaxed);
    seldepth  = 0;
    minNmpPly = 0;
}

}
