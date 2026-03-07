#pragma once
#include "board.h"

// Starts the UCI protocol loop (reads from stdin).
void uciLoop();

// Move format helpers
std::string moveToString(const Move& m);
Move stringToMove(Board& board, const std::string& s);
