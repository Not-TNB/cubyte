#ifndef DESUGARER_H 
#define DESUGARER_H
#include "program_ast.h"
#include "typechecker.h"

/*  precondition: all statements are valid, all types are valid, basically
    the program has no errors. 
    
    postcondition: all operations will be primitive operations as specified. */
extern ProgramAST *desugared_statement_ast(ProgramAST *program_ast, TypeEnv *type_env);

#endif