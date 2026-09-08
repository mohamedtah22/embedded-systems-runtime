#include "parser.h"
#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_INIT_CAP 16
#define BUF_INIT_CAP 64

typedef enum { TOK_WORD, TOK_PIPE, TOK_IN, TOK_OUT, TOK_APPEND, TOK_BACKGROUND } TokenType;
typedef struct { TokenType type; char *text; } Token;
typedef struct { Token *items; size_t count; size_t cap; } TokenVec;

static void set_error(char *err, size_t err_size, const char *msg) {
    if (err && err_size) snprintf(err, err_size, "%s", msg);
}

static void token_vec_push(TokenVec *vec, TokenType type, const char *text) {
    if (vec->count == vec->cap) {
        size_t new_cap = vec->cap ? vec->cap * 2 : TOKEN_INIT_CAP;
        Token *new_items = realloc(vec->items, new_cap * sizeof(*new_items));
        if (!new_items) mlrt_die("out of memory while tokenizing shell command");
        vec->items = new_items;
        vec->cap = new_cap;
    }
    vec->items[vec->count].type = type;
    vec->items[vec->count].text = text ? mlrt_xstrdup(text) : NULL;
    vec->count++;
}

static void token_vec_free(TokenVec *vec) {
    for (size_t i = 0; i < vec->count; ++i) free(vec->items[i].text);
    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

static int append_char(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 1 >= *cap) {
        size_t new_cap = *cap ? *cap * 2 : BUF_INIT_CAP;
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) return -1;
        *buf = new_buf;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
    return 0;
}

static int tokenize(const char *line, TokenVec *out, char *err, size_t err_size) {
    size_t i = 0;
    while (line[i]) {
        while (isspace((unsigned char)line[i])) ++i;
        if (!line[i]) break;
        if (line[i] == '|') { token_vec_push(out, TOK_PIPE, NULL); ++i; continue; }
        if (line[i] == '<') { token_vec_push(out, TOK_IN, NULL); ++i; continue; }
        if (line[i] == '>') {
            if (line[i + 1] == '>') { token_vec_push(out, TOK_APPEND, NULL); i += 2; }
            else { token_vec_push(out, TOK_OUT, NULL); ++i; }
            continue;
        }
        if (line[i] == '&') { token_vec_push(out, TOK_BACKGROUND, NULL); ++i; continue; }

        char *buf = NULL;
        size_t len = 0, cap = 0;
        int quote = 0;
        while (line[i]) {
            char c = line[i];
            if (!quote && (isspace((unsigned char)c) || c == '|' || c == '<' || c == '>' || c == '&')) break;
            if (!quote && (c == '\'' || c == '"')) { quote = c; ++i; continue; }
            if (quote && c == quote) { quote = 0; ++i; continue; }
            if (c == '\\' && quote != '\'') {
                ++i;
                if (!line[i]) { free(buf); set_error(err, err_size, "trailing escape in command"); return -1; }
                c = line[i];
            }
            if (append_char(&buf, &len, &cap, c) != 0) { free(buf); set_error(err, err_size, "out of memory while parsing command"); return -1; }
            ++i;
        }
        if (quote) { free(buf); set_error(err, err_size, "unterminated quote"); return -1; }
        if (!buf) buf = mlrt_xstrdup("");
        token_vec_push(out, TOK_WORD, buf);
        free(buf);
    }
    return 0;
}

static void command_add_arg(ShellCommand *cmd, const char *arg) {
    char **new_argv = realloc(cmd->argv, (cmd->argc + 2) * sizeof(*new_argv));
    if (!new_argv) mlrt_die("out of memory while building command arguments");
    cmd->argv = new_argv;
    cmd->argv[cmd->argc++] = mlrt_xstrdup(arg);
    cmd->argv[cmd->argc] = NULL;
}

static int pipeline_add_command(ShellPipeline *pipeline) {
    ShellCommand *new_commands = realloc(pipeline->commands, (pipeline->count + 1) * sizeof(*new_commands));
    if (!new_commands) return -1;
    pipeline->commands = new_commands;
    memset(&pipeline->commands[pipeline->count], 0, sizeof(ShellCommand));
    pipeline->count++;
    return 0;
}

int shell_parse_line(const char *line, ShellPipeline *pipeline, char *err, size_t err_size) {
    if (!line || !pipeline) { set_error(err, err_size, "invalid parser arguments"); return -1; }
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->source = mlrt_xstrdup(line);
    TokenVec tokens = {0};
    if (tokenize(line, &tokens, err, err_size) != 0) { shell_pipeline_free(pipeline); token_vec_free(&tokens); return -1; }
    if (tokens.count == 0) { token_vec_free(&tokens); return 0; }
    if (pipeline_add_command(pipeline) != 0) { token_vec_free(&tokens); shell_pipeline_free(pipeline); set_error(err, err_size, "out of memory creating command pipeline"); return -1; }

    ShellCommand *cmd = &pipeline->commands[0];
    for (size_t i = 0; i < tokens.count; ++i) {
        Token *tok = &tokens.items[i];
        if (tok->type == TOK_WORD) { command_add_arg(cmd, tok->text); continue; }
        if (tok->type == TOK_PIPE) {
            if (cmd->argc == 0) { set_error(err, err_size, "empty command before pipe"); goto fail; }
            if (i + 1 == tokens.count) { set_error(err, err_size, "pipe requires a following command"); goto fail; }
            if (pipeline_add_command(pipeline) != 0) { set_error(err, err_size, "out of memory creating pipeline stage"); goto fail; }
            cmd = &pipeline->commands[pipeline->count - 1];
            continue;
        }
        if (tok->type == TOK_BACKGROUND) {
            if (i + 1 != tokens.count) { set_error(err, err_size, "'&' is only valid at the end of a pipeline"); goto fail; }
            pipeline->background = 1;
            continue;
        }
        if (tok->type == TOK_IN || tok->type == TOK_OUT || tok->type == TOK_APPEND) {
            if (i + 1 >= tokens.count || tokens.items[i + 1].type != TOK_WORD) { set_error(err, err_size, "redirection operator requires a file path"); goto fail; }
            const char *path = tokens.items[++i].text;
            if (tok->type == TOK_IN) {
                if (cmd->input_path) { set_error(err, err_size, "multiple input redirections for one command"); goto fail; }
                cmd->input_path = mlrt_xstrdup(path);
            } else {
                if (cmd->output_path) { set_error(err, err_size, "multiple output redirections for one command"); goto fail; }
                cmd->output_path = mlrt_xstrdup(path);
                cmd->append_output = tok->type == TOK_APPEND;
            }
        }
    }
    for (size_t i = 0; i < pipeline->count; ++i) {
        if (pipeline->commands[i].argc == 0) { set_error(err, err_size, "empty command in pipeline"); goto fail; }
        if (i > 0 && pipeline->commands[i].input_path) { set_error(err, err_size, "input redirection is only valid on the first pipeline stage"); goto fail; }
        if (i + 1 < pipeline->count && pipeline->commands[i].output_path) { set_error(err, err_size, "output redirection is only valid on the last pipeline stage"); goto fail; }
    }
    token_vec_free(&tokens);
    return 0;
fail:
    token_vec_free(&tokens);
    shell_pipeline_free(pipeline);
    return -1;
}

void shell_pipeline_free(ShellPipeline *pipeline) {
    if (!pipeline) return;
    for (size_t i = 0; i < pipeline->count; ++i) {
        ShellCommand *cmd = &pipeline->commands[i];
        for (size_t j = 0; j < cmd->argc; ++j) free(cmd->argv[j]);
        free(cmd->argv);
        free(cmd->input_path);
        free(cmd->output_path);
    }
    free(pipeline->commands);
    free(pipeline->source);
    memset(pipeline, 0, sizeof(*pipeline));
}
