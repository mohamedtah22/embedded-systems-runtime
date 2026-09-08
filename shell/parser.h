#ifndef MLRT_SHELL_PARSER_H
#define MLRT_SHELL_PARSER_H

#include <stddef.h>

typedef struct {
    char **argv;
    size_t argc;
    char *input_path;
    char *output_path;
    int append_output;
} ShellCommand;

typedef struct {
    ShellCommand *commands;
    size_t count;
    int background;
    char *source;
} ShellPipeline;

int shell_parse_line(const char *line, ShellPipeline *pipeline, char *err, size_t err_size);
void shell_pipeline_free(ShellPipeline *pipeline);

#endif
