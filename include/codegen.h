#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>

#include "program_ast.h"
#include "regalloc.h"
#include "typechecker.h"

typedef struct CodeGen CodeGen;

void codegen_program(FILE *out, ProgramAST *program, TypeEnv *type_env, 
                        RegTable *regs, const int *colouring);

#endif
