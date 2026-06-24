#include "../include/util.h"

#include "../include/piece.h"
#include "../include/lexer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdnoreturn.h>

int sourcemap_translate(const SourceMap *source_map, int pp_line) {
    if (source_map == NULL || source_map->orig_lines == NULL) {
        return pp_line;
    }
    if (pp_line < 1 || pp_line > source_map->count) {
        return pp_line;
    }
    int mapped = source_map->orig_lines[pp_line];
    return mapped > 0 ? mapped : pp_line;
}

ErrSite errsite_from_token(Token *token,
                           const SourceMap *source_map,
                           const char *filename) {
    if (token == NULL) {
        ErrSite empty = {0};
        empty.filename = filename;
        return empty;
    }
    return errsite_from_lexer(source_map, filename,
                              (int)token->line, (int)token->column);
}

ErrSite errsite_at_line(int line, const char *filename) {
    ErrSite site = {0};
    site.filename = filename;
    site.line = line;
    site.column = 0;
    return site;
}

ErrSite errsite_from_lexer(const SourceMap *source_map,
                           const char *filename,
                           int line, int column) {
    ErrSite site = {0};
    site.filename = filename;
    site.line = sourcemap_translate(source_map, line);
    site.column = column;
    return site;
}

// Print the source-line + caret block below the banner, when source_line
// is non-NULL. With column 0 we still print the source line (without a
// caret) so the user has context.
static void print_source_context(ErrSite site) {
    if (site.source_line == NULL) return;

    // Strip trailing newline so the caret lines up.
    size_t len = strlen(site.source_line);
    while (len > 0 &&
           (site.source_line[len - 1] == '\n' ||
            site.source_line[len - 1] == '\r')) {
        len--;
    }
    fprintf(stderr, "  |\n");
    fprintf(stderr, "%3d | %.*s\n", site.line, (int)len, site.source_line);
    if (site.column > 0) {
        fprintf(stderr, "  | %*s^\n", site.column, "");
    } else {
        fputc('\n', stderr);
    }
}

static void emit_diagnostic(const char *stage, ErrSite site,
                            const char *fmt, va_list args) {
    if (site.filename != NULL) {
        fprintf(stderr, "%s:", site.filename);
    }
    fprintf(stderr, "%d:%d: error in %s: ", site.line, site.column, stage);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    print_source_context(site);
}

noreturn void die(const int exit_code, const char *stage, ErrSite site,
                  const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit_diagnostic(stage, site, fmt, args);
    va_end(args);
    exit(exit_code);
}

void report_error(const char *stage, ErrSite site, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit_diagnostic(stage, site, fmt, args);
    va_end(args);
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