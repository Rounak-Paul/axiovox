#include "ui.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <GLFW/glfw3.h>
static const uint32_t vertex_shader[] =
#include "ui.vert.inc"
;
static const uint32_t fragment_shader[] =
#include "ui.frag.inc"
;
/** Uploads a baked R8 atlas with explicit transfer/sample transitions. */
static bool create_atlas(AxUi *ui)
{
    AxRenderer *r = ui->renderer;
    AxBuffer staging = {0};
    VkCommandBuffer command = VK_NULL_HANDLE;
    bool success = false;
    unsigned char *pixels = calloc(AX_FONT_ATLAS * AX_FONT_ATLAS, 1);
    if (!pixels) return false;
    if (!ax_font_bake(&ui->font, pixels) || !ax_buffer_create(r, &staging, AX_FONT_ATLAS * AX_FONT_ATLAS, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) goto cleanup;
    memcpy(staging.mapped, pixels, AX_FONT_ATLAS * AX_FONT_ATLAS);
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM, .extent = {AX_FONT_ATLAS, AX_FONT_ATLAS, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateImage(r->device, &image, NULL, &ui->atlas) != VK_SUCCESS) goto cleanup;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(r->device, ui->atlas, &requirements);
    uint32_t type = ax_memory_type(r->physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) goto cleanup;
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (vkAllocateMemory(r->device, &alloc, NULL, &ui->atlas_memory) != VK_SUCCESS || vkBindImageMemory(r->device, ui->atlas, ui->atlas_memory, 0) != VK_SUCCESS) goto cleanup;
    VkCommandBufferAllocateInfo command_alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    if (vkAllocateCommandBuffers(r->device, &command_alloc, &command) != VK_SUCCESS) goto cleanup;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) goto cleanup;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = ui->atlas, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {AX_FONT_ATLAS, AX_FONT_ATLAS, 1}};
    vkCmdCopyBufferToImage(command, staging.handle, ui->atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (vkEndCommandBuffer(command) != VK_SUCCESS) goto cleanup;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    if (vkQueueSubmit(r->graphics_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) goto cleanup;
    if (vkQueueWaitIdle(r->graphics_queue) != VK_SUCCESS) goto cleanup;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = ui->atlas,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(r->device, &view, NULL, &ui->atlas_view) != VK_SUCCESS) goto cleanup;
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    success = vkCreateSampler(r->device, &sampler, NULL, &ui->sampler) == VK_SUCCESS;
cleanup:
    if (command) vkFreeCommandBuffers(r->device, r->command_pool, 1, &command);
    ax_buffer_destroy(r, &staging);
    free(pixels);
    return success;
}
/** Creates the font descriptor and alpha-blended pipeline compatible with dynamic rendering. */
static bool create_pipeline(AxUi *ui)
{
    VkDevice device = ui->renderer->device;
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    if (vkCreateDescriptorSetLayout(device, &layout, NULL, &ui->descriptor_layout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size};
    if (vkCreateDescriptorPool(device, &pool, NULL, &ui->descriptor_pool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = ui->descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &ui->descriptor_layout};
    if (vkAllocateDescriptorSets(device, &alloc, &ui->descriptor) != VK_SUCCESS) return false;
    VkDescriptorImageInfo image = {ui->sampler, ui->atlas_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ui->descriptor, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &image};
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo pipeline_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
        .pSetLayouts = &ui->descriptor_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
    if (vkCreatePipelineLayout(device, &pipeline_layout, NULL, &ui->pipeline_layout) != VK_SUCCESS) return false;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(vertex_shader), .pCode = vertex_shader};
    if (vkCreateShaderModule(device, &module, NULL, &vert) != VK_SUCCESS) return false;
    module.codeSize = sizeof(fragment_shader); module.pCode = fragment_shader;
    if (vkCreateShaderModule(device, &module, NULL, &frag) != VK_SUCCESS) { vkDestroyShaderModule(device, vert, NULL); return false; }
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"}};
    VkVertexInputBindingDescription vertex_binding = {0, sizeof(AxUiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(AxUiVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(AxUiVertex, u)}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AxUiVertex, r)}};
    VkPipelineVertexInputStateCreateInfo vertex = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachment = {.blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_attachment};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = states};
    VkPipelineRenderingCreateInfoKHR rendering = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, .colorAttachmentCount = 1, .pColorAttachmentFormats = &ui->renderer->format};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex, .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &multisample, .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = ui->pipeline_layout};
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &ui->pipeline);
    vkDestroyShaderModule(device, vert, NULL); vkDestroyShaderModule(device, frag, NULL);
    return result == VK_SUCCESS;
}
/** Appends a rectangle or textured glyph as two triangles within the fixed vertex budget. */
static void quad(AxUiVertex *vertices, uint32_t *count, float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1, float red, float green, float blue, float alpha)
{
    if (*count > AX_UI_VERTICES - 6) return;
    AxUiVertex points[] = {{x0,y0,u0,v0,red,green,blue,alpha}, {x1,y0,u1,v0,red,green,blue,alpha}, {x1,y1,u1,v1,red,green,blue,alpha},
        {x0,y0,u0,v0,red,green,blue,alpha}, {x1,y1,u1,v1,red,green,blue,alpha}, {x0,y1,u0,v1,red,green,blue,alpha}};
    memcpy(vertices + *count, points, sizeof(points));
    *count += 6;
}
/** Draws one clipped line of UTF-8 text with Latin-1 glyphs and replacement for other codepoints. */
static float text(AxUi *ui, AxUiVertex *vertices, uint32_t *count, const char *string, size_t bytes, float x, float y, float right)
{
    const char *p = string;
    while (*p && (size_t)(p - string) < bytes) {
        const AxGlyph *glyph = ax_font_glyph(&ui->font, &p);
        float scale = 0.5f;
        if (x + glyph->advance * scale > right) break;
        quad(vertices, count, x + glyph->x0 * scale, y + glyph->y0 * scale, x + glyph->x1 * scale, y + glyph->y1 * scale,
            glyph->u0, glyph->v0, glyph->u1, glyph->v1, 0.87f, 0.92f, 0.98f, 1.0f);
        x += glyph->advance * scale;
    }
    return x;
}
/** Records the final UI node using a fence-protected per-frame vertex buffer. */
static bool record_ui(const AxRenderFrame *frame, void *user)
{
    AxUi *ui = user;
    AxConsole *console = ui->console;
    float width = frame->extent.width / frame->scale_x, height = frame->extent.height / frame->scale_y;
    AxUiVertex *vertices = ui->vertices[frame->frame_index].mapped;
    uint32_t count = 0;
    if (!console->open) {
        const char *hint = "Press / for commands";
        text(ui, vertices, &count, hint, strlen(hint), 16, height - 20, width - 16);
        size_t visible = console->output_count < 4 ? console->output_count : 4;
        for (size_t i = 0; i < visible; ++i) {
            const char *line = console->output[console->output_count - visible + i];
            text(ui, vertices, &count, line, strlen(line), 16, height - 50 - (float)(visible - 1 - i) * 20, width - 16);
        }
    } else {
        float panel_height = fminf(height, 300.0f);
        quad(vertices, &count, 0, height - panel_height, width, height, -1, -1, -1, -1, 0.02f, 0.025f, 0.04f, 0.95f);
        quad(vertices, &count, 0, height - 38, width, height, -1, -1, -1, -1, 0.07f, 0.09f, 0.13f, 1);
        size_t visible = panel_height > 60 ? (size_t)((panel_height - 60) / 20) : 0;
        if (visible > console->output_count) visible = console->output_count;
        for (size_t i = 0; i < visible; ++i) {
            const char *line = console->output[console->output_count - visible + i];
            text(ui, vertices, &count, line, strlen(line), 14, height - 50 - (float)(visible - 1 - i) * 20, width - 14);
        }
        /* Keep the cursor visible by scrolling the editable line horizontally. */
        size_t start = 0;
        float cursor_width = 0;
        const char *p = console->line;
        while ((size_t)(p - console->line) < console->cursor) cursor_width += ax_font_glyph(&ui->font, &p)->advance * 0.5f;
        while (cursor_width > width - 40 && start < console->cursor) {
            p = console->line + start;
            cursor_width -= ax_font_glyph(&ui->font, &p)->advance * 0.5f;
            start = (size_t)(p - console->line);
        }
        text(ui, vertices, &count, console->line + start, console->length - start, 14, height - 14, width - 14);
        if (fmod(glfwGetTime(), 1.0) < 0.6) quad(vertices, &count, 14 + cursor_width, height - 29, 15 + cursor_width, height - 11, -1,-1,-1,-1, 0.8f,0.9f,1,1);
    }
    VkCommandBuffer command = frame->command;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui->pipeline);
    VkViewport viewport = {0, 0, (float)frame->extent.width, (float)frame->extent.height, 0, 1};
    VkRect2D scissor = {{0,0}, frame->extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui->pipeline_layout, 0, 1, &ui->descriptor, 0, NULL);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &ui->vertices[frame->frame_index].handle, &offset);
    float extent[] = {width, height};
    vkCmdPushConstants(command, ui->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(extent), extent);
    vkCmdDraw(command, count, 1, 0, 0);
    return true;
}
bool ax_ui_create(AxUi *ui, AxRenderer *r, AxConsole *console)
{
    if (!ui || !r || !r->device || !console) return false;
    *ui = (AxUi){.renderer = r, .console = console};
    if (!create_atlas(ui) || !create_pipeline(ui)) goto fail;
    for (size_t i = 0; i < AX_RENDER_FRAMES; ++i)
        if (!ax_buffer_create(r, &ui->vertices[i], sizeof(AxUiVertex) * AX_UI_VERTICES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true)) goto fail;
    if (!ax_renderer_set_ui(r, record_ui, ui)) goto fail;
    return true;
fail:
    ax_ui_destroy(ui);
    return false;
}
void ax_ui_destroy(AxUi *ui)
{
    if (!ui || !ui->renderer) return;
    AxRenderer *r = ui->renderer;
    if (r->ui_node.user == ui) r->ui_node = (AxRenderNode){0};
    for (size_t i = 0; i < AX_RENDER_FRAMES; ++i) ax_buffer_destroy(r, &ui->vertices[i]);
    if (ui->pipeline) vkDestroyPipeline(r->device, ui->pipeline, NULL);
    if (ui->pipeline_layout) vkDestroyPipelineLayout(r->device, ui->pipeline_layout, NULL);
    if (ui->descriptor_pool) vkDestroyDescriptorPool(r->device, ui->descriptor_pool, NULL);
    if (ui->descriptor_layout) vkDestroyDescriptorSetLayout(r->device, ui->descriptor_layout, NULL);
    if (ui->sampler) vkDestroySampler(r->device, ui->sampler, NULL);
    if (ui->atlas_view) vkDestroyImageView(r->device, ui->atlas_view, NULL);
    if (ui->atlas) vkDestroyImage(r->device, ui->atlas, NULL);
    if (ui->atlas_memory) vkFreeMemory(r->device, ui->atlas_memory, NULL);
    *ui = (AxUi){0};
}
