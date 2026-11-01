#pragma once
#include "renderer.h"
typedef struct { VkBuffer handle; VkDeviceMemory memory; void *mapped; } AxBuffer;
/** Finds a memory type matching required flags; UINT32_MAX if none exists. */
uint32_t ax_memory_type(VkPhysicalDevice physical, uint32_t bits, VkMemoryPropertyFlags required);
/** Allocates/binds a buffer and optionally maps host-coherent memory; cleans up on failure. */
bool ax_buffer_create(AxRenderer *renderer, AxBuffer *buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool mapped);
/** Unmaps and frees an owned buffer; accepts zero state. */
void ax_buffer_destroy(AxRenderer *renderer, AxBuffer *buffer);
