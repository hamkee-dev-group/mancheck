/* Test: pthread calls with unchecked returns */
#include <pthread.h>

void *worker(void *arg) { return arg; }

void test_pthread(void) {
    pthread_t t;
    pthread_mutex_t m;
    pthread_cond_t c;

    pthread_create(&t, NULL, worker, NULL);     /* unchecked */
    pthread_join(t, NULL);                      /* unchecked */
    pthread_mutex_init(&m, NULL);               /* unchecked */
    pthread_mutex_lock(&m);                     /* unchecked */
    pthread_mutex_unlock(&m);                   /* unchecked */
    pthread_cond_init(&c, NULL);                /* unchecked */
    pthread_cond_wait(&c, &m);                  /* unchecked */
    pthread_cond_signal(&c);                    /* unchecked */
}
