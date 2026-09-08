#include "jobs.h"
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void jobs_init(JobList *list) {
    memset(list, 0, sizeof(*list));
    list->next_id = 1;
}

const char *job_state_name(JobState state) {
    switch (state) {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE: return "Done";
        default: return "Unknown";
    }
}

Job *jobs_add(JobList *list, pid_t pgid, const pid_t *pids, size_t count, const char *command, JobState state) {
    Job *job = mlrt_xcalloc(1, sizeof(*job));
    job->id = list->next_id++;
    job->pgid = pgid;
    job->pid_count = count;
    job->remaining = count;
    job->state = state;
    job->command = mlrt_xstrdup(command ? command : "");
    if (count) {
        job->pids = mlrt_xmalloc(count * sizeof(*job->pids));
        memcpy(job->pids, pids, count * sizeof(*job->pids));
    }
    job->next = list->head;
    list->head = job;
    return job;
}

Job *jobs_find_id(JobList *list, int id) {
    for (Job *job = list->head; job; job = job->next) if (job->id == id) return job;
    return NULL;
}

Job *jobs_find_pid(JobList *list, pid_t pid) {
    for (Job *job = list->head; job; job = job->next) {
        for (size_t i = 0; i < job->pid_count; ++i) if (job->pids[i] == pid) return job;
    }
    return NULL;
}

void jobs_update_status(JobList *list, pid_t pid, int wait_status) {
    Job *job = jobs_find_pid(list, pid);
    if (!job) return;
    if (WIFSTOPPED(wait_status)) job->state = JOB_STOPPED;
    else if (WIFCONTINUED(wait_status)) job->state = JOB_RUNNING;
    else if (WIFEXITED(wait_status) || WIFSIGNALED(wait_status)) {
        if (job->remaining > 0) job->remaining--;
        if (job->remaining == 0) job->state = JOB_DONE;
    }
}

void jobs_reap(JobList *list, int notify_fd) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *job = jobs_find_pid(list, pid);
        JobState before = job ? job->state : JOB_DONE;
        jobs_update_status(list, pid, status);
        if (job && notify_fd >= 0 && job->state != before) dprintf(notify_fd, "[%d] %-8s %s\n", job->id, job_state_name(job->state), job->command);
    }
    if (pid < 0 && errno != ECHILD) perror("waitpid");
}

void jobs_print(JobList *list, int fd) {
    jobs_reap(list, -1);
    for (Job *job = list->head; job; job = job->next) dprintf(fd, "[%d] %-8s pgid=%d  %s\n", job->id, job_state_name(job->state), (int)job->pgid, job->command);
    jobs_remove_done(list);
}

void jobs_remove(JobList *list, Job *target) {
    if (!target) return;
    Job **cursor = &list->head;
    while (*cursor) {
        if (*cursor == target) {
            *cursor = target->next;
            free(target->pids);
            free(target->command);
            free(target);
            return;
        }
        cursor = &(*cursor)->next;
    }
}

void jobs_remove_done(JobList *list) {
    Job *job = list->head;
    while (job) {
        Job *next = job->next;
        if (job->state == JOB_DONE) jobs_remove(list, job);
        job = next;
    }
}

void jobs_destroy(JobList *list) {
    Job *job = list->head;
    while (job) {
        Job *next = job->next;
        free(job->pids);
        free(job->command);
        free(job);
        job = next;
    }
    memset(list, 0, sizeof(*list));
}
