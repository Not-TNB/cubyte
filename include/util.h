#ifndef ARMV8_37_EXTENSION_SRC_UTIL_H
#define ARMV8_37_EXTENSION_SRC_UTIL_H

#include <stdnoreturn.h>

typedef enum {
    EXIT_CODE_OK = 0,
    EXIT_CODE_LEXER = 1,
    EXIT_CODE_PARSE = 2,
    EXIT_CODE_TYPECHECK = 3,
    EXIT_CODE_REGALLOC = 4,
    EXIT_CODE_INTERNAL = 5,
} ExitCode;

#define STAGE_LEXER "lexer"
#define STAGE_PARSE "parse"
#define STAGE_TYPECHECK "typecheck"
#define STAGE_REGALLOC "regalloc"
#define STAGE_INTERNAL "internal"

noreturn void die(int exit_code, const char *stage,
                  int line, const char *fmt, ...);

/* Standard dynamic-array capacity doubling: initial size 8, then 2×. */
static inline int next_cap(int cap) { return cap ? cap * 2 : 8; }

#endif // ARMV8_37_EXTENSION_SRC_UTIL_H
