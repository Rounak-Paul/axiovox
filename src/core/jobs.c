#include "jobs.h"
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION Mutex;
typedef CONDITION_VARIABLE Condition;
typedef HANDLE Thread;
#define LOCK(m) EnterCriticalSection(m)
#define UNLOCK(m) LeaveCriticalSection(m)
#define WAIT(c,m) SleepConditionVariableCS(c,m,INFINITE)
#define WAKE(c) WakeAllConditionVariable(c)
#else
#include <pthread.h>
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;
typedef pthread_t Thread;
#define LOCK(m) pthread_mutex_lock(m)
#define UNLOCK(m) pthread_mutex_unlock(m)
#define WAIT(c,m) pthread_cond_wait(c,m)
#define WAKE(c) pthread_cond_broadcast(c)
#endif
struct AxJobs {
    Mutex mutex;
    Condition ready, idle;
    Thread threads[64];
    struct { AxJobFn fn; void *user; } queue[256];
    size_t workers, head, count, active;
    bool stopping;
};
/** Runs queued jobs outside the queue lock until shutdown has drained the queue. */
#ifdef _WIN32
static DWORD WINAPI worker(void *arg)
#else
static void *worker(void *arg)
#endif
{
    AxJobs *jobs = arg;
    LOCK(&jobs->mutex);
    for (;;) {
        while (!jobs->count && !jobs->stopping) WAIT(&jobs->ready, &jobs->mutex);
        if (!jobs->count && jobs->stopping) break;
        AxJobFn fn = jobs->queue[jobs->head].fn;
        void *user = jobs->queue[jobs->head].user;
        jobs->head = (jobs->head + 1) % 256;
        --jobs->count;
        ++jobs->active;
        UNLOCK(&jobs->mutex);
        fn(user);
        LOCK(&jobs->mutex);
        --jobs->active;
        if (!jobs->count && !jobs->active) WAKE(&jobs->idle);
    }
    UNLOCK(&jobs->mutex);
    return 0;
}
AxJobs *ax_jobs_create(size_t workers)
{
    if (!workers || workers > 64) return NULL;
    AxJobs *jobs = calloc(1, sizeof(*jobs));
    if (!jobs) return NULL;
#ifdef _WIN32
    InitializeCriticalSection(&jobs->mutex);
    InitializeConditionVariable(&jobs->ready);
    InitializeConditionVariable(&jobs->idle);
#else
    if (pthread_mutex_init(&jobs->mutex, NULL)) { free(jobs); return NULL; }
    if (pthread_cond_init(&jobs->ready, NULL)) { pthread_mutex_destroy(&jobs->mutex); free(jobs); return NULL; }
    if (pthread_cond_init(&jobs->idle, NULL)) { pthread_cond_destroy(&jobs->ready); pthread_mutex_destroy(&jobs->mutex); free(jobs); return NULL; }
#endif
    for (size_t i = 0; i < workers; ++i) {
#ifdef _WIN32
        jobs->threads[i] = CreateThread(NULL, 0, worker, jobs, 0, NULL);
        bool failed = jobs->threads[i] == NULL;
#else
        bool failed = pthread_create(&jobs->threads[i], NULL, worker, jobs) != 0;
#endif
        if (failed) { ax_jobs_destroy(jobs); return NULL; }
        ++jobs->workers;
    }
    return jobs;
}
bool ax_jobs_submit(AxJobs *jobs, AxJobFn fn, void *user)
{
    if (!jobs || !fn) return false;
    LOCK(&jobs->mutex);
    bool accepted = !jobs->stopping && jobs->count < 256;
    if (accepted) {
        size_t tail = (jobs->head + jobs->count++) % 256;
        jobs->queue[tail].fn = fn;
        jobs->queue[tail].user = user;
        WAKE(&jobs->ready);
    }
    UNLOCK(&jobs->mutex);
    return accepted;
}
void ax_jobs_wait(AxJobs *jobs)
{
    if (!jobs) return;
    LOCK(&jobs->mutex);
    while (jobs->count || jobs->active) WAIT(&jobs->idle, &jobs->mutex);
    UNLOCK(&jobs->mutex);
}
void ax_jobs_destroy(AxJobs *jobs)
{
    if (!jobs) return;
    LOCK(&jobs->mutex);
    jobs->stopping = true;
    WAKE(&jobs->ready);
    UNLOCK(&jobs->mutex);
    for (size_t i = 0; i < jobs->workers; ++i) {
#ifdef _WIN32
        WaitForSingleObject(jobs->threads[i], INFINITE);
        CloseHandle(jobs->threads[i]);
#else
        pthread_join(jobs->threads[i], NULL);
#endif
    }
#ifdef _WIN32
    DeleteCriticalSection(&jobs->mutex);
#else
    pthread_cond_destroy(&jobs->idle);
    pthread_cond_destroy(&jobs->ready);
    pthread_mutex_destroy(&jobs->mutex);
#endif
    free(jobs);
}
