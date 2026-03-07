#pragma once
#include "board.h"
#include <cstdint>

uint64_t perft(Board& board, int depth);
void runPerftSuite();
