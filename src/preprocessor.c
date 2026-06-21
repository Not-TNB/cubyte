#include "../include/preprocessor.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/util.h"

// ---------------------------------------------------------------------------
// Macro table
// ---------------------------------------------------------------------------

typedef struct {
    char *name;            // owned
    int defined_line;      // source line of the #define for error messages
    bool is_function_like; // false => object-like
    char **params;         // owned, function-like only
    int param_count;
    char *body;            // owned substitution text
} Macro;

#define MAX_MACRO_EXPANSION_DEPTH 32

static Macro *macros = NULL;
static int macro_count = 0;
static int macro_capacity = 0;

static Macro *find_macro(const char *name, int name_len) {
    for (int i = 0; i < macro_count; i++) {
        if (strlen(macros[i].name) == (size_t)name_len &&
            memcmp(macros[i].name, name, name_len) == 0) {
            return &macros[i];
        }
    }
    return NULL;
}

static void free_macro(Macro *m) {
    free(m->name);
    free(m->body);
    for (int i = 0; i < m->param_count; i++) {
        free(m->params[i]);
    }
    free(m->params);
    m->name = NULL;
    m->body = NULL;
    m->params = NULL;
    m->param_count = 0;
}

static void free_macro_table(void) {
    for (int i = 0; i < macro_count; i++) {
        free_macro(&macros[i]);
    }
    free(macros);
    macros = NULL;
    macro_count = 0;
    macro_capacity = 0;
}

static void grow_macro_table(void) {
    if (macro_count < macro_capacity) return;
    int new_cap = next_cap(macro_capacity);
    Macro *resized = realloc(macros, (size_t)new_cap * sizeof(Macro));
    if (resized == NULL) {
        perror("realloc failed");
        exit(EXIT_FAILURE);
    }
    macros = resized;
    macro_capacity = new_cap;
}

static char *dup_cstr_n(const char *src, size_t n) {
    char *out = malloc(n + 1);
    if (out == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    memcpy(out, src, n);
    out[n] = '\0';
    return out;
}

// ---------------------------------------------------------------------------
// Comment stripping (shared by both passes)
// ---------------------------------------------------------------------------

// Replace the first "//" with a NUL terminator. Returns nothing; mutates line.
static void strip_line_comment(char *line) {
    char *cursor = line;
    while (*cursor != '\0') {
        if (*cursor == '/' && cursor[1] == '/') {
            *cursor = '\0';
            return;
        }
        cursor++;
    }
}

// ---------------------------------------------------------------------------
// Identifier scanning helpers
// ---------------------------------------------------------------------------

// True iff `c` can start an identifier.
static bool is_ident_start(char c) {
    return c == '_' || isalpha((unsigned char)c);
}

// True iff `c` can appear inside an identifier.
static bool is_ident_cont(char c) {
    return c == '_' || isalnum((unsigned char)c);
}

// Skip horizontal whitespace; returns pointer into `s`.
static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// ---------------------------------------------------------------------------
// Macro definition parsing (pass 1)
// ---------------------------------------------------------------------------

// After "#define" has been consumed and whitespace skipped, `p` points at the
// remainder of the line (NUL-terminated). On success populates the macro
// table. `line_no` is the source line for error messages.
static void parse_define_directive(const char *p, int line_no) {
    p = skip_ws(p);
    if (*p == '\0') {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
            "#define missing macro name");
    }

    if (!is_ident_start(*p)) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
            "#define expected identifier, got '%c'", *p);
    }

    const char *name_start = p;
    while (is_ident_cont(*p)) p++;
    size_t name_len = (size_t)(p - name_start);

    p = skip_ws(p);

    bool is_function = false;
    char **params = NULL;
    int param_count = 0;

    if (*p == '(') {
        is_function = true;
        p++; // consume '('

        // Empty parameter list `#define f()` is allowed; treat as function-like
        // with zero params.
        p = skip_ws(p);
        if (*p == ')') {
            p++; // consume ')'
        } else {
            // Parse comma-separated identifiers.
            for (;;) {
                p = skip_ws(p);
                if (!is_ident_start(*p)) {
                    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
                        "malformed #define for function-like macro '%.*s'",
                        (int)name_len, name_start);
                }
                const char *param_start = p;
                while (is_ident_cont(*p)) p++;
                size_t param_len = (size_t)(p - param_start);

                char **grown = realloc(params,
                                       (size_t)(param_count + 1) * sizeof(char *));
                if (grown == NULL) {
                    perror("realloc failed");
                    exit(EXIT_FAILURE);
                }
                params = grown;
                params[param_count++] = dup_cstr_n(param_start, param_len);

                p = skip_ws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == ')') {
                    p++;
                    break;
                }
                die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
                    "malformed #define for function-like macro '%.*s' "
                    "(expected ',' or ')')",
                    (int)name_len, name_start);
            }
        }
    }

    // Whatever remains is the body (trimmed of trailing whitespace).
    const char *body_start = skip_ws(p);
    size_t body_len = strlen(body_start);
    while (body_len > 0 &&
           (body_start[body_len - 1] == ' ' || body_start[body_len - 1] == '\t' ||
            body_start[body_len - 1] == '\r' || body_start[body_len - 1] == '\n')) {
        body_len--;
    }

    if (body_len == 0 && !is_function) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
            "#define missing body for macro '%.*s'", (int)name_len, name_start);
    }

    char *name = dup_cstr_n(name_start, name_len);
    // Reject redefinition.
    Macro *existing = find_macro(name, (int)name_len);
    if (existing != NULL) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, line_no,
            "redefinition of macro '%s' (previously defined on line %d)",
            name, existing->defined_line);
    }
    char *body = dup_cstr_n(body_start, body_len);

    grow_macro_table();
    macros[macro_count].name = name;
    macros[macro_count].defined_line = line_no;
    macros[macro_count].is_function_like = is_function;
    macros[macro_count].params = params;
    macros[macro_count].param_count = param_count;
    macros[macro_count].body = body;
    macro_count++;
}

// True if `line` (after possible leading whitespace) starts with "#define".
static bool line_starts_with_define(const char *line) {
    const char *p = skip_ws(line);
    static const char kw[] = "#define";
    for (int i = 0; i < 7; i++) {
        if (p[i] != kw[i]) return false;
    }
    char after = p[7];
    return after == '\0' || after == ' ' || after == '\t';
}

// Read the file once, strip comments, and record every `#define` into the
// macro table. Caller is responsible for opening `input`.
static void collect_macros(FILE *input) {
    char *line = malloc(MAX_LINE_LENGTH);
    if (line == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    int line_no = 1;
    while (fgets(line, MAX_LINE_LENGTH, input) != NULL) {
        strip_line_comment(line);
        if (line_starts_with_define(line)) {
            // Skip the leading "#define" token and the whitespace after it.
            const char *p = skip_ws(line);
            p += 7; // skip "#define"
            parse_define_directive(p, line_no);
        }
        line_no++;
    }

    free(line);
}

// ---------------------------------------------------------------------------
// Macro expansion (pass 2)
// ---------------------------------------------------------------------------

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->data = malloc(64);
    if (b->data == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    b->data[0] = '\0';
    b->len = 0;
    b->cap = 64;
}

static void buf_reserve(Buf *b, size_t need) {
    if (need <= b->cap) return;
    size_t new_cap = b->cap;
    while (new_cap < need) new_cap *= 2;
    char *grown = realloc(b->data, new_cap);
    if (grown == NULL) {
        perror("realloc failed");
        exit(EXIT_FAILURE);
    }
    b->data = grown;
    b->cap = new_cap;
}

static void buf_append_n(Buf *b, const char *src, size_t n) {
    buf_reserve(b, b->len + n + 1);
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_append_char(Buf *b, char c) {
    buf_append_n(b, &c, 1);
}

static void buf_free(Buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

// Parse the parenthesised argument list starting at `p` (which points just
// past the opening '('). On success returns a pointer past the closing ')'
// and writes the joined argument text into `args`, plus per-argument offsets
// and lengths. The joined text preserves quotes so the rescan can skip
// strings correctly.
static const char *parse_arg_list(const char *p, Buf *args,
                                  size_t **arg_offsets_out,
                                  size_t **arg_lengths_out,
                                  int *arg_count_out) {
    int depth = 1;
    while (*p != '\0') {
        char c = *p;
        if (c == '"') {
            buf_append_char(args, c);
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    buf_append_char(args, *p++);
                }
                if (*p == '\0') break;
                buf_append_char(args, *p++);
            }
            if (*p == '"') {
                buf_append_char(args, *p++);
            }
            continue;
        }
        if (c == '(') {
            buf_append_char(args, c);
            depth++;
            p++;
            continue;
        }
        if (c == ')') {
            depth--;
            if (depth == 0) {
                // End of arg list. Split joined text on top-level commas.
                size_t cap = 4;
                size_t *offs = malloc(cap * sizeof(size_t));
                size_t *lens = malloc(cap * sizeof(size_t));
                if (offs == NULL || lens == NULL) {
                    perror("malloc failed");
                    exit(EXIT_FAILURE);
                }
                int count = 0;
                size_t astart = 0;
                int d = 0;
                bool in_str = false;
                for (size_t i = 0; i < args->len; i++) {
                    char ch = args->data[i];
                    if (in_str) {
                        if (ch == '"' && (i == 0 || args->data[i - 1] != '\\')) {
                            in_str = false;
                        }
                        continue;
                    }
                    if (ch == '"') {
                        in_str = true;
                        continue;
                    }
                    if (ch == '(') {
                        d++;
                    } else if (ch == ')') {
                        d--;
                    } else if (ch == ',' && d == 0) {
                        if (count == (int)cap) {
                            cap *= 2;
                            size_t *grown_offs = realloc(offs, cap * sizeof(size_t));
                            size_t *grown_lens = realloc(lens, cap * sizeof(size_t));
                            if (grown_offs == NULL || grown_lens == NULL) {
                                perror("realloc failed");
                                exit(EXIT_FAILURE);
                            }
                            offs = grown_offs;
                            lens = grown_lens;
                        }
                        offs[count] = astart;
                        lens[count] = i - astart;
                        count++;
                        astart = i + 1;
                    }
                }
                if (astart <= args->len) {
                    if (count == (int)cap) {
                        cap *= 2;
                        size_t *grown_offs = realloc(offs, cap * sizeof(size_t));
                        size_t *grown_lens = realloc(lens, cap * sizeof(size_t));
                        if (grown_offs == NULL || grown_lens == NULL) {
                            perror("realloc failed");
                            exit(EXIT_FAILURE);
                        }
                        offs = grown_offs;
                        lens = grown_lens;
                    }
                    offs[count] = astart;
                    lens[count] = args->len - astart;
                    count++;
                }

                *arg_offsets_out = offs;
                *arg_lengths_out = lens;
                *arg_count_out = count;
                p++; // consume ')'
                return p;
            }
            buf_append_char(args, c);
            p++;
            continue;
        }
        buf_append_char(args, c);
        p++;
    }

    die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
        "unterminated macro argument list");
    return NULL; // unreachable
}

// Trim leading/trailing horizontal whitespace from a substring.
static void trim_substring(const char *src, size_t start, size_t len,
                           const char **out_start, size_t *out_len) {
    while (len > 0 && (src[start] == ' ' || src[start] == '\t')) {
        start++;
        len--;
    }
    while (len > 0 &&
           (src[start + len - 1] == ' ' || src[start + len - 1] == '\t')) {
        len--;
    }
    *out_start = src + start;
    *out_len = len;
}

// Apply parameter substitution to a macro body. Emits to `out`.
static void substitute_params(const char *body, size_t body_len,
                              char **params, int param_count,
                              const char **arg_strs, const size_t *arg_str_lens,
                              Buf *out) {
    size_t i = 0;
    while (i < body_len) {
        char c = body[i];
        if (is_ident_start(c)) {
            size_t j = i;
            while (j < body_len && is_ident_cont(body[j])) j++;
            bool word_boundary_before = (i == 0) || !is_ident_cont(body[i - 1]);
            bool word_boundary_after = (j == body_len) || !is_ident_cont(body[j]);
            if (word_boundary_before && word_boundary_after) {
                int matched = -1;
                for (int k = 0; k < param_count; k++) {
                    size_t plen = strlen(params[k]);
                    if (plen == j - i &&
                        memcmp(params[k], body + i, plen) == 0) {
                        matched = k;
                        break;
                    }
                }
                if (matched >= 0) {
                    buf_append_n(out, arg_strs[matched], arg_str_lens[matched]);
                    i = j;
                    continue;
                }
            }
            buf_append_n(out, body + i, j - i);
            i = j;
        } else {
            buf_append_char(out, c);
            i++;
        }
    }
}

// Recursively expand macros in [src, src+len). Handles string literals and
// identifier substitution. `depth` starts at 0 and is incremented per
// recursive call.
static void expand_substring(const char *src, size_t len, Buf *out, int depth) {
    if (depth > MAX_MACRO_EXPANSION_DEPTH) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "macro expansion exceeded depth limit (possible recursion)");
    }

    size_t i = 0;
    while (i < len) {
        char c = src[i];

        // String literal: copy verbatim, no expansion.
        if (c == '"') {
            size_t j = i;
            buf_append_char(out, src[j++]);
            while (j < len && src[j] != '"') {
                if (src[j] == '\\' && j + 1 < len) {
                    buf_append_char(out, src[j++]);
                }
                if (j >= len) break;
                buf_append_char(out, src[j++]);
            }
            if (j < len && src[j] == '"') {
                buf_append_char(out, src[j++]);
            }
            i = j;
            continue;
        }

        // Identifier: maybe substitute as object-like or function-like macro.
        if (is_ident_start(c)) {
            size_t j = i;
            while (j < len && is_ident_cont(src[j])) j++;
            size_t name_len = j - i;
            Macro *m = find_macro(src + i, (int)name_len);
            if (m == NULL) {
                // Not a macro -- copy identifier verbatim.
                buf_append_n(out, src + i, name_len);
                i = j;
                continue;
            }

            // Look ahead past whitespace to see whether '(' follows.
            size_t k = j;
            while (k < len && (src[k] == ' ' || src[k] == '\t')) k++;
            bool has_paren = (k < len && src[k] == '(');

            if (m->is_function_like && has_paren) {
                // Function-like invocation: parse arg list, expand args,
                // substitute params in body, then recursively expand.
                if (m->param_count == 0) {
                    // Empty arg list still parsed (and discarded).
                    Buf empty_args;
                    buf_init(&empty_args);
                    size_t *arg_offs = NULL;
                    size_t *arg_lens = NULL;
                    int arg_count = 0;
                    const char *after = parse_arg_list(src + k + 1, &empty_args,
                                                       &arg_offs, &arg_lens,
                                                       &arg_count);
                    (void)arg_offs;
                    (void)arg_lens;
                    (void)arg_count;
                    Buf eb;
                    buf_init(&eb);
                    expand_substring(m->body, strlen(m->body), &eb, depth + 1);
                    buf_append_n(out, eb.data, eb.len);
                    buf_free(&eb);
                    buf_free(&empty_args);
                    free(arg_offs);
                    free(arg_lens);
                    i = (size_t)(after - src);
                    continue;
                }

                Buf args;
                buf_init(&args);
                size_t *arg_offs = NULL;
                size_t *arg_lens = NULL;
                int arg_count = 0;
                const char *after = parse_arg_list(src + k + 1, &args,
                                                   &arg_offs, &arg_lens,
                                                   &arg_count);

                // Trim and recursively expand each argument.
                const char **arg_strs =
                    malloc((size_t)arg_count * sizeof(char *));
                size_t *arg_trim_lens =
                    malloc((size_t)arg_count * sizeof(size_t));
                if (arg_strs == NULL || arg_trim_lens == NULL) {
                    perror("malloc failed");
                    exit(EXIT_FAILURE);
                }
                char **arg_owned =
                    malloc((size_t)arg_count * sizeof(char *));
                if (arg_owned == NULL) {
                    perror("malloc failed");
                    exit(EXIT_FAILURE);
                }

                for (int a = 0; a < arg_count; a++) {
                    const char *t_start;
                    size_t t_len;
                    trim_substring(args.data, arg_offs[a], arg_lens[a],
                                   &t_start, &t_len);
                    Buf eb;
                    buf_init(&eb);
                    expand_substring(t_start, t_len, &eb, depth + 1);
                    arg_owned[a] = eb.data;
                    arg_strs[a] = eb.data;
                    arg_trim_lens[a] = eb.len;
                }

                // Substitute parameters in body.
                Buf sub;
                buf_init(&sub);
                substitute_params(m->body, strlen(m->body),
                                  m->params, m->param_count,
                                  arg_strs, arg_trim_lens, &sub);

                // Recursively expand the substituted body.
                Buf eb;
                buf_init(&eb);
                expand_substring(sub.data, sub.len, &eb, depth + 1);
                buf_append_n(out, eb.data, eb.len);

                buf_free(&eb);
                buf_free(&sub);
                for (int a = 0; a < arg_count; a++) free(arg_owned[a]);
                free(arg_owned);
                free(arg_strs);
                free(arg_trim_lens);
                free(arg_offs);
                free(arg_lens);
                buf_free(&args);

                i = (size_t)(after - src);
                continue;
            }

            if (!m->is_function_like) {
                // Object-like macro: expand body recursively.
                Buf eb;
                buf_init(&eb);
                expand_substring(m->body, strlen(m->body), &eb, depth + 1);
                buf_append_n(out, eb.data, eb.len);
                buf_free(&eb);
                i = j;
                continue;
            }

            // Function-like macro but used without '('. Leave identifier
            // untouched.
            buf_append_n(out, src + i, name_len);
            i = j;
            continue;
        }

        buf_append_char(out, c);
        i++;
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

// Preprocesses the file then saves the result to {base}-pp.cbyte
// where {base} is `filename` with a trailing ".cbyte" extension stripped.
void preprocess(const char *filename) {
    static const char suffix[] = ".cbyte";
    const size_t suffix_len = sizeof(suffix) - 1;
    const size_t filename_length = strlen(filename);

    if (filename_length < suffix_len ||
        strcmp(filename + filename_length - suffix_len, suffix) != 0) {
        die(EXIT_CODE_INTERNAL, STAGE_INTERNAL, 0,
            "preprocess: expected filename ending in '.cbyte', got '%s'",
            filename);
    }

    const size_t base_length = filename_length - suffix_len;
    char *input_path = calloc(base_length + suffix_len + 1, sizeof(char));
    char *output_path = calloc(base_length + sizeof("-pp.cbyte"), sizeof(char));

    if (input_path == NULL || output_path == NULL) {
        perror("Malloc failed");
        exit(EXIT_FAILURE);
    }

    memcpy(input_path, filename, base_length);
    strcat(input_path, suffix);
    memcpy(output_path, filename, base_length);
    strcat(output_path, "-pp.cbyte");

    FILE *input = fopen(input_path, "r");
    FILE *output = fopen(output_path, "w");

    if (input == NULL || output == NULL) {
        free(input_path);
        free(output_path);
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    // Pass 1: collect macro definitions.
    collect_macros(input);

    // Pass 2: re-read source, strip comments, drop #define lines, expand
    // remaining macros.
    rewind(input);

    char *line = malloc(MAX_LINE_LENGTH);
    if (line == NULL) {
        free(input_path);
        free(output_path);
        fclose(input);
        fclose(output);
        perror("Malloc failed");
        exit(EXIT_FAILURE);
    }

    while (fgets(line, MAX_LINE_LENGTH, input) != NULL) {
        strip_line_comment(line);
        if (line_starts_with_define(line)) {
            // Definition already collected in pass 1; don't emit it.
            continue;
        }
        Buf expanded;
        buf_init(&expanded);
        expand_substring(line, strlen(line), &expanded, 0);
        fputs(expanded.data, output);
        buf_free(&expanded);
    }

    free_macro_table();
    free(output_path);
    free(input_path);
    free(line);

    fclose(input);
    fclose(output);
}
