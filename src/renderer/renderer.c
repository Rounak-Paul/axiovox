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
bool ax_renderer_create(AxRenderer *renderer, AxWindow *window, AxEvents *events, int preferred_index)
{
    if (!renderer || !window || !window->handle || preferred_index < -1) return false;
    *renderer = (AxRenderer){.window = window};
    PFN_vkEnumerateInstanceVersion enumerate_version = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (!enumerate_version || enumerate_version(&loader_version) != VK_SUCCESS || loader_version < VK_API_VERSION_1_2) {
        ax_log(AX_LOG_ERROR, "Vulkan 1.2 or newer is required");
        return false;
    }
    renderer->api_version = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
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
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(renderer->instance, &count, NULL) != VK_SUCCESS || !count) goto fail;
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) goto fail;
    result = vkEnumeratePhysicalDevices(renderer->instance, &count, devices);
    renderer->devices = calloc(count, sizeof(*renderer->devices));
    if (!renderer->devices) { free(devices); goto fail; }
    renderer->device_count = count;
    int best_score = -1;
    if (result == VK_SUCCESS) for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        uint32_t graphics, present;
        bool compatible = suitable(devices[i], renderer->surface, &graphics, &present, renderer->api_version);
        renderer->devices[i].index = i;
        renderer->devices[i].compatible = compatible;
        memcpy(renderer->devices[i].name, properties.deviceName, AX_DEVICE_NAME_CAPACITY);
        renderer->devices[i].name[AX_DEVICE_NAME_CAPACITY - 1] = 0;
        ax_log(AX_LOG_INFO, "GPU %u: %s (%s)", i, properties.deviceName, compatible ? "compatible" : "incompatible");
        if (!compatible || (preferred_index >= 0 && i != (uint32_t)preferred_index)) continue;
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 400 :
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 300 :
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 200 : 100;
        if (score > best_score) {
            best_score = score;
            renderer->physical = devices[i];
            renderer->selected_index = i;
            renderer->graphics_family = graphics;
            renderer->present_family = present;
        }
    }
    free(devices);
    if (best_score < 0) { ax_log(AX_LOG_ERROR, "No compatible Vulkan device for requested index %d", preferred_index); goto fail; }
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
    result = vkCreateDevice(renderer->physical, &device, NULL, &renderer->device);
    if (result != VK_SUCCESS) { ax_log(AX_LOG_ERROR, "vkCreateDevice failed: %d", result); goto fail; }
    vkGetDeviceQueue(renderer->device, renderer->graphics_family, 0, &renderer->graphics_queue);
    vkGetDeviceQueue(renderer->device, renderer->present_family, 0, &renderer->present_queue);
    renderer->begin_rendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(renderer->device, core_dynamic ? "vkCmdBeginRendering" : "vkCmdBeginRenderingKHR");
    renderer->end_rendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(renderer->device, core_dynamic ? "vkCmdEndRendering" : "vkCmdEndRenderingKHR");
    if (!renderer->begin_rendering || !renderer->end_rendering || !ax_renderer_frames_create(renderer)) goto fail;
    if (!ax_renderer_add_node(renderer, "background", record_background, NULL)) goto fail;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(renderer->physical, &properties);
    AxEvent selected = {.type = AX_EVENT_DEVICE_SELECTED, .data.device = {renderer->selected_index, properties.deviceName}};
    ax_event_emit(events, &selected);
    return true;
fail:
    ax_renderer_destroy(renderer);
    return false;
}
void ax_renderer_destroy(AxRenderer *renderer)
{
    if (!renderer) return;
    if (renderer->device) { vkDeviceWaitIdle(renderer->device); ax_renderer_frames_destroy(renderer); vkDestroyDevice(renderer->device, NULL); }
    if (renderer->surface) vkDestroySurfaceKHR(renderer->instance, renderer->surface, NULL);
    if (renderer->instance) vkDestroyInstance(renderer->instance, NULL);
    free(renderer->devices);
    *renderer = (AxRenderer){0};
}
