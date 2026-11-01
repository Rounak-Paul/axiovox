#pragma once
#include "core/window.h"
#define AX_RENDER_FRAMES 2
#define AX_RENDER_NODES 32
typedef struct {
    VkCommandBuffer command;
    VkExtent2D extent;
    VkFormat format;
    uint32_t frame_index;
    float scale_x, scale_y;
} AxRenderFrame;
typedef bool (*AxRenderNodeFn)(const AxRenderFrame *frame, void *user);
typedef struct { const char *name; AxRenderNodeFn record; void *user; bool enabled; } AxRenderNode;
typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    uint32_t graphics_family, present_family, selected_index, api_version;
    PFN_vkCmdBeginRenderingKHR begin_rendering;
    PFN_vkCmdEndRenderingKHR end_rendering;
    AxWindow *window;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkImage *images;
    VkImageView *views;
    VkSemaphore *present_ready;
    uint32_t image_count, frame_index;
    VkCommandPool command_pool;
    VkCommandBuffer commands[AX_RENDER_FRAMES];
    VkSemaphore acquired[AX_RENDER_FRAMES];
    VkFence fences[AX_RENDER_FRAMES];
    AxRenderNode nodes[AX_RENDER_NODES], ui_node;
    size_t node_count;
    bool recording, resize_pending;
} AxRenderer;
/** Creates a Vulkan 1.2/KHR or 1.3 dynamic-rendering device and presentation resources; -1 auto-selects GPU. */
bool ax_renderer_create(AxRenderer *renderer, AxWindow *window, AxEvents *events, int preferred_index);
/** Drains GPU and releases resources; accepts partial state. Destroy node-owned GPU resources first after draining. */
void ax_renderer_destroy(AxRenderer *renderer);
/** Adds a uniquely named ordered node before UI. Borrowed name/user data must outlive registration. */
bool ax_renderer_add_node(AxRenderer *renderer, const char *name, AxRenderNodeFn record, void *user);
/** Installs the final UI node. Node callbacks must not begin/end rendering or mutate the registry. */
bool ax_renderer_set_ui(AxRenderer *renderer, AxRenderNodeFn record, void *user);
/** Enables/disables a named scene node between frames. */
bool ax_renderer_enable_node(AxRenderer *renderer, const char *name, bool enabled);
/** Acquires, records dynamic rendering, submits, and presents; minimized windows skip work. */
bool ax_renderer_draw(AxRenderer *renderer);
