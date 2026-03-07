#include "eval.h"
#include <algorithm>

// ── Piece-Square Tables ───────────────────────────────────────────────────────

static const int PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_KNIGHT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

static const int PST_BISHOP[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

static const int PST_ROOK[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};

static const int PST_QUEEN[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};

static const int PST_KING_MG[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

static int pstValue(Piece p, int sq) {
    int mirroredSq = sq;
    if (isBlackPiece(p)) {
        int r = rankOf(sq), f = fileOf(sq);
        mirroredSq = makeSquare(7 - r, f);
    }
    switch (p) {
        case WP: case BP: return PST_PAWN[mirroredSq];
        case WN: case BN: return PST_KNIGHT[mirroredSq];
        case WB: case BB: return PST_BISHOP[mirroredSq];
        case WR: case BR: return PST_ROOK[mirroredSq];
        case WQ: case BQ: return PST_QUEEN[mirroredSq];
        case WK: case BK: return PST_KING_MG[mirroredSq];
        default: return 0;
    }
}

// ── Passed pawn bonus by rank (white's perspective, rank 0..7) ────────────────
static const int PASSED_PAWN_BONUS[8] = { 0, 5, 10, 20, 40, 65, 90, 0 };

// ── King safety: pawn shield + center penalty ──────────────────────────────────
static int kingShield(const Board& board, bool white) {
    Piece myK = white ? WK : BK;
    Piece myP = white ? WP : BP;
    int kSq = -1;
    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] == myK) { kSq = sq; break; }
    }
    if (kSq < 0) return 0;

    int kFile = fileOf(kSq);
    int kRank = rankOf(kSq);
    int homeRank = white ? 0 : 7;

    // Penalty if king in center files (c..f) away from home rank
    if (kFile >= 2 && kFile <= 5 && kRank != homeRank) return -30;

    // Pawn shield: three files in front of king
    int penalty = 0;
    int shieldDir = white ? 1 : -1;
    int rank1 = kRank + shieldDir;
    int rank2 = kRank + 2 * shieldDir;

    for (int df = -1; df <= 1; ++df) {
        int f = kFile + df;
        if (f < 0 || f > 7) continue;

        bool pawn1 = (rank1 >= 0 && rank1 <= 7 &&
                      board.squares[makeSquare(rank1, f)] == myP);
        bool pawn2 = (rank2 >= 0 && rank2 <= 7 &&
                      board.squares[makeSquare(rank2, f)] == myP);

        if (!pawn1 && !pawn2) penalty -= 20;
        else if (!pawn1 && pawn2) penalty -= 8;
    }
    return penalty;
}

// ── evaluate ──────────────────────────────────────────────────────────────────
int evaluate(const Board& board) {
    int score = 0;

    int whiteBishops = 0, blackBishops = 0;
    int pawnFiles[2][8] = {};
    // frontRank[color][file]: for passed pawn detection
    // white: highest white pawn rank on that file (-1 if none)
    // black: lowest black pawn rank on that file (8 if none)
    int whitePawnRank[8], blackPawnRank[8];
    for (int f = 0; f < 8; ++f) {
        whitePawnRank[f] = -1;  // highest white pawn rank
        blackPawnRank[f] =  8;  // lowest black pawn rank (8 = none)
    }

    // Single pass: material + PST + pawn/bishop counts
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board.squares[sq];
        if (p == EMPTY) continue;

        int val = PIECE_VALUE[p] + pstValue(p, sq);
        if (isWhitePiece(p)) {
            score += val;
            if (p == WB) ++whiteBishops;
            if (p == WP) {
                int f = fileOf(sq), r = rankOf(sq);
                ++pawnFiles[WHITE][f];
                whitePawnRank[f] = std::max(whitePawnRank[f], r);
            }
        } else {
            score -= val;
            if (p == BB) ++blackBishops;
            if (p == BP) {
                int f = fileOf(sq), r = rankOf(sq);
                ++pawnFiles[BLACK][f];
                blackPawnRank[f] = std::min(blackPawnRank[f], r);
            }
        }
    }

    // Bishop pair
    if (whiteBishops >= 2) score += 30;
    if (blackBishops >= 2) score -= 30;

    // Pawn structure + passed pawns (using pre-computed pawn ranks)
    for (int f = 0; f < 8; ++f) {
        // Doubled pawns
        if (pawnFiles[WHITE][f] > 1) score -= 20 * (pawnFiles[WHITE][f] - 1);
        if (pawnFiles[BLACK][f] > 1) score += 20 * (pawnFiles[BLACK][f] - 1);

        // Isolated pawns
        bool wHasNeighbor = (f > 0 && pawnFiles[WHITE][f-1] > 0) ||
                            (f < 7 && pawnFiles[WHITE][f+1] > 0);
        bool bHasNeighbor = (f > 0 && pawnFiles[BLACK][f-1] > 0) ||
                            (f < 7 && pawnFiles[BLACK][f+1] > 0);
        if (pawnFiles[WHITE][f] > 0 && !wHasNeighbor) score -= 15;
        if (pawnFiles[BLACK][f] > 0 && !bHasNeighbor) score += 15;

        // Passed white pawn: highest white pawn rank on this file with no black
        // pawn on same/adjacent files at a higher rank.
        if (whitePawnRank[f] >= 0) {
            int wr = whitePawnRank[f];
            bool passed = true;
            for (int df = -1; df <= 1 && passed; ++df) {
                int nf = f + df;
                if (nf < 0 || nf > 7) continue;
                // Black pawn blocks if it's on a rank > wr
                if (blackPawnRank[nf] > wr && blackPawnRank[nf] <= 7) passed = false;
            }
            if (passed) score += PASSED_PAWN_BONUS[wr];
        }

        // Passed black pawn: lowest black pawn rank on this file with no white
        // pawn on same/adjacent files at a lower rank.
        if (blackPawnRank[f] <= 7) {
            int br = blackPawnRank[f];
            bool passed = true;
            for (int df = -1; df <= 1 && passed; ++df) {
                int nf = f + df;
                if (nf < 0 || nf > 7) continue;
                if (whitePawnRank[nf] >= 0 && whitePawnRank[nf] < br) passed = false;
            }
            if (passed) score -= PASSED_PAWN_BONUS[7 - br];
        }
    }

    // Rook on open / semi-open file
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board.squares[sq];
        if (p != WR && p != BR) continue;
        int f = fileOf(sq);
        bool noWP = (pawnFiles[WHITE][f] == 0);
        bool noBP = (pawnFiles[BLACK][f] == 0);
        int bonus = 0;
        if (noWP && noBP)       bonus = 20;
        else if (p==WR && noWP) bonus = 10;
        else if (p==BR && noBP) bonus = 10;
        if (p == WR) score += bonus;
        else         score -= bonus;
    }

    // King safety
    score += kingShield(board, true);
    score -= kingShield(board, false);

    return board.whiteToMove ? score : -score;
}
