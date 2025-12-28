#pragma once

#include <iostream>

namespace cursor {
static void clearAll(std::ostream& out = std::cout) {
    out << "\033[2J\033[H";
}
static void clear(std::ostream& out = std::cout) {
    out << "\033[2K\r";
}
static void clearDown(std::ostream& out = std::cout) {
    out << "\x1b[J";
}
static void home(std::ostream& out = std::cout) {
    out << "\033[H";
}
static void up(std::ostream& out = std::cout) {
    out << "\033[A";
}
static void down(std::ostream& out = std::cout) {
    out << "\033[B";
}
static void begin(std::ostream& out = std::cout) {
    out << "\033[1G";
}
static void goTo(const unsigned x, const unsigned y, std::ostream& out = std::cout) {
    out << "\033[" << y << ";" << x << "H";
}

static void hide(std::ostream& out = std::cout) {
    out << "\033[?25l";
}
static void show(std::ostream& out = std::cout) {
    out << "\033[?25h";
}
static void cache(std::ostream& out = std::cout) {
    out << "\033[s";
}
static void load(std::ostream& out = std::cout) {
    out << "\033[u";
}
}