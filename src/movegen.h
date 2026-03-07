#pragma once
#include "board.h"
#include <vector>

// Returns all legal moves for the side to move.
std::vector<Move> generateMoves(Board& board);

// Returns true if 'square' is attacked by 'byColor'.
bool isSquareAttacked(const Board& board, int square, Color byColor);

// Returns true if 'color' king is in check.
bool isInCheck(Board& board, Color color);
