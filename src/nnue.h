#pragma once
#include "board.h"
#include <string>

// Load network weights from a binary file.
// Returns true on success; on failure the NNUE stays unloaded.
bool nnueLoad(const std::string& path);

// Returns true if a network has been loaded successfully.
bool nnueLoaded();

// Evaluate the position. Returns centipawns relative to the side to move
// (positive = good for the side to move), matching the HCE sign convention.
int nnueEvaluate(const Board& board);
