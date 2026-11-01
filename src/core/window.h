#pragma once
#include "input.h"
#include "config.h"
#include <vulkan/vulkan.h>
typedef struct GLFWwindow GLFWwindow;
typedef struct { GLFWwindow *handle; AxEvents *events; AxInput *input; } AxWindow;
/** Initializes GLFW and a Vulkan-capable window. Main-thread only; one window owns GLFW lifetime. */
bool ax_window_create(AxWindow *window, AxEvents *events, int width, int height);
/** Destroys the window and terminates GLFW after renderer shutdown. Accepts an empty window. */
void ax_window_destroy(AxWindow *window);

/** Attaches input state and installs GLFW input callbacks; input must outlive the window. */
void ax_window_set_input(AxWindow *window, AxInput *input);

/** Publishes actual logical window dimensions to config before UI/commands run; ignores minimized windows. */
bool ax_window_sync_config(AxWindow *window, AxConfig *config);
