#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct AxJobs AxJobs;
typedef void (*AxJobFn)(void *user);
/** Creates a bounded worker pool; returns NULL on allocation or thread failure. */
AxJobs *ax_jobs_create(size_t workers);
/** Submits without blocking; returns false if full or stopping. User data must live until completion. */
bool ax_jobs_submit(AxJobs *jobs, AxJobFn fn, void *user);
/** Waits for all accepted work. Call from the owner thread, never from a worker. */
void ax_jobs_wait(AxJobs *jobs);
/** Drains accepted work and joins workers. Owner must first stop all producers. Accepts NULL. */
void ax_jobs_destroy(AxJobs *jobs);

typedef struct AxJobsChange AxJobsChange;
/** Reads the committed worker count under the queue lock; accepts NULL. */
size_t ax_jobs_worker_count(AxJobs *jobs);
/** Resizes from the owner thread while preserving the pool handle and accepted jobs. Never call from workers. */
bool ax_jobs_resize(AxJobs *jobs, size_t workers);
/** Stages a resize with new threads on standby; submissions remain valid. Only one owner-thread change may be pending. */
bool ax_jobs_prepare_resize(AxJobs *jobs, size_t workers, AxJobsChange **change);
/** Commits or cancels a staged resize; retiring workers finish their current jobs. Complete before destroying the pool. */
void ax_jobs_finish_resize(AxJobsChange *change, bool commit);
