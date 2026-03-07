#pragma once
#include "types.h"
#include <cstdint>

static constexpr uint8_t TT_EXACT      = 0;
static constexpr uint8_t TT_LOWERBOUND = 1;
static constexpr uint8_t TT_UPPERBOUND = 2;

struct TTEntry {
    uint64_t key;       // full Zobrist key for verification
    int      score;     // score at this node
    Move     bestMove;  // best / refutation move (may be NULL_MOVE)
    int16_t  depth;     // remaining depth when stored
    uint8_t  flag;      // TT_EXACT / TT_LOWERBOUND / TT_UPPERBOUND
    uint8_t  pad;
};

namespace TT {
    // Returns pointer to entry if key matches, nullptr on miss.
    TTEntry* probe(uint64_t key);

    // Store entry; replaces if new depth is >= stored depth, or exact flag.
    void store(uint64_t key, int depth, int score, uint8_t flag, const Move& best);

    void clear();
}
