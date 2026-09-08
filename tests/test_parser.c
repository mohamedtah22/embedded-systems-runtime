#include "parser.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

int main(void) {
    ShellPipeline p;
    char err[256];
    CHECK(shell_parse_line("cat < input.txt | grep hello | wc -l >> out.txt &", &p, err, sizeof(err)) == 0, err);
    CHECK(p.count == 3, "expected three pipeline stages");
    CHECK(p.background == 1, "expected background pipeline");
    CHECK(strcmp(p.commands[0].argv[0], "cat") == 0, "stage 1 command");
    CHECK(strcmp(p.commands[0].input_path, "input.txt") == 0, "input redirection");
    CHECK(strcmp(p.commands[1].argv[0], "grep") == 0, "stage 2 command");
    CHECK(strcmp(p.commands[1].argv[1], "hello") == 0, "stage 2 arg");
    CHECK(strcmp(p.commands[2].output_path, "out.txt") == 0, "output redirection");
    CHECK(p.commands[2].append_output == 1, "append redirection");
    shell_pipeline_free(&p);

    CHECK(shell_parse_line("printf 'hello world' | wc -c", &p, err, sizeof(err)) == 0, err);
    CHECK(strcmp(p.commands[0].argv[1], "hello world") == 0, "quoted word");
    shell_pipeline_free(&p);

    CHECK(shell_parse_line("echo x | | wc", &p, err, sizeof(err)) != 0, "invalid empty stage must fail");
    printf("test_parser: PASS\n");
    return 0;
}
