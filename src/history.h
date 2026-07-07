#pragma once

#include "config.h"
#include "types.h"

#include <algorithm>

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
        const i32 clampedBonus = std::clamp<i32>(bonus, -MAX_VALUE, MAX_VALUE);
        value += clampedBonus - value * abs(clampedBonus) / MAX_VALUE;
    }
};

using ConthistSegment = MultiArray<HistoryEntry<MAX_HISTORY>, 2, 6, 64>;