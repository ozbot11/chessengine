#pragma once
#include "board.h"
#include <string>

// Load a Polyglot binary opening book from disk.
// Returns true on success. Engine plays without book on false.
bool bookLoad(const std::string& path);

// Probe the book for the current board position.
// Returns a legal Move if found, or NULL_MOVE if no book hit.
Move bookProbe(const Board& board);
