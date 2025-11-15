#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Enclose thread related functions into wrapper functions in the case of errors

void pthread_create_w(pthread_t *thread, 
                      const pthread_attr_t *attr,
                      void *(*start_routine)(void *), 
                      void *arg) {
    int rc = pthread_create(thread, attr, start_routine, arg);
    if (rc != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_join_w(pthread_t thread, void **retval) {
    int rc = pthread_join(thread, retval);
    if (rc != 0) {
        fprintf(stderr, "pthread_join failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_mutex_init_w(pthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *attr) {
    int rc = pthread_mutex_init(mutex, attr);
    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_init failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_mutex_lock_w(pthread_mutex_t *mutex) {
    int rc = pthread_mutex_lock(mutex);
    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_lock failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_mutex_unlock_w(pthread_mutex_t *mutex) {
    int rc = pthread_mutex_unlock(mutex);
    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_mutex_destroy_w(pthread_mutex_t *mutex) {
    int rc = pthread_mutex_destroy(mutex);
    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_destroy failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

void pthread_cancel_w(pthread_t thread) {
    int rc = pthread_cancel(thread);
    if (rc != 0) {
        fprintf(stderr, "pthread_cancel failed: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}