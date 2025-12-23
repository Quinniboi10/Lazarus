#pragma once

#include "move.h"
#include "tunable.h"
#include "types.h"

#include "../external/fmt/fmt/color.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

inline bool readBit(const u64 bb, const usize idx) {
    return (1ULL << idx) & bb;
}

template<u8 value>
inline void setBit(u64& bitboard, const usize idx) {
    assert(idx <= sizeof(u64) * 8);
    if constexpr (value)
        bitboard |= (1ULL << idx);
    else
        bitboard &= ~(1ULL << idx);
}

inline Square popLSB(u64& bb) {
    assert(bb > 0);
    const auto sq = static_cast<Square>(std::countr_zero(bb));
    bb &= bb - 1;
    return sq;
}

inline Square getLSB(const u64 bb) {
    return static_cast<Square>(std::countr_zero(bb));
}

template<int dir>
inline u64 shift(const u64 bb) {
    return dir > 0 ? bb << dir : bb >> -dir;
}

inline u64 shift(const int dir, const u64 bb) {
    return dir > 0 ? bb << dir : bb >> -dir;
}

inline std::vector<string> split(const string& str, const char delim) {
    std::vector<std::string> result;

    std::istringstream stream(str);

    for (std::string token{}; std::getline(stream, token, delim);) {
        if (token.empty())
            continue;

        result.push_back(token);
    }

    return result;
}

inline string mergeFromIndex(const std::vector<string>& arr, const usize n) {
    std::ostringstream oss;
    for (usize i = n; i < arr.size(); ++i) {
        if (i > n)
            oss << ' ';
        oss << arr[i];
    }
    return oss.str();
}

// Function from stockfish
template<typename IntType>
inline IntType readLittleEndian(std::istream& stream) {
    IntType result;

    if (IS_LITTLE_ENDIAN)
        stream.read(reinterpret_cast<char*>(&result), sizeof(IntType));
    else {
        std::uint8_t                  u[sizeof(IntType)];
        std::make_unsigned_t<IntType> v = 0;

        stream.read(reinterpret_cast<char*>(u), sizeof(IntType));
        for (usize i = 0; i < sizeof(IntType); ++i)
            v = (v << 8) | u[sizeof(IntType) - i - 1];

        std::memcpy(&result, &v, sizeof(IntType));
    }

    return result;
}

template<typename T, typename U>
inline void deepFill(T& dest, const U& val) {
    dest = val;
}

template<typename T, usize N, typename U>
inline void deepFill(std::array<T, N>& arr, const U& value) {
    for (auto& element : arr) {
        deepFill(element, value);
    }
}

inline i32 getPieceValue(const PieceType pt) {
    if (pt == PAWN)
        return PAWN_VALUE;
    if (pt == KNIGHT)
        return KNIGHT_VALUE;
    if (pt == BISHOP)
        return BISHOP_VALUE;
    if (pt == ROOK)
        return ROOK_VALUE;
    if (pt == QUEEN)
        return QUEEN_VALUE;
    return 0;
}


constexpr Rank rankOf(const Square s) {
    return static_cast<Rank>(s >> 3);
}
constexpr File fileOf(const Square s) {
    return static_cast<File>(s & 0b111);
}

constexpr Rank flipRank(const Square s) {
    return static_cast<Rank>(s ^ 0b111000);
}

constexpr Square toSquare(const Rank rank, const File file) {
    return static_cast<Square>((static_cast<int>(rank) << 3) | file);
}

// Takes square (h8) and converts it into a bitboard index (64)
constexpr Square parseSquare(const std::string_view square) {
    return static_cast<Square>((square.at(1) - '1') * 8 + (square.at(0) - 'a'));
}

// Takes a square (64) and converts into algebraic notation (h8)
inline string squareToAlgebraic(const int sq) {
    return fmt::format("{}{}", static_cast<char>('a' + (sq % 8)), static_cast<char>('1' + (sq / 8)));
}

constexpr u8 castleIndex(const Color c, const bool kingside) {
    return c == WHITE ? (kingside ? 3 : 2) : (kingside ? 1 : 0);
}

// Print a bitboard (for debugging individual bitboards)
inline void printBitboard(const u64 bitboard) {
    for (int rank = 7; rank >= 0; --rank) {
        cout << "+---+---+---+---+---+---+---+---+" << endl;
        for (int file = 0; file < 8; ++file) {
            const int  i            = rank * 8 + file;  // Map rank and file to bitboard index
            const char currentPiece = readBit(bitboard, i) ? '1' : ' ';

            cout << "| " << currentPiece << " ";
        }
        cout << "|" << endl;
    }
    cout << "+---+---+---+---+---+---+---+---+" << endl;
}

// Formats a number with commas
inline string formatNum(const i64 v) {
    auto s = std::to_string(v);

    int n = s.length() - 3;
    if (v < 0)
        n--;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }

    return s;
}

// Fancy formats a time
inline string formatTime(const u64 timeInMS) {
    long long       seconds = timeInMS / 1000;
    const long long hours   = seconds / 3600;
    seconds %= 3600;
    const long long minutes = seconds / 60;
    seconds %= 60;

    string result;

    if (hours > 0)
        result += std::to_string(hours) + "h ";
    if (minutes > 0 || hours > 0)
        result += std::to_string(minutes) + "m ";
    if (seconds > 0 || minutes > 0 || hours > 0)
        result += std::to_string(seconds) + "s";
    if (result == "")
        return std::to_string(timeInMS) + "ms";
    return result;
}

inline int findIndexOf(const auto arr, string entry) {
    auto it = std::find(arr.begin(), arr.end(), entry);
    if (it != arr.end()) {
        return std::distance(arr.begin(), it);  // Calculate the index
    }
    return -1;  // Not found
}

inline string suffixNum(double num) {
    char suffix = '\0';
    if (num >= 1'000'000'000 * 10.0) {
        num /= 1'000'000'000;
        suffix = 'G';
    }
    else if (num >= 1'000'000 * 10.0) {
        num /= 1'000'000;
        suffix = 'M';
    }
    else if (num >= 1'000 * 10.0) {
        num /= 1'000;
        suffix = 'K';
    }

    return fmt::format("{:.2f}{}", num, suffix);
}

// Parses human-readable numbers
inline u64 parseSuffixedNum(string text) {
    assert(!text.empty());

    // Trim leading/trailing whitespace
    auto is_space = [](const unsigned char c) { return std::isspace(c); };
    while (!text.empty() && is_space(text.front()))
        text.erase(0, 1);
    while (!text.empty() && is_space(text.back()))
        text.erase(text.size() - 1);

    assert(!text.empty());

    double multiplier = 1.0;

    // Handle optional suffix
    const auto last = static_cast<unsigned char>(text.back());
    if (std::isalpha(last)) {
        const char suffix = static_cast<char>(std::tolower(last));
        text.erase(text.size() - 1);

        switch (suffix) {
        case 'k': multiplier = 1'000.0; break;
        case 'm': multiplier = 1'000'000.0; break;
        case 'b':
        case 'g': multiplier = 1'000'000'000.0; break;
        case 't': multiplier = 1'000'000'000'000.0; break;
        default:  cerr << "Unknown number suffix" << endl; std::abort();
        }
    }

    assert(!text.empty());

    std::erase(text, ',');

    const string numeric(text);
    const double value = std::stod(numeric);

    return std::round(value * multiplier);
}

inline int getTerminalRows() {
    auto envLines = []() -> int {
        if (const char* s = std::getenv("LINES")) {
            char*      end = nullptr;
            const long v   = std::strtol(s, &end, 10);
            if (end != s && v > 0 && v < 100000)
                return v;
        }
        return -1;
    };

#ifdef _WIN32
    // Try the visible window height of the current console.
    if (HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); h != nullptr && h != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            int win_rows = static_cast<int>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
            if (win_rows > 0)
                return win_rows;

            if (csbi.dwSize.Y > 0)
                return static_cast<int>(csbi.dwSize.Y);
        }
    }

    int r = envLines();
    return (r > 0) ? r : 24;

#else
    winsize ws{};

    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;

    if (isatty(STDIN_FILENO) && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;

    const int r = envLines();
    return (r > 0) ? r : 24;
#endif
}

// Heat color
inline void heatColor(float t, const string& text) {
    t = std::clamp(t, 0.0f, 1.0f);
    u8 r, g, b = 0;
    if (t < 0.5f) {
        const float ratio = t / 0.5f;
        r                 = 255;
        g                 = static_cast<u8>(ratio * 255);
    }
    else {
        const float ratio = (t - 0.5f) / 0.5f;
        r                 = static_cast<u8>(255 * (1.0f - ratio));
        g                 = 255;
    }

    fmt::print(fmt::fg(fmt::rgb(r, g, b)), "{}", text);
}

// Colored progress bar
inline void coloredProgBar(const usize length, const float fill) {
    if (length == 0) {
        fmt::print("[] 0%");
        return;
    }
    fmt::print("[");
    for (usize i = 0; i < length; ++i) {
        const float percentage = static_cast<float>(i) / (length - 1);
        if (percentage <= fill) {
            heatColor(1 - percentage, "#");
        }
        else {
            fmt::print(".");
        }
    }
    fmt::print("] {}%", static_cast<usize>(fill * 100));
}

// Score color
inline void printColoredScore(const i16 cp) {
    const double wdl      = 2 / (1 + std::pow(std::numbers::e, -(cp / 400.0f))) - 1;
    const double colorWdl = std::clamp(wdl * 1.5f, -1.0, 1.0);
    u8           r, g, b;

    const auto lerp = [](const double a, const double b, const double t) { return a + t * (b - a); };

    if (colorWdl < 0) {
        const double t = colorWdl + 1.0;
        r              = static_cast<u8>(lerp(255, 255, t));  // red stays max
        g              = static_cast<u8>(lerp(0, 255, t));    // green rises
        b              = static_cast<u8>(lerp(0, 255, t));    // blue rises
    }
    else {
        const double t = colorWdl;                            // maps 0 -> 1
        r              = static_cast<u8>(lerp(255, 0, t));    // red drops
        g              = static_cast<u8>(lerp(255, 255, t));  // green stays max
        b              = static_cast<u8>(lerp(255, 0, t));    // blue drops
    }

    fmt::print(fmt::fg(fmt::rgb(r, g, b)), "{:.2f}", cp / 100.0f);
}

inline void printPV(const PvList& pv, const usize numToShow = 12, const u8 colorDecay = 10, const u8 minColor = 96) {
    fmt::rgb color(255, 255, 255);

    const usize endIdx = std::min<usize>(numToShow, pv.length);

    for (usize idx = 0; idx < endIdx; idx++) {
        fmt::print(fg(color), "{}", pv.moves[idx].toString());
        if (idx != endIdx - 1)
            fmt::print(" ");

        color.r -= colorDecay;
        color.g -= colorDecay;
        color.b -= colorDecay;

        color.r = std::max(color.r, minColor);
        color.g = std::max(color.g, minColor);
        color.b = std::max(color.b, minColor);
    }

    const usize remaining = pv.length - endIdx;
    if (remaining > 0)
        fmt::print(fg(color), " ({} remaining)", remaining);
}