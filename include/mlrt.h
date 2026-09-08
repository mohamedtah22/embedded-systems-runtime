#ifndef MLRT_H
#define MLRT_H

int shell_cli_main(int argc, char **argv);
int elf_cli_main(int argc, char **argv);
int loader_cli_main(int argc, char **argv);
int tracer_cli_main(int argc, char **argv);
int allocator_cli_main(int argc, char **argv);
int network_server_cli_main(int argc, char **argv);
int network_client_cli_main(int argc, char **argv);

#endif
