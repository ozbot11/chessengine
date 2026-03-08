# Chess Engine

A UCI-compatible chess engine written in C++. Supports alpha-beta search with common optimizations, optional NNUE evaluation, and Polyglot opening books. Actively being improved with neural networks and reinforcement learning.

## Requirements

- **C++17** compiler (e.g. GCC, Clang, MSVC)
- **CMake** 3.16 or later

## Building

From the project root:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

The executable is `chess_engine` (or `chess_engine.exe` on Windows). For a Release build with optimizations, either run `cmake -DCMAKE_BUILD_TYPE=Release ..` or rely on the default Release build type set in the project.

## Running

### UCI mode (default)

Start the engine and it will wait for UCI commands on stdin:

```bash
./chess_engine
```

Or from the build directory:

```bash
./chess_engine
```

You can then type UCI commands (see below) or pipe them in. To use the engine in a chess GUI, point the GUI to this executable as the “engine path”; the engine speaks UCI over stdin/stdout.

### Perft suite

Run the built-in perft test suite (fixed positions and depths):

```bash
./chess_engine perft
```

## Optional files (next to the executable)

Place these in the **same directory as the executable** (often your `build/` folder when running from there):

| File      | Description |
|-----------|-------------|
| **book.bin** | Polyglot-format opening book. If present, the engine will use it for the opening; otherwise it plays from the search from move 1. |
| **nn.bin**   | NNUE evaluation network. If present, the engine uses NNUE evaluation; otherwise it falls back to the handcrafted evaluator. |

On startup the engine prints to stderr whether each was loaded (e.g. `info string Book loaded: book.bin`).

## UCI commands (reference)

The engine supports the standard UCI protocol. Commands you can send (e.g. when using a GUI or scripting):

| Command | Description |
|---------|-------------|
| **uci** | Identify as UCI engine; engine replies with `id name`, `id author`, then `uciok`. |
| **isready** | Engine replies `readyok` when ready for the next command. |
| **ucinewgame** | Reset the game (start position, clear search state). Send before a new game. |
| **position** | Set the current position. See below. |
| **go** | Start thinking and eventually output `bestmove <move>`. See below. |
| **quit** / **exit** | Exit the engine. |

**Setting the position**

- **position startpos** — Start from the initial position.
- **position startpos moves e2e4 e7e5 ...** — Start position, then play the given moves (long algebraic: from-square to-square, e.g. `e2e4`, promotions like `e7e8q`).
- **position fen &lt;fen&gt;** — Set position from a FEN string (six fields).
- **position fen &lt;fen&gt; moves ...** — Set from FEN, then play moves.

**Go options**

- **go** — Search with default depth (6) and no time limit.
- **go depth N** — Search to depth N.
- **go movetime ms** — Allocate `ms` milliseconds for this move.
- **go wtime ms btime ms** — Time remaining for white and black (milliseconds). The engine uses a simple fraction of remaining time when these are set.
- **go winc ms binc ms** — Increment per move (used together with `wtime`/`btime`).
- **go perft N** — Run perft at depth N for the current position; prints node count (useful for debugging).

Examples:

- `go depth 10`
- `go movetime 5000`
- `go wtime 60000 btime 60000 winc 1000 binc 1000`

## Using in a chess GUI

1. Build the engine as above.
2. In your GUI (e.g. Arena, Scid, Nibbler, Cute Chess), add a new **UCI engine**.
3. Set the **engine path** to the full path of `chess_engine` (or `chess_engine.exe`).
4. (Optional) Copy `book.bin` and/or `nn.bin` into the same folder as the executable so the engine can find them.
5. The engine should appear in the engine list; start a game or analysis as usual.

No extra “engine parameters” are required for basic use.

## Command-line quick test

After starting the engine, you can type:

```
uci
isready
position startpos
go depth 5
```

The engine will reply with `uciok`, `readyok`, and after thinking, `bestmove <move>`.

## Project structure

- **src/** — C++ source and headers. Main entry is `main.cpp`; UCI loop and commands in `uci.cpp`; board, move generation, search, evaluation, NNUE, book, and Zobrist hashing in the corresponding files.
- **ARCHITECTURE.md** — Describes the main types and algorithms (Zobrist hashing, search, evaluation, etc.).
- **CMakeLists.txt** — CMake configuration for the executable.

## License

See the repository for license information.
