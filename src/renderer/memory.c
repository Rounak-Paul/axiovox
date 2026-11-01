#include "memory.h"
#include <string.h>
uint32_t ax_memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags required)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & required) == required) return i;
    return UINT32_MAX;
}
bool ax_buffer_create(AxRenderer *r, AxBuffer *buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool mapped)
{
    *buffer = (AxBuffer){0};
    VkBufferCreateInfo create = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateBuffer(r->device, &create, NULL, &buffer->handle) != VK_SUCCESS) goto fail;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(r->device, buffer->handle, &requirements);
    uint32_t type = ax_memory_type(r->physical, requirements.memoryTypeBits, mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) goto fail;
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (vkAllocateMemory(r->device, &alloc, NULL, &buffer->memory) != VK_SUCCESS || vkBindBufferMemory(r->device, buffer->handle, buffer->memory, 0) != VK_SUCCESS) goto fail;
    if (mapped && vkMapMemory(r->device, buffer->memory, 0, size, 0, &buffer->mapped) != VK_SUCCESS) goto fail;
    return true;
fail:
    ax_buffer_destroy(r, buffer);
    return false;
}
void ax_buffer_destroy(AxRenderer *r, AxBuffer *buffer)
{
    if (buffer->mapped) vkUnmapMemory(r->device, buffer->memory);
    if (buffer->handle) vkDestroyBuffer(r->device, buffer->handle, NULL);
    if (buffer->memory) vkFreeMemory(r->device, buffer->memory, NULL);
    *buffer = (AxBuffer){0};
}
bool ax_buffer_upload(AxRenderer *r, AxBuffer *buffer, const void *data, VkDeviceSize size, VkBufferUsageFlags usage)
{
    *buffer = (AxBuffer){0};
    if (!r || !r->device || !data || !size) return false;
    AxBuffer staging = {0};
    VkCommandBuffer command = VK_NULL_HANDLE;
    bool success = false;
    if (!ax_buffer_create(r, &staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true) ||
        !ax_buffer_create(r, buffer, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false)) goto cleanup;
    memcpy(staging.mapped, data, (size_t)size);
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    if (vkAllocateCommandBuffers(r->device, &alloc, &command) != VK_SUCCESS) { command = VK_NULL_HANDLE; goto cleanup; }
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) goto cleanup;
    VkBufferCopy copy = {0, 0, size};
    vkCmdCopyBuffer(command, staging.handle, buffer->handle, 1, &copy);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    if (vkEndCommandBuffer(command) != VK_SUCCESS) goto cleanup;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    success = vkQueueSubmit(r->graphics_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(r->graphics_queue) == VK_SUCCESS;
cleanup:
    if (command) vkFreeCommandBuffers(r->device, r->command_pool, 1, &command);
    ax_buffer_destroy(r, &staging);
    if (!success) ax_buffer_destroy(r, buffer);
    return success;
}
