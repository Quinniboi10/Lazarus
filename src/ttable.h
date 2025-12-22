#pragma once

#include "move.h"
#include "types.h"

#include <cstring>
#include <thread>
#include <vector>

struct Transposition {
    u64  key;
    Move move;
    i16  score;
    u8   flag;
    u8   depth;

    Transposition() {
        key   = 0;
        move  = Move::null();
        flag  = 0;
        score = 0;
        depth = 0;
    }
    Transposition(const u64 key, const Move bestMove, const u8 flag, const i16 score, const u8 depth) {
        this->key   = key;
        this->move  = bestMove;
        this->flag  = flag;
        this->score = score;
        this->depth = depth;
    }
};

class TranspositionTable {
    Transposition* table;

   public:
    u64 size;

    explicit TranspositionTable(const usize sizeInMB = 16) {
        table = nullptr;
        reserve(sizeInMB);
    }

    ~TranspositionTable() {
        if (table != nullptr)
            std::free(table);
    }


    void clear(const usize threadCount = 1) {
        assert(threadCount > 0);

        std::vector<std::thread> threads;

        auto clearTT = [&](const usize threadId) {
            // The segment length is the number of entries each thread must clear
            // To find where your thread should start (in entries), you can do threadId * segmentLength
            // Converting segment length into the number of entries to clear can be done via length * bytes per entry

            const usize start = (size * threadId) / threadCount;
            const usize end   = std::min((size * (threadId + 1)) / threadCount, size);

            std::memset(table + start, 0, (end - start) * sizeof(Transposition));
        };

        for (usize thread = 1; thread < threadCount; thread++)
            threads.emplace_back(clearTT, thread);

        clearTT(0);

        for (std::thread& t : threads)
            if (t.joinable())
                t.join();
    }

    void reserve(const usize newSizeMiB) {
        assert(newSizeMiB > 0);
        // Find number of bytes allowed
        size = newSizeMiB * 1024 * 1024 / sizeof(Transposition);
        if (table != nullptr)
            std::free(table);
        table = static_cast<Transposition*>(std::malloc(size * sizeof(Transposition)));
    }

    u64 index(const u64 key) const {
        return static_cast<u64>((static_cast<u128>(key) * static_cast<u128>(size)) >> 64);
    }

    void prefetch(const u64 key) {
        __builtin_prefetch(&this->getEntry(key));
    }

    Transposition& getEntry(const u64 key) {
        return table[index(key)];
    }


    bool shouldReplace(const Transposition& entry, const Transposition& newEntry) const {
        return true;
    }

    usize hashfull() const {
        const usize samples = std::min<u64>(1000, size);
        usize       hits    = 0;
        for (usize sample = 0; sample < samples; sample++)
            hits += table[sample].key != 0;
        const usize hash = hits * 1000.0 / samples;
        assert(hash <= 1000);
        return hash;
    }
};