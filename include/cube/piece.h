#ifndef PIECE_H
#define PIECE_H

typedef enum {
    PC_UFL,
    PC_UF,
    PC_UFR,
    PC_UL,
    PC_UR,
    PC_UBL,
    PC_UB,
    PC_UBR,
    PC_DFL,
    PC_DF,
    PC_DFR,
    PC_DL,
    PC_DR,
    PC_DBL,
    PC_DB,
    PC_DBR,
    PC_FL,
    PC_FR,
    PC_BL,
    PC_BR,
    PC_COUNT,
} PieceLabel;

/* R0 is the synthetic input/output register. It deliberately uses only 4
 * corners (a twisted 1+3 corner pattern, order 9) instead of all 8 corners.
 * Spending the dense, high-order corner pieces on the IO register starved the
 * actual computation, which was confined to the 12 edges; freeing four corners
 * lets variable/scratch registers live there too. Trade-off: IO values are now
 * tracked mod 9 instead of mod 18. */
#define R0_ALGORITHM "B2 U D L2 B2 D2 R2 F2 U R U' R' D R U R"
#define R0_SYNTHETIC_VARIABLE "_io"
#define R0_SYNTHETIC_TYPE "int"

extern const char *const piece_label_strings[PC_COUNT];

int piece_from_string(const char *s);
const char *piece_to_string(int p);

#endif /* PIECE_H */
