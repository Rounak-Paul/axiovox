#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct { AxRuntime *runtime; AxJobsChange *workers; AxRendererChange *device; } RuntimeChange;
/** Reserves all fallible resources without disrupting the running services. */
static bool prepare(void *user, const AxConfig *current, const AxConfig *next, void **out, char *error, size_t capacity)
{
    AxRuntime *runtime = user;
    int gpu = ax_config_get(next, "renderer.device")->value;
    int workers = ax_config_get(next, "jobs.workers")->value;
    bool change_gpu = gpu != ax_config_get(current, "renderer.device")->value;
    bool change_workers = workers != ax_config_get(current, "jobs.workers")->value;
    *out = NULL;
    if (runtime->renderer->recording || runtime->renderer->changing) {
        snprintf(error, capacity, "Service changes must run between frames."); return false;
    }
    if (runtime->renderer->failed) runtime->healthy = false;
    if (!runtime->healthy) { snprintf(error, capacity, "The runtime is no longer available."); return false; }
    if (!change_gpu && !change_workers) return true;
    RuntimeChange *change = calloc(1, sizeof(*change));
    if (!change) { snprintf(error, capacity, "Not enough memory to change services."); return false; }
    change->runtime = runtime;
    *out = change;
    if (change_workers && !ax_jobs_prepare_resize(runtime->jobs, (size_t)workers, &change->workers)) {
        snprintf(error, capacity, "Could not prepare the requested worker count."); return false;
    }
    if (change_gpu && !ax_renderer_prepare_device(runtime->renderer, gpu, &change->device)) {
        snprintf(error, capacity, "Could not prepare the graphics device. Previous device retained."); return false;
    }
    return true;
}
/** Transfers presentation ownership before saving; old GPU resources remain available for rollback. */
static bool activate(void *token, char *error, size_t capacity)
{
    RuntimeChange *change = token;
    if (!change || !change->device) return true;
    if (ax_renderer_activate_device(change->device)) return true;
    snprintf(error, capacity, "Could not switch graphics devices. Previous device restored.");
    return false;
}
/** Commits standby workers/device resources, or restores the previous services after a failed transaction. */
static bool finish(void *token, bool commit)
{
    RuntimeChange *change = token;
    if (!change) return true;
    if (commit) ax_jobs_finish_resize(change->workers, true);
    bool ok = ax_renderer_finish_device(change->device, commit);
    if (!commit) ax_jobs_finish_resize(change->workers, false);
    change->runtime->healthy = ok && !change->runtime->renderer->failed;
    free(change);
    return ok;
}
bool ax_runtime_bind(AxRuntime *runtime, AxConfig *config, AxRenderer *renderer, AxJobs *jobs)
{
    if (!runtime || !config || !renderer || !renderer->device || !jobs) return false;
    *runtime = (AxRuntime){.renderer = renderer, .jobs = jobs, .healthy = true};
    return ax_config_bind_runtime(config, (AxConfigRuntime){prepare, activate, finish}, runtime);
}
