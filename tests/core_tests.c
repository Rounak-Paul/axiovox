#include "core/command.h"
#include "core/config.h"
#include "core/jobs.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; } } while (0)
/** Counts dispatched events. */
static void count_event(const AxEvent *event, void *user) { (void)event; ++*(int *)user; }
/** Increments a shared counter from a worker. */
static void increment(void *user) { atomic_fetch_add((atomic_int *)user, 1); }
/** Adapts config writes to the command registry. */
static bool set_command(int argc, const char *const *argv, void *user) { return argc == 3 && ax_config_set(user, argv[1], argv[2]); }
/** Checks config transactions, event lifetimes, command validation, and worker draining. */
int main(void)
{
    AxEvents events = {0};
    AxConfig config;
    ax_config_init(&config, &events);
    int count = 0;
    uint64_t id = ax_event_subscribe(&events, AX_EVENT_CONFIG_CHANGED, count_event, &count);
    CHECK(id);
    CHECK(!ax_config_set(&config, "window.width", "-1"));
    CHECK(!ax_config_set(&config, "window.width", "999999999999999999999"));
    CHECK(!ax_config_set(&config, "unknown", "2"));
    AxCommands commands = {0};
    CHECK(ax_command_register(&commands, "set", set_command, &config));
    CHECK(!ax_command_register(&commands, "set", set_command, &config));
    CHECK(ax_command_execute(&commands, "set window.width 1920"));
    CHECK(count == 1);
    CHECK(ax_command_execute(&commands, "set window.width 1920"));
    CHECK(count == 1);
    CHECK(!ax_command_execute(&commands, "unknown"));
    CHECK(ax_config_set(&config, "renderer.device", "0"));
    CHECK(config.restart_pending);
    CHECK(ax_config_save(&config, "core-test.cfg"));
    CHECK(ax_config_set(&config, "window.width", "800"));
    CHECK(ax_config_load(&config, "core-test.cfg"));
    CHECK(ax_config_get(&config, "window.width")->value == 1920);
    FILE *file = fopen("core-test.cfg", "w");
    CHECK(file);
    CHECK(fputs("window.width=900\nunknown=1\n", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(!ax_config_load(&config, "core-test.cfg"));
    CHECK(ax_config_get(&config, "window.width")->value == 1920);
    CHECK(remove("core-test.cfg") == 0);
    ax_event_unsubscribe(&events, id);
    count = 0;
    CHECK(ax_config_set(&config, "window.width", "800"));
    CHECK(count == 0);
    CHECK(!ax_jobs_create(0));
    AxJobs *jobs = ax_jobs_create(4);
    CHECK(jobs);
    atomic_int completed = 0;
    for (int i = 0; i < 1000; ++i) {
        if (!ax_jobs_submit(jobs, increment, &completed)) { ax_jobs_wait(jobs); --i; }
    }
    ax_jobs_wait(jobs);
    CHECK(atomic_load(&completed) == 1000);
    for (int i = 0; i < 100; ++i) CHECK(ax_jobs_submit(jobs, increment, &completed));
    ax_jobs_destroy(jobs);
    CHECK(atomic_load(&completed) == 1100);
    return EXIT_SUCCESS;
}
