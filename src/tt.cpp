#include "tt.h"
#include <cstring>

// 1 M entries  ≈  32 MB
static constexpr int TT_SIZE = 1 << 20;
static TTEntry g_table[TT_SIZE];

namespace TT {
    TTEntry* probe(uint64_t key) {
        TTEntry& e = g_table[key & (TT_SIZE - 1)];
        if (e.key == key) return &e;
        return nullptr;
    }

    void store(uint64_t key, int depth, int score, uint8_t flag, const Move& best) {
        TTEntry& e = g_table[key & (TT_SIZE - 1)];
        // Replace if: different position, deeper search, or exact result
        if (e.key != key || depth >= (int)e.depth || flag == TT_EXACT) {
            e.key      = key;
            e.score    = score;
            e.bestMove = best;
            e.depth    = static_cast<int16_t>(depth);
            e.flag     = flag;
        }
    }

    void clear() {
        for (int i = 0; i < TT_SIZE; ++i)
            g_table[i] = TTEntry{0, 0, NULL_MOVE, 0, 0, 0};
    }
}
