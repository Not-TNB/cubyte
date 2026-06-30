#ifndef PIECE4_H
#define PIECE4_H

typedef enum {
    /* Corners (0–7) */
    PC4_UFL,
    PC4_UFR,
    PC4_UBL,
    PC4_UBR,
    PC4_DFL,
    PC4_DFR,
    PC4_DBL,
    PC4_DBR,

    /* Wing edges (8–31): _A = wing closer to first-named face, _B = other */
    PC4_UF_A, PC4_UF_B,
    PC4_UB_A, PC4_UB_B,
    PC4_UL_A, PC4_UL_B,
    PC4_UR_A, PC4_UR_B,
    PC4_DF_A, PC4_DF_B,
    PC4_DB_A, PC4_DB_B,
    PC4_DL_A, PC4_DL_B,
    PC4_DR_A, PC4_DR_B,
    PC4_FL_A, PC4_FL_B,
    PC4_FR_A, PC4_FR_B,
    PC4_BL_A, PC4_BL_B,
    PC4_BR_A, PC4_BR_B,

    /* X-centres (32–55): named by adjacent corner of their face */
    PC4_U_FL, PC4_U_FR, PC4_U_BL, PC4_U_BR,
    PC4_D_FL, PC4_D_FR, PC4_D_BL, PC4_D_BR,
    PC4_L_UF, PC4_L_UB, PC4_L_DF, PC4_L_DB,
    PC4_R_UF, PC4_R_UB, PC4_R_DF, PC4_R_DB,
    PC4_F_UL, PC4_F_UR, PC4_F_DL, PC4_F_DR,
    PC4_B_UL, PC4_B_UR, PC4_B_DL, PC4_B_DR,

    PC4_COUNT, /* = 56 */
} PieceLabel4;

extern const char *const piece4_label_strings[PC4_COUNT];

const char *piece4_to_string(int p);

#endif /* PIECE4_H */
