#pragma once

#include <chrono>

#include "types.h"

template<typename Precision>
class Stopwatch {
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point pause_time;

    bool paused;

    u64 paused_time;

   public:
    Stopwatch() {
        start();
    }

    void start() {
        start_time  = std::chrono::high_resolution_clock::now();
        paused_time = 0;
        paused      = false;
    }

    void reset() {
        start();
    }

    u64 elapsed() {
        u64 paused_time = this->paused_time;
        if (paused)
            paused_time += std::chrono::duration_cast<Precision>(std::chrono::high_resolution_clock::now() - pause_time).count();
        return std::chrono::duration_cast<Precision>(std::chrono::high_resolution_clock::now() - start_time).count() - paused_time;
    }

    void pause() {
        paused     = true;
        pause_time = std::chrono::high_resolution_clock::now();
    }
    void resume() {
        paused = false;
        paused_time += std::chrono::duration_cast<Precision>(std::chrono::high_resolution_clock::now() - pause_time).count();
    }
};