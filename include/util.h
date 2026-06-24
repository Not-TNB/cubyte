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
#define STAGE_PREPROCESSOR "preprocessor"
#define STAGE_PARSE "parse"
#define STAGE_TYPECHECK "typecheck"
#define STAGE_REGALLOC "regalloc"
#define STAGE_INTERNAL "internal"

/*
 * A location in source code for error reporting. `filename` and `source_line`
 * may be NULL; `column` may be 0 if unknown. Both pointers are borrowed and
 * must outlive the call to die()/report_error().
 */
typedef struct {
    const char *filename;
    int line;
    int column;
    const char *source_line;
} ErrSite;

/*
 * Print a diagnostic and exit with `exit_code`. Used for unrecoverable
 * errors that abort compilation.
 */
noreturn void die(int exit_code, const char *stage, ErrSite site,
                  const char *fmt, ...);

/*
 * Print a diagnostic but return. Used by parser/lexer recovery paths that
 * need to keep going to find more errors.
 */
void report_error(const char *stage, ErrSite site, const char *fmt, ...);

/*
 * Maps preprocessed-file line numbers back to original-file line numbers.
 * `orig_lines[i]` is the original line corresponding to preprocessed line
 * `i`. Index 0 is unused (1-indexed to mirror the lexer's current_line).
 *
 * Owned by parse_program; passed by pointer to the lexer / parser. May be
 * NULL, in which case line numbers are not translated (1:1 identity).
 */
typedef struct {
    const int *orig_lines;
    int count;
} SourceMap;

/*
 * Translate a preprocessed-file line into the original-file line. Returns
 * `pp_line` unchanged if `source_map` is NULL or `pp_line` is out of range.
 */
int sourcemap_translate(const SourceMap *source_map, int pp_line);

/*
 * Build an ErrSite pointing at `token`'s line/column (after translation
 * through `source_map` if non-NULL). `filename` is borrowed. The returned
 * ErrSite is value-typed; no allocation, no ownership.
 */
struct Token;
ErrSite errsite_from_token(struct Token *token,
                           const SourceMap *source_map,
                           const char *filename);

/*
 * Convenience: build an ErrSite at a known source line, with column 0 and
 * no source line text. Used by passes that only have a line number (e.g.
 * preprocessor).
 */
ErrSite errsite_at_line(int line, const char *filename);

/*
 * Build an ErrSite from a Lexer's current position. `source_map` is the
 * optional line-remap (NULL = identity). Used by the lexer itself.
 */
ErrSite errsite_from_lexer(const struct SourceMap *source_map,
                           const char *filename,
                           int line, int column);

/* Standard dynamic-array capacity doubling: initial size 8, then 2×. */
static inline int next_cap(int cap) { return cap ? cap * 2 : 8; }

#endif // ARMV8_37_EXTENSION_SRC_UTIL_H