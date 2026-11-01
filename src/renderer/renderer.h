#pragma once
#include "core/window.h"
#include "device_info.h"
typedef struct AxRenderer AxRenderer;
typedef struct AxRendererChange AxRendererChange;
#define AX_RENDER_FRAMES 2
#define AX_RENDER_NODES 32
typedef struct {
    VkCommandBuffer command;
    VkExtent2D extent;
    VkFormat format;
    uint32_t frame_index;
    float scale_x, scale_y;
    void *device_state;
} AxRenderFrame;
typedef bool (*AxRenderNodeFn)(const AxRenderFrame *frame, void *user);
typedef bool (*AxRenderDeviceCreateFn)(AxRenderer *device, void *user, void **state);
typedef void (*AxRenderDeviceDestroyFn)(AxRenderer *device, void *user, void *state);
typedef struct {
    const char *name;
    AxRenderNodeFn record;
    void *user;
    bool enabled;
    AxRenderDeviceCreateFn create_device;
    AxRenderDeviceDestroyFn destroy_device;
    void *device_state;
} AxRenderNode;
struct AxRenderer {
    VkInstance instance;
    AxEvents *events;
    VkPhysicalDevice *physical_devices;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    AxDeviceInfo *devices;
    uint32_t device_count;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    uint32_t graphics_family, present_family, selected_index, api_version, instance_api_version;
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
    bool recording, resize_pending, changing, failed;
};
/** Creates a Vulkan 1.2/KHR or 1.3 dynamic-rendering device and presentation resources; -1 auto-selects GPU. */
bool ax_renderer_create(AxRenderer *renderer, AxWindow *window, AxEvents *events, int preferred_index);
/** Drains GPU and releases resources; accepts partial state. Destroy node-owned GPU resources first after draining. */
void ax_renderer_destroy(AxRenderer *renderer);
/** Adds a uniquely named ordered node before UI. Borrowed name/user data must outlive registration. */
bool ax_renderer_add_node(AxRenderer *renderer, const char *name, AxRenderNodeFn record, void *user);
/** Installs the final UI node. Node callbacks must not begin/end rendering or mutate the registry. */
bool ax_renderer_set_ui(AxRenderer *renderer, AxRenderNodeFn record, void *user);
/** Drains the GPU, destroys a named scene node's device state, and removes it preserving order; rejects calls during rendering/device changes. */
bool ax_renderer_remove_node(AxRenderer *renderer, const char *name);
/** Enables/disables a named scene node between frames. */
bool ax_renderer_enable_node(AxRenderer *renderer, const char *name, bool enabled);
/** Acquires, records dynamic rendering, submits, and presents; minimized windows skip work. */
bool ax_renderer_draw(AxRenderer *renderer);

/** Registers a device-owning scene node. User data survives swaps; callbacks own state, accept NULL/partial state for destruction, and must not retain temporary renderer pointers or mutate the registry. */
bool ax_renderer_register_node(AxRenderer *renderer, const AxRenderNode *node);
/** Registers a final UI node with device lifecycle callbacks; retains the old node if preparation fails. */
bool ax_renderer_register_ui(AxRenderer *renderer, const AxRenderNode *node);
/** Removes final UI and its device state while idle; rejects calls during rendering/device changes. */
bool ax_renderer_clear_ui(AxRenderer *renderer, void *user);
/** Stages a replacement device, command resources, and per-node GPU states without altering the running renderer. */
bool ax_renderer_prepare_device(AxRenderer *renderer, int index, AxRendererChange **change);
/** Transfers presentation ownership while idle. Failure keeps/restores the previous device; inspect renderer.failed for unrecoverable loss. */
bool ax_renderer_activate_device(AxRendererChange *change);
/** Finalizes or rolls back a staged change and releases its token. Rollback failure marks renderer.failed. */
bool ax_renderer_finish_device(AxRendererChange *change, bool commit);
/** Performs a complete live device switch from the owner thread, outside node callbacks. */
bool ax_renderer_switch_device(AxRenderer *renderer, int index);
