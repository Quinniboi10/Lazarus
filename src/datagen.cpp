#include "datagen.h"

#include <random>
#include <vector>

#include "util.h"
#include "movegen.h"
#include "searcher.h"

namespace datagen {
    void gen_fens(const string& params) {
        if (params.empty())
            return;

        std::vector<string> tokens = split(params, ' ');

        const auto get_value_following = [&](const string& value, const auto& default_value) {
            const auto loc  = std::find(tokens.begin(), tokens.end(), value);
            const usize idx = std::distance(tokens.begin(), loc) + 1;
            if (loc == tokens.end() || idx >= tokens.size()) {
                std::ostringstream ss;
                ss << default_value;
                return ss.str();
            }
            return tokens[idx];
        };

        const auto is_valid_pos = [](const Board& board) {
            Searcher searcher(false);
            Stopwatch<std::chrono::milliseconds> time;
            searcher.start(board, SearchParams(time, BENCH_DEPTH, 0, 0, 0, 0, 0, 0, 0, 0));
            searcher.wait_unit_done();

            return std::abs(searcher.score) <= MAX_STARTPOS_SCORE;
        };

        const u64 num_fens = std::stoull(get_value_following("genfens", 1));
        const u64 seed     = std::stoull(get_value_following("seed", std::time(nullptr)));

        std::mt19937 eng(seed);
        std::uniform_int_distribution<int> dist(0, 1);
        const auto rand_bool = [&]() { return dist(eng); };

        u64 fens = 0;
        while (fens < num_fens) {
start_loop:
            Board board;
            board.reset();
            const usize n_random_moves = datagen::RAND_MOVES + rand_bool();
            for (usize i = 0; i < n_random_moves; i++) {
                MoveList moves = movegen::gen_legal_moves(board);
                std::uniform_int_distribution<int> dist(0, moves.length - 1);
                board.move(moves.moves[dist(eng)]);
                if (board.is_game_over())
                    goto start_loop;
            }

            if (!is_valid_pos(board))
                continue;

            cout << "info string genfens " << board.fen() << endl;
            fens++;
        }

        cout << "info string Generated " << fens << " positions" << endl;
    }
}