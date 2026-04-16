/*
 * Showcase: POSIX-deprecated and obsolescent interfaces.
 *
 * POSIX man pages (3posix) explicitly mark interfaces as obsolescent
 * and name their replacements. By cross-referencing section 3 vs 3posix,
 * mancheck can flag these with the correct modern replacement.
 * Neither cppcheck nor clang-tidy knows the POSIX deprecation status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

/* Case 1: usleep is obsolescent in POSIX.1-2001, removed in POSIX.1-2008.
 * Replacement: nanosleep(2). */
void use_usleep(void) {
    usleep(500000);         /* POSIX obsolescent: use nanosleep */
}

/* Case 2: tmpnam is dangerous and obsolescent.
 * tmpnam(3posix): "Applications should use tmpfile(3) instead." */
void use_tmpnam(void) {
    char buf[L_tmpnam];
    tmpnam(buf);            /* POSIX obsolescent + unsafe: use mkstemp */
}

/* Case 3: signal() has unspecified behavior for multiple signals in POSIX.
 * sigaction(2) should be used instead.
 * signal(3posix): "the sigaction() function provides a more comprehensive
 * and reliable mechanism". */
void use_signal(void) {
    signal(SIGINT, SIG_IGN);  /* POSIX: use sigaction instead */
}

/* Case 4: asctime is not thread-safe and has fixed-size buffer.
 * asctime(3posix): deprecated in favor of strftime. */
void use_asctime(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char *s = asctime(tm);  /* deprecated: use strftime */
    puts(s);
}

/* Case 5: gets was removed from C11 and POSIX.1-2008.
 * Already flagged as dangerous, but POSIX confirms removal. */
void use_gets(void) {
    char buf[128];
    gets(buf);              /* removed from POSIX and C11: use fgets */
}

/* Case 6: rand/srand — POSIX notes they are not suitable for
 * cryptographic or high-quality randomness.
 * rand(3posix): "use random() for better quality". */
void use_rand(void) {
    srand(42);
    int r = rand();         /* POSIX: consider random() or arc4random() */
    (void)r;
}

/* Case 7: sprintf — no buffer overflow protection.
 * POSIX does not mark it obsolescent but notes "use snprintf". */
void use_sprintf(char *dst, const char *name) {
    sprintf(dst, "hello %s", name);  /* use snprintf for safety */
}

/* Case 8: ftime was removed in POSIX.1-2008.
 * Replacement: clock_gettime(2). */
#include <sys/timeb.h>
void use_ftime(void) {
    struct timeb t;
    ftime(&t);              /* removed from POSIX: use clock_gettime */
}
