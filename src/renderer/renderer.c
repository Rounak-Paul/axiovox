#include "renderer_internal.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
/** Draws the initial scene background; further scene nodes are appended before UI. */
static bool record_background(const AxRenderFrame *frame, void *user)
{
    (void)user;
    VkClearAttachment attachment = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = 0,
        .clearValue = {.color = {{0.025f, 0.035f, 0.055f, 1.0f}}}};
    VkClearRect rect = {.rect = {{0, 0}, frame->extent}, .baseArrayLayer = 0, .layerCount = 1};
    vkCmdClearAttachments(frame->command, 1, &attachment, 1, &rect);
    return true;
}
/** Checks device extension availability without retaining temporary enumeration data. */
static bool has_extension(VkPhysicalDevice device, const char *name)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS || !count) return false;
    VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions) return false;
    VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    bool found = false;
    if (result == VK_SUCCESS) for (uint32_t i = 0; i < count; ++i) if (!strcmp(extensions[i].extensionName, name)) found = true;
    free(extensions);
    return found;
}
/** Finds graphics/presentation queues and verifies surface formats and presentation modes. */
static bool suitable(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t *graphics, uint32_t *present, uint32_t instance_version)
{
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    bool core_dynamic = properties.apiVersion >= VK_API_VERSION_1_3 && instance_version >= VK_API_VERSION_1_3;
    if (properties.apiVersion < VK_API_VERSION_1_2 || !has_extension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        (!core_dynamic && !has_extension(device, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))) return false;
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &dynamic};
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!dynamic.dynamicRendering) return false;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (!count) return false;
    VkQueueFamilyProperties *queues = calloc(count, sizeof(*queues));
    if (!queues) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queues);
    *graphics = *present = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        if (!queues[i].queueCount) continue;
        VkBool32 supported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supported) != VK_SUCCESS) continue;
        if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && *graphics == UINT32_MAX) *graphics = i;
        if (supported && *present == UINT32_MAX) *present = i;
        if (supported && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { *graphics = *present = i; break; }
    }
    free(queues);
    uint32_t formats = 0, modes = 0;
    return *graphics != UINT32_MAX && *present != UINT32_MAX &&
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formats, NULL) == VK_SUCCESS && formats &&
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modes, NULL) == VK_SUCCESS && modes;
}
/** Retains the instance's device identities and presentation compatibility for settings and live replacement. */
static bool enumerate_devices(AxRenderer *renderer)
{
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(renderer->instance, &count, NULL) != VK_SUCCESS || !count) return false;
    renderer->physical_devices = calloc(count, sizeof(*renderer->physical_devices));
    renderer->devices = calloc(count, sizeof(*renderer->devices));
    if (!renderer->physical_devices || !renderer->devices) return false;
    if (vkEnumeratePhysicalDevices(renderer->instance, &count, renderer->physical_devices) != VK_SUCCESS) return false;
    renderer->device_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(renderer->physical_devices[i], &properties);
        uint32_t graphics, present;
        AxDeviceInfo *info = &renderer->devices[i];
        info->index = i;
        info->compatible = suitable(renderer->physical_devices[i], renderer->surface, &graphics, &present, renderer->instance_api_version);
        memcpy(info->name, properties.deviceName, AX_DEVICE_NAME_CAPACITY);
        info->name[AX_DEVICE_NAME_CAPACITY - 1] = 0;
        ax_log(AX_LOG_INFO, "GPU %u: %s (%s)", i, info->name, info->compatible ? "compatible" : "incompatible");
    }
    return true;
}
/** Creates only device-local queues, command resources, and format selection; instance/surface are borrowed. */
static bool create_device(AxRenderer *renderer, int preferred_index)
{
    int best_score = -1;
    renderer->api_version = renderer->instance_api_version;
    for (uint32_t i = 0; i < renderer->device_count; ++i) {
        if (preferred_index >= 0 && i != (uint32_t)preferred_index) continue;
        uint32_t graphics, present;
        VkPhysicalDevice physical = renderer->physical_devices[i];
        if (!suitable(physical, renderer->surface, &graphics, &present, renderer->instance_api_version)) continue;
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 400 :
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 300 :
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 200 : 100;
        if (score > best_score) {
            best_score = score; renderer->physical = physical; renderer->selected_index = i;
            renderer->graphics_family = graphics; renderer->present_family = present;
        }
    }
    if (best_score < 0) return false;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = renderer->graphics_family, .queueCount = 1, .pQueuePriorities = &priority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = renderer->present_family, .queueCount = 1, .pQueuePriorities = &priority}};
    VkPhysicalDeviceProperties selected_properties;
    vkGetPhysicalDeviceProperties(renderer->physical, &selected_properties);
    if (selected_properties.apiVersion < renderer->api_version) renderer->api_version = selected_properties.apiVersion;
    bool core_dynamic = renderer->api_version >= VK_API_VERSION_1_3;
    const char *device_extensions[3] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t device_extension_count = 1;
    if (!core_dynamic) device_extensions[device_extension_count++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    if (has_extension(renderer->physical, "VK_KHR_portability_subset")) device_extensions[device_extension_count++] = "VK_KHR_portability_subset";
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, .dynamicRendering = VK_TRUE};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &dynamic, .queueCreateInfoCount = renderer->graphics_family == renderer->present_family ? 1u : 2u, .pQueueCreateInfos = queues, .enabledExtensionCount = device_extension_count, .ppEnabledExtensionNames = device_extensions};
    VkResult result = vkCreateDevice(renderer->physical, &device, NULL, &renderer->device);
    if (result != VK_SUCCESS) { ax_log(AX_LOG_ERROR, "vkCreateDevice failed: %d", result); return false; }
    vkGetDeviceQueue(renderer->device, renderer->graphics_family, 0, &renderer->graphics_queue);
    vkGetDeviceQueue(renderer->device, renderer->present_family, 0, &renderer->present_queue);
    renderer->begin_rendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(renderer->device, core_dynamic ? "vkCmdBeginRendering" : "vkCmdBeginRenderingKHR");
    renderer->end_rendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(renderer->device, core_dynamic ? "vkCmdEndRendering" : "vkCmdEndRenderingKHR");
    return renderer->begin_rendering && renderer->end_rendering && ax_renderer_choose_format(renderer) && ax_renderer_frames_create(renderer);
}
/** Emits successful selection only after the replacement is committed. */
static void emit_selected(AxRenderer *renderer)
{
    AxEvent selected = {.type = AX_EVENT_DEVICE_SELECTED,
        .data.device = {renderer->selected_index, renderer->devices[renderer->selected_index].name}};
    ax_event_emit(renderer->events, &selected);
}
bool ax_renderer_create(AxRenderer *renderer, AxWindow *window, AxEvents *events, int preferred_index)
{
    if (!renderer || !window || !window->handle || preferred_index < -1) return false;
    *renderer = (AxRenderer){.window = window, .events = events};
    PFN_vkEnumerateInstanceVersion enumerate_version = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (!enumerate_version || enumerate_version(&loader_version) != VK_SUCCESS || loader_version < VK_API_VERSION_1_2) {
        ax_log(AX_LOG_ERROR, "Vulkan 1.2 or newer is required");
        return false;
    }
    renderer->api_version = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
    renderer->instance_api_version = renderer->api_version;
    uint32_t required_count = 0;
    const char **required = glfwGetRequiredInstanceExtensions(&required_count);
    if (!required || !required_count) return false;
    const char **extensions = calloc((size_t)required_count + 2, sizeof(*extensions));
    if (!extensions) return false;
    for (uint32_t i = 0; i < required_count; ++i) extensions[i] = required[i];
    uint32_t extension_count = required_count;
    VkInstanceCreateFlags flags = 0;
#if defined(__APPLE__) && !defined(AX_VULKAN_DIRECT_MOLTENVK)
    extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    extensions[extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Axiovox", .applicationVersion = VK_MAKE_VERSION(0, 1, 0), .apiVersion = renderer->api_version};
    VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .flags = flags, .pApplicationInfo = &application, .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions};
    VkResult result = vkCreateInstance(&instance, NULL, &renderer->instance);
    free(extensions);
    if (result != VK_SUCCESS) { ax_log(AX_LOG_ERROR, "vkCreateInstance failed: %d", result); goto fail; }
    if (glfwCreateWindowSurface(renderer->instance, window->handle, NULL, &renderer->surface) != VK_SUCCESS) goto fail;
    if (!enumerate_devices(renderer) || !create_device(renderer, preferred_index) || !ax_renderer_swapchain_create(renderer)) goto fail;
    if (!ax_renderer_add_node(renderer, "background", record_background, NULL)) goto fail;
    emit_selected(renderer);
    return true;
fail:
    ax_renderer_destroy(renderer);
    return false;
}
void ax_renderer_device_destroy(AxRenderer *renderer)
{
    if (!renderer->device) return;
    if (renderer->ui_node.destroy_device) renderer->ui_node.destroy_device(renderer, renderer->ui_node.user, renderer->ui_node.device_state);
    for (size_t i = renderer->node_count; i > 0; --i) {
        AxRenderNode *node = &renderer->nodes[i - 1];
        if (node->destroy_device) node->destroy_device(renderer, node->user, node->device_state);
    }
    ax_renderer_frames_destroy(renderer);
    vkDestroyDevice(renderer->device, NULL);
}
void ax_renderer_destroy(AxRenderer *renderer)
{
    if (!renderer) return;
    if (renderer->device) { vkDeviceWaitIdle(renderer->device); ax_renderer_device_destroy(renderer); }
    if (renderer->surface) vkDestroySurfaceKHR(renderer->instance, renderer->surface, NULL);
    if (renderer->instance) vkDestroyInstance(renderer->instance, NULL);
    free(renderer->devices); free(renderer->physical_devices);
    *renderer = (AxRenderer){0};
}
struct AxRendererChange { AxRenderer *live; AxRenderer next, previous; bool activated; };
bool ax_renderer_prepare_device(AxRenderer *renderer, int index, AxRendererChange **out)
{
    if (!out) return false;
    *out = NULL;
    if (!renderer || !renderer->device || renderer->recording || renderer->changing || renderer->failed || index < -1) return false;
    AxRendererChange *change = calloc(1, sizeof(*change));
    if (!change) return false;
    change->live = renderer;
    AxRenderer *next = &change->next;
    *next = (AxRenderer){.instance = renderer->instance, .surface = renderer->surface, .events = renderer->events,
        .physical_devices = renderer->physical_devices, .devices = renderer->devices, .device_count = renderer->device_count,
        .window = renderer->window, .instance_api_version = renderer->instance_api_version, .format = renderer->format,
        .node_count = renderer->node_count, .changing = true};
    renderer->changing = true;
    if (!create_device(next, index)) goto fail;
    for (size_t i = 0; i < renderer->node_count; ++i) {
        next->nodes[i] = renderer->nodes[i]; next->nodes[i].device_state = NULL;
    }
    next->ui_node = renderer->ui_node; next->ui_node.device_state = NULL;
    for (size_t i = 0; i < next->node_count; ++i) {
        AxRenderNode *node = &next->nodes[i];
        if (node->create_device && !node->create_device(next, node->user, &node->device_state)) goto fail;
    }
    if (next->ui_node.create_device && !next->ui_node.create_device(next, next->ui_node.user, &next->ui_node.device_state)) goto fail;
    *out = change;
    return true;
fail:
    ax_renderer_finish_device(change, false);
    return false;
}
bool ax_renderer_activate_device(AxRendererChange *change)
{
    if (!change || change->activated) return false;
    AxRenderer *live = change->live;
    if (vkDeviceWaitIdle(change->next.device) != VK_SUCCESS) return false;
    if (vkDeviceWaitIdle(live->device) != VK_SUCCESS) { live->failed = true; return false; }
    ax_renderer_swapchain_destroy(live);
    if (!ax_renderer_swapchain_create(&change->next)) {
        ax_renderer_swapchain_destroy(&change->next);
        if (!ax_renderer_swapchain_create(live)) live->failed = true;
        return false;
    }
    change->previous = *live;
    *live = change->next;
    change->activated = true;
    return true;
}
bool ax_renderer_finish_device(AxRendererChange *change, bool commit)
{
    if (!change) return true;
    AxRenderer *live = change->live;
    bool ok = !live->failed;
    if (commit && !change->activated) { commit = false; ok = false; }
    if (change->activated) {
        if (commit) ax_renderer_device_destroy(&change->previous);
        else {
            vkDeviceWaitIdle(live->device);
            ax_renderer_swapchain_destroy(live);
            ok = ax_renderer_swapchain_create(&change->previous);
            AxRenderer discarded = *live;
            *live = change->previous;
            live->failed = !ok;
            ax_renderer_device_destroy(&discarded);
        }
    } else {
        if (change->next.device) vkDeviceWaitIdle(change->next.device);
        ax_renderer_device_destroy(&change->next);
    }
    live->changing = false;
    if (commit && change->activated && ok) emit_selected(live);
    free(change);
    return ok;
}
bool ax_renderer_switch_device(AxRenderer *renderer, int index)
{
    AxRendererChange *change;
    if (!ax_renderer_prepare_device(renderer, index, &change)) return false;
    if (!ax_renderer_activate_device(change)) { ax_renderer_finish_device(change, false); return false; }
    return ax_renderer_finish_device(change, true);
}
