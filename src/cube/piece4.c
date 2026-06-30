#include "../../include/cube/piece4.h"

const char *const piece4_label_strings[PC4_COUNT] = {
    /* Corners (0–7) */
    [PC4_UFL] = "UFL", [PC4_UFR] = "UFR",
    [PC4_UBL] = "UBL", [PC4_UBR] = "UBR",
    [PC4_DFL] = "DFL", [PC4_DFR] = "DFR",
    [PC4_DBL] = "DBL", [PC4_DBR] = "DBR",
    /* Wing edges (8–31) */
    [PC4_UF_A] = "UF_A", [PC4_UF_B] = "UF_B",
    [PC4_UB_A] = "UB_A", [PC4_UB_B] = "UB_B",
    [PC4_UL_A] = "UL_A", [PC4_UL_B] = "UL_B",
    [PC4_UR_A] = "UR_A", [PC4_UR_B] = "UR_B",
    [PC4_DF_A] = "DF_A", [PC4_DF_B] = "DF_B",
    [PC4_DB_A] = "DB_A", [PC4_DB_B] = "DB_B",
    [PC4_DL_A] = "DL_A", [PC4_DL_B] = "DL_B",
    [PC4_DR_A] = "DR_A", [PC4_DR_B] = "DR_B",
    [PC4_FL_A] = "FL_A", [PC4_FL_B] = "FL_B",
    [PC4_FR_A] = "FR_A", [PC4_FR_B] = "FR_B",
    [PC4_BL_A] = "BL_A", [PC4_BL_B] = "BL_B",
    [PC4_BR_A] = "BR_A", [PC4_BR_B] = "BR_B",
    /* X-centres (32–55) */
    [PC4_U_FL] = "U_FL", [PC4_U_FR] = "U_FR",
    [PC4_U_BL] = "U_BL", [PC4_U_BR] = "U_BR",
    [PC4_D_FL] = "D_FL", [PC4_D_FR] = "D_FR",
    [PC4_D_BL] = "D_BL", [PC4_D_BR] = "D_BR",
    [PC4_L_UF] = "L_UF", [PC4_L_UB] = "L_UB",
    [PC4_L_DF] = "L_DF", [PC4_L_DB] = "L_DB",
    [PC4_R_UF] = "R_UF", [PC4_R_UB] = "R_UB",
    [PC4_R_DF] = "R_DF", [PC4_R_DB] = "R_DB",
    [PC4_F_UL] = "F_UL", [PC4_F_UR] = "F_UR",
    [PC4_F_DL] = "F_DL", [PC4_F_DR] = "F_DR",
    [PC4_B_UL] = "B_UL", [PC4_B_UR] = "B_UR",
    [PC4_B_DL] = "B_DL", [PC4_B_DR] = "B_DR",
};

const char *piece4_to_string(int p) {
    if (p < 0 || p >= PC4_COUNT) return "?";
    return piece4_label_strings[p];
}
