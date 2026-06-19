#include "../include/preprocessor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Preprocesses the file then saves the result to {filename}-pp.cbyte
void preprocess(const char *filename) {
    const unsigned int filename_length = strlen(filename);
    char *input_path = calloc(filename_length + 20, sizeof(char));
    char *output_path = calloc(filename_length + 20, sizeof(char));

    if (input_path == NULL || output_path == NULL) {
        perror("Malloc failed");
        exit(EXIT_FAILURE);
    }

    strcpy(input_path, filename);
    strcpy(output_path, filename);

    strcat(input_path, ".cbyte");
    strcat(output_path, "-pp.cbyte");

    FILE *input = fopen(input_path, "r");
    FILE *output = fopen(output_path, "w");

    if (input == NULL || output == NULL) {
        free(input_path);
        free(output_path);
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    // Malloc here because the size is rather large
    char *line = malloc(MAX_LINE_LENGTH);

    if (line == NULL) {
        free(input_path);
        free(output_path);
        fclose(input);
        fclose(output);
        perror("Malloc failed");
        exit(EXIT_FAILURE);
    }

    // Go through line by line to remove comments
    while (fgets(line, MAX_LINE_LENGTH, input) != NULL) {
        char *comment_start = line;

        while (*comment_start != '\0') {
            if (*comment_start == '/' && comment_start[1] == '/') {
                *comment_start = '\0';
                break;
            }

            comment_start++;
        }

        fputs(line, output);
    }

    // Clean up our resources
    free(output_path);
    free(input_path);
    free(line);

    fclose(input);
    fclose(output);
}
