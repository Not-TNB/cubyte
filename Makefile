CC     = cc
CFLAGS = -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -D_POSIX_C_SOURCE=200809L
# -Iinclude exposes the public headers (program_ast.h, alg.h, piece.h, ...)
IFLAGS = -Iinclude

CKOCIEMBA_DIR = third_party/ckociemba
KOCIEMBA_PATH_FLAGS = \
    -DCKOCIEMBA_BIN='"$(CURDIR)/$(CKOCIEMBA_DIR)/bin/kociemba"' \
    -DCKOCIEMBA_CACHE='"$(CURDIR)/$(CKOCIEMBA_DIR)/cprunetables"'

# Main compiler binary
EXT_BIN = cubyte

# Sources grouped by layer
EXT_SRC = \
    src/main.c \
    src/util.c \
    src/cube/cube3.c \
    src/cube/cube4.c \
    src/cube/piece4.c \
    src/cube/cube_impl.c \
    src/cube/alg.c \
    src/cube/kociemba.c \
    src/cube/ccf.c \
    src/cube/ccs.c \
    src/cube/ccf4.c \
    src/cube/ccs4.c \
    src/frontend/lexer.c \
    src/frontend/preprocessor.c \
    src/frontend/program_ast.c \
    src/frontend/print_ast.c \
    src/frontend/typechecker.c \
    src/frontend/desugarer.c \
    src/backend/liveness.c \
    src/backend/interference.c \
    src/backend/regalloc.c \
    src/backend/codegen.c

.PHONY: all clean test-desugarer test-cube4 cube4-shell

all: $(EXT_BIN) kociemba

kociemba:
	$(MAKE) -C $(CKOCIEMBA_DIR)

$(EXT_BIN): $(EXT_SRC)
	$(CC) $(CFLAGS) $(IFLAGS) $(KOCIEMBA_PATH_FLAGS) $^ -o $@

test-desugarer: tests/test_desugarer
	./tests/test_desugarer

tests/test_desugarer: tests/test_desugarer.c src/frontend/desugarer.c src/cube/alg.c src/util.c
	$(CC) $(CFLAGS) $(IFLAGS) $^ -o $@

test-cube4: tests/test_cube4
	./tests/test_cube4

tests/test_cube4: tests/test_cube4.c src/cube/cube4.c src/cube/piece4.c src/cube/alg.c src/util.c
	$(CC) $(CFLAGS) $(IFLAGS) $^ -o $@

cube4-shell: tools/cube4_shell
	./tools/cube4_shell

tools/cube4_shell: tools/cube4_shell.c src/cube/cube4.c src/cube/piece4.c src/cube/alg.c src/util.c
	$(CC) $(CFLAGS) $(IFLAGS) $^ -o $@

clean:
	rm -f $(EXT_BIN) tests/test_kociemba tests/kociemba_interactive
	rm -rf $(CKOCIEMBA_DIR)/bin
	rm -f $(EXT_BIN) tests/test_desugarer
