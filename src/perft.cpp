#include "perft.h"
#include "movegen.h"
#include <iostream>
#include <iomanip>
#include <vector>

uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1ULL;

    std::vector<Move> moves = generateMoves(board);
    if (depth == 1) return moves.size();

    uint64_t nodes = 0;
    for (const Move& m : moves) {
        board.makeMove(m);
        nodes += perft(board, depth - 1);
        board.undoMove();
    }
    return nodes;
}

void runPerftSuite() {
    struct Case { const char* fen; int depth; uint64_t expected; };
    static const Case cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1,       20ULL},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2,      400ULL},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3,     8902ULL},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4,   197281ULL},
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5,  4865609ULL},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 1,    48ULL},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 2,  2039ULL},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 3, 97862ULL},
    };

    std::cout << "\n=== Perft Suite ===\n\n";
    bool allPassed = true;

    for (const auto& c : cases) {
        Board board;
        board.loadFEN(c.fen);

        uint64_t result = perft(board, c.depth);
        bool passed = (result == c.expected);
        if (!passed) allPassed = false;

        std::cout << "depth " << c.depth
                  << "  expected " << std::setw(10) << c.expected
                  << "  got " << std::setw(10) << result
                  << "  " << (passed ? "PASS" : "FAIL")
                  << "\n";
    }

    std::cout << "\nOverall: " << (allPassed ? "ALL PASSED" : "SOME FAILED") << "\n\n";
}
