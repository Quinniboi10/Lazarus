#pragma once

#include "move.h"
#include "types.h"

#include <cstring>
#include <thread>
#include <vector>

struct Transposition {
    u64 key;
    Move move;
    i16 score;
    u8 flag;
    u8 depth;

    Transposition() {
        key   = 0;
        move  = Move::null();
        flag  = 0;
        score = 0;
        depth = 0;
    }
    Transposition(const u64 key, const Move best_move, const u8 flag, const i16 score, const u8 depth) {
        this->key   = key;
        this->move  = best_move;
        this->flag  = flag;
        this->score = score;
        this->depth = depth;
    }
};

class TranspositionTable {
    Transposition* table;

   public:
    u64 size;

    explicit TranspositionTable(const usize size_mib = 16) {
        table = nullptr;
        reserve(size_mib);
    }

    ~TranspositionTable() {
        if (table != nullptr)
            std::free(table);
    }


    void clear(const usize thread_count = 1) {
        traced_assert(thread_count > 0);

        std::vector<std::thread> threads;

        auto clear_tt = [&](const usize thread_id) {
            // The segment length is the number of entries each thread must clear
            // To find where your thread should start (in entries), you can do thread_id * segmentLength
            // Converting segment length into the number of entries to clear can be done via length * bytes per entry

            const usize start = (size * thread_id) / thread_count;
            const usize end   = std::min((size * (thread_id + 1)) / thread_count, size);

            std::memset(table + start, 0, (end - start) * sizeof(Transposition));
        };

        for (usize thread = 1; thread < thread_count; thread++)
            threads.emplace_back(clear_tt, thread);

        clear_tt(0);

        for (std::thread& t : threads)
            if (t.joinable())
                t.join();
    }

    void reserve(const usize new_size_mib) {
        traced_assert(new_size_mib > 0);
        // Find number of bytes allowed
        size = new_size_mib * 1024 * 1024 / sizeof(Transposition);
        if (table != nullptr)
            std::free(table);
        table = static_cast<Transposition*>(std::malloc(size * sizeof(Transposition)));
    }

    u64 index(const u64 key) const {
        return static_cast<u64>((static_cast<u128>(key) * static_cast<u128>(size)) >> 64);
    }

    void prefetch(const u64 key) {
        __builtin_prefetch(&this->get(key));
    }

    Transposition& get(const u64 key) {
        return table[index(key)];
    }


    bool should_replace(const Transposition& entry, const Transposition& new_entry) const {
        return true;
    }

    usize hashfull() const {
        const usize samples = std::min<u64>(1000, size);
        usize hits          = 0;
        for (usize sample = 0; sample < samples; sample++)
            hits += table[sample].key != 0;
        const usize hash = hits * 1000.0 / samples;
        traced_assert(hash <= 1000);
        return hash;
    }
};