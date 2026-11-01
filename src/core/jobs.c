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
typedef struct { AxJobs *jobs; Thread thread; bool retiring, standby; } Worker;
struct AxJobs {
    Mutex mutex;
    Condition ready, idle;
    Worker threads[64];
    struct { AxJobFn fn; void *user; } queue[256];
    size_t workers, head, count, active;
    bool stopping, resizing;
};
struct AxJobsChange { AxJobs *jobs; size_t previous, target, created; };
/** Runs jobs outside the queue lock; retiring workers finish their current job first. */
#ifdef _WIN32
static DWORD WINAPI worker(void *arg)
#else
static void *worker(void *arg)
#endif
{
    Worker *slot = arg;
    AxJobs *jobs = slot->jobs;
    LOCK(&jobs->mutex);
    for (;;) {
        while (!slot->retiring && !jobs->stopping && (slot->standby || !jobs->count)) WAIT(&jobs->ready, &jobs->mutex);
        if (slot->retiring || (!jobs->count && jobs->stopping)) break;
        AxJobFn fn = jobs->queue[jobs->head].fn;
        void *user = jobs->queue[jobs->head].user;
        jobs->head = (jobs->head + 1) % 256;
        --jobs->count; ++jobs->active;
        UNLOCK(&jobs->mutex);
        fn(user);
        LOCK(&jobs->mutex);
        --jobs->active;
        if (!jobs->count && !jobs->active) WAKE(&jobs->idle);
    }
    UNLOCK(&jobs->mutex);
    return 0;
}
/** Starts a stable worker slot; caller initializes its flags before publishing it. */
static bool start_worker(Worker *slot)
{
#ifdef _WIN32
    slot->thread = CreateThread(NULL, 0, worker, slot, 0, NULL);
    return slot->thread != NULL;
#else
    return pthread_create(&slot->thread, NULL, worker, slot) == 0;
#endif
}
/** Joins a worker previously signaled to retire or drain on shutdown. */
static void join_worker(Worker *slot)
{
#ifdef _WIN32
    WaitForSingleObject(slot->thread, INFINITE);
    CloseHandle(slot->thread);
#else
    pthread_join(slot->thread, NULL);
#endif
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
        jobs->threads[i] = (Worker){.jobs = jobs};
        if (!start_worker(&jobs->threads[i])) { ax_jobs_destroy(jobs); return NULL; }
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
        jobs->queue[tail].fn = fn; jobs->queue[tail].user = user;
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
size_t ax_jobs_worker_count(AxJobs *jobs)
{
    if (!jobs) return 0;
    LOCK(&jobs->mutex); size_t count = jobs->workers; UNLOCK(&jobs->mutex);
    return count;
}
bool ax_jobs_prepare_resize(AxJobs *jobs, size_t workers, AxJobsChange **out)
{
    if (!out) return false;
    *out = NULL;
    if (!jobs || !workers || workers > 64) return false;
    AxJobsChange *change = calloc(1, sizeof(*change));
    if (!change) return false;
    LOCK(&jobs->mutex);
    if (jobs->stopping || jobs->resizing) { UNLOCK(&jobs->mutex); free(change); return false; }
    jobs->resizing = true;
    *change = (AxJobsChange){.jobs = jobs, .previous = jobs->workers, .target = workers};
    UNLOCK(&jobs->mutex);
    for (size_t i = change->previous; i < workers; ++i) {
        jobs->threads[i] = (Worker){.jobs = jobs, .standby = true};
        if (!start_worker(&jobs->threads[i])) { ax_jobs_finish_resize(change, false); return false; }
        ++change->created;
    }
    *out = change;
    return true;
}
void ax_jobs_finish_resize(AxJobsChange *change, bool commit)
{
    if (!change) return;
    AxJobs *jobs = change->jobs;
    size_t first = 0, last = 0;
    LOCK(&jobs->mutex);
    if (commit) {
        if (change->target < change->previous) { first = change->target; last = change->previous; }
        for (size_t i = change->previous; i < change->target; ++i) jobs->threads[i].standby = false;
        jobs->workers = change->target;
    } else { first = change->previous; last = change->previous + change->created; }
    for (size_t i = first; i < last; ++i) jobs->threads[i].retiring = true;
    WAKE(&jobs->ready);
    UNLOCK(&jobs->mutex);
    for (size_t i = first; i < last; ++i) join_worker(&jobs->threads[i]);
    LOCK(&jobs->mutex); jobs->resizing = false; UNLOCK(&jobs->mutex);
    free(change);
}
bool ax_jobs_resize(AxJobs *jobs, size_t workers)
{
    AxJobsChange *change;
    if (!ax_jobs_prepare_resize(jobs, workers, &change)) return false;
    ax_jobs_finish_resize(change, true);
    return true;
}
void ax_jobs_destroy(AxJobs *jobs)
{
    if (!jobs) return;
    LOCK(&jobs->mutex); jobs->stopping = true; WAKE(&jobs->ready); UNLOCK(&jobs->mutex);
    for (size_t i = 0; i < jobs->workers; ++i) join_worker(&jobs->threads[i]);
#ifdef _WIN32
    DeleteCriticalSection(&jobs->mutex);
#else
    pthread_cond_destroy(&jobs->idle); pthread_cond_destroy(&jobs->ready); pthread_mutex_destroy(&jobs->mutex);
#endif
    free(jobs);
}
