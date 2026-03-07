#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ── Piece ────────────────────────────────────────────────────────────────────
enum Piece : uint8_t {
    EMPTY = 0,
    WP, WN, WB, WR, WQ, WK,
    BP, BN, BB, BR, BQ, BK
};

// ── Color ────────────────────────────────────────────────────────────────────
enum Color : uint8_t { WHITE = 0, BLACK = 1 };

inline Color opponent(Color c) { return c == WHITE ? BLACK : WHITE; }

inline bool isWhitePiece(Piece p) { return p >= WP && p <= WK; }
inline bool isBlackPiece(Piece p) { return p >= BP && p <= BK; }
inline bool isEmpty(Piece p)      { return p == EMPTY; }

inline Color pieceColor(Piece p) {
    return isWhitePiece(p) ? WHITE : BLACK;
}

// ── Square constants ──────────────────────────────────────────────────────────
// Indexed as rank*8 + file  (A1=0, B1=1, … H8=63)
enum Square : int {
    A1=0,  B1,  C1,  D1,  E1,  F1,  G1,  H1,
    A2,  B2,  C2,  D2,  E2,  F2,  G2,  H2,
    A3,  B3,  C3,  D3,  E3,  F3,  G3,  H3,
    A4,  B4,  C4,  D4,  E4,  F4,  G4,  H4,
    A5,  B5,  C5,  D5,  E5,  F5,  G5,  H5,
    A6,  B6,  C6,  D6,  E6,  F6,  G6,  H6,
    A7,  B7,  C7,  D7,  E7,  F7,  G7,  H7,
    A8,  B8,  C8,  D8,  E8,  F8,  G8,  H8,
    NO_SQUARE = -1
};

inline int rankOf(int sq) { return sq >> 3; }
inline int fileOf(int sq) { return sq & 7; }
inline int makeSquare(int rank, int file) { return rank * 8 + file; }

// ── Castling right indices ───────────────────────────────────────────────────
//  0=WK  1=WQ  2=BK  3=BQ
static constexpr int CR_WK = 0;
static constexpr int CR_WQ = 1;
static constexpr int CR_BK = 2;
static constexpr int CR_BQ = 3;

// ── Move ─────────────────────────────────────────────────────────────────────
struct Move {
    int   from         = 0;
    int   to           = 0;
    Piece promotion    = EMPTY;  // EMPTY = no promotion
    bool  isCastling   = false;
    bool  isEnPassant  = false;

    bool operator==(const Move& o) const {
        return from == o.from && to == o.to &&
               promotion == o.promotion &&
               isCastling == o.isCastling &&
               isEnPassant == o.isEnPassant;
    }
    bool isNull() const { return from == 0 && to == 0; }
};

static const Move NULL_MOVE{};

// ── Piece values (centipawns) ────────────────────────────────────────────────
static constexpr int PIECE_VALUE[13] = {
    0,          // EMPTY
    100,        // WP
    320,        // WN
    330,        // WB
    500,        // WR
    900,        // WQ
    20000,      // WK
    100,        // BP
    320,        // BN
    330,        // BB
    500,        // BR
    900,        // BQ
    20000       // BK
};

// Piece value by type index 0-5 (P/N/B/R/Q/K)
static constexpr int PT_VALUE[6] = {100, 320, 330, 500, 900, 20000};

static constexpr int INF_SCORE  = 1000000;
static constexpr int MATE_SCORE = 900000;
