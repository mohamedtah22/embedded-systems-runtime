#ifndef MLRT_H
#define MLRT_H

int shell_cli_main(int argc, char **argv);
int elf_cli_main(int argc, char **argv);
int loader_cli_main(int argc, char **argv);
int tracer_cli_main(int argc, char **argv);
int allocator_cli_main(int argc, char **argv);
int network_server_cli_main(int argc, char **argv);
int network_client_cli_main(int argc, char **argv);

int rt_demo_cli_main(int argc, char **argv);
int event_demo_cli_main(int argc, char **argv);
int serial_demo_cli_main(int argc, char **argv);
int target_server_cli_main(int argc, char **argv);
int target_client_cli_main(int argc, char **argv);
int fw_cli_main(int argc, char **argv);
int benchmark_cli_main(int argc, char **argv);

#endif
