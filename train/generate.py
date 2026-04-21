"""
generate.py — Generate NNUE training data by querying the chess engine.

Usage:
    python train/generate.py --engine build/chess_engine.exe \
                             --games 2000 \
                             --depth 6 \
                             --out train/data.csv

Requires:  pip install chess

The script plays random-ish games (book-opening + random moves), then for each
quiet position calls the engine at the given depth and records the score.
Output: CSV with columns  fen,score_cp  (score from White's perspective).
"""
import argparse
import csv
import random
import subprocess
import sys
import time
import chess
import chess.pgn


# ── UCI engine wrapper ────────────────────────────────────────────────────────
class UCIEngine:
    def __init__(self, path: str):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._send("uci")
        self._wait_for("uciok")
        self._send("isready")
        self._wait_for("readyok")

    def _send(self, cmd: str):
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()

    def _wait_for(self, token: str):
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("Engine closed unexpectedly")
            if token in line:
                return line.strip()

    def evaluate(self, fen: str, depth: int) -> int | None:
        """Return centipawn score (White's POV) at given depth, or None on error."""
        self._send(f"position fen {fen}")
        self._send(f"go depth {depth}")
        score_cp = None
        while True:
            line = self.proc.stdout.readline().strip()
            if line.startswith("bestmove"):
                break
            if line.startswith(f"info depth {depth}"):
                # Parse  score cp N  or  score mate N
                parts = line.split()
                try:
                    si = parts.index("score")
                    if parts[si + 1] == "cp":
                        score_cp = int(parts[si + 2])
                    elif parts[si + 1] == "mate":
                        m = int(parts[si + 2])
                        score_cp = 30000 if m > 0 else -30000
                except (ValueError, IndexError):
                    pass
        return score_cp

    def close(self):
        self._send("quit")
        self.proc.wait(timeout=5)


# ── Position generation ───────────────────────────────────────────────────────
def is_quiet(board: chess.Board, move: chess.Move) -> bool:
    """True if the move is not a capture, promotion, or check."""
    return (
        not board.is_capture(move)
        and move.promotion is None
        and not board.gives_check(move)
    )


def play_game(board: chess.Board, random_plies: int = 20) -> list[str]:
    """
    Play a game with random_plies random moves from startpos, then collect
    quiet FEN positions for the rest of the game (up to 120 half-moves total).
    Returns list of FEN strings for quiet positions after the random opening.
    """
    positions = []
    board.reset()

    # Random opening
    for _ in range(random_plies):
        legal = list(board.legal_moves)
        if not legal or board.is_game_over():
            return positions
        board.push(random.choice(legal))

    prev_was_capture = False
    for _ in range(100):
        if board.is_game_over():
            break
        legal = list(board.legal_moves)
        if not legal:
            break

        # Prefer quiet moves, but fall back to any
        quiet_moves = [m for m in legal if is_quiet(board, m)]
        move = random.choice(quiet_moves if quiet_moves else legal)

        # Only record the position if this move is quiet and last move wasn't capture
        if is_quiet(board, move) and not prev_was_capture and not board.is_check():
            positions.append(board.fen())

        prev_was_capture = board.is_capture(move)
        board.push(move)

    return positions


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Generate NNUE training data")
    parser.add_argument("--engine", default="build/chess_engine.exe")
    parser.add_argument("--games",  type=int, default=2000,
                        help="Number of self-play games to generate positions from")
    parser.add_argument("--depth",  type=int, default=6,
                        help="Search depth for labeling each position")
    parser.add_argument("--out",    default="train/data.csv")
    parser.add_argument("--max-pos", type=int, default=500_000,
                        help="Stop after this many positions")
    args = parser.parse_args()

    # Count already-saved rows so we can resume
    existing = 0
    import os
    if os.path.exists(args.out):
        with open(args.out, newline="") as rf:
            existing = sum(1 for _ in rf) - 1  # subtract header
        existing = max(existing, 0)
        print(f"Resuming: {existing} positions already in {args.out}")
    else:
        existing = 0

    # Track how many games have been completed across all runs so we can
    # show a global game counter in the logs.
    state_path = os.path.join(os.path.dirname(args.out), "generate_state.txt")
    completed_games = 0
    if os.path.exists(state_path):
        try:
            with open(state_path) as sf:
                line = sf.readline().strip()
                if line:
                    completed_games = int(line)
        except (OSError, ValueError):
            completed_games = 0

    print(f"Starting engine: {args.engine}")
    engine = UCIEngine(args.engine)

    board = chess.Board()
    total = existing
    t0 = time.time()

    file_mode = "a" if existing > 0 else "w"
    with open(args.out, file_mode, newline="") as f:
        writer = csv.writer(f)
        if existing == 0:
            writer.writerow(["fen", "score_cp"])

        for game_idx in range(args.games):
            if total >= args.max_pos:
                break

            # Progress: which game we are on (both this run and globally)
            global_game_idx = completed_games + game_idx + 1

            fens = play_game(board, random_plies=random.randint(10, 30))
            for _, fen in enumerate(fens, start=1):
                if total >= args.max_pos:
                    break
                score = engine.evaluate(fen, args.depth)
                if score is None:
                    continue
                writer.writerow([fen, score])
                total += 1

            # After each game, print a concise progress summary
            print(
                f"Game {game_idx + 1}/{args.games} "
                f"(global {global_game_idx}) finished, "
                f"total positions={total}/{args.max_pos}",
                flush=True,
            )

            # Persist global game counter after each game so it survives restarts.
            try:
                with open(state_path, "w") as sf:
                    sf.write(str(completed_games + game_idx + 1) + "\n")
            except OSError:
                pass

            if (game_idx + 1) % 100 == 0:
                elapsed = time.time() - t0
                rate = total / elapsed if elapsed > 0 else 0
                print(f"  Game {game_idx+1}/{args.games}  positions={total}  "
                      f"{rate:.0f} pos/s", flush=True)

    engine.close()
    print(f"\nDone. {total} positions written to {args.out}")
    print(f"Elapsed: {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
