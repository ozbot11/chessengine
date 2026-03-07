#include "search.h"
#include "movegen.h"
#include "eval.h"
#include "tt.h"
#include "zobrist.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

static constexpr int MAX_PLY = 64;

// ── Killer moves ──────────────────────────────────────────────────────────────
static Move g_killers[MAX_PLY][2];

// ── History heuristic ─────────────────────────────────────────────────────────
static int g_history[13][64];

// ── Game + search path hashes for repetition detection ────────────────────────
// g_gameHashes: all positions from the game before the search started
// g_searchStack: positions along the current search path (indexed by ply)
static std::vector<uint64_t> g_gameHashes;
static uint64_t g_searchStack[MAX_PLY * 2];

// ── Recent game moves for shuffle/reversal detection ──────────────────────────
static std::vector<std::pair<int,int>> g_gameMoveHistory;

void setGameMoves(const std::vector<std::pair<int,int>>& moves) {
    g_gameMoveHistory = moves;
}

// ── Time management ────────────────────────────────────────────────────────────
using Clock = std::chrono::steady_clock;
static Clock::time_point g_startTime;
static int               g_timeLimitMs = -1;
static bool              g_timesUp     = false;

static bool isTimesUp() {
    if (g_timeLimitMs < 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - g_startTime).count();
    return elapsed >= g_timeLimitMs;
}

// ── Move ordering ─────────────────────────────────────────────────────────────
static const int MVV_LVA[6][6] = {
    {15, 25, 35, 45, 55, 65},
    {14, 24, 34, 44, 54, 64},
    {13, 23, 33, 43, 53, 63},
    {12, 22, 32, 42, 52, 62},
    {11, 21, 31, 41, 51, 61},
    {10, 20, 30, 40, 50, 60},
};

static int pieceTypeIdx(Piece p) {
    if (p >= WP && p <= WK) return p - WP;
    if (p >= BP && p <= BK) return p - BP;
    return 0;
}

static int scoreMoveForOrdering(const Board& board, const Move& m,
                                const Move& ttMove, int ply) {
    if (m == ttMove) return 30000;

    Piece victim = board.squares[m.to];
    if (m.isEnPassant) victim = board.whiteToMove ? BP : WP;

    if (victim != EMPTY) {
        Piece attacker = board.squares[m.from];
        return 10000 + MVV_LVA[pieceTypeIdx(attacker)][pieceTypeIdx(victim)];
    }
    if (m.promotion != EMPTY) return 9000;

    if (ply >= 0 && ply < MAX_PLY) {
        if (m == g_killers[ply][0]) return 8000;
        if (m == g_killers[ply][1]) return 7000;
    }

    Piece p = board.squares[m.from];
    if (p != EMPTY && p < 13) {
        return g_history[(int)p][(int)m.to];
    }
    return 0;
}

static void orderMoves(std::vector<Move>& moves, const Board& board,
                       const Move& ttMove, int ply) {
    std::sort(moves.begin(), moves.end(),
              [&](const Move& a, const Move& b) {
                  return scoreMoveForOrdering(board, a, ttMove, ply) >
                         scoreMoveForOrdering(board, b, ttMove, ply);
              });
}

// ── Quiescence search ─────────────────────────────────────────────────────────
static int quiescence(Board& board, int alpha, int beta) {
    int standPat = evaluate(board);
    if (standPat >= beta) return beta;
    if (standPat > alpha) alpha = standPat;

    std::vector<Move> moves = generateMoves(board);
    for (const Move& m : moves) {
        bool isCapture = board.squares[m.to] != EMPTY || m.isEnPassant;
        bool isPromo   = m.promotion != EMPTY;
        if (!isCapture && !isPromo) continue;

        board.makeMove(m);
        int score = -quiescence(board, -beta, -alpha);
        board.undoMove();

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// ── Alpha-beta with TT, LMR, PVS, check extensions ───────────────────────────
// extLeft: remaining extension budget for this branch (prevents cascade)
static int alphaBetaSearch(Board& board, int depth, int alpha, int beta,
                           bool inNullMove, int ply, int extLeft) {
    // Hard ply cap
    if (ply >= MAX_PLY) return quiescence(board, alpha, beta);

    // Check extension: if we're in check and have budget, extend by 1
    Color us = board.sideToMove();
    bool inCheck = isInCheck(board, us);
    if (inCheck && extLeft > 0) {
        ++depth;
        --extLeft;
    }

    // Time check
    if (g_timeLimitMs >= 0 && (ply & 3) == 0 && isTimesUp()) {
        g_timesUp = true;
        return 0;
    }

    if (depth <= 0) return quiescence(board, alpha, beta);

    // Record current position in the search path for repetition detection
    g_searchStack[ply] = board.zobristHash;

    int origAlpha = alpha;

    // ── TT probe ──────────────────────────────────────────────────────────────
    Move ttMove = NULL_MOVE;
    TTEntry* tte = TT::probe(board.zobristHash);
    if (tte) {
        ttMove = tte->bestMove;
        if ((int)tte->depth >= depth) {
            int s = tte->score;
            if (tte->flag == TT_EXACT)      return s;
            if (tte->flag == TT_LOWERBOUND) { if (s > alpha) alpha = s; }
            if (tte->flag == TT_UPPERBOUND) { if (s < beta)  beta  = s; }
            if (alpha >= beta) return s;
        }
    }

    // ── Generate moves ────────────────────────────────────────────────────────
    std::vector<Move> moves = generateMoves(board);
    if (moves.empty()) {
        return inCheck ? -MATE_SCORE - depth : 0;
    }

    // ── Null move pruning ─────────────────────────────────────────────────────
    if (!inNullMove && !inCheck && depth >= 3) {
        Piece myN = (us==WHITE)?WN:BN, myB=(us==WHITE)?WB:BB;
        Piece myR = (us==WHITE)?WR:BR, myQ=(us==WHITE)?WQ:BQ;
        bool hasNonPawn = false;
        for (int sq = 0; sq < 64 && !hasNonPawn; ++sq) {
            Piece p = board.squares[sq];
            if (p==myN||p==myB||p==myR||p==myQ) hasNonPawn = true;
        }
        if (hasNonPawn) {
            board.whiteToMove  = !board.whiteToMove;
            board.zobristHash ^= Zobrist::sideKey;
            int oldEP = board.enPassantSquare;
            if (oldEP >= 0) {
                board.zobristHash ^= Zobrist::epKeys[fileOf(oldEP)];
                board.enPassantSquare = -1;
            }

            constexpr int R = 2;
            int score = -alphaBetaSearch(board, depth - 1 - R, -beta, -beta + 1,
                                         true, ply + 1, extLeft);

            board.whiteToMove  = !board.whiteToMove;
            board.zobristHash ^= Zobrist::sideKey;
            board.enPassantSquare = oldEP;
            if (oldEP >= 0)
                board.zobristHash ^= Zobrist::epKeys[fileOf(oldEP)];

            if (!g_timesUp && score >= beta) return beta;
        }
    }

    // ── Order moves ───────────────────────────────────────────────────────────
    orderMoves(moves, board, ttMove, ply);

    // ── Search ────────────────────────────────────────────────────────────────
    Move bestMove = NULL_MOVE;
    int moveIdx = 0;

    for (const Move& m : moves) {
        bool isQuiet = (board.squares[m.to] == EMPTY && !m.isEnPassant
                        && m.promotion == EMPTY);
        Piece movingPiece = board.squares[m.from];

        board.makeMove(m);

        // ── Repetition detection: score as draw if position seen before ────────
        // Count occurrences in game history and current search path
        int score;
        {
            uint64_t h = board.zobristHash;
            int reps = 0;
            for (uint64_t gh : g_gameHashes) if (gh == h) ++reps;
            for (int p = ply - 1; p >= 0; p -= 2) if (g_searchStack[p] == h) ++reps;
            if (reps >= 1) {
                score = -10;
                board.undoMove();
                // Apply draw score to alpha-beta bounds
                if (score >= beta) return beta;
                if (score > alpha) { alpha = score; bestMove = m; }
                ++moveIdx;
                continue;
            }
        }

        if (moveIdx == 0) {
            score = -alphaBetaSearch(board, depth - 1, -beta, -alpha,
                                     inNullMove, ply + 1, extLeft);
        } else {
            int reduction = 0;
            if (!inCheck && isQuiet && depth >= 3 && moveIdx >= 3) {
                reduction = (moveIdx >= 6) ? 2 : 1;
            }

            score = -alphaBetaSearch(board, depth - 1 - reduction, -alpha - 1, -alpha,
                                     inNullMove, ply + 1, extLeft);

            if (!g_timesUp && score > alpha && reduction > 0) {
                score = -alphaBetaSearch(board, depth - 1, -alpha - 1, -alpha,
                                         inNullMove, ply + 1, extLeft);
            }

            if (!g_timesUp && score > alpha && score < beta) {
                score = -alphaBetaSearch(board, depth - 1, -beta, -alpha,
                                         inNullMove, ply + 1, extLeft);
            }
        }

        board.undoMove();

        if (g_timesUp) return 0;

        if (score >= beta) {
            TT::store(board.zobristHash, depth, beta, TT_LOWERBOUND, m);
            if (isQuiet && ply < MAX_PLY) {
                if (!(m == g_killers[ply][0])) {
                    g_killers[ply][1] = g_killers[ply][0];
                    g_killers[ply][0] = m;
                }
                if (movingPiece != EMPTY && (int)movingPiece < 13) {
                    int& h = g_history[(int)movingPiece][(int)m.to];
                    h = std::min(6000, h + depth * depth);
                }
            }
            return beta;
        }
        if (score > alpha) {
            alpha    = score;
            bestMove = m;
        }
        ++moveIdx;
    }

    uint8_t flag = (alpha > origAlpha) ? TT_EXACT : TT_UPPERBOUND;
    TT::store(board.zobristHash, depth, alpha, flag, bestMove);
    return alpha;
}

// ── Public helpers ────────────────────────────────────────────────────────────
void clearSearchState() {
    TT::clear();
    for (int i = 0; i < MAX_PLY; ++i) {
        g_killers[i][0] = NULL_MOVE;
        g_killers[i][1] = NULL_MOVE;
    }
    for (int p = 0; p < 13; ++p)
        for (int sq = 0; sq < 64; ++sq)
            g_history[p][sq] = 0;
}

// ── Iterative deepening ───────────────────────────────────────────────────────
Move iterativeDeepening(Board& board, int maxDepth, int timeLimitMs) {
    g_startTime   = Clock::now();
    g_timeLimitMs = timeLimitMs;
    g_timesUp     = false;

    // Populate game history hashes for repetition detection
    g_gameHashes.clear();
    for (const auto& u : board.history)
        g_gameHashes.push_back(u.zobristHash);
    g_gameHashes.push_back(board.zobristHash);  // include current position

    for (int i = 0; i < MAX_PLY; ++i) {
        g_killers[i][0] = NULL_MOVE;
        g_killers[i][1] = NULL_MOVE;
    }
    for (int p = 0; p < 13; ++p)
        for (int sq = 0; sq < 64; ++sq)
            g_history[p][sq] = 0;

    Move bestMove = NULL_MOVE;
    Move prevBest = NULL_MOVE;

    for (int depth = 1; depth <= maxDepth; ++depth) {
        // 1 check extension per branch; budget is independent per root move
        int extBudget = 1;

        std::vector<Move> moves = generateMoves(board);
        if (moves.empty()) break;

        orderMoves(moves, board, prevBest, 0);

        int  alpha     = -INF_SCORE;
        int  beta      =  INF_SCORE;
        Move depthBest = moves[0];

        for (int moveIdx = 0; moveIdx < (int)moves.size(); ++moveIdx) {
            const Move& m = moves[moveIdx];
            board.makeMove(m);

            // Root-level repetition check: if this move enters a seen position, score as slight loss
            int score;
            {
                uint64_t h = board.zobristHash;
                bool isRep = false;
                for (uint64_t gh : g_gameHashes) if (gh == h) { isRep = true; break; }
                if (isRep) {
                    board.undoMove();
                    score = -10;
                    if (score > alpha) { alpha = score; depthBest = m; }
                    continue;
                }
            }

            if (moveIdx == 0) {
                score = -alphaBetaSearch(board, depth - 1, -beta, -alpha,
                                         false, 1, extBudget);
            } else {
                score = -alphaBetaSearch(board, depth - 1, -alpha - 1, -alpha,
                                         false, 1, extBudget);
                if (!g_timesUp && score > alpha && score < beta) {
                    score = -alphaBetaSearch(board, depth - 1, -beta, -alpha,
                                             false, 1, extBudget);
                }
            }

            board.undoMove();

            if (g_timesUp)
                return bestMove.isNull() ? depthBest : bestMove;

            if (score > alpha) {
                alpha     = score;
                depthBest = m;
            }
        }

        bestMove = depthBest;
        prevBest = bestMove;

        std::cout << "info depth " << depth << " score cp " << alpha << "\n";

        if (timeLimitMs >= 0 && isTimesUp()) break;
    }

    return bestMove;
}
