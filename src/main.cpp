#include "uci.h"
#include "perft.h"
#include "zobrist.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    Zobrist::init();  // must be called before any Board is created

    if (argc >= 2 && std::string(argv[1]) == "perft") {
        runPerftSuite();
        return 0;
    }

    uciLoop();
    return 0;
}
