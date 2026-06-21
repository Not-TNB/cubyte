CC     = cc
CFLAGS = -std=c17 -Wall -Wextra -Werror -fsanitize=address,undefined -g
# -Iinclude exposes the public headers (program_ast.h, alg.h, piece.h, ...)
IFLAGS = -Iinclude

CKOCIEMBA_DIR = third_party/ckociemba
KOCIEMBA_PATH_FLAGS = \
    -DCKOCIEMBA_BIN='"$(CURDIR)/$(CKOCIEMBA_DIR)/bin/kociemba"' \
    -DCKOCIEMBA_CACHE='"$(CURDIR)/$(CKOCIEMBA_DIR)/cprunetables"'

# Main compiler binary
EXT_BIN = cubyte

# All extension sources live in src/
EXT_SRC = \
    src/main.c \
    src/program_ast.c \
    src/lexer.c \
    src/preprocessor.c \
    src/print_ast.c \
    src/typechecker.c \
    src/desugarer.c \
    src/alg.c \
    src/cube.c \
    src/liveness.c \
    src/interference.c \
    src/regalloc.c \
    src/ccf.c \
    src/ccs.c \
    src/kociemba.c \
    src/codegen.c \
    src/util.c

.PHONY: all clean test-desugarer

all: $(EXT_BIN) kociemba

kociemba:
	$(MAKE) -C $(CKOCIEMBA_DIR)

$(EXT_BIN): $(EXT_SRC)
	$(CC) $(CFLAGS) $(IFLAGS) $(KOCIEMBA_PATH_FLAGS) $^ -o $@

test-desugarer: tests/test_desugarer
	./tests/test_desugarer

tests/test_desugarer: tests/test_desugarer.c src/desugarer.c src/alg.c src/util.c
	$(CC) $(CFLAGS) $(IFLAGS) $^ -o $@

clean:
	rm -f $(EXT_BIN) tests/test_kociemba tests/kociemba_interactive
	rm -rf $(CKOCIEMBA_DIR)/bin
	rm -f $(EXT_BIN) tests/test_desugarer
