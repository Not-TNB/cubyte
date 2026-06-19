#include "../include/interference.h"
#include "../include/liveness.h"
#include "../include/piece.h"
#include "../include/program_ast.h"
#include "../include/typechecker.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__,      \
                    __LINE__, #expr);                                          \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define ASSERT_EQ_U64(actual, expected)                                         \
    do {                                                                       \
        uint64_t actual_value = (uint64_t)(actual);                            \
        uint64_t expected_value = (uint64_t)(expected);                        \
        if (actual_value != expected_value) {                                  \
            fprintf(stderr,                                                     \
                    "assertion failed at %s:%d: got 0x%llx, expected 0x%llx\n",\
                    __FILE__, __LINE__,                                        \
                    (unsigned long long)actual_value,                          \
                    (unsigned long long)expected_value);                       \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static char *xstrdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    ASSERT_TRUE(copy != NULL);
    memcpy(copy, s, len);
    return copy;
}

static LiveSet bit(TypeEnv *env, const char *name) {
    int index = typeenv_index(env, name);
    ASSERT_TRUE(index >= 0);
    return UINT64_C(1) << (unsigned)index;
}

static Expr *expr_int(int value) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_INT_LIT;
    expr->resolved_type = TYPE_INT;
    expr->int_val = value;
    return expr;
}

static Expr *expr_var(const char *name) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_VAR;
    expr->resolved_type = TYPE_INT;
    expr->var_name = xstrdup(name);
    return expr;
}

static Expr *expr_add(Expr *left, Expr *right) {
    Expr *expr = calloc(1, sizeof(*expr));
    ASSERT_TRUE(expr != NULL);
    expr->kind = EXPR_ADD;
    expr->resolved_type = TYPE_INT;
    expr->binop.left = left;
    expr->binop.right = right;
    return expr;
}

static Statement *stmt_let_int(const char *name, Expr *init) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_LET;
    stmt->let.name = xstrdup(name);
    stmt->let.type = TYPE_INT;
    stmt->let.init = init;
    return stmt;
}

static Statement *stmt_assign(const char *name, Expr *rhs) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_ASSIGN;
    stmt->assign.name = xstrdup(name);
    stmt->assign.rhs = rhs;
    return stmt;
}

static Cond *cond_eq_var(const char *lhs, const char *rhs,
                         const char *temp1, const char *temp2) {
    Cond *cond = calloc(1, sizeof(*cond));
    ASSERT_TRUE(cond != NULL);
    cond->kind = COND_EQ_VAR;
    cond->eq_var.lhs = xstrdup(lhs);
    cond->eq_var.rhs = xstrdup(rhs);
    cond->eq_var.temp_name_1 = xstrdup(temp1);
    cond->eq_var.temp_name_2 = xstrdup(temp2);
    return cond;
}

static Statement *stmt_if_empty(Cond *cond) {
    Statement *stmt = calloc(1, sizeof(*stmt));
    ASSERT_TRUE(stmt != NULL);
    stmt->kind = STMT_IF;
    stmt->if_.cond = cond;
    return stmt;
}

static CFGNode *node_for_stmt(CFG *cfg, Statement *stmt) {
    for (int i = 0; i < cfg->count; i++) {
        if (cfg->nodes[i].stmt == stmt) {
            return &cfg->nodes[i];
        }
    }
    ASSERT_TRUE(false);
    return NULL;
}

static void declare_ints(TypeEnv *env, const char **names, int count) {
    for (int i = 0; i < count; i++) {
        ASSERT_TRUE(typeenv_declare(env, names[i], TYPE_INT, 1));
    }
}

static void test_straight_line_liveness_and_interference(void) {
    TypeEnv env;
    typeenv_init(&env);
    const char *vars[] = {"x", "y", "z"};
    declare_ints(&env, vars, 3);

    ProgramAST *program = init_program_ast();
    Statement *let_x = stmt_let_int("x", expr_int(1));
    Statement *let_y = stmt_let_int("y", expr_int(2));
    Statement *let_z =
        stmt_let_int("z", expr_add(expr_var("x"), expr_var("y")));
    Statement *assign_x = stmt_assign("x", expr_var("z"));
    append_statement(program, let_x);
    append_statement(program, let_y);
    append_statement(program, let_z);
    append_statement(program, assign_x);

    CFG *cfg = cfg_build(program);
    LivenessResult *live = liveness_analyze(cfg, &env);

    LiveSet x = bit(&env, "x");
    LiveSet y = bit(&env, "y");
    LiveSet z = bit(&env, "z");

    CFGNode *nx = node_for_stmt(cfg, let_x);
    CFGNode *ny = node_for_stmt(cfg, let_y);
    CFGNode *nz = node_for_stmt(cfg, let_z);
    CFGNode *na = node_for_stmt(cfg, assign_x);

    ASSERT_EQ_U64(nx->use, 0);
    ASSERT_EQ_U64(nx->def, x);
    ASSERT_EQ_U64(nx->live_in, 0);
    ASSERT_EQ_U64(nx->live_out, x);

    ASSERT_EQ_U64(ny->use, 0);
    ASSERT_EQ_U64(ny->def, y);
    ASSERT_EQ_U64(ny->live_in, x);
    ASSERT_EQ_U64(ny->live_out, x | y);

    ASSERT_EQ_U64(nz->use, x | y);
    ASSERT_EQ_U64(nz->def, z);
    ASSERT_EQ_U64(nz->live_in, x | y);
    ASSERT_EQ_U64(nz->live_out, z);

    ASSERT_EQ_U64(na->use, z);
    ASSERT_EQ_U64(na->def, x);
    ASSERT_EQ_U64(na->live_in, z);
    ASSERT_EQ_U64(na->live_out, 0);

    InterferenceGraph *ig = ig_build(&env, live->ig_nodes, live->node_count);
    int xi = typeenv_index(&env, "x");
    int yi = typeenv_index(&env, "y");
    int zi = typeenv_index(&env, "z");

    ASSERT_TRUE(ig->count == 3);
    ASSERT_TRUE(ig_interfere(ig, xi, yi));
    ASSERT_TRUE(!ig_interfere(ig, xi, zi));
    ASSERT_TRUE(!ig_interfere(ig, yi, zi));

    ig_free(ig);
    liveness_free(live);
    cfg_free(cfg);
    free_program_ast(program);
    typeenv_free(&env);
}

static void test_condition_temps_are_defined(void) {
    TypeEnv env;
    typeenv_init(&env);
    const char *vars[] = {"x", "y", "__t0", "__t1"};
    declare_ints(&env, vars, 4);

    ProgramAST *program = init_program_ast();
    Statement *let_x = stmt_let_int("x", expr_int(1));
    Statement *let_y = stmt_let_int("y", expr_int(2));
    Statement *if_xy = stmt_if_empty(cond_eq_var("x", "y", "__t0", "__t1"));
    append_statement(program, let_x);
    append_statement(program, let_y);
    append_statement(program, if_xy);

    CFG *cfg = cfg_build(program);
    LivenessResult *live = liveness_analyze(cfg, &env);
    (void)live;

    CFGNode *cond = node_for_stmt(cfg, if_xy);
    ASSERT_EQ_U64(cond->use, bit(&env, "x") | bit(&env, "y"));
    ASSERT_EQ_U64(cond->def, bit(&env, "__t0") | bit(&env, "__t1"));

    liveness_free(live);
    cfg_free(cfg);
    free_program_ast(program);
    typeenv_free(&env);
}

static void test_interference_graph_excludes_r0_and_alg_vars(void) {
    TypeEnv env;
    typeenv_init(&env);
    const char *vars[] = {"x", "y", "z"};
    declare_ints(&env, vars, 3);
    ASSERT_TRUE(typeenv_declare(&env, "moves", TYPE_ALG, 1));

    LiveSet x = bit(&env, "x");
    LiveSet y = bit(&env, "y");
    LiveSet z = bit(&env, "z");
    LiveSet r0 = bit(&env, R0_SYNTHETIC_VARIABLE);
    LiveSet moves = bit(&env, "moves");

    IGLivenessNode nodes[] = {
        {.def = x, .live_out = y | z | r0 | moves},
        {.def = y, .live_out = z},
    };

    InterferenceGraph *ig = ig_build(&env, nodes, 2);
    int xi = typeenv_index(&env, "x");
    int yi = typeenv_index(&env, "y");
    int zi = typeenv_index(&env, "z");
    int r0i = typeenv_index(&env, R0_SYNTHETIC_VARIABLE);
    int moves_i = typeenv_index(&env, "moves");

    ASSERT_TRUE(ig->count == 3);
    ASSERT_TRUE(ig_find_node(ig, xi) != NULL);
    ASSERT_TRUE(ig_find_node(ig, yi) != NULL);
    ASSERT_TRUE(ig_find_node(ig, zi) != NULL);
    ASSERT_TRUE(ig_find_node(ig, r0i) == NULL);
    ASSERT_TRUE(ig_find_node(ig, moves_i) == NULL);

    ASSERT_TRUE(ig_interfere(ig, xi, yi));
    ASSERT_TRUE(ig_interfere(ig, xi, zi));
    ASSERT_TRUE(ig_interfere(ig, yi, zi));
    ASSERT_TRUE(!ig_interfere(ig, xi, r0i));
    ASSERT_TRUE(!ig_interfere(ig, xi, moves_i));

    ig_free(ig);
    typeenv_free(&env);
}

int main(void) {
    test_straight_line_liveness_and_interference();
    test_condition_temps_are_defined();
    test_interference_graph_excludes_r0_and_alg_vars();
    puts("liveness/interference tests passed");
    return 0;
}
