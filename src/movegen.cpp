#include "movegen.h"
#include <algorithm>

// ── Square-attacked check ─────────────────────────────────────────────────────
bool isSquareAttacked(const Board& board, int square, Color byColor) {
    // Pawns
    if (byColor == WHITE) {
        // White pawns attack diagonally upward: sq attacked from sq-9 (left) or sq-7 (right)
        int r = rankOf(square), f = fileOf(square);
        if (r > 0 && f > 0 && board.squares[square - 9] == WP) return true;
        if (r > 0 && f < 7 && board.squares[square - 7] == WP) return true;
    } else {
        int r = rankOf(square), f = fileOf(square);
        if (r < 7 && f > 0 && board.squares[square + 7] == BP) return true;
        if (r < 7 && f < 7 && board.squares[square + 9] == BP) return true;
    }

    // Knights
    Piece knight = (byColor == WHITE) ? WN : BN;
    static const int knightOff[8] = {-17,-15,-10,-6,6,10,15,17};
    int r = rankOf(square), f = fileOf(square);
    for (int off : knightOff) {
        int sq2 = square + off;
        if (sq2 < 0 || sq2 > 63) continue;
        int r2 = rankOf(sq2), f2 = fileOf(sq2);
        if (std::abs(r2 - r) + std::abs(f2 - f) != 3) continue;
        if (std::abs(r2 - r) > 2 || std::abs(f2 - f) > 2) continue;
        if (board.squares[sq2] == knight) return true;
    }

    // Bishops / Queens (diagonals)
    Piece bishop = (byColor == WHITE) ? WB : BB;
    Piece queen  = (byColor == WHITE) ? WQ : BQ;
    static const int diagOff[4] = {-9, -7, 7, 9};
    for (int off : diagOff) {
        int sq2 = square;
        for (;;) {
            int prevF = fileOf(sq2);
            sq2 += off;
            if (sq2 < 0 || sq2 > 63) break;
            int df = std::abs(fileOf(sq2) - prevF);
            if (df != 1) break;  // wrapped around board edge
            Piece p = board.squares[sq2];
            if (p == bishop || p == queen) return true;
            if (p != EMPTY) break;
        }
    }

    // Rooks / Queens (cardinal)
    Piece rook = (byColor == WHITE) ? WR : BR;
    static const int cardOff[4] = {-8, -1, 1, 8};
    for (int off : cardOff) {
        int sq2 = square;
        for (;;) {
            int prevF = fileOf(sq2);
            sq2 += off;
            if (sq2 < 0 || sq2 > 63) break;
            // Prevent file wrapping for horizontal moves
            if (off == -1 || off == 1) {
                if (std::abs(fileOf(sq2) - prevF) != 1) break;
            }
            Piece p = board.squares[sq2];
            if (p == rook || p == queen) return true;
            if (p != EMPTY) break;
        }
    }

    // King
    Piece king = (byColor == WHITE) ? WK : BK;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int df2 = -1; df2 <= 1; ++df2) {
            if (!dr && !df2) continue;
            int r2 = rankOf(square) + dr;
            int f2 = fileOf(square) + df2;
            if (r2 < 0 || r2 > 7 || f2 < 0 || f2 > 7) continue;
            if (board.squares[makeSquare(r2, f2)] == king) return true;
        }
    }

    return false;
}

// ── isInCheck ─────────────────────────────────────────────────────────────────
bool isInCheck(Board& board, Color color) {
    Piece king = (color == WHITE) ? WK : BK;
    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] == king)
            return isSquareAttacked(board, sq, opponent(color));
    }
    return false;
}

// ── Pseudo-legal move helpers ─────────────────────────────────────────────────

static void addMove(std::vector<Move>& moves, int from, int to,
                    Piece promo = EMPTY, bool castle = false, bool ep = false) {
    moves.push_back({from, to, promo, castle, ep});
}

static void generatePawnMoves(const Board& board, std::vector<Move>& moves, Color us) {
    int dir       = (us == WHITE) ? 8  : -8;
    int startRank = (us == WHITE) ? 1  : 6;
    int promoRank = (us == WHITE) ? 6  : 1;  // rank BEFORE promotion
    Piece myPawn  = (us == WHITE) ? WP : BP;

    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] != myPawn) continue;
        int r = rankOf(sq), f = fileOf(sq);

        // Single push
        int fwd = sq + dir;
        if (fwd >= 0 && fwd < 64 && board.squares[fwd] == EMPTY) {
            if (r == promoRank) {
                // Promotion
                for (Piece p : {(us==WHITE?WQ:BQ),(us==WHITE?WR:BR),
                                 (us==WHITE?WB:BB),(us==WHITE?WN:BN)})
                    addMove(moves, sq, fwd, p);
            } else {
                addMove(moves, sq, fwd);
                // Double push
                if (r == startRank) {
                    int dbl = sq + 2 * dir;
                    if (board.squares[dbl] == EMPTY)
                        addMove(moves, sq, dbl);
                }
            }
        }

        // Captures
        for (int df : {-1, 1}) {
            int f2 = f + df;
            if (f2 < 0 || f2 > 7) continue;
            int capSq = sq + dir + df;
            if (capSq < 0 || capSq > 63) continue;

            bool isEP = (capSq == board.enPassantSquare);
            bool isCapture = (!isEmpty(board.squares[capSq]) &&
                              pieceColor(board.squares[capSq]) != us);

            if (isEP) {
                addMove(moves, sq, capSq, EMPTY, false, true);
            } else if (isCapture) {
                if (r == promoRank) {
                    for (Piece p : {(us==WHITE?WQ:BQ),(us==WHITE?WR:BR),
                                     (us==WHITE?WB:BB),(us==WHITE?WN:BN)})
                        addMove(moves, sq, capSq, p);
                } else {
                    addMove(moves, sq, capSq);
                }
            }
        }
    }
}

static void generateKnightMoves(const Board& board, std::vector<Move>& moves, Color us) {
    Piece myKnight = (us == WHITE) ? WN : BN;
    static const int offsets[8] = {-17,-15,-10,-6,6,10,15,17};

    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] != myKnight) continue;
        int r = rankOf(sq), f = fileOf(sq);
        for (int off : offsets) {
            int sq2 = sq + off;
            if (sq2 < 0 || sq2 > 63) continue;
            int dr = std::abs(rankOf(sq2) - r);
            int df = std::abs(fileOf(sq2) - f);
            if ((dr == 2 && df == 1) || (dr == 1 && df == 2)) {
                Piece target = board.squares[sq2];
                if (target == EMPTY || pieceColor(target) != us)
                    addMove(moves, sq, sq2);
            }
        }
    }
}

static void generateSlidingMoves(const Board& board, std::vector<Move>& moves,
                                  Color us, Piece piece, const int* offsets, int nOff) {
    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] != piece) continue;
        for (int i = 0; i < nOff; ++i) {
            int off = offsets[i];
            int sq2 = sq;
            for (;;) {
                int prevF = fileOf(sq2);
                sq2 += off;
                if (sq2 < 0 || sq2 > 63) break;
                // Prevent wrap-around on horizontal rays
                if (off == 1 || off == -1) {
                    if (std::abs(fileOf(sq2) - prevF) != 1) break;
                }
                // Prevent wrap-around on diagonal rays
                if (off == 7 || off == -7 || off == 9 || off == -9) {
                    if (std::abs(fileOf(sq2) - prevF) != 1) break;
                }
                Piece target = board.squares[sq2];
                if (target == EMPTY) {
                    addMove(moves, sq, sq2);
                } else {
                    if (pieceColor(target) != us)
                        addMove(moves, sq, sq2);
                    break;
                }
            }
        }
    }
}

static void generateBishopMoves(const Board& board, std::vector<Move>& moves, Color us) {
    Piece myBishop = (us == WHITE) ? WB : BB;
    static const int diag[4] = {-9, -7, 7, 9};
    generateSlidingMoves(board, moves, us, myBishop, diag, 4);
}

static void generateRookMoves(const Board& board, std::vector<Move>& moves, Color us) {
    Piece myRook = (us == WHITE) ? WR : BR;
    static const int card[4] = {-8, -1, 1, 8};
    generateSlidingMoves(board, moves, us, myRook, card, 4);
}

static void generateQueenMoves(const Board& board, std::vector<Move>& moves, Color us) {
    Piece myQueen = (us == WHITE) ? WQ : BQ;
    static const int all[8] = {-9,-8,-7,-1,1,7,8,9};
    generateSlidingMoves(board, moves, us, myQueen, all, 8);
}

static void generateKingMoves(const Board& board, std::vector<Move>& moves, Color us) {
    Piece myKing = (us == WHITE) ? WK : BK;
    for (int sq = 0; sq < 64; ++sq) {
        if (board.squares[sq] != myKing) continue;
        int r = rankOf(sq), f = fileOf(sq);
        for (int dr = -1; dr <= 1; ++dr) {
            for (int df = -1; df <= 1; ++df) {
                if (!dr && !df) continue;
                int r2 = r + dr, f2 = f + df;
                if (r2 < 0 || r2 > 7 || f2 < 0 || f2 > 7) continue;
                int sq2 = makeSquare(r2, f2);
                Piece target = board.squares[sq2];
                if (target == EMPTY || pieceColor(target) != us)
                    addMove(moves, sq, sq2);
            }
        }

        // Castling
        Color opp = opponent(us);
        if (us == WHITE && sq == E1) {
            // Kingside
            if (board.castlingRights[CR_WK] &&
                board.squares[F1] == EMPTY && board.squares[G1] == EMPTY &&
                !isSquareAttacked(board, E1, opp) &&
                !isSquareAttacked(board, F1, opp) &&
                !isSquareAttacked(board, G1, opp))
                addMove(moves, E1, G1, EMPTY, true);
            // Queenside
            if (board.castlingRights[CR_WQ] &&
                board.squares[D1] == EMPTY && board.squares[C1] == EMPTY && board.squares[B1] == EMPTY &&
                !isSquareAttacked(board, E1, opp) &&
                !isSquareAttacked(board, D1, opp) &&
                !isSquareAttacked(board, C1, opp))
                addMove(moves, E1, C1, EMPTY, true);
        } else if (us == BLACK && sq == E8) {
            if (board.castlingRights[CR_BK] &&
                board.squares[F8] == EMPTY && board.squares[G8] == EMPTY &&
                !isSquareAttacked(board, E8, opp) &&
                !isSquareAttacked(board, F8, opp) &&
                !isSquareAttacked(board, G8, opp))
                addMove(moves, E8, G8, EMPTY, true);
            if (board.castlingRights[CR_BQ] &&
                board.squares[D8] == EMPTY && board.squares[C8] == EMPTY && board.squares[B8] == EMPTY &&
                !isSquareAttacked(board, E8, opp) &&
                !isSquareAttacked(board, D8, opp) &&
                !isSquareAttacked(board, C8, opp))
                addMove(moves, E8, C8, EMPTY, true);
        }
    }
}

// ── generateMoves (legal) ─────────────────────────────────────────────────────
std::vector<Move> generateMoves(Board& board) {
    Color us = board.sideToMove();
    std::vector<Move> pseudo;
    pseudo.reserve(64);

    generatePawnMoves(board, pseudo, us);
    generateKnightMoves(board, pseudo, us);
    generateBishopMoves(board, pseudo, us);
    generateRookMoves(board, pseudo, us);
    generateQueenMoves(board, pseudo, us);
    generateKingMoves(board, pseudo, us);

    // Filter: keep only moves that don't leave our king in check
    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const Move& m : pseudo) {
        board.makeMove(m);
        if (!isInCheck(board, us))
            legal.push_back(m);
        board.undoMove();
    }
    return legal;
}
