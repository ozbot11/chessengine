#include "nnue.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

// ── Network dimensions ────────────────────────────────────────────────────────
static constexpr int INPUT_SIZE  = 768;   // 6 piece types × 2 colors × 64 squares
static constexpr int L1_SIZE     = 256;
static constexpr int L2_SIZE     = 32;
static constexpr int OUTPUT_SIZE = 1;

// ── Weight storage ────────────────────────────────────────────────────────────
static float g_L1_w[INPUT_SIZE][L1_SIZE];
static float g_L1_b[L1_SIZE];
static float g_L2_w[L1_SIZE][L2_SIZE];
static float g_L2_b[L2_SIZE];
static float g_L3_w[L2_SIZE];
static float g_L3_b;

static bool g_loaded = false;

// ── Feature indexing ──────────────────────────────────────────────────────────
// feat = piece_type_idx * 128 + color_idx * 64 + square
// piece_type_idx: P=0, N=1, B=2, R=3, Q=4, K=5
// color_idx: White=0, Black=1
static int pieceTypeIdx(Piece p) {
    if (p >= WP && p <= WK) return p - WP;  // 0..5
    if (p >= BP && p <= BK) return p - BP;  // 0..5
    return -1;
}

static void getActiveFeatures(const Board& board, int* feats, int& count) {
    count = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board.squares[sq];
        if (p == EMPTY) continue;
        int typeIdx  = pieceTypeIdx(p);
        int colorIdx = isWhitePiece(p) ? 0 : 1;
        feats[count++] = typeIdx * 128 + colorIdx * 64 + sq;
    }
}

// ── File loading ──────────────────────────────────────────────────────────────
bool nnueLoad(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    // Magic "NNUE"
    char magic[4];
    if (!f.read(magic, 4)) return false;
    if (magic[0]!='N' || magic[1]!='N' || magic[2]!='U' || magic[3]!='E') return false;

    // Version
    uint32_t version;
    if (!f.read(reinterpret_cast<char*>(&version), 4)) return false;
    if (version != 1) return false;

    // Weights
    if (!f.read(reinterpret_cast<char*>(g_L1_w), sizeof(g_L1_w))) return false;
    if (!f.read(reinterpret_cast<char*>(g_L1_b), sizeof(g_L1_b))) return false;
    if (!f.read(reinterpret_cast<char*>(g_L2_w), sizeof(g_L2_w))) return false;
    if (!f.read(reinterpret_cast<char*>(g_L2_b), sizeof(g_L2_b))) return false;
    if (!f.read(reinterpret_cast<char*>(g_L3_w), sizeof(g_L3_w))) return false;
    if (!f.read(reinterpret_cast<char*>(&g_L3_b), sizeof(g_L3_b))) return false;

    g_loaded = true;
    return true;
}

bool nnueLoaded() { return g_loaded; }

// ── Forward pass ─────────────────────────────────────────────────────────────
int nnueEvaluate(const Board& board) {
    // Collect active feature indices
    int feats[32];
    int featCount = 0;
    getActiveFeatures(board, feats, featCount);

    // Layer 1: sparse accumulation + ReLU
    float hidden1[L1_SIZE];
    for (int j = 0; j < L1_SIZE; ++j) hidden1[j] = g_L1_b[j];
    for (int i = 0; i < featCount; ++i) {
        int fi = feats[i];
        for (int j = 0; j < L1_SIZE; ++j)
            hidden1[j] += g_L1_w[fi][j];
    }
    for (int j = 0; j < L1_SIZE; ++j)
        hidden1[j] = hidden1[j] > 0.0f ? hidden1[j] : 0.0f;

    // Layer 2: dense + ReLU
    float hidden2[L2_SIZE];
    for (int j = 0; j < L2_SIZE; ++j) hidden2[j] = g_L2_b[j];
    for (int i = 0; i < L1_SIZE; ++i) {
        if (hidden1[i] == 0.0f) continue;
        for (int j = 0; j < L2_SIZE; ++j)
            hidden2[j] += hidden1[i] * g_L2_w[i][j];
    }
    for (int j = 0; j < L2_SIZE; ++j)
        hidden2[j] = hidden2[j] > 0.0f ? hidden2[j] : 0.0f;

    // Output layer: dot product
    float out = g_L3_b;
    for (int i = 0; i < L2_SIZE; ++i)
        out += hidden2[i] * g_L3_w[i];

    // Convert to integer centipawns; sign relative to side to move
    int score = static_cast<int>(out);
    return board.whiteToMove ? score : -score;
}
