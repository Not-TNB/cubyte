#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>

#include "frontend/program_ast.h"
#include "backend/regalloc.h"
#include "frontend/typechecker.h"

typedef struct CodeGen CodeGen;

void codegen_program(FILE *out, ProgramAST *program, TypeEnv *type_env, 
                        RegTable *regs, const int *colouring);

#endif
