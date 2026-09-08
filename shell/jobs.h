#ifndef MLRT_SHELL_JOBS_H
#define MLRT_SHELL_JOBS_H

#include <stddef.h>
#include <sys/types.h>

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct Job {
    int id;
    pid_t pgid;
    pid_t *pids;
    size_t pid_count;
    size_t remaining;
    JobState state;
    char *command;
    struct Job *next;
} Job;

typedef struct {
    Job *head;
    int next_id;
} JobList;

void jobs_init(JobList *list);
Job *jobs_add(JobList *list, pid_t pgid, const pid_t *pids, size_t count, const char *command, JobState state);
Job *jobs_find_id(JobList *list, int id);
Job *jobs_find_pid(JobList *list, pid_t pid);
void jobs_update_status(JobList *list, pid_t pid, int wait_status);
void jobs_reap(JobList *list, int notify_fd);
void jobs_print(JobList *list, int fd);
void jobs_remove_done(JobList *list);
void jobs_remove(JobList *list, Job *target);
void jobs_destroy(JobList *list);
const char *job_state_name(JobState state);

#endif
