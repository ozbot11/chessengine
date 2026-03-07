# Chess Engine Architecture

A brief overview of the main types and algorithms in this UCI chess engine.

---

## Types and Classes

### `Move` (types.h)

Represents a single move. Fields: `from`, `to` (square indices), `promotion` (piece if promotion, else `EMPTY`), `isCastling`, `isEnPassant`. Used everywhere for move generation, search, and UCI I/O.

### `UndoInfo` (board.h)

One undo record per move. Stores: the move, captured piece, castling rights and en passant square before the move, half-move clock, and **Zobrist hash before the move**. Restoring this hash on `undoMove()` keeps the board’s hash correct without recomputing it.

### `Board` (board.h)

The chess position and game state:

- **State:** `squares[64]`, `whiteToMove`, `castlingRights[4]`, `enPassantSquare`, `halfMoveClock`, `fullMoveNumber`, **`zobristHash`** (incrementally updated).
- **History:** `std::vector<UndoInfo> history` for undo.
- **Setup:** `reset()`, `loadFEN()`, `toFEN()`, `display()`.
- **Play:** `makeMove()`, `undoMove()`.
- **Helpers:** `sideToMove()`, `computeHash()` (full recompute, used after `loadFEN()`).

All move execution and undo goes through `Board`; the hash is updated incrementally in `makeMove()` and restored from `UndoInfo` in `undoMove()`.

### `TTEntry` (tt.h)

One transposition table entry:

- `key` — full Zobrist key (for validation).
- `score` — stored score.
- `bestMove` — best or refutation move.
- `depth` — depth at which it was stored.
- `flag` — `TT_EXACT`, `TT_LOWERBOUND`, or `TT_UPPERBOUND` for score interpretation.

Used by the search to avoid re-searching the same position and to get a good first move at each node.

### `BookEntry` (book.cpp, internal)

One Polyglot book entry: `key` (position hash), `move` (encoded move), `weight` (for random choice). The book is a sorted vector of these; lookup is by binary search on the current position’s Polyglot key.

### `Zobrist` namespace (zobrist.h / zobrist.cpp)

Not a class; holds global Zobrist key tables and `init()`:

- **`pieceKeys[13][64]`** — one random 64-bit value per (piece, square). Piece index 0 = EMPTY (unused for hashing), 1–6 white P,N,B,R,Q,K, 7–12 black.
- **`sideKey`** — XOR’d when black to move.
- **`castleKeys[4]`** — one per castling right (WK, WQ, BK, BQ).
- **`epKeys[8]`** — one per en passant file.

`init()` must be called once (e.g. in `main`) before any `Board` is created. Keys are generated with a deterministic xorshift64 RNG from a fixed seed so hashes are reproducible.

---

## Basic Algorithms

### Zobrist hashing

**Purpose:** A 64-bit fingerprint of the position so that:

- Equal positions (same pieces, side, castling, en passant) get the same hash.
- Different positions get different hashes with very high probability.
- The hash can be updated in O(1) when a move is made or undone, instead of recomputing over all 64 squares.

**Idea:** For each “feature” of the position (e.g. “white pawn on e4”), there is a precomputed random 64-bit value. The position hash is the XOR of the values for all features that are true.

**Initialization (Zobrist::init):**

- For each (piece, square) and for side/castling/ep, generate a random 64-bit value with xorshift64 and store it in the right table.
- Same seed ⇒ same keys ⇒ reproducible hashes.

**Computing from scratch (Board::computeHash):**

- Start with 0.
- For each non-empty square: `h ^= pieceKeys[piece][sq]`.
- If black to move: `h ^= sideKey`.
- For each castling right that is true: `h ^= castleKeys[i]`.
- If there is an en passant square: `h ^= epKeys[file]`.

**Incremental update in makeMove():**

- XOR *out* the old features that change, then XOR *in* the new ones. Because XOR is its own inverse, “remove feature” = “XOR same value again”.
- Typical steps: XOR out old en passant and old castling; XOR out piece on `from`; if capture, XOR out captured piece on `to` (or on the en passant square for ep captures); XOR in piece on `to` (or promotion piece); for castling, XOR out rook on old square and XOR in rook on new square; set new en passant and XOR in its key if any; update castling and XOR in keys for rights that remain; finally XOR `sideKey` to flip the side.

**Undo:** The hash is not recomputed; it is restored from `UndoInfo.zobristHash` (the hash *before* the move), so undo is O(1).

**Usage in this engine:** Transposition table index (key modulo table size), repetition detection (compare current hash to game history and search stack), and opening book (Polyglot uses its own key scheme that mirrors this idea).

---

### Move generation (movegen.cpp)

- **Legal moves:** Generate all pseudo-legal moves (per piece type: pawn pushes/captures/promotions, knight jumps, sliding rays for B/R/Q, king moves, castling, en passant), then filter out any that leave the side’s king in check (e.g. make move, test `isInCheck(sideToMove)`).
- **Attack detection:** `isSquareAttacked(board, square, color)` checks if any piece of `color` attacks the square (pawns diagonally, knights by offset, bishops/queens along diagonals, rooks/queens along ranks/files, king adjacent). Used for check detection and legality.
- **Check:** `isInCheck(board, color)` finds that color’s king and returns whether that square is attacked by the opponent.

---

### Evaluation (eval.cpp)

Score in centipawns for the **side to move** (positive = better for them). Combines:

- **Material:** Piece values from `types.h` (e.g. P=100, N=320, B=330, R=500, Q=900, K=20000).
- **Piece-square tables (PST):** Per-piece, per-square bonuses (e.g. knights toward center, pawns advancing). Black pieces use the same PSTs on a mirrored rank so “good for black” is negative when added to white’s score.
- **Bishop pair:** Bonus for having two bishops.
- **Pawn structure:** Penalties for doubled and isolated pawns; bonuses for passed pawns by rank.
- **Rooks:** Bonus for rooks on open or semi-open files.
- **King safety:** Penalty for king in center (when not on home rank), and for missing pawn shield in front of the king.

Final score is flipped for black to move so the result is always “from side to move’s perspective”.

---

### Search (search.cpp)

- **Iterative deepening:** Search at depth 1, 2, 3, … up to `maxDepth` (or until time is up). Each iteration reuses the transposition table and prints `info depth X score cp Y`. The best move from the previous depth is used as the first move at the root (PVS and move ordering).
- **Alpha-beta:** Minimax with alpha/beta bounds. When a move returns a score ≥ beta, the node is a beta cutoff (fail high); when score ≤ alpha, we don’t need the exact value for the parent. Scores are from the side-to-move’s perspective; negation is applied at each level.
- **Transposition table (TT):** Before searching, probe by `board.zobristHash`. On hit, use stored score if depth is sufficient and adjust alpha/beta or return immediately for exact scores; always use `bestMove` for move ordering. After searching, store score, depth, flag (exact/lower/upper), and best move.
- **Quiescence:** When depth reaches 0, search only captures (and promotions) to a fixed depth to avoid horizon effect. Score is the maximum of stand-pat and capture replies (minimax in the capture tree).
- **Move ordering:** (1) TT best move first, (2) captures by MVV-LVA (most valuable victim – least valuable attacker), (3) promotions, (4) killer moves (two per ply), (5) history heuristic. Good ordering increases cutoffs and speeds up search.
- **Null-move pruning:** If not in check and depth ≥ 3 and side has a non-pawn piece, try “passing” (flip side, clear ep, no move). If the null-move search (depth − 1 − R, R=2) fails high, assume the position is so good that the real search would also fail high and return beta. Restore side and ep after the probe.
- **Late move reductions (LMR):** For non-PV, quiet moves after the first few, search at reduced depth first. If the reduced search fails high (score > alpha), re-search at full depth (and possibly with a full window) to confirm.
- **Principal variation search (PVS):** First move is searched with full window [alpha, beta]. Later moves are searched with a null window [alpha, alpha+1]; if the score is above alpha, the move is re-searched with full window. This saves time when the first move is already strong.
- **Check extension:** If the side to move is in check and an extension budget allows, add one extra plies of search (with a cap to avoid runaway extensions).
- **Repetition:** Compare current position hash to game history and to the current search path; if the position has appeared before, score the node as a draw (e.g. 0 or a small draw score) so the engine avoids or accepts repeats as desired.
- **Time management:** Periodically check elapsed time; when time is up, set a flag and stop starting new work, returning the best move found so far.

---

### Transposition table (tt.cpp)

Fixed-size hash table (e.g. 2^20 entries). Index = `key % SIZE`. Each entry holds key, score, best move, depth, and flag. **Probe:** if `entry.key == key` return the entry else miss. **Store:** replace if the slot is empty, or the new search is from a different position (key mismatch), or the new depth is ≥ stored depth or the new result is exact. No aging is implemented; table is cleared on “ucinewgame”.

---

### Opening book (book.cpp)

- **Format:** Polyglot binary (16 bytes per entry: 8-byte key, 2-byte move, 2-byte weight, 4-byte learn — learn ignored). Keys use Polyglot’s own piece ordering and castling/ep/side tables (the 781 constants in `PolyRandom`).
- **Load:** Read file, parse entries with weight > 0, sort by key. Build a vector of `BookEntry`.
- **Probe:** Compute Polyglot key for current board; binary search for that key; collect all entries with that key; choose a move at random with probability proportional to weight. Return that move as a legal move, or `NULL_MOVE` if none or not loaded.

---

## Data flow (high level)

1. **Startup:** `Zobrist::init()` → optional `bookLoad(path)` → `uciLoop()`.
2. **UCI:** Commands set position (e.g. `startpos` or FEN + moves) and ask for a move (`go`). Position is applied with `Board::loadFEN` and a sequence of `makeMove(stringToMove(...))`.
3. **Go:** If book has a move for the current position, return it; else run `iterativeDeepening(board, maxDepth, timeLimitMs)`.
4. **Search:** At each node, probe TT, generate legal moves, order them, run alpha-beta (with null move, LMR, PVS, extensions, repetition) and quiescence; store results in TT and update killers/history; at root, report depth and score and update best move.
5. **Best move:** Returned to UCI as `bestmove <move>`.

All position identity (TT, repetition, book) ultimately relies on the Zobrist hash (or Polyglot key in the book), which is maintained incrementally on the `Board` for speed.
