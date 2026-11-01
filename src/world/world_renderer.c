#include "world_renderer.h"
#include "core/log.h"
#include "renderer/memory.h"
#include <math.h>
#include <stdlib.h>
typedef struct {
    AxBuffer octree;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
} AxWorldDevice;
typedef struct {
    float origin[4], forward[4], right[4], up[4], sun[4];
    uint32_t info[4];
} AxWorldView;
static const uint32_t vertex_shader[] =
#include "world.vert.inc"
;
static const uint32_t fragment_shader[] =
#include "world.frag.inc"
;
/** Uploads octree words into a device-local storage buffer within the device's addressable range. */
static bool upload_octree(AxRenderer *r, const AxOctree *octree, AxWorldDevice *gpu)
{
    VkDeviceSize bytes = (VkDeviceSize)octree->word_count * sizeof(uint32_t);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(r->physical, &properties);
    if (bytes > properties.limits.maxStorageBufferRange) {
        ax_log(AX_LOG_ERROR, "World octree (%llu bytes) exceeds storage buffer range (%u bytes)",
               (unsigned long long)bytes, properties.limits.maxStorageBufferRange);
        return false;
    }
    return ax_buffer_upload(r, &gpu->octree, octree->words, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}
/** Creates the octree descriptor and the full-screen ray casting pipeline for dynamic rendering. */
static bool create_pipeline(AxRenderer *r, AxWorldDevice *gpu)
{
    VkDevice device = r->device;
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    if (vkCreateDescriptorSetLayout(device, &layout, NULL, &gpu->descriptor_layout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    if (vkCreateDescriptorPool(device, &pool, NULL, &gpu->descriptor_pool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = gpu->descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &gpu->descriptor_layout};
    if (vkAllocateDescriptorSets(device, &alloc, &gpu->descriptor) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo buffer = {gpu->octree.handle, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = gpu->descriptor, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer};
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(AxWorldView)};
    VkPipelineLayoutCreateInfo pipeline_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
        .pSetLayouts = &gpu->descriptor_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
    if (vkCreatePipelineLayout(device, &pipeline_layout, NULL, &gpu->pipeline_layout) != VK_SUCCESS) return false;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(vertex_shader), .pCode = vertex_shader};
    if (vkCreateShaderModule(device, &module, NULL, &vert) != VK_SUCCESS) return false;
    module.codeSize = sizeof(fragment_shader); module.pCode = fragment_shader;
    if (vkCreateShaderModule(device, &module, NULL, &frag) != VK_SUCCESS) { vkDestroyShaderModule(device, vert, NULL); return false; }
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vertex = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_attachment};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = states};
    VkPipelineRenderingCreateInfoKHR rendering = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, .colorAttachmentCount = 1, .pColorAttachmentFormats = &r->format};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex, .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &multisample, .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = gpu->pipeline_layout};
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &gpu->pipeline);
    vkDestroyShaderModule(device, vert, NULL); vkDestroyShaderModule(device, frag, NULL);
    return result == VK_SUCCESS;
}
/** Releases one device's world resources, including partially prepared state. */
static void destroy_world_device(AxRenderer *r, void *user, void *state)
{
    (void)user;
    AxWorldDevice *gpu = state;
    if (!gpu) return;
    if (gpu->pipeline) vkDestroyPipeline(r->device, gpu->pipeline, NULL);
    if (gpu->pipeline_layout) vkDestroyPipelineLayout(r->device, gpu->pipeline_layout, NULL);
    if (gpu->descriptor_pool) vkDestroyDescriptorPool(r->device, gpu->descriptor_pool, NULL);
    if (gpu->descriptor_layout) vkDestroyDescriptorSetLayout(r->device, gpu->descriptor_layout, NULL);
    ax_buffer_destroy(r, &gpu->octree);
    free(gpu);
}
/** Prepares world GPU resources for a live or candidate renderer device. */
static bool create_world_device(AxRenderer *r, void *user, void **state)
{
    AxWorldRenderer *world = user;
    AxWorldDevice *gpu = calloc(1, sizeof(*gpu));
    *state = gpu;
    return gpu && upload_octree(r, world->octree, gpu) && create_pipeline(r, gpu);
}
/** Builds per-frame view constants: octree-space eye, scaled camera basis, cone footprint, and layout offsets. */
static AxWorldView view_constants(const AxWorldRenderer *world, VkExtent2D extent)
{
    const AxCamera *camera = world->camera;
    float size = (float)world->octree->size;
    float tangent = tanf(camera->fov_y * 0.5f), aspect = (float)extent.width / (float)extent.height;
    float forward[3], right[3], up[3];
    ax_camera_basis(camera, forward, right, up);
    AxWorldView view = {.info = {world->octree->lookup_offset, world->octree->value_offset, world->primary_steps, world->shadow_steps}};
    for (int i = 0; i < 3; ++i) {
        view.origin[i] = 1.0f + camera->position[i] / size;
        view.forward[i] = forward[i];
        view.right[i] = right[i] * tangent * aspect;
        view.up[i] = up[i] * tangent;
        view.sun[i] = world->sun[i];
    }
    view.origin[3] = 2.0f * tangent / (float)extent.height * world->lod_bias;
    view.forward[3] = size;
    return view;
}
/** Records the full-screen ray cast; the fragment shader writes every pixel. */
static bool record_world(const AxRenderFrame *frame, void *user)
{
    AxWorldRenderer *world = user;
    AxWorldDevice *gpu = frame->device_state;
    if (!gpu || !frame->extent.width || !frame->extent.height) return false;
    AxWorldView view = view_constants(world, frame->extent);
    VkCommandBuffer command = frame->command;
    VkViewport viewport = {0, 0, (float)frame->extent.width, (float)frame->extent.height, 0, 1};
    VkRect2D scissor = {{0, 0}, frame->extent};
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline);
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline_layout, 0, 1, &gpu->descriptor, 0, NULL);
    vkCmdPushConstants(command, gpu->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(view), &view);
    vkCmdDraw(command, 3, 1, 0, 0);
    return true;
}
bool ax_world_renderer_create(AxWorldRenderer *world, AxRenderer *renderer, const AxOctree *octree, const AxCamera *camera)
{
    if (!world || !renderer || !renderer->device || !octree || !octree->words || !octree->size || !camera) return false;
    float sun[3] = {0.42f, 0.78f, 0.46f};
    float length = sqrtf(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    *world = (AxWorldRenderer){.renderer = renderer, .octree = octree, .camera = camera, .lod_bias = 1.0f,
        .sun = {sun[0] / length, sun[1] / length, sun[2] / length}, .primary_steps = 768, .shadow_steps = 384};
    AxRenderNode node = {.name = "world", .record = record_world, .user = world, .enabled = true,
        .create_device = create_world_device, .destroy_device = destroy_world_device};
    if (ax_renderer_register_node(renderer, &node)) return true;
    *world = (AxWorldRenderer){0};
    return false;
}
void ax_world_renderer_destroy(AxWorldRenderer *world)
{
    if (!world || !world->renderer) return;
    ax_renderer_remove_node(world->renderer, "world");
    *world = (AxWorldRenderer){0};
}
