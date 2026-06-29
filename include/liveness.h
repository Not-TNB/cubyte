#ifndef LIVENESS_H
#define LIVENESS_H

#include <stdio.h>

#include "interference.h"
#include "program_ast.h"
#include "typechecker.h"

typedef enum {
    CFG_NODE_ENTRY,
    CFG_NODE_EXIT,
    CFG_NODE_STMT,
} CFGNodeKind;

typedef struct {
    int id;
    CFGNodeKind kind;
    Statement *stmt;

    int succ[2];
    int succ_count;

    LiveSet use;
    LiveSet def;
    LiveSet live_in;
    LiveSet live_out;
} CFGNode;

typedef struct {
    CFGNode *nodes;
    int count;
    int capacity;

    int entry;
    int exit;
} CFG;

typedef struct {
    CFG *cfg;
    TypeEnv *env;

    IGLivenessNode *ig_nodes;
    int node_count;
} LivenessResult;

/*
  Build a control-flow graph from a desugared AST.

  The graph always contains synthetic entry and exit nodes. Structured
  statements get condition nodes for STMT_IF and STMT_WHILE; labels are
  ordinary no-op statement nodes, so a goto targets the label node and then
  falls through to the labelled code.
 */
CFG *cfg_build(ProgramAST *program);

void cfg_free(CFG *cfg);

/*
  Print:
    node <id>: <summary> -> <succ ids>
 */
void cfg_dump(const CFG *cfg, FILE *out);

/*
  Compute use/def and live-in/live-out sets for cfg.

  The result borrows cfg and env. Its ig_nodes array is owned by the result
  and can be passed directly to ig_build(env, result->ig_nodes,
  result->node_count).
 */
LivenessResult *liveness_analyze(CFG *cfg, TypeEnv *env);

void liveness_free(LivenessResult *result);

/*
  Print:
    node <id>: in={...} out={...} use={...} def={...}
 */
void liveness_dump(const LivenessResult *result, FILE *out);

#endif
