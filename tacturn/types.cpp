// TacTurn Chess Engine v1 — types implementation.

#include "types.h"

namespace tacturn {

const int PIECE_VALUES[7] = {100, 320, 330, 500, 950, 20000, 0};

char pieceFenChar(int type) {
    static const char CH[6] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    return CH[type];
}

int pieceTypeFromFen(char c) {
    switch (c) {
        case 'P': case 'p': return PAWN;
        case 'N': case 'n': return KNIGHT;
        case 'B': case 'b': return BISHOP;
        case 'R': case 'r': return ROOK;
        case 'Q': case 'q': return QUEEN;
        case 'K': case 'k': return KING;
        default: return NO_PIECE;
    }
}

int pieceSideFromFen(char c) { return (c >= 'A' && c <= 'Z') ? WHITE : BLACK; }

char sanPromoLetter(int type) {
    switch (type) {
        case KNIGHT: return 'N';
        case BISHOP: return 'B';
        case ROOK: return 'R';
        default: return 'Q';
    }
}

void squareName(int sq, char out[3]) {
    out[0] = char('a' + (sq & 7));
    out[1] = char('1' + (sq >> 3));
    out[2] = 0;
}

int squareFromName(const char *name) {
    if (!name || name[0] == 0 || name[1] == 0) return NO_SQUARE;
    int f = name[0] - 'a';
    int r = name[1] - '1';
    if (f < 0 || f > 7 || r < 0 || r > 7) return NO_SQUARE;
    return r * 8 + f;
}

void moveToUci(Move m, char out[8]) {
    if (m == MOVE_NONE) {
        out[0] = '0'; out[1] = '0'; out[2] = '0'; out[3] = '0'; out[4] = 0;
        return;
    }
    squareName(moveFrom(m), out);
    squareName(moveTo(m), out + 2);
    int promo = movePromo(m);
    if (promo != 0) {
        char c = sanPromoLetter(promo);
        out[4] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        out[5] = 0;
    } else {
        out[4] = 0;
    }
}

std::string moveToUciStr(Move m) {
    char buf[8];
    moveToUci(m, buf);
    return std::string(buf);
}

}  // namespace tacturn
