#include "mlrt.h"
#include "logger.h"

#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

static int add_epoll_fd(int epfd, int fd, uint32_t tag) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.u32 = tag;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static void event_usage(FILE *out) {
    fprintf(out,
        "usage: mlrt event-demo [--ticks N] [--period-ms N]\n"
        "Demonstrates an embedded-Linux style epoll loop over timerfd, eventfd and signalfd.\n");
}

int event_demo_cli_main(int argc, char **argv) {
    int ticks_target = 10;
    int period_ms = 100;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) ticks_target = atoi(argv[++i]);
        else if (strcmp(argv[i], "--period-ms") == 0 && i + 1 < argc) period_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) { event_usage(stdout); return 0; }
        else { event_usage(stderr); return 2; }
    }
    if (ticks_target < 1 || ticks_target > 10000 || period_ms < 1 || period_ms > 10000) {
        fprintf(stderr, "mlrt event-demo: invalid timing argument\n");
        return 2;
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &mask, &oldmask) != 0) {
        fprintf(stderr, "mlrt event-demo: pthread_sigmask failed\n");
        return 1;
    }

    int epfd = -1, tfd = -1, efd = -1, sfd = -1;
    int rc = 1;
    epfd = epoll_create1(EPOLL_CLOEXEC);
    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (epfd < 0 || tfd < 0 || efd < 0 || sfd < 0) {
        perror("mlrt event-demo");
        goto out;
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = period_ms / 1000;
    its.it_value.tv_nsec = (long)(period_ms % 1000) * 1000000L;
    its.it_interval = its.it_value;
    if (timerfd_settime(tfd, 0, &its, NULL) != 0) {
        perror("timerfd_settime");
        goto out;
    }

    enum { TAG_TIMER = 1, TAG_EVENT = 2, TAG_SIGNAL = 3 };
    if (add_epoll_fd(epfd, tfd, TAG_TIMER) != 0 || add_epoll_fd(epfd, efd, TAG_EVENT) != 0 || add_epoll_fd(epfd, sfd, TAG_SIGNAL) != 0) {
        perror("epoll_ctl");
        goto out;
    }

    printf("Embedded Event Loop Demo\n");
    printf("  backend: epoll\n  timer: timerfd (%d ms)\n  worker notifications: eventfd\n  signals: signalfd\n", period_ms);

    int ticks = 0;
    int worker_events = 0;
    int stop = 0;
    while (!stop && ticks < ticks_target) {
        struct epoll_event events[4];
        int n = epoll_wait(epfd, events, 4, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            goto out;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.u32 == TAG_TIMER) {
                uint64_t expirations = 0;
                if (read(tfd, &expirations, sizeof(expirations)) == (ssize_t)sizeof(expirations)) {
                    ticks += (int)expirations;
                    if (ticks >= ticks_target / 2 && worker_events == 0) {
                        uint64_t one = 1;
                        (void)write(efd, &one, sizeof(one));
                    }
                }
            } else if (events[i].data.u32 == TAG_EVENT) {
                uint64_t value = 0;
                if (read(efd, &value, sizeof(value)) == (ssize_t)sizeof(value)) worker_events += (int)value;
            } else if (events[i].data.u32 == TAG_SIGNAL) {
                struct signalfd_siginfo info;
                if (read(sfd, &info, sizeof(info)) == (ssize_t)sizeof(info)) {
                    mlrt_log(MLRT_LOG_INFO, "EVENT", "received signal %u through signalfd", info.ssi_signo);
                    stop = 1;
                }
            }
        }
    }

    printf("  timer_ticks: %d\n  eventfd_notifications: %d\n  result: PASS\n", ticks, worker_events);
    rc = 0;
out:
    if (sfd >= 0) close(sfd);
    if (efd >= 0) close(efd);
    if (tfd >= 0) close(tfd);
    if (epfd >= 0) close(epfd);
    (void)pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
    return rc;
}
