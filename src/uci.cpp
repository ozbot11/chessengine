#include "uci.h"
#include "book.h"
#include "movegen.h"
#include "search.h"
#include "perft.h"
#include "tt.h"
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static std::vector<std::pair<int,int>> g_gameMoves;

// ── Move format ───────────────────────────────────────────────────────────────
std::string moveToString(const Move& m) {
    char buf[6] = {};
    buf[0] = 'a' + fileOf(m.from);
    buf[1] = '1' + rankOf(m.from);
    buf[2] = 'a' + fileOf(m.to);
    buf[3] = '1' + rankOf(m.to);
    if (m.promotion != EMPTY) {
        static const char promoChar[] = ".pnbrqk.pnbrqk";
        buf[4] = promoChar[m.promotion];
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }
    return std::string(buf);
}

Move stringToMove(Board& board, const std::string& s) {
    if (s.size() < 4) return NULL_MOVE;

    int fromFile = s[0] - 'a';
    int fromRank = s[1] - '1';
    int toFile   = s[2] - 'a';
    int toRank   = s[3] - '1';

    if (fromFile < 0 || fromFile > 7 || fromRank < 0 || fromRank > 7 ||
        toFile   < 0 || toFile   > 7 || toRank   < 0 || toRank   > 7)
        return NULL_MOVE;

    int from = makeSquare(fromRank, fromFile);
    int to   = makeSquare(toRank,   toFile);

    Piece promo = EMPTY;
    if (s.size() >= 5) {
        bool white = board.whiteToMove;
        switch (s[4]) {
            case 'q': promo = white ? WQ : BQ; break;
            case 'r': promo = white ? WR : BR; break;
            case 'b': promo = white ? WB : BB; break;
            case 'n': promo = white ? WN : BN; break;
        }
    }

    // Find the matching legal move
    std::vector<Move> moves = generateMoves(board);
    for (const Move& m : moves) {
        if (m.from == from && m.to == to && m.promotion == promo)
            return m;
    }
    return NULL_MOVE;
}

// ── Position parsing ──────────────────────────────────────────────────────────
static void handlePosition(Board& board, const std::string& line) {
    g_gameMoves.clear();

    std::istringstream ss(line);
    std::string token;
    ss >> token;  // "position"

    ss >> token;
    if (token == "startpos") {
        board.reset();
        ss >> token;  // might be "moves" or eof
    } else if (token == "fen") {
        std::string fen;
        while (ss >> token && token != "moves") {
            if (!fen.empty()) fen += ' ';
            fen += token;
        }
        board.loadFEN(fen);
        // token might now be "moves"
    }

    // token should be "moves" at this point (if present)
    if (token == "moves") {
        while (ss >> token) {
            Move m = stringToMove(board, token);
            if (!m.isNull()) {
                g_gameMoves.emplace_back((int)m.from, (int)m.to);
                board.makeMove(m);
            }
        }
    }
}

// ── Go command ────────────────────────────────────────────────────────────────
static void handleGo(Board& board, const std::string& line) {
    std::istringstream ss(line);
    std::string token;
    ss >> token;  // "go"

    int depth       = 6;
    int movetime    = -1;
    int wtime       = -1, btime = -1;
    int winc        = 0,  binc  = 0;
    bool perftMode  = false;
    int perftDepth  = 1;

    while (ss >> token) {
        if      (token == "depth")    { ss >> depth; }
        else if (token == "movetime") { ss >> movetime; }
        else if (token == "wtime")    { ss >> wtime; }
        else if (token == "btime")    { ss >> btime; }
        else if (token == "winc")     { ss >> winc; }
        else if (token == "binc")     { ss >> binc; }
        else if (token == "perft")    { perftMode = true; ss >> perftDepth; }
    }

    if (perftMode) {
        uint64_t nodes = perft(board, perftDepth);
        std::cout << "nodes " << nodes << "\n";
        return;
    }

    // Basic time management: use 1/20 of remaining time
    int timeLimit = movetime;
    if (timeLimit < 0) {
        int myTime = board.whiteToMove ? wtime : btime;
        int myInc  = board.whiteToMove ? winc  : binc;
        if (myTime > 0) {
            timeLimit = myTime / 20 + myInc / 2;
        }
    }

    // Try book first, but skip if it would repeat or reverse a recent move
    Move best = bookProbe(board);
    if (!best.isNull()) {
        board.makeMove(best);
        uint64_t newHash = board.zobristHash;
        board.undoMove();

        bool wouldRepeat = false;
        for (const auto& u : board.history) {
            if (u.zobristHash == newHash) { wouldRepeat = true; break; }
        }

        // Also skip if book move reverses or replays a recent game move
        if (!wouldRepeat) {
            int sz = (int)g_gameMoves.size();
            for (int back = 1; back <= 6 && back <= sz; ++back) {
                auto [pfrom, pto] = g_gameMoves[sz - back];
                bool isReversal = ((int)best.from == pto   && (int)best.to == pfrom);
                bool isSameMove = ((int)best.from == pfrom && (int)best.to == pto);
                if (isReversal || isSameMove) { wouldRepeat = true; break; }
            }
        }

        if (!wouldRepeat) {
            std::cout << "bestmove " << moveToString(best) << "\n";
            return;
        }
    }

    setGameMoves(g_gameMoves);
    best = iterativeDeepening(board, depth, timeLimit);
    std::cout << "bestmove " << moveToString(best) << "\n";
}

// ── Main UCI loop ─────────────────────────────────────────────────────────────
void uciLoop() {
    Board board;
    board.reset();

    // Load opening book (look next to the executable)
    if (bookLoad("book.bin"))
        std::cerr << "info string Book loaded: book.bin\n";
    else
        std::cerr << "info string No book found, playing without book\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            std::cout << "id name MyEngine\n";
            std::cout << "id author Me\n";
            std::cout << "uciok\n";
        } else if (cmd == "isready") {
            std::cout << "readyok\n";
        } else if (cmd == "ucinewgame") {
            board.reset();
            clearSearchState();
        } else if (cmd == "position") {
            handlePosition(board, line);
        } else if (cmd == "go") {
            handleGo(board, line);
        } else if (cmd == "display" || cmd == "d") {
            board.display();
        } else if (cmd == "perft") {
            int d = 5;
            ss >> d;
            runPerftSuite();
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        }

        std::cout.flush();
    }
}
