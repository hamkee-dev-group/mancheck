/*
 * Showcase: MT-Unsafe functions used in threaded code.
 *
 * Linux man pages ATTRIBUTES section marks functions as MT-Safe or
 * MT-Unsafe. Using MT-Unsafe functions in code that creates pthreads
 * is a data race. Neither cppcheck nor clang-tidy flags these.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>

/* Case 1: strtok is MT-Unsafe (uses internal static buffer) */
void *worker_strtok(void *arg) {
    char buf[128];
    strncpy(buf, (char *)arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ",");           /* BUG: MT-Unsafe */
    while (tok) {
        printf("%s\n", tok);
        tok = strtok(NULL, ",");            /* BUG: MT-Unsafe */
    }
    return NULL;
}

/* Case 2: localtime is MT-Unsafe (returns pointer to static struct) */
void *worker_localtime(void *arg) {
    (void)arg;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);        /* BUG: MT-Unsafe, use localtime_r */
    printf("hour: %d\n", tm->tm_hour);
    return NULL;
}

/* Case 3: getpwnam is MT-Unsafe (returns pointer to static struct) */
void *worker_getpwnam(void *arg) {
    (void)arg;
    struct passwd *pw = getpwnam("root");   /* BUG: MT-Unsafe, use getpwnam_r */
    if (pw)
        printf("uid: %d\n", pw->pw_uid);
    return NULL;
}

/* Case 4: getenv is MT-Unsafe in POSIX (another thread can call setenv) */
void *worker_getenv(void *arg) {
    (void)arg;
    char *val = getenv("HOME");             /* BUG: MT-Unsafe if setenv used */
    if (val)
        printf("HOME=%s\n", val);
    return NULL;
}

int main(void) {
    pthread_t t1, t2, t3, t4;
    const char *csv = "a,b,c,d";

    pthread_create(&t1, NULL, worker_strtok, (void *)csv);
    pthread_create(&t2, NULL, worker_localtime, NULL);
    pthread_create(&t3, NULL, worker_getpwnam, NULL);
    pthread_create(&t4, NULL, worker_getenv, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    return 0;
}
