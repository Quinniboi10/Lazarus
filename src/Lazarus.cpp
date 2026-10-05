#include <atomic>
#include <bitset>
#include <string>

#include "board.h"
#include "datagen.h"
#include "move.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "searcher.h"
#include "types.h"

#ifndef EVALFILE
static_assert(false && "EVALFILE must be defined for network embedding or loading to work.");
#endif

#ifdef _MSC_VER
    #define MSVC
    #pragma push_macro("_MSC_VER")
    #undef _MSC_VER
#endif

#include "../external/incbin.h"

#ifdef MSVC
    #pragma pop_macro("_MSC_VER")
    #undef MSVC
#endif

#if !defined(_MSC_VER) || defined(__clang__)
INCBIN(EVAL, EVALFILE);
#endif

NNUE nnue;
bool chess960       = false;
bool use_soft_nodes = false;

// ****** MAIN ENTRY POINT, HANDLES UCI ******
int main(const int argc, char* argv[]) {
    movegen::init_databases();

    auto load_default_net = [&]([[maybe_unused]] bool warn_msvc = false) {
#if defined(_MSC_VER) && !defined(__clang__) && defined(EVALFILE)
        nnue.load_net(EVALFILE);
        if (warn_msvc)
            cerr << "WARNING: This file was compiled with MSVC, this means that an nnue was NOT embedded into the exe." << endl;
#else
        nnue = *reinterpret_cast<const NNUE*>(gEVALData);
#endif
    };

    load_default_net(true);

    Board board;
    string command;

    board.reset();

    Searcher searcher(true);

    const auto get_value_following = [&](const string& str, const string& value, const auto& default_value) {
        std::istringstream ss(str);
        string token;
        while (ss >> token) {
            if (token == value) {
                ss >> token;
                return token;
            }
        }

        std::ostringstream default_ss;
        default_ss << default_value;
        return default_ss.str();
    };


    // *********** ./Lazarus <ARGS> ************
    if (argc > 1) {
        // Convert args into strings
        std::vector<string> args;
        args.resize(argc);
        for (int i = 0; i < argc; i++)
            args[i] = argv[i];

        if (args[1] == "bench")
            bench();
        else if (args[1].substr(0, 7) == "genfens")
            datagen::gen_fens(args[1]);
        else if (args[1] == "tune-config") {
#ifdef TUNE
            print_tune_info();
#endif
        }
        return 0;
    }

    // ************ UCI ************

    cout << "Lazarus ready" << endl;
    while (true) {
        std::getline(std::cin, command);
        const Stopwatch<std::chrono::milliseconds> command_time;
        if (command.empty())
            continue;
        const std::vector<string> tokens = split(command, ' ');

        if (command == "uci") {
            searcher.do_uci = true;

            cout << "id name Lazarus"
#ifdef GIT_HEAD_COMMIT_ID
                 << " (" << GIT_HEAD_COMMIT_ID << ")"
#endif
                 << endl;
            cout << "id author Quinniboi10" << endl;
            cout << "option name Threads type spin default 1 min 1 max 2048" << endl;
            cout << "option name Hash type spin default 16 min 1 max 524288" << endl;
            cout << "option name Move Overhead type spin default 20 min 0 max 1000" << endl;
            cout << "option name EvalFile type string default internal" << endl;
            cout << "option name UCI_Chess960 type check default false" << endl;
            cout << "option name Softnodes type check default false" << endl;
#ifdef TUNE
            print_tune_uci();
#endif
            cout << "uciok" << endl;
        }
        else if (command == "icu") {
            searcher.do_uci = false;
            cout << "koicu" << endl;
        }
        else if (command == "ucinewgame")
            searcher.reset();
        else if (command == "isready")
            cout << "readyok" << endl;
        else if (tokens[0] == "position") {
            if (tokens[1] == "startpos") {
                board.reset();
                if (tokens.size() > 2 && tokens[2] == "moves")
                    for (usize i = 3; i < tokens.size(); i++)
                        board.move(tokens[i]);
            }
            else if (tokens[1] == "kiwipete")
                board.load_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
            else if (tokens[1] == "fen") {
                board.load_fen(command.substr(13));
                if (tokens.size() > 8 && tokens[8] == "moves")
                    for (usize i = 9; i < tokens.size(); i++)
                        board.move(tokens[i]);
            }
        }
        else if (tokens[0] == "go") {
            searcher.stop();

            const usize depth = std::stoi(get_value_following(command, "depth", MAX_PLY));

            usize hard_nodes = std::stoi(get_value_following(command, "nodes", 0));
            usize soft_nodes = std::stoi(get_value_following(command, "softnodes", 0));

            const usize mtime = std::stoi(get_value_following(command, "movetime", 0));
            const i64 wtime   = std::stoi(get_value_following(command, "wtime", 0));
            const i64 btime   = std::stoi(get_value_following(command, "btime", 0));

            const usize winc = std::stoi(get_value_following(command, "winc", 0));
            const usize binc = std::stoi(get_value_following(command, "binc", 0));

            const usize mate = std::stoi(get_value_following(command, "mate", 0));

            if (use_soft_nodes && hard_nodes) {
                soft_nodes = hard_nodes;
                hard_nodes = 0;
            }

            searcher.start(board, SearchParams(command_time, depth, hard_nodes, soft_nodes, mtime, wtime, btime, winc, binc, mate));
        }
        else if (tokens[0] == "setoption") {
            if (tokens[2] == "Threads")
                searcher.set_threads(std::stoull(get_value_following(command, "value", 1)));
            else if (tokens[2] == "Hash")
                searcher.resize_tt(std::stoull(get_value_following(command, "value", 16)));
            else if (tokens[2] == "Move" && tokens[3] == "Overhead")
                MOVE_OVERHEAD = std::stoi(tokens[get_index(tokens, "value") + 1]);
            else if (tokens[2] == "EvalFile") {
                const string value = tokens[get_index(tokens, "value") + 1];
                if (value == "internal")
                    load_default_net();
                else
                    nnue.load_net(value);
            }
            else if (tokens[2] == "UCI_Chess960")
                chess960 = tokens[get_index(tokens, "value") + 1] == "true";
            else if (tokens[2] == "Softnodes")
                use_soft_nodes = tokens[get_index(tokens, "value") + 1] == "true";
#ifdef TUNE
            else
                set_tunable(tokens[2], std::stoi(tokens[get_index(tokens, "value") + 1]));
#endif
        }
        else if (command == "stop")
            searcher.stop();
        else if (command == "wait")
            searcher.wait_unit_done();
        else if (command == "quit") {
            searcher.stop();
            return 0;
        }


        // ************ NON-UCI ************


        else if (command == "help")
            cout << "Lazarus is a UCI compatiable chess engine. For a list of commands please refer to the UCI spec." << endl;
        else if (command == "d")
            cout << board.str() << endl;
        else if (tokens[0] == "move")
            board.move(Move(tokens[1], board));
        else if (tokens[0] == "bulk") {
            if (tokens.size() < 2) {
                cout << "Usage: bulk <depth>" << endl;
                continue;
            }
            movegen::perft(board, std::stoi(tokens[1]), true);
        }
        else if (tokens[0] == "perft") {
            if (tokens.size() < 2) {
                cout << "Usage: perft <depth>" << endl;
                continue;
            }
            movegen::perft(board, std::stoi(tokens[1]), false);
        }
        else if (tokens[0] == "perftsuite")
            movegen::perft_suite(tokens[1]);
        else if (command == "eval") {
            searcher.thread_data[0].refresh(board);
            cout << "Raw eval: " << nnue.evaluate(&board, searcher.thread_data[0].accum_stack.top()) << endl;
            nnue.print_buckets(&board, searcher.thread_data[0].accum_stack.top());
        }
        else if (command == "moves") {
            for (Move m : movegen::gen_moves<ALL_MOVES>(board)) {
                cout << m;
                if (board.is_legal(m))
                    cout << " <- legal" << endl;
                else
                    cout << " <- illegal" << endl;
            }
        }
        else if (command == "gamestate") {
            const Square white_king = get_lsb(board.pieces(WHITE, KING));
            const Square black_king = get_lsb(board.pieces(BLACK, KING));
            cout << board.str() << endl;
            cout << "Is in check (white): " << board.is_under_attack(WHITE, white_king) << endl;
            cout << "Is in check (black): " << board.is_under_attack(BLACK, black_king) << endl;
            cout << "En passant square: " << (board.ep_sq != NO_SQUARE ? sq_to_algebraic(board.ep_sq) : "-") << endl;
            cout << "Half move clock: " << board.halfmove_ctr << endl;
            cout << "Castling rights: { ";
            cout << sq_to_algebraic(board.castling[castle_idx(WHITE, true)]) << ", ";
            cout << sq_to_algebraic(board.castling[castle_idx(WHITE, false)]) << ", ";
            cout << sq_to_algebraic(board.castling[castle_idx(BLACK, true)]) << ", ";
            cout << sq_to_algebraic(board.castling[castle_idx(BLACK, false)]);
            cout << " }" << endl;
        }
        else if (command == "incheck")
            cout << "Stm is " << (board.in_check() ? "in check" : "NOT in check") << endl;
        else if (tokens[0] == "islegal")
            cout << tokens[1] << " is " << (board.is_legal(Move(tokens[1], board)) ? "" : "not ") << "legal" << endl;
        else if (tokens[0] == "keyafter")
            cout << "Expected hash: 0x" << std::hex << std::uppercase << board.approx_key_after(Move(tokens[1], board)) << std::dec << endl;
        else if (command == "piececount") {
            cout << "White pawns: " << popcount(board.pieces(WHITE, PAWN)) << endl;
            cout << "White knights: " << popcount(board.pieces(WHITE, KNIGHT)) << endl;
            cout << "White bishops: " << popcount(board.pieces(WHITE, BISHOP)) << endl;
            cout << "White rooks: " << popcount(board.pieces(WHITE, ROOK)) << endl;
            cout << "White queens: " << popcount(board.pieces(WHITE, QUEEN)) << endl;
            cout << "White king: " << popcount(board.pieces(WHITE, KING)) << endl;
            cout << endl;
            cout << "Black pawns: " << popcount(board.pieces(BLACK, PAWN)) << endl;
            cout << "Black knights: " << popcount(board.pieces(BLACK, KNIGHT)) << endl;
            cout << "Black bishops: " << popcount(board.pieces(BLACK, BISHOP)) << endl;
            cout << "Black rooks: " << popcount(board.pieces(BLACK, ROOK)) << endl;
            cout << "Black queens: " << popcount(board.pieces(BLACK, QUEEN)) << endl;
            cout << "Black king: " << popcount(board.pieces(BLACK, KING)) << endl;
        }
        else {
            cerr << "Unknown command: " << command << endl;
        }
    }
}