#include "zobrist.h"

namespace Zobrist {
    uint64_t pieceKeys[13][64];
    uint64_t sideKey;
    uint64_t castleKeys[4];
    uint64_t epKeys[8];

    // Deterministic xorshift64
    static uint64_t rng(uint64_t& s) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }

    void init() {
        uint64_t seed = 0x123456789ABCDEF0ULL;
        for (int p = 0; p < 13; ++p)
            for (int sq = 0; sq < 64; ++sq)
                pieceKeys[p][sq] = rng(seed);
        sideKey = rng(seed);
        for (int i = 0; i < 4; ++i) castleKeys[i] = rng(seed);
        for (int i = 0; i < 8; ++i) epKeys[i]     = rng(seed);
    }
}
