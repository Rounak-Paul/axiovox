#include "renderer/renderer.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { int sequence, world_calls, effect_calls, ui_calls; } State;
/** Validates the first node's frame context and order. */
static bool world(const AxRenderFrame *frame, void *user)
{
    State *state = user;
    ++state->world_calls;
    return state->sequence++ == 0 && frame->command && frame->extent.width && frame->extent.height;
}
/** Validates the second registered node follows world. */
static bool effect(const AxRenderFrame *frame, void *user)
{
    (void)frame;
    State *state = user;
    ++state->effect_calls;
    return state->sequence++ == 1;
}
/** Validates UI is always last. */
static bool ui(const AxRenderFrame *frame, void *user)
{
    (void)frame;
    State *state = user;
    ++state->ui_calls;
    return state->sequence == 1 || state->sequence == 2;
}
/** Exercises presentation, resize, ordered nodes, and disabling nodes on a real GPU. */
int main(void)
{
    AxWindow window = {0};
    AxRenderer renderer = {0};
    AxEvents events = {0};
    State state = {0};
    int status = EXIT_FAILURE;
    if (!ax_window_create(&window, &events, 800, 600) || !ax_renderer_create(&renderer, &window, &events, -1)) goto cleanup;
    if (!ax_renderer_set_ui(&renderer, ui, &state) || !ax_renderer_add_node(&renderer, "world", world, &state) ||
        !ax_renderer_add_node(&renderer, "effects", effect, &state) || ax_renderer_add_node(&renderer, "world", world, &state)) goto cleanup;
    for (int i = 0; i < 24; ++i) {
        if (i == 6) glfwSetWindowSize(window.handle, 640, 480);
        if (i == 12) glfwSetWindowSize(window.handle, 1024, 720);
        if (i == 18 && !ax_renderer_enable_node(&renderer, "effects", false)) goto cleanup;
        state.sequence = 0;
        glfwPollEvents();
        if (!ax_renderer_draw(&renderer)) goto cleanup;
    }
    if (!state.world_calls || !state.effect_calls || state.ui_calls != state.world_calls || state.effect_calls >= state.world_calls) goto cleanup;
    status = EXIT_SUCCESS;
cleanup:
    ax_renderer_destroy(&renderer);
    ax_window_destroy(&window);
    if (status) fprintf(stderr, "Renderer integration checks failed\n");
    return status;
}
