#include "app/runtime.h"
#include "ui/ui.h"
#include "renderer/memory.h"
#include <GLFW/glfw3.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); goto cleanup; } } while (0)
/** Counts observed config changes or successful live device selections. */
static void count_event(const AxEvent *event, void *user) { (void)event; ++*(int *)user; }
/** Completes accepted background work exactly once. */
static void job(void *user) { atomic_fetch_add((atomic_int *)user, 1); }
typedef struct { AxBuffer buffer; VkDevice device; } NodeDevice;
typedef struct { AxRenderer *renderer; int created, destroyed; bool fail_prepare, valid; } NodeOwner;
/** Allocates a real buffer for each candidate device, with a controllable post-allocation failure. */
static bool create_node(AxRenderer *renderer, void *user, void **out)
{
    NodeOwner *owner = user;
    NodeDevice *state = calloc(1, sizeof(*state)); *out = state;
    if (!state) return false;
    ++owner->created; state->device = renderer->device;
    return ax_buffer_create(renderer, &state->buffer, 64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true) && !owner->fail_prepare;
}
/** Checks destruction uses the state-owning device, including partial candidates and rollback. */
static void destroy_node(AxRenderer *renderer, void *user, void *data)
{
    NodeOwner *owner = user; NodeDevice *state = data;
    if (!state) return;
    if (state->device != renderer->device) owner->valid = false;
    ax_buffer_destroy(renderer, &state->buffer); free(state); ++owner->destroyed;
}
/** Rejects stale device-local state after any committed or rolled-back migration. */
static bool record_node(const AxRenderFrame *frame, void *user)
{
    NodeOwner *owner = user; NodeDevice *state = frame->device_state;
    return state && state->device == owner->renderer->device && state->buffer.mapped;
}
/** Exercises real UI resource migration, transactional rollback, and worker resizing without restarting. */
int main(void)
{
    AxEvents events = {0}; AxConfig config; ax_config_init(&config, &events);
    AxWindow window = {0}; AxRenderer renderer = {0}; AxUi ui = {0};
    AxInput input = {.events = &events}; AxCommands commands = {0}; AxConsole console = {0};
    AxJobs *jobs = NULL; AxRuntime runtime;
    int status = EXIT_FAILURE, selections = 0;
    NodeOwner owner = {.renderer = &renderer, .valid = true};
    CHECK(ax_console_init(&console, &commands, &input, &events));
    CHECK(ax_window_create(&window, &events, 800, 600));
    ax_window_set_input(&window, &input);
    CHECK(ax_renderer_create(&renderer, &window, &events, -1));
    CHECK(ax_ui_create(&ui, &renderer, &console));
    AxRenderNode node = {.name = "device-resource", .record = record_node, .user = &owner, .enabled = true,
        .create_device = create_node, .destroy_device = destroy_node};
    CHECK(ax_renderer_register_node(&renderer, &node));
    ax_ui_bind_settings(&ui, &config, "runtime-test.cfg");
    jobs = ax_jobs_create(4); CHECK(jobs);
    CHECK(ax_runtime_bind(&runtime, &config, &renderer, jobs));
    CHECK(ax_event_subscribe(&events, AX_EVENT_DEVICE_SELECTED, count_event, &selections));
    int config_changes = 0;
    uint64_t config_listener = ax_event_subscribe(&events, AX_EVENT_CONFIG_CHANGED, count_event, &config_changes);
    CHECK(config_listener);
    CHECK(ax_window_sync_config(&window, &config));
    CHECK(ax_config_get(&config, "window.width")->value == 800);
    CHECK(ax_config_get(&config, "window.height")->value == 600);
    CHECK(config_changes == 2);
    CHECK(ax_window_sync_config(&window, &config) && config_changes == 2);
    ui.hud.paused = true;
    CHECK(ax_menu_open_settings(&ui.hud.menu));
    CHECK(ax_config_set(&ui.hud.menu.draft, "ui.scale", "3"));
    glfwSetWindowSize(window.handle, 937, 613);
    glfwPollEvents();
    CHECK(ax_window_sync_config(&window, &config));
    int logical_width, logical_height;
    glfwGetWindowSize(window.handle, &logical_width, &logical_height);
    CHECK(ax_config_get(&config, "window.width")->value == logical_width);
    CHECK(ax_config_get(&config, "window.height")->value == logical_height);
    ax_ui_update(&ui, glfwGetTime());
    CHECK(ax_config_get(&ui.hud.menu.draft, "window.width")->value == logical_width);
    CHECK(ax_config_get(&ui.hud.menu.draft, "window.height")->value == logical_height);
    CHECK(ax_config_get(&ui.hud.menu.draft, "ui.scale")->value == 3);
    CHECK(config.active_values[0] == logical_width && config.active_values[1] == logical_height);
    int notifications = config_changes;
    CHECK(ax_window_sync_config(&window, &config) && config_changes == notifications);
    ax_event_unsubscribe(&events, config_listener);

    ui.hud.active_row = 2; ui.hud.selected_slots[2] = 7; ui.hud.paused = true;
    CHECK(ax_menu_open_settings(&ui.hud.menu));
    CHECK(ax_config_set(&ui.hud.menu.draft, "ui.scale", "3"));
    strcpy(console.history[0], "/preserved.command"); console.history_count = 1;
    atomic_int completed = 0;
    for (int i = 0; i < 200; ++i) CHECK(ax_jobs_submit(jobs, job, &completed));
    CHECK(ax_config_set(&config, "jobs.workers", "2"));
    CHECK(ax_jobs_worker_count(jobs) == 2);
    VkDevice previous = renderer.device;
    CHECK(ax_config_set(&config, "renderer.device", "0"));
    CHECK(renderer.device != previous && selections == 1 && runtime.healthy);
    CHECK(ui.hud.active_row == 2 && ui.hud.selected_slots[2] == 7 && ui.hud.paused);
    CHECK(ui.hud.menu.page == AX_MENU_SETTINGS && ax_config_get(&ui.hud.menu.draft, "ui.scale")->value == 3);
    CHECK(console.history_count == 1 && !strcmp(console.history[0], "/preserved.command"));
    glfwPollEvents(); ax_ui_update(&ui, glfwGetTime()); CHECK(ax_renderer_draw(&renderer));
    previous = renderer.device;
    void *previous_ui = renderer.ui_node.device_state;
    owner.fail_prepare = true;
    CHECK(!ax_config_set(&config, "renderer.device", "-1"));
    CHECK(renderer.device == previous && renderer.ui_node.device_state == previous_ui && selections == 1);
    CHECK(runtime.healthy && owner.valid && ax_renderer_draw(&renderer));
    owner.fail_prepare = false;
    AxConfig draft; ax_config_stage(&draft, &config);
    CHECK(ax_config_set(&draft, "renderer.device", "-1"));
    CHECK(ax_config_set(&draft, "jobs.workers", "6"));
    CHECK(!ax_config_apply_saved(&config, &draft, ""));
    CHECK(renderer.device == previous && renderer.ui_node.device_state == previous_ui);
    CHECK(selections == 1 && runtime.healthy && !renderer.changing);
    CHECK(ax_jobs_worker_count(jobs) == 2 && ax_config_get(&config, "renderer.device")->value == 0);
    CHECK(ax_renderer_draw(&renderer));
    ax_config_stage(&draft, &config);
    CHECK(ax_config_set(&draft, "renderer.device", "65535"));
    CHECK(ax_config_set(&draft, "jobs.workers", "8"));
    CHECK(!ax_config_apply(&config, &draft));
    CHECK(renderer.device == previous && renderer.ui_node.device_state == previous_ui);
    CHECK(ax_jobs_worker_count(jobs) == 2 && runtime.healthy && selections == 1);
    CHECK(ax_renderer_draw(&renderer));
    ax_config_stage(&draft, &config);
    CHECK(ax_config_set(&draft, "renderer.device", "-1"));
    CHECK(ax_config_set(&draft, "jobs.workers", "6"));
    CHECK(ax_config_apply_saved(&config, &draft, "runtime-test.cfg"));
    CHECK(renderer.device != previous && selections == 2 && ax_jobs_worker_count(jobs) == 6);
    CHECK(ui.hud.active_row == 2 && ui.hud.selected_slots[2] == 7 && ui.hud.menu.page == AX_MENU_SETTINGS);
    AxConfig loaded; ax_config_init(&loaded, NULL);
    CHECK(ax_config_load(&loaded, "runtime-test.cfg"));
    CHECK(ax_config_get(&loaded, "jobs.workers")->value == 6 && ax_config_get(&loaded, "renderer.device")->value == -1);
    ax_jobs_wait(jobs); CHECK(atomic_load(&completed) == 200);
    for (int i = 0; i < 8; ++i) {
        glfwPollEvents(); ax_ui_update(&ui, glfwGetTime()); CHECK(ax_renderer_draw(&renderer));
    }
    CHECK(remove("runtime-test.cfg") == 0);
    status = EXIT_SUCCESS;
cleanup:
    ax_jobs_destroy(jobs);
    if (renderer.device) vkDeviceWaitIdle(renderer.device);
    ax_ui_destroy(&ui); ax_renderer_destroy(&renderer);
    ax_console_destroy(&console); ax_window_destroy(&window);
    if (!owner.valid || owner.created != owner.destroyed) { fprintf(stderr, "Node device lifetime mismatch\n"); status = EXIT_FAILURE; }
    return status;
}
