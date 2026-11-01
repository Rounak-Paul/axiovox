#pragma once
#include "core/config.h"
#include "core/jobs.h"
#include "renderer/renderer.h"
typedef struct { AxRenderer *renderer; AxJobs *jobs; bool healthy; } AxRuntime;
/** Binds staged live service changes to config transactions. Services and runtime must outlive the configuration binding. */
bool ax_runtime_bind(AxRuntime *runtime, AxConfig *config, AxRenderer *renderer, AxJobs *jobs);
