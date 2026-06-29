#ifndef INTERFERENCE_H
#define INTERFERENCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "typechecker.h"

/*
 * Interference graph public interface.
 *
 * This module consumes liveness results after desugaring. Liveness bitsets use
 * TypeEnv indices: bit i means the int variable with VarEntry.index == i is in
 * that set. Alg variables are compile-time-only and are ignored here. R0 is
 * also excluded from the graph; P3 enforces R0 disjointness unconditionally.
 *
 * How register allocation uses this graph:
 *
 *   typecheck -> desugar -> cfg -> liveness -> ig_build -> graph colouring
 *
 * Each int variable becomes a graph node. An edge x--y means x and y are live
 * at the same time somewhere in the program, so they cannot share the same
 * physical cube register. If two variables have no edge, their lifetimes do
 * not overlap and the allocator is free to reuse one register for both.
 *
 * In an ordinary compiler, a graph colour is a hardware register number. In
 * CuBit, a colour is a physical cube register record:
 *
 *   variable -> Ri, with an algorithm string, order K, and cycle set C
 *
 * A colouring pass should usually visit IG nodes in a deterministic order
 * such as highest degree first, then alphabetical by name. For each variable:
 *
 *   1. Start forbidden_cycles with R0's cycle set.
 *   2. For every already-coloured neighbour, add that neighbour register's
 *      cycle set to forbidden_cycles.
 *   3. Reuse an existing register if its cycle set is disjoint from
 *      forbidden_cycles.
 *   4. Otherwise ask the register generator/IDA* search for a new algorithm
 *      whose cycle set is disjoint from forbidden_cycles, then assign that new
 *      register to the variable.
 *
 * The graph answers only "which variables cannot share?" The allocator still
 * owns register generation, cycle-set disjointness checks, and final invariant
 * checking. R0 is not a node because user variables are never allowed to
 * clobber it; its cycle set is always included in forbidden_cycles instead.
 */

typedef uint64_t LiveSet;

typedef struct {
    /*
     * Variables defined by this CFG/liveness node. Most nodes define zero or
     * one variable, but this is a bitset so future lowering can represent
     * multi-def nodes without changing the IG API.
     */
    LiveSet def;

    /*
     * Variables live after this node.
     */
    LiveSet live_out;
} IGLivenessNode;

typedef struct {
    /*
     * Variable represented by this graph node. name is borrowed from TypeEnv
     * and must not be freed by callers.
     */
    int var_index;
    const char *name;

    /*
     * Adjacency bitset keyed by TypeEnv index. If bit j is set, this variable
     * interferes with variable j.
     */
    LiveSet neighbours;

    /* Copied from VarEntry.required_order at graph construction time. */
    int required_order;
} IGNode;

typedef struct {
    /*
     * Nodes for all int variables except R0. The array is owned by this graph.
     * The env pointer is borrowed and must outlive the graph.
     */
    IGNode *nodes;
    int count;
    int capacity;
    TypeEnv *env;
} InterferenceGraph;

/*
 * Build an interference graph from liveness nodes.
 *
 * The graph contains every int variable in env except R0, even if it has no
 * edges. For every node defining x, ig_build adds x--y for every
 * y in live_out \ {x}. This includes dead stores where x itself is not live
 * out: the write still needs a register at that program point.
 *
 * Returns an owned graph; release it with ig_free.
 */
InterferenceGraph *ig_build(TypeEnv *env, const IGLivenessNode *nodes,
                            int node_count);

/*
 * Release a graph returned by ig_build. Safe to call with NULL.
 */
void ig_free(InterferenceGraph *ig);

/*
 * Return the node for var_index, or NULL if var_index is not represented.
 * R0, alg variables, out-of-range indices, and missing variables return NULL.
 */
IGNode *ig_find_node(InterferenceGraph *ig, int var_index);

/*
 * Add an undirected edge between two variable indices.
 *
 * Self-edges, R0, alg variables, and missing nodes are ignored. This makes the
 * helper safe to call directly from bitset walks.
 */
void ig_add_edge(InterferenceGraph *ig, int a, int b);

/*
 * Return true iff a and b currently interfere.
 */
bool ig_interfere(InterferenceGraph *ig, int a, int b);

/*
 * Print a deterministic name-sorted adjacency list.
 *
 * Format:
 *   name: neighbour1 neighbour2
 *
 * Neighbours are also sorted by name. Isolated nodes print as "name:".
 */
void ig_dump(InterferenceGraph *ig, FILE *out);

#endif
