#pragma once
#include <cstdint>

namespace Zobrist {
    extern uint64_t pieceKeys[13][64];  // [piece 0-12][square 0-63]
    extern uint64_t sideKey;            // XOR in when black to move
    extern uint64_t castleKeys[4];      // one per castling right (CR_WK…CR_BQ)
    extern uint64_t epKeys[8];          // one per en-passant file

    void init();  // must be called once before any Board is created
}
