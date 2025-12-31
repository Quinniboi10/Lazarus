#pragma once

#include "types.h"

// ************ TUNING ************
// #define TUNE

// ************ HISTORIES ************
constexpr i32 MAX_HISTORY   = 16384;
constexpr i32 MAX_CORRHIST  = 1024;
constexpr i32 CORRHIST_SIZE = 16384;

// ************ SEARCH ************
constexpr usize MAX_PLY     = 255;
constexpr i16   BENCH_DEPTH = 9;

inline usize MOVE_OVERHEAD = 20;

// ************ NNUE ************
constexpr i16    QA             = 255;
constexpr i16    QB             = 64;
constexpr i16    EVAL_SCALE     = 400;
constexpr size_t HL_SIZE        = 1024;
constexpr size_t OUTPUT_BUCKETS = 8;

constexpr int ReLU   = 0;
constexpr int CReLU  = 1;
constexpr int SCReLU = 2;

constexpr int ACTIVATION = SCReLU;

// ************ VERIFICATIONS ************
constexpr bool VERIFY_BOARD_KEYAFTER = false;
constexpr bool VERIFY_BOARD_HASH     = false;