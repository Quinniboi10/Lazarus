#pragma once

#include "types.h"

namespace datagen {
    constexpr int RAND_MOVES          = 8;
    constexpr int MAX_STARTPOS_SCORE  = 400;
    constexpr int GENFENS_VERIF_NODES = 2'000;

    void genFens(const string& params);
}