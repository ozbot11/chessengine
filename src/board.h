#pragma once
#include "types.h"
#include <cstdint>
#include <string>
#include <vector>

// ── Undo record ───────────────────────────────────────────────────────────────
struct UndoInfo {
    Move     move;
    Piece    captured;
    bool     castlingRights[4];
    int      enPassantSquare;
    int      halfMoveClock;
    uint64_t zobristHash;      // hash before this move; restored by undoMove
};

// ── Board ────────────────────────────────────────────────────────────────────
struct Board {
    Piece    squares[64];
    bool     whiteToMove;
    bool     castlingRights[4];   // CR_WK, CR_WQ, CR_BK, CR_BQ
    int      enPassantSquare;     // -1 if none
    int      halfMoveClock;
    int      fullMoveNumber;
    uint64_t zobristHash;         // incrementally maintained Zobrist hash

    std::vector<UndoInfo> history;

    // ── Setup ────────────────────────────────────────────────────────────────
    void reset();
    void loadFEN(const std::string& fen);
    std::string toFEN() const;
    void display() const;

    // ── Move execution ───────────────────────────────────────────────────────
    void makeMove(const Move& m);
    void undoMove();

    // ── Helpers ──────────────────────────────────────────────────────────────
    Color    sideToMove()   const { return whiteToMove ? WHITE : BLACK; }
    uint64_t computeHash()  const;  // compute hash from scratch (called by loadFEN)
};
