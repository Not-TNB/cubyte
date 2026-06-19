#include "../include/util.h"

#include "../include/piece.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdnoreturn.h>

noreturn void die(const int exit_code, const char *stage,
                  const int line, const char *fmt, ...) {
    va_list args;

    fprintf(stderr, "[%s] line %d: ", stage, line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    exit(exit_code);
}

const char *const piece_label_strings[PC_COUNT] = {
    "UFL",
    "UF",
    "UFR",
    "UL",
    "UR",
    "UBL",
    "UB",
    "UBR",
    "DFL",
    "DF",
    "DFR",
    "DL",
    "DR",
    "DBL",
    "DB",
    "DBR",
    "FL",
    "FR",
    "BL",
    "BR",
};



int piece_from_string(const char *s) {
    if (s == NULL) {
        return -1;
    }

    for (int i = 0; i < PC_COUNT; i++) {
        if (!strcmp(s, piece_label_strings[i])) {
            return i;
        }
    }

    return -1;
}

const char *piece_to_string(int p) {
    //out of bounds
    if (p < 0 || p >= PC_COUNT) {
        return NULL;
    }

    return piece_label_strings[p];
}
