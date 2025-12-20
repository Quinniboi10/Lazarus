#pragma once

#include "types.h"

#ifdef TUNE
struct IndividualOption {
    string name;
    i32    value;
    i32    min;
    i32    max;
    i32    step;

    IndividualOption(const string& name, i32 value);

    void setValue(const i32 value) {
        this->value = value;
    }

    operator i32() const {
        return value;
    }
};

inline std::vector<IndividualOption*> tunables;

inline IndividualOption::IndividualOption(const string& name, const i32 value) {
    this->name  = name;
    this->value = value;
    this->min   = value / 2;
    this->max   = value * 2;
    this->step  = (max - min) / 20;

    tunables.push_back(this);
}

static void setTunable(const string& name, const i32 value) {
    for (const auto& tunable : tunables) {
        if (tunable->name == name) {
            tunable->value = value;
            break;
        }
    }
}

static void printTuneUCI() {
    for (const auto& tunable : tunables)
        cout << "option name " << tunable->name << " type spin default " << tunable->value << " min " << tunable->min << " max " << tunable->max << endl;
}

static void printTuneOB() {
    for (const auto& tunable : tunables)
        cout << tunable->name << ", int, " << tunable->value << ", " << tunable->min << ", " << tunable->max << ", " << tunable->step << ", 0.002" << endl;
}

    #define Tunable(name, value) \
        inline IndividualOption name { \
            #name, value \
        }
#else
    #define Tunable(name, value) constexpr i32 name = value
#endif

// Piece values
Tunable(PAWN_VALUE, 100);
Tunable(KNIGHT_VALUE, 300);
Tunable(BISHOP_VALUE, 300);
Tunable(ROOK_VALUE, 500);
Tunable(QUEEN_VALUE, 800);

// Move ordering
Tunable(MO_VICTIM_SCALAR, 100);

// Time management
Tunable(DEFAULT_MOVES_TO_GO, 19018);  // Quantized by 1024
Tunable(INC_DIVISOR, 2156);           // Quantized by 1024