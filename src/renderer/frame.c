#include "renderer_internal.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
/** Frees swapchain-owned views and semaphores after presentation is idle. */
void ax_renderer_swapchain_destroy(AxRenderer *r)
{
    for (uint32_t i = 0; i < r->image_count; ++i) {
        if (r->views && r->views[i]) vkDestroyImageView(r->device, r->views[i], NULL);
        if (r->present_ready && r->present_ready[i]) vkDestroySemaphore(r->device, r->present_ready[i], NULL);
    }
    free(r->images); free(r->views); free(r->present_ready);
    r->images = NULL; r->views = NULL; r->present_ready = NULL;
    r->image_count = 0;
    if (r->swapchain) vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    r->swapchain = VK_NULL_HANDLE;
}
/** Selects a supported surface format, preferring the renderer's current format where available. */
static bool surface_format(AxRenderer *r, VkSurfaceFormatKHR *out)
{
    uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical, r->surface, &count, NULL) != VK_SUCCESS || !count) return false;
    VkSurfaceFormatKHR *formats = calloc(count, sizeof(*formats));
    if (!formats) return false;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical, r->surface, &count, formats);
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        if ((r->format && formats[i].format == r->format) ||
            (!r->format && formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) { format = formats[i]; break; }
    }
    free(formats);
    if (result != VK_SUCCESS) return false;
    if (format.format == VK_FORMAT_UNDEFINED) format.format = r->format ? r->format : VK_FORMAT_B8G8R8A8_UNORM;
    *out = format;
    return true;
}
bool ax_renderer_choose_format(AxRenderer *r)
{
    VkSurfaceFormatKHR format;
    if (!surface_format(r, &format)) return false;
    r->format = format.format;
    return true;
}
/** Rebuilds presentation resources for the actual framebuffer extent after draining GPU work. */
bool ax_renderer_swapchain_create(AxRenderer *r)
{
    int width, height;
    glfwGetFramebufferSize(r->window->handle, &width, &height);
    if (width <= 0 || height <= 0) { r->resize_pending = true; return true; }
    if (vkDeviceWaitIdle(r->device) != VK_SUCCESS) return false;
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical, r->surface, &caps) != VK_SUCCESS || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) return false;
    VkSurfaceFormatKHR format;
    if (!surface_format(r, &format) || (r->format && r->format != format.format)) return false;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)width < caps.minImageExtent.width ? caps.minImageExtent.width : (uint32_t)width;
        extent.height = (uint32_t)height < caps.minImageExtent.height ? caps.minImageExtent.height : (uint32_t)height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alphas[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (size_t i = 0; i < 4; ++i) if (caps.supportedCompositeAlpha & alphas[i]) { alpha = alphas[i]; break; }
    uint32_t families[] = {r->graphics_family, r->present_family};
    VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = r->surface,
        .minImageCount = image_count, .imageFormat = format.format, .imageColorSpace = format.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = families[0] == families[1] ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = families[0] == families[1] ? 0u : 2u, .pQueueFamilyIndices = families,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE, .oldSwapchain = r->swapchain};
    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(r->device, &info, NULL, &swapchain) != VK_SUCCESS) return false;
    ax_renderer_swapchain_destroy(r);
    r->swapchain = swapchain;
    r->format = format.format;
    r->extent = extent;
    if (vkGetSwapchainImagesKHR(r->device, swapchain, &r->image_count, NULL) != VK_SUCCESS || !r->image_count) return false;
    r->images = calloc(r->image_count, sizeof(*r->images));
    r->views = calloc(r->image_count, sizeof(*r->views));
    r->present_ready = calloc(r->image_count, sizeof(*r->present_ready));
    if (!r->images || !r->views || !r->present_ready) return false;
    if (vkGetSwapchainImagesKHR(r->device, swapchain, &r->image_count, r->images) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < r->image_count; ++i) {
        VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateImageView(r->device, &view, NULL, &r->views[i]) != VK_SUCCESS ||
            vkCreateSemaphore(r->device, &semaphore, NULL, &r->present_ready[i]) != VK_SUCCESS) return false;
    }
    r->resize_pending = false;
    return true;
}
bool ax_renderer_frames_create(AxRenderer *r)
{
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = r->graphics_family};
    if (vkCreateCommandPool(r->device, &pool, NULL, &r->command_pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = AX_RENDER_FRAMES};
    if (vkAllocateCommandBuffers(r->device, &alloc, r->commands) != VK_SUCCESS) return false;
    for (size_t i = 0; i < AX_RENDER_FRAMES; ++i) {
        VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        if (vkCreateSemaphore(r->device, &semaphore, NULL, &r->acquired[i]) != VK_SUCCESS ||
            vkCreateFence(r->device, &fence, NULL, &r->fences[i]) != VK_SUCCESS) return false;
    }
    return true;
}
void ax_renderer_frames_destroy(AxRenderer *r)
{
    ax_renderer_swapchain_destroy(r);
    for (size_t i = 0; i < AX_RENDER_FRAMES; ++i) {
        if (r->acquired[i]) vkDestroySemaphore(r->device, r->acquired[i], NULL);
        if (r->fences[i]) vkDestroyFence(r->device, r->fences[i], NULL);
    }
    if (r->command_pool) vkDestroyCommandPool(r->device, r->command_pool, NULL);
}
/** Prepares one node state without changing the live registry. Failed states are released by their owner callback. */
static bool prepare_node(AxRenderer *r, AxRenderNode *node)
{
    node->device_state = NULL;
    if (!node->create_device) return !node->destroy_device;
    if (!node->destroy_device) return false;
    if (node->create_device(r, node->user, &node->device_state)) return true;
    vkDeviceWaitIdle(r->device);
    node->destroy_device(r, node->user, node->device_state);
    node->device_state = NULL;
    return false;
}
bool ax_renderer_register_node(AxRenderer *r, const AxRenderNode *node)
{
    if (!r || r->recording || r->changing || !node || !node->name || !*node->name || !strcmp(node->name, "ui") || !node->record || r->node_count == AX_RENDER_NODES) return false;
    for (size_t i = 0; i < r->node_count; ++i) if (!strcmp(node->name, r->nodes[i].name)) return false;
    AxRenderNode prepared = *node;
    if (!prepare_node(r, &prepared)) return false;
    r->nodes[r->node_count++] = prepared;
    return true;
}
bool ax_renderer_add_node(AxRenderer *r, const char *name, AxRenderNodeFn record, void *user)
{
    AxRenderNode node = {.name = name, .record = record, .user = user, .enabled = true};
    return ax_renderer_register_node(r, &node);
}
bool ax_renderer_register_ui(AxRenderer *r, const AxRenderNode *node)
{
    if (!r || r->recording || r->changing || !node || !node->record) return false;
    AxRenderNode prepared = *node; prepared.name = "ui";
    if (!prepare_node(r, &prepared)) return false;
    if (vkDeviceWaitIdle(r->device) != VK_SUCCESS) {
        if (prepared.destroy_device) prepared.destroy_device(r, prepared.user, prepared.device_state);
        return false;
    }
    if (r->ui_node.destroy_device) r->ui_node.destroy_device(r, r->ui_node.user, r->ui_node.device_state);
    r->ui_node = prepared;
    return true;
}
bool ax_renderer_set_ui(AxRenderer *r, AxRenderNodeFn record, void *user)
{
    AxRenderNode node = {.name = "ui", .record = record, .user = user, .enabled = true};
    return ax_renderer_register_ui(r, &node);
}
bool ax_renderer_clear_ui(AxRenderer *r, void *user)
{
    if (!r || r->recording || r->changing || r->ui_node.user != user) return false;
    if (vkDeviceWaitIdle(r->device) != VK_SUCCESS) return false;
    if (r->ui_node.destroy_device) r->ui_node.destroy_device(r, user, r->ui_node.device_state);
    r->ui_node = (AxRenderNode){0};
    return true;
}
bool ax_renderer_remove_node(AxRenderer *r, const char *name)
{
    if (!r || !name || r->recording || r->changing) return false;
    for (size_t i = 0; i < r->node_count; ++i) {
        if (strcmp(name, r->nodes[i].name)) continue;
        if (r->nodes[i].destroy_device) {
            if (vkDeviceWaitIdle(r->device) != VK_SUCCESS) return false;
            r->nodes[i].destroy_device(r, r->nodes[i].user, r->nodes[i].device_state);
        }
        memmove(&r->nodes[i], &r->nodes[i + 1], (r->node_count - i - 1) * sizeof(r->nodes[0]));
        r->nodes[--r->node_count] = (AxRenderNode){0};
        return true;
    }
    return false;
}
bool ax_renderer_enable_node(AxRenderer *r, const char *name, bool enabled)
{
    if (!r || !name || r->recording || r->changing) return false;
    for (size_t i = 0; i < r->node_count; ++i) if (!strcmp(name, r->nodes[i].name)) { r->nodes[i].enabled = enabled; return true; }
    return false;
}
bool ax_renderer_draw(AxRenderer *r)
{
    if (!r || r->failed || r->changing) return false;
    int width, height;
    glfwGetFramebufferSize(r->window->handle, &width, &height);
    if (width <= 0 || height <= 0) return true;
    if (!r->swapchain || r->resize_pending || (uint32_t)width != r->extent.width || (uint32_t)height != r->extent.height) {
        if (!ax_renderer_swapchain_create(r)) return false;
    }
    uint32_t frame = r->frame_index, image;
    if (vkWaitForFences(r->device, 1, &r->fences[frame], VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;
    VkResult result = vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->acquired[frame], VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { r->resize_pending = true; return true; }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return false;
    if (result == VK_SUBOPTIMAL_KHR) r->resize_pending = true;
    VkCommandBuffer command = r->commands[frame];
    if (vkResetCommandBuffer(command, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) return false;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = r->images[image], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    VkRenderingAttachmentInfoKHR attachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = r->views[image], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}};
    VkRenderingInfoKHR rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, .renderArea = {{0, 0}, r->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attachment};
    r->begin_rendering(command, &rendering);
    VkViewport viewport = {0, 0, (float)r->extent.width, (float)r->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, r->extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    int logical_width, logical_height;
    glfwGetWindowSize(r->window->handle, &logical_width, &logical_height);
    AxRenderFrame context = {command, r->extent, r->format, frame,
        logical_width > 0 ? (float)r->extent.width / logical_width : 1.0f,
        logical_height > 0 ? (float)r->extent.height / logical_height : 1.0f, NULL};
    bool ok = true;
    r->recording = true;
    for (size_t i = 0; i < r->node_count && ok; ++i) if (r->nodes[i].enabled) {
        context.device_state = r->nodes[i].device_state;
        ok = r->nodes[i].record(&context, r->nodes[i].user);
    }
    if (ok && r->ui_node.enabled && r->ui_node.record) {
        context.device_state = r->ui_node.device_state;
        ok = r->ui_node.record(&context, r->ui_node.user);
    }
    r->recording = false;
    r->end_rendering(command);
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (!ok || vkEndCommandBuffer(command) != VK_SUCCESS) return false;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->acquired[frame], .pWaitDstStageMask = &stage,
        .commandBufferCount = 1, .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &r->present_ready[image]};
    if (vkResetFences(r->device, 1, &r->fences[frame]) != VK_SUCCESS || vkQueueSubmit(r->graphics_queue, 1, &submit, r->fences[frame]) != VK_SUCCESS) return false;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->present_ready[image], .swapchainCount = 1, .pSwapchains = &r->swapchain, .pImageIndices = &image};
    result = vkQueuePresentKHR(r->present_queue, &present);
    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) r->resize_pending = true;
    else if (result != VK_SUCCESS) return false;
    r->frame_index = (frame + 1) % AX_RENDER_FRAMES;
    return true;
}
