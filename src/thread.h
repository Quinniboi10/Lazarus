#pragma once

#include "accumulator.h"
#include "search.h"
#include "types.h"

#include <utility>

template<i32 MAX_VALUE>
struct HistoryEntry {
    i32 value;

    HistoryEntry() :
        value(0) {
    }
    HistoryEntry(const i32 v) :
        value(v) {
    }

    operator i32() const {
        return value;
    }

    void update(const i32 bonus) {
        const i32 clamped_bonus = std::clamp<i32>(bonus, -MAX_VALUE, MAX_VALUE);
        value += clamped_bonus - value * abs(clamped_bonus) / MAX_VALUE;
    }
};

struct ThreadData {
    // History is indexed [stm][from][to]
    MultiArray<HistoryEntry<MAX_HISTORY>, 2, 64, 64> history;

    // Capthist is indexed [stm][pt][captured pt][to]
    // En passant is a possible capture with no targeted type
    MultiArray<HistoryEntry<MAX_HISTORY>, 2, 6, 7, 64> capthist;

    // Pawn correction history indexed [stm][pawn key % size]
    MultiArray<HistoryEntry<MAX_CORRHIST>, 2, CORRHIST_SIZE> pawn_corrhist;

    // Major correction history indexed [stm][major key % size]
    MultiArray<HistoryEntry<MAX_CORRHIST>, 2, CORRHIST_SIZE> major_corrhist;

    // All the accumulators for each thread's search
    Stack<AccumulatorPair, MAX_PLY + 1> accum_stack;

    ThreadType type;

    std::atomic<bool>& break_flag;

    std::atomic<u64> nodes;
    usize seldepth;

    ThreadData(ThreadType type, std::atomic<bool>& break_flag);

    // Copy constructor
    ThreadData(const ThreadData& other);

    // Accessors for the histories
    auto& get_history(const Board& b, const Move m) {
        return history[b.stm][m.from()][m.to()];
    }
    auto& get_history(const Board& b, const Move m) const {
        return history[b.stm][m.from()][m.to()];
    }
    auto& get_capture_history(const Board& b, const Move m) {
        return capthist[b.stm][b.get_piece(m.from())][b.get_piece(m.to())][m.to()];
    }
    auto& get_capture_history(const Board& b, const Move m) const {
        return capthist[b.stm][b.get_piece(m.from())][b.get_piece(m.to())][m.to()];
    }
    void update_corrhist(const Board& b, const i16 depth, const i16 score, const i16 eval) {
        const i32 bonus = std::clamp<i32>((score - eval) * depth / 8, -MAX_CORRHIST / 4, MAX_CORRHIST / 4);
        pawn_corrhist[b.stm][b.pawn_hash % CORRHIST_SIZE].update(bonus);
        major_corrhist[b.stm][b.major_hash % CORRHIST_SIZE].update(bonus);
    }
    i16 correct_static_eval(const Board& b, const i16 static_eval) const {
        i32 correction = 0;
        correction += pawn_corrhist[b.stm][b.pawn_hash % CORRHIST_SIZE] * PAWN_CORRHIST_WEIGHT;
        correction += major_corrhist[b.stm][b.major_hash % CORRHIST_SIZE] * MAJOR_CORRHIST_WEIGHT;

        return std::clamp<i16>(static_eval + correction / 512, MATED_IN_MAX_PLY, MATE_IN_MAX_PLY);
    }

    std::pair<Board, ThreadStackManager> make_move(const Board& board, Move m);
    std::pair<Board, ThreadStackManager> make_null_move(const Board& board);

    // Reset the accumulator stack for a given position
    void refresh(const Board& b);
    // Reset data that lasts between searches
    void reset();
};

struct ThreadStackManager {
    ThreadData& this_thread;

    explicit ThreadStackManager(ThreadData& this_thread) :
        this_thread(this_thread) {
    }

    ThreadStackManager(const ThreadStackManager& other) = delete;

    ~ThreadStackManager() {
        this_thread.accum_stack.pop();
    }
};