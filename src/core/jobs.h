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
