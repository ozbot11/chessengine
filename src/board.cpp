#include "board.h"
#include "zobrist.h"
#include <iostream>
#include <sstream>
#include <cassert>

// ── reset ─────────────────────────────────────────────────────────────────────
void Board::reset() {
    loadFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// ── FEN helpers ───────────────────────────────────────────────────────────────
static Piece charToPiece(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
        default:  return EMPTY;
    }
}

static char pieceToChar(Piece p) {
    static const char tbl[] = ".PNBRQKpnbrqk";
    return tbl[p];
}

// ── computeHash ───────────────────────────────────────────────────────────────
uint64_t Board::computeHash() const {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; ++sq)
        if (squares[sq] != EMPTY)
            h ^= Zobrist::pieceKeys[squares[sq]][sq];
    if (!whiteToMove)
        h ^= Zobrist::sideKey;
    for (int i = 0; i < 4; ++i)
        if (castlingRights[i])
            h ^= Zobrist::castleKeys[i];
    if (enPassantSquare >= 0)
        h ^= Zobrist::epKeys[fileOf(enPassantSquare)];
    return h;
}

// ── loadFEN ───────────────────────────────────────────────────────────────────
void Board::loadFEN(const std::string& fen) {
    for (auto& p : squares) p = EMPTY;
    for (auto& c : castlingRights) c = false;
    enPassantSquare = -1;
    halfMoveClock   = 0;
    fullMoveNumber  = 1;
    history.clear();

    std::istringstream ss(fen);
    std::string piece_part, side, castle, ep, half, full;
    ss >> piece_part >> side >> castle >> ep >> half >> full;

    int rank = 7, file = 0;
    for (char c : piece_part) {
        if (c == '/') { --rank; file = 0; }
        else if (c >= '1' && c <= '8') { file += (c - '0'); }
        else { squares[rank * 8 + file] = charToPiece(c); ++file; }
    }

    whiteToMove = (side == "w");

    if (castle != "-") {
        for (char c : castle) {
            if      (c == 'K') castlingRights[CR_WK] = true;
            else if (c == 'Q') castlingRights[CR_WQ] = true;
            else if (c == 'k') castlingRights[CR_BK] = true;
            else if (c == 'q') castlingRights[CR_BQ] = true;
        }
    }

    if (ep != "-" && ep.size() >= 2)
        enPassantSquare = (ep[1] - '1') * 8 + (ep[0] - 'a');

    if (!half.empty()) halfMoveClock  = std::stoi(half);
    if (!full.empty()) fullMoveNumber = std::stoi(full);

    zobristHash = computeHash();
}

// ── toFEN ─────────────────────────────────────────────────────────────────────
std::string Board::toFEN() const {
    std::string result;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Piece p = squares[rank * 8 + file];
            if (p == EMPTY) { ++empty; }
            else {
                if (empty) { result += ('0' + empty); empty = 0; }
                result += pieceToChar(p);
            }
        }
        if (empty) result += ('0' + empty);
        if (rank > 0) result += '/';
    }
    result += whiteToMove ? " w " : " b ";
    std::string castle;
    if (castlingRights[CR_WK]) castle += 'K';
    if (castlingRights[CR_WQ]) castle += 'Q';
    if (castlingRights[CR_BK]) castle += 'k';
    if (castlingRights[CR_BQ]) castle += 'q';
    result += (castle.empty() ? "-" : castle);
    if (enPassantSquare >= 0) {
        result += ' ';
        result += (char)('a' + fileOf(enPassantSquare));
        result += (char)('1' + rankOf(enPassantSquare));
    } else {
        result += " -";
    }
    result += ' '; result += std::to_string(halfMoveClock);
    result += ' '; result += std::to_string(fullMoveNumber);
    return result;
}

// ── display ───────────────────────────────────────────────────────────────────
void Board::display() const {
    std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    for (int rank = 7; rank >= 0; --rank) {
        std::cout << (rank + 1) << " |";
        for (int file = 0; file < 8; ++file)
            std::cout << ' ' << pieceToChar(squares[rank * 8 + file]) << " |";
        std::cout << "\n  +---+---+---+---+---+---+---+---+\n";
    }
    std::cout << "    a   b   c   d   e   f   g   h\n";
    std::cout << (whiteToMove ? "White" : "Black") << " to move\n";
    std::cout << "FEN: " << toFEN() << "\n\n";
}

// ── makeMove ──────────────────────────────────────────────────────────────────
void Board::makeMove(const Move& m) {
    // ── Save undo record ─────────────────────────────────────────────────────
    UndoInfo ui;
    ui.move            = m;
    ui.captured        = squares[m.to];
    ui.enPassantSquare = enPassantSquare;
    ui.halfMoveClock   = halfMoveClock;
    ui.zobristHash     = zobristHash;
    for (int i = 0; i < 4; ++i) ui.castlingRights[i] = castlingRights[i];

    Piece moving = squares[m.from];

    // ── Hash: XOR out old EP and old castling rights ──────────────────────────
    if (enPassantSquare >= 0)
        zobristHash ^= Zobrist::epKeys[fileOf(enPassantSquare)];
    for (int i = 0; i < 4; ++i)
        if (castlingRights[i]) zobristHash ^= Zobrist::castleKeys[i];

    // ── Hash: remove moving piece from source ─────────────────────────────────
    zobristHash ^= Zobrist::pieceKeys[moving][m.from];

    // ── En passant capture ────────────────────────────────────────────────────
    if (m.isEnPassant) {
        int capSq       = enPassantSquare + (whiteToMove ? -8 : 8);
        ui.captured     = squares[capSq];
        zobristHash    ^= Zobrist::pieceKeys[squares[capSq]][capSq];
        squares[capSq]  = EMPTY;
    } else if (ui.captured != EMPTY) {
        // Hash: remove captured piece from destination
        zobristHash ^= Zobrist::pieceKeys[ui.captured][m.to];
    }

    // ── Move the piece ────────────────────────────────────────────────────────
    squares[m.to]   = moving;
    squares[m.from] = EMPTY;

    // ── Promotion ─────────────────────────────────────────────────────────────
    if (m.promotion != EMPTY) {
        squares[m.to]   = m.promotion;
        zobristHash    ^= Zobrist::pieceKeys[m.promotion][m.to];
    } else {
        zobristHash ^= Zobrist::pieceKeys[moving][m.to];
    }

    // ── Castling: move the rook ───────────────────────────────────────────────
    if (m.isCastling) {
        if (m.to == G1) {
            squares[F1] = WR; squares[H1] = EMPTY;
            zobristHash ^= Zobrist::pieceKeys[WR][H1] ^ Zobrist::pieceKeys[WR][F1];
        } else if (m.to == C1) {
            squares[D1] = WR; squares[A1] = EMPTY;
            zobristHash ^= Zobrist::pieceKeys[WR][A1] ^ Zobrist::pieceKeys[WR][D1];
        } else if (m.to == G8) {
            squares[F8] = BR; squares[H8] = EMPTY;
            zobristHash ^= Zobrist::pieceKeys[BR][H8] ^ Zobrist::pieceKeys[BR][F8];
        } else if (m.to == C8) {
            squares[D8] = BR; squares[A8] = EMPTY;
            zobristHash ^= Zobrist::pieceKeys[BR][A8] ^ Zobrist::pieceKeys[BR][D8];
        }
    }

    // ── Update en passant square ──────────────────────────────────────────────
    enPassantSquare = -1;
    if (moving == WP || moving == BP) {
        int diff = m.to - m.from;
        if (diff == 16)  enPassantSquare = m.from + 8;
        if (diff == -16) enPassantSquare = m.from - 8;
    }
    if (enPassantSquare >= 0)
        zobristHash ^= Zobrist::epKeys[fileOf(enPassantSquare)];

    // ── Update castling rights ────────────────────────────────────────────────
    auto revoke = [&](int sq) {
        if (sq == E1) { castlingRights[CR_WK] = false; castlingRights[CR_WQ] = false; }
        if (sq == E8) { castlingRights[CR_BK] = false; castlingRights[CR_BQ] = false; }
        if (sq == H1) castlingRights[CR_WK] = false;
        if (sq == A1) castlingRights[CR_WQ] = false;
        if (sq == H8) castlingRights[CR_BK] = false;
        if (sq == A8) castlingRights[CR_BQ] = false;
    };
    revoke(m.from);
    revoke(m.to);
    for (int i = 0; i < 4; ++i)
        if (castlingRights[i]) zobristHash ^= Zobrist::castleKeys[i];

    // ── Half-move clock ───────────────────────────────────────────────────────
    if (moving == WP || moving == BP || ui.captured != EMPTY)
        halfMoveClock = 0;
    else
        ++halfMoveClock;

    if (!whiteToMove) ++fullMoveNumber;

    // ── Flip side ─────────────────────────────────────────────────────────────
    zobristHash ^= Zobrist::sideKey;
    whiteToMove  = !whiteToMove;

    history.push_back(ui);
}

// ── undoMove ──────────────────────────────────────────────────────────────────
void Board::undoMove() {
    assert(!history.empty());
    UndoInfo ui = history.back();
    history.pop_back();

    const Move& m = ui.move;
    whiteToMove = !whiteToMove;

    // Restore hash and board flags from saved state
    zobristHash     = ui.zobristHash;
    for (int i = 0; i < 4; ++i) castlingRights[i] = ui.castlingRights[i];
    enPassantSquare = ui.enPassantSquare;
    halfMoveClock   = ui.halfMoveClock;
    if (!whiteToMove) --fullMoveNumber;

    Piece moved = squares[m.to];
    if (m.promotion != EMPTY) moved = whiteToMove ? WP : BP;

    squares[m.from] = moved;
    squares[m.to]   = EMPTY;

    if (m.isEnPassant) {
        int capSq      = ui.enPassantSquare + (whiteToMove ? -8 : 8);
        squares[capSq] = ui.captured;
    } else {
        squares[m.to] = ui.captured;
    }

    if (m.isCastling) {
        if (m.to == G1) { squares[H1] = WR; squares[F1] = EMPTY; }
        else if (m.to == C1) { squares[A1] = WR; squares[D1] = EMPTY; }
        else if (m.to == G8) { squares[H8] = BR; squares[F8] = EMPTY; }
        else if (m.to == C8) { squares[A8] = BR; squares[D8] = EMPTY; }
    }
}
