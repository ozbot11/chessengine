#pragma once
#include "board.h"

// Returns score in centipawns relative to the side to move.
// Positive = good for side to move.
int evaluate(const Board& board);
