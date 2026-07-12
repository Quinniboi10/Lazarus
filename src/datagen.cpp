#include "datagen.h"

#include <random>
#include <vector>

#include "util.h"
#include "movegen.h"
#include "searcher.h"

namespace datagen {
    void genFens(const string& params) {
        if (params.empty())
            return;

        std::vector<string> tokens = split(params, ' ');

        const auto getValueFollowing = [&](const string& value, const auto& defaultValue) {
            const auto loc  = std::find(tokens.begin(), tokens.end(), value);
            const usize idx = std::distance(tokens.begin(), loc) + 1;
            if (loc == tokens.end() || idx >= tokens.size()) {
                std::ostringstream ss;
                ss << defaultValue;
                return ss.str();
            }
            return tokens[idx];
        };

        const auto isValidPosition = [](const Board& board) {
            Searcher searcher(false);
            Stopwatch<std::chrono::milliseconds> time;
            searcher.start(board, SearchParams(time, BENCH_DEPTH, 0, 0, 0, 0, 0, 0, 0, 0));
            searcher.waitUntilFinished();

            return std::abs(searcher.score) <= MAX_STARTPOS_SCORE;
        };

        const u64 numFens = std::stoull(getValueFollowing("genfens", 1));
        const u64 seed    = std::stoull(getValueFollowing("seed", std::time(nullptr)));

        std::mt19937 eng(seed);
        std::uniform_int_distribution<int> dist(0, 1);
        const auto randBool = [&]() { return dist(eng); };

        u64 fens = 0;
        while (fens < numFens) {
startLoop:
            Board board;
            board.reset();
            const usize randomMoves = datagen::RAND_MOVES + randBool();
            for (usize i = 0; i < randomMoves; i++) {
                MoveList moves = Movegen::generateLegalMoves(board);
                std::uniform_int_distribution<int> dist(0, moves.length - 1);
                board.move(moves.moves[dist(eng)]);
                if (board.isGameOver())
                    goto startLoop;
            }

            if (!isValidPosition(board))
                continue;

            cout << "info string genfens " << board.fen() << endl;
            fens++;
        }

        cout << "info string Generated " << fens << " positions" << endl;
    }
}