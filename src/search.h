#pragma once
#include "board.h"
#include <utility>
#include <vector>

// Run iterative deepening; prints "info depth X score cp Y" each iteration.
Move iterativeDeepening(Board& board, int maxDepth, int timeLimitMs = -1);

// Clear transposition table and killer tables (call on ucinewgame).
void clearSearchState();

// Set the list of recent game moves (from,to pairs) for shuffle detection.
void setGameMoves(const std::vector<std::pair<int,int>>& moves);
