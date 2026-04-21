"""
train.py — Train an NNUE network on labelled chess positions.

Usage:
    python train/train.py --data train/data.csv \
                          --out nn.bin \
                          --epochs 10 \
                          --batch 4096

Requires:  pip install torch chess

Input CSV:  fen,score_cp  (White's POV centipawns)
Output:     nn.bin in the engine's binary format (magic "NNUE", version 1)

Architecture:  768 → 256 → 32 → 1  (ReLU hidden layers)
"""
import argparse
import csv
import struct
import sys
import time
import chess
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader


# ── Feature encoding ──────────────────────────────────────────────────────────
PIECE_TYPE_IDX = {
    chess.PAWN:   0,
    chess.KNIGHT: 1,
    chess.BISHOP: 2,
    chess.ROOK:   3,
    chess.QUEEN:  4,
    chess.KING:   5,
}

def fen_to_features(fen: str) -> list[int]:
    """Return list of active feature indices (0..767) for a FEN string."""
    board = chess.Board(fen)
    feats = []
    for sq in chess.SQUARES:
        piece = board.piece_at(sq)
        if piece is None:
            continue
        type_idx  = PIECE_TYPE_IDX[piece.piece_type]
        color_idx = 0 if piece.color == chess.WHITE else 1
        # Match engine square encoding: rank*8 + file, A1=0
        # python-chess uses same encoding (A1=0)
        feats.append(type_idx * 128 + color_idx * 64 + sq)
    return feats


def features_to_tensor(feat_list: list[list[int]]) -> torch.Tensor:
    """Convert a batch of feature index lists to a dense (N, 768) float tensor."""
    N = len(feat_list)
    x = torch.zeros(N, 768, dtype=torch.float32)
    for i, feats in enumerate(feat_list):
        for f in feats:
            x[i, f] = 1.0
    return x


# ── Dataset ───────────────────────────────────────────────────────────────────
class PositionDataset(Dataset):
    def __init__(self, path: str, max_rows: int = 0):
        self.fens = []
        self.scores = []
        print(f"Loading {path} ...")
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            for i, row in enumerate(reader):
                if max_rows and i >= max_rows:
                    break
                self.fens.append(row["fen"])
                self.scores.append(float(row["score_cp"]))
        print(f"  {len(self.fens)} positions loaded")

    def __len__(self):
        return len(self.fens)

    def __getitem__(self, idx):
        feats = fen_to_features(self.fens[idx])
        x = torch.zeros(768, dtype=torch.float32)
        for f in feats:
            x[f] = 1.0
        y = torch.tensor([self.scores[idx]], dtype=torch.float32)
        return x, y


# ── Model ─────────────────────────────────────────────────────────────────────
class NNUE(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(768, 256),
            nn.ReLU(),
            nn.Linear(256, 32),
            nn.ReLU(),
            nn.Linear(32, 1),
        )

    def forward(self, x):
        return self.net(x)


# ── Export to nn.bin ──────────────────────────────────────────────────────────
def export_nnue(model: NNUE, path: str):
    """
    Binary layout (all floats, little-endian):
      magic[4]      'N','N','U','E'
      version u32   1
      L1_w[768][256]
      L1_b[256]
      L2_w[256][32]
      L2_b[32]
      L3_w[32]
      L3_b          (scalar)
    """
    state = model.state_dict()

    # net.0 = L1, net.2 = L2, net.4 = L3
    L1_w = state["net.0.weight"].detach().cpu()   # shape (256, 768) — PyTorch stores transposed
    L1_b = state["net.0.bias"].detach().cpu()     # shape (256,)
    L2_w = state["net.2.weight"].detach().cpu()   # shape (32, 256)
    L2_b = state["net.2.bias"].detach().cpu()     # shape (32,)
    L3_w = state["net.4.weight"].detach().cpu()   # shape (1, 32)
    L3_b = state["net.4.bias"].detach().cpu()     # shape (1,)

    with open(path, "wb") as f:
        # Magic + version
        f.write(b"NNUE")
        f.write(struct.pack("<I", 1))

        # L1_w: engine expects [INPUT][L1] = [768][256], i.e. transposed from PyTorch
        f.write(L1_w.T.contiguous().numpy().astype("float32").tobytes())
        f.write(L1_b.numpy().astype("float32").tobytes())

        # L2_w: engine expects [L1][L2] = [256][32], transposed from PyTorch [32][256]
        f.write(L2_w.T.contiguous().numpy().astype("float32").tobytes())
        f.write(L2_b.numpy().astype("float32").tobytes())

        # L3_w: engine expects [L2] = [32], row vector
        f.write(L3_w.squeeze(0).numpy().astype("float32").tobytes())
        f.write(struct.pack("<f", L3_b.item()))

    print(f"Exported model to {path}")


# ── Training loop ─────────────────────────────────────────────────────────────
def train(args):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")

    dataset = PositionDataset(args.data, max_rows=args.max_rows)
    loader = DataLoader(dataset, batch_size=args.batch, shuffle=True,
                        num_workers=0, pin_memory=(device.type == "cuda"))

    model = NNUE().to(device)
    optimizer = optim.Adam(model.parameters(), lr=args.lr)
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)
    criterion = nn.MSELoss()

    print(f"\nTraining for {args.epochs} epochs, batch={args.batch}, lr={args.lr}")
    for epoch in range(1, args.epochs + 1):
        model.train()
        total_loss = 0.0
        t0 = time.time()
        for x, y in loader:
            x, y = x.to(device), y.to(device)
            pred = model(x)
            loss = criterion(pred, y)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            total_loss += loss.item() * len(x)
        scheduler.step()
        avg_loss = total_loss / len(dataset)
        print(f"  Epoch {epoch:3d}/{args.epochs}  loss={avg_loss:.2f}  "
              f"lr={scheduler.get_last_lr()[0]:.2e}  {time.time()-t0:.1f}s")

    export_nnue(model, args.out)


# ── Entry point ───────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="Train NNUE from labelled positions")
    parser.add_argument("--data",     default="train/data.csv")
    parser.add_argument("--out",      default="nn.bin")
    parser.add_argument("--epochs",   type=int,   default=10)
    parser.add_argument("--batch",    type=int,   default=4096)
    parser.add_argument("--lr",       type=float, default=1e-3)
    parser.add_argument("--max-rows", type=int,   default=0,
                        help="Limit dataset size (0 = no limit)")
    args = parser.parse_args()
    train(args)


if __name__ == "__main__":
    main()
