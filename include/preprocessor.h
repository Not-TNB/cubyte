#ifndef ARMV8_37_PREPROCESSOR_H
#define ARMV8_37_PREPROCESSOR_H

#include "../include/lexer.h"

#define MAX_LINE_LENGTH (MAX_TOKEN_LENGTH * 1024)

// Preprocesses the file then saves the result to {base}-pp.cbyte
// where {base} is `filename` with a trailing ".cbyte" extension stripped.
void preprocess(const char *filename);

#endif //ARMV8_37_PREPROCESSOR_H
