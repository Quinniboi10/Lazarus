#include <atomic>
#include <bitset>
#include <string>

#include "board.h"
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
bool chess960          = false;
bool nodesAreSoftNodes = false;

// ****** MAIN ENTRY POINT, HANDLES UCI ******
int main(const int argc, char* argv[]) {
    Movegen::initializeAllDatabases();

    auto loadDefaultNet = [&]([[maybe_unused]] bool warnMSVC = false) {
#if defined(_MSC_VER) && !defined(__clang__) && defined(EVALFILE)
        nnue.loadNetwork(EVALFILE);
        if (warnMSVC)
            cerr << "WARNING: This file was compiled with MSVC, this means that an nnue was NOT embedded into the exe." << endl;
#else
        nnue = *reinterpret_cast<const NNUE*>(gEVALData);
#endif
    };

    loadDefaultNet(true);

    Board  board;
    string command;

    board.reset();

    Searcher searcher(true);

    const auto getValueFollowing = [&](const string& str, const string& value, const auto& defaultValue) {
        std::istringstream ss(str);
        string             token;
        while (ss >> token) {
            if (token == value) {
                ss >> token;
                return token;
            }
        }

        std::ostringstream defaultSS;
        defaultSS << defaultValue;
        return defaultSS.str();
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
        else if (args[1] == "tune-config") {
#ifdef TUNE
            printTuneOB();
#endif
        }
        return 0;
    }

    // ************ UCI ************

    cout << "Lazarus ready" << endl;
    while (true) {
        std::getline(std::cin, command);
        const Stopwatch<std::chrono::milliseconds> commandTime;
        if (command.empty())
            continue;
        const std::vector<string> tokens = split(command, ' ');

        if (command == "uci") {
            searcher.doUci = true;

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
            printTuneUCI();
#endif
            cout << "uciok" << endl;
        }
        else if (command == "icu") {
            searcher.doUci = false;
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
                board.loadFromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
            else if (tokens[1] == "fen") {
                board.loadFromFEN(command.substr(13));
                if (tokens.size() > 8 && tokens[8] == "moves")
                    for (usize i = 9; i < tokens.size(); i++)
                        board.move(tokens[i]);
            }
        }
        else if (tokens[0] == "go") {
            searcher.stop();

            const usize depth = std::stoi(getValueFollowing(command, "depth", MAX_PLY));

            usize maxNodes  = std::stoi(getValueFollowing(command, "nodes", 0));
            usize softNodes = std::stoi(getValueFollowing(command, "softnodes", 0));

            const usize mtime = std::stoi(getValueFollowing(command, "movetime", 0));
            const i64   wtime = std::stoi(getValueFollowing(command, "wtime", 0));
            const i64   btime = std::stoi(getValueFollowing(command, "btime", 0));

            const usize winc = std::stoi(getValueFollowing(command, "winc", 0));
            const usize binc = std::stoi(getValueFollowing(command, "binc", 0));

            const usize mate = std::stoi(getValueFollowing(command, "mate", 0));

            if (nodesAreSoftNodes && maxNodes) {
                softNodes = maxNodes;
                maxNodes  = 0;
            }

            searcher.start(board, SearchParams(commandTime, depth, maxNodes, softNodes, mtime, wtime, btime, winc, binc, mate));
        }
        else if (tokens[0] == "setoption") {
            if (tokens[2] == "Threads")
                searcher.setThreads(std::stoull(getValueFollowing(command, "value", 1)));
            else if (tokens[2] == "Hash")
                searcher.resizeTT(std::stoull(getValueFollowing(command, "value", 16)));
            else if (tokens[2] == "Move" && tokens[3] == "Overhead")
                MOVE_OVERHEAD = std::stoi(tokens[findIndexOf(tokens, "value") + 1]);
            else if (tokens[2] == "EvalFile") {
                const string value = tokens[findIndexOf(tokens, "value") + 1];
                if (value == "internal")
                    loadDefaultNet();
                else
                    nnue.loadNetwork(value);
            }
            else if (tokens[2] == "UCI_Chess960")
                chess960 = tokens[findIndexOf(tokens, "value") + 1] == "true";
            else if (tokens[2] == "Softnodes")
                nodesAreSoftNodes = tokens[findIndexOf(tokens, "value") + 1] == "true";
#ifdef TUNE
            else
                setTunable(tokens[2], std::stoi(tokens[findIndexOf(tokens, "value") + 1]));
#endif
        }
        else if (command == "stop")
            searcher.stop();
        else if (command == "wait")
            searcher.waitUntilFinished();
        else if (command == "quit") {
            searcher.stop();
            return 0;
        }


        // ************ NON-UCI ************


        else if (command == "help")
            cout << "Lazarus is a UCI compatiable chess engine. For a list of commands please refer to the UCI spec." << endl;
        else if (command == "d")
            cout << board.toString() << endl;
        else if (tokens[0] == "move")
            board.move(Move(tokens[1], board));
        else if (tokens[0] == "bulk") {
            if (tokens.size() < 2) {
                cout << "Usage: bulk <depth>" << endl;
                continue;
            }
            Movegen::perft(board, std::stoi(tokens[1]), true);
        }
        else if (tokens[0] == "perft") {
            if (tokens.size() < 2) {
                cout << "Usage: perft <depth>" << endl;
                continue;
            }
            Movegen::perft(board, std::stoi(tokens[1]), false);
        }
        else if (tokens[0] == "perftsuite")
            Movegen::perftSuite(tokens[1]);
        else if (command == "eval") {
            searcher.threadData[0].refresh(board);
            cout << "Raw eval: " << nnue.forwardPass(&board, searcher.threadData[0].accumulatorStack.top()) << endl;
            nnue.showBuckets(&board, searcher.threadData[0].accumulatorStack.top());
        }
        else if (command == "moves") {
            for (Move m : Movegen::generateMoves<ALL_MOVES>(board)) {
                cout << m;
                if (board.isLegal(m))
                    cout << " <- legal" << endl;
                else
                    cout << " <- illegal" << endl;
            }
        }
        else if (command == "gamestate") {
            const Square whiteKing = getLSB(board.pieces(WHITE, KING));
            const Square blackKing = getLSB(board.pieces(BLACK, KING));
            cout << board.toString() << endl;
            cout << "Is in check (white): " << board.isUnderAttack(WHITE, whiteKing) << endl;
            cout << "Is in check (black): " << board.isUnderAttack(BLACK, blackKing) << endl;
            cout << "En passant square: " << (board.epSquare != NO_SQUARE ? squareToAlgebraic(board.epSquare) : "-") << endl;
            cout << "Half move clock: " << board.halfMoveClock << endl;
            cout << "Castling rights: { ";
            cout << squareToAlgebraic(board.castling[castleIndex(WHITE, true)]) << ", ";
            cout << squareToAlgebraic(board.castling[castleIndex(WHITE, false)]) << ", ";
            cout << squareToAlgebraic(board.castling[castleIndex(BLACK, true)]) << ", ";
            cout << squareToAlgebraic(board.castling[castleIndex(BLACK, false)]);
            cout << " }" << endl;
        }
        else if (command == "incheck")
            cout << "Stm is " << (board.inCheck() ? "in check" : "NOT in check") << endl;
        else if (tokens[0] == "islegal")
            cout << tokens[1] << " is " << (board.isLegal(Move(tokens[1], board)) ? "" : "not ") << "legal" << endl;
        else if (tokens[0] == "keyafter")
            cout << "Expected hash: 0x" << std::hex << std::uppercase << board.roughKeyAfter(Move(tokens[1], board)) << std::dec << endl;
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