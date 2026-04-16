/*
 * Showcase: async-signal-unsafe functions called from signal handlers.
 *
 * signal-safety(7) documents which functions are safe to call from
 * signal handlers. Calling malloc, printf, fprintf, or fopen inside
 * a handler is undefined behavior. Neither cppcheck nor clang-analyzer
 * checks this.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Case 1: printf in signal handler — async-signal-UNSAFE */
void handler_printf(int sig) {
    printf("caught signal %d\n", sig);      /* BUG: printf is not AS-safe */
}

/* Case 2: malloc in signal handler — async-signal-UNSAFE */
void handler_malloc(int sig) {
    char *p = malloc(64);                   /* BUG: malloc is not AS-safe */
    if (p) {
        snprintf(p, 64, "signal %d", sig);  /* BUG: snprintf not AS-safe */
        free(p);                             /* BUG: free is not AS-safe */
    }
}

/* Case 3: fprintf in signal handler — async-signal-UNSAFE */
void handler_fprintf(int sig) {
    fprintf(stderr, "signal %d\n", sig);    /* BUG: fprintf is not AS-safe */
}

/* Case 4: safe handler — only uses write(2), which IS async-signal-safe */
void handler_safe(int sig) {
    const char msg[] = "signal caught\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);  /* OK: write is AS-safe */
    (void)sig;
}

/* Registration: shows the handlers are actually installed */
int main(void) {
    signal(SIGUSR1, handler_printf);
    signal(SIGUSR2, handler_malloc);
    signal(SIGTERM, handler_fprintf);
    signal(SIGINT, handler_safe);

    /* Keep running to receive signals */
    pause();
    return 0;
}
