#include "world_renderer.h"
#include "renderer/memory.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define AX_SHADOW_SIZE 1024u
#define AX_SHADOW_RANGE 128.0f
typedef enum { PIPELINE_SOLID, PIPELINE_LINES, PIPELINE_DEPTH } PipelineView;
typedef struct { VkImage image; VkDeviceMemory memory; VkImageView view; } Target;
#define AX_WORLD_FADE_SECONDS 0.25f
/** Longest frame step a fade advances by, so a stalled frame cannot skip the blend. */
#define AX_WORLD_FADE_MAX_STEP (1.0f/30.0f)
/** One uploaded terrain layout with the draw table that places it. */
typedef struct {
    AxBuffer vertices, indices, edges;
    AxWorldRegion *regions;
    size_t count;
} Cut;
/** Slot 0 renders the current cut; slot 1 renders the outgoing cut while a fade blends between them. */
enum { SLOT_CURRENT, SLOT_PREVIOUS, SLOT_COUNT };
typedef struct {
    Cut cuts[SLOT_COUNT];
    AxBuffer regions[SLOT_COUNT][AX_RENDER_FRAMES];
    Target color[SLOT_COUNT], depth, shadow;
    bool previous_readable;
    VkExtent2D extent;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool pool;
    VkDescriptorSet sets[SLOT_COUNT][AX_RENDER_FRAMES];
    VkPipelineLayout layout;
    VkPipeline terrain, shadow_pipeline, sky, composite, wireframe, depth_only;
    VkSampler color_sampler, shadow_sampler;
} Device;
typedef struct { float offset_extent[4]; uint32_t identity[4]; } GpuRegion;
typedef struct { float forward[4],right[4],up[4],sun[4],local_up[4],light_right[4],light_up[4]; uint32_t info[4]; } View;
_Static_assert(sizeof(View)==128,"Vulkan minimum push-constant budget");
_Static_assert(sizeof(GpuRegion)==32,"GLSL metadata layout");
static const uint32_t terrain_vertex[]=
#include "world.vert.inc"
;
static const uint32_t terrain_fragment[]=
#include "world.frag.inc"
;
static const uint32_t shadow_fragment[]=
#include "world_shadow.frag.inc"
;
static const uint32_t fullscreen_vertex[]=
#include "world_sky.vert.inc"
;
static const uint32_t sky_fragment[]=
#include "world_sky.frag.inc"
;
static const uint32_t composite_fragment[]=
#include "world_composite.frag.inc"
;
/** Releases an idle attachment, including partial allocation. */
static void destroy_target(AxRenderer *r,Target *target)
{
    if (target->view) vkDestroyImageView(r->device,target->view,NULL);
    if (target->image) vkDestroyImage(r->device,target->image,NULL);
    if (target->memory) vkFreeMemory(r->device,target->memory,NULL);
    *target=(Target){0};
}
/** Allocates an optimal-tiled color/depth attachment with an explicit aspect. */
static bool create_target(AxRenderer *r,VkFormat format,VkExtent2D extent,VkImageUsageFlags usage,VkImageAspectFlags aspect,Target *target)
{
    VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
        .extent={extent.width,extent.height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateImage(r->device,&image,NULL,&target->image)!=VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(r->device,target->image,&requirements);
    uint32_t type=ax_memory_type(r->physical,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type==UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};
    if (vkAllocateMemory(r->device,&allocation,NULL,&target->memory)!=VK_SUCCESS || vkBindImageMemory(r->device,target->image,target->memory,0)!=VK_SUCCESS) return false;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=target->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={aspect,0,1,0,1}};
    return vkCreateImageView(r->device,&view,NULL,&target->view)==VK_SUCCESS;
}
/** Creates a dynamic-rendering pipeline for indexed terrain, shadow depth, sky, or final compositing.
 * Shadows cast from light-facing-away surfaces: closed terrain then never shadows its own lit side at creases.
 */
static VkPipeline pipeline(AxRenderer *r,Device *gpu,const uint32_t *vert,size_t vert_bytes,const uint32_t *frag,size_t frag_bytes,
                            bool mesh,bool shadow,bool offscreen,PipelineView debug)
{
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;
    VkPipeline result=VK_NULL_HANDLE;
    VkShaderModuleCreateInfo module={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=vert_bytes,.pCode=vert};
    if (vkCreateShaderModule(r->device,&module,NULL,&vertex)!=VK_SUCCESS) return VK_NULL_HANDLE;
    module.codeSize=frag_bytes; module.pCode=frag;
    if (vkCreateShaderModule(r->device,&module,NULL,&fragment)!=VK_SUCCESS) goto cleanup;
    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkVertexInputBindingDescription binding={0,sizeof(AxSurfaceVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[]={
        {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(AxSurfaceVertex,position)},
        {1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(AxSurfaceVertex,normal)},
        {2,0,VK_FORMAT_R32_UINT,offsetof(AxSurfaceVertex,color)}};
    VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=mesh ? 1 : 0,.pVertexBindingDescriptions=&binding,
        .vertexAttributeDescriptionCount=mesh ? 3 : 0,.pVertexAttributeDescriptions=attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=debug==PIPELINE_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.depthBiasEnable=debug==PIPELINE_LINES,.depthBiasConstantFactor=1,.cullMode=shadow ? VK_CULL_MODE_FRONT_BIT : mesh && debug!=PIPELINE_LINES ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=mesh,.depthWriteEnable=mesh && debug!=PIPELINE_LINES,.depthCompareOp=shadow ? VK_COMPARE_OP_LESS : debug==PIPELINE_LINES ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_GREATER};
    VkPipelineColorBlendAttachmentState attachment={.colorWriteMask=debug==PIPELINE_DEPTH ? 0 : VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=shadow ? 0 : 1,.pAttachments=&attachment};
    VkDynamicState dynamic_states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamic_states};
    VkPipelineRenderingCreateInfoKHR rendering={.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount=shadow ? 0 : 1,.pColorAttachmentFormats=&r->format,.depthAttachmentFormat=offscreen ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.pNext=&rendering,
        .stageCount=2,.pStages=stages,.pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport,
        .pRasterizationState=&raster,.pMultisampleState=&samples,.pDepthStencilState=&depth,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=gpu->layout};
    if (vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&info,NULL,&result)!=VK_SUCCESS) result=VK_NULL_HANDLE;
cleanup:
    if (fragment) vkDestroyShaderModule(r->device,fragment,NULL);
    vkDestroyShaderModule(r->device,vertex,NULL);
    return result;
}
/** Releases a cut's buffers and draw table; the device must be idle. Accepts zero state. */
static void destroy_cut(AxRenderer *r,Cut *cut)
{
    ax_buffer_destroy(r,&cut->vertices); ax_buffer_destroy(r,&cut->indices); ax_buffer_destroy(r,&cut->edges);
    free(cut->regions);
    *cut=(Cut){0};
}
/** Uploads both candidate buffers before touching the current render asset. */
static bool upload(AxRenderer *r,const AxWorldGeometry *geometry,AxBuffer *vertices,AxBuffer *indices)
{
    if (!geometry || !geometry->count || geometry->count>AX_WORLD_MAX_REGIONS ||
        geometry->vertex_count>INT32_MAX || geometry->index_count>UINT32_MAX ||
        geometry->vertex_count>SIZE_MAX/sizeof(AxSurfaceVertex) || geometry->index_count>SIZE_MAX/sizeof(uint32_t) ||
        (geometry->vertex_count && !geometry->vertices) || (geometry->index_count && !geometry->indices)) return false;
    for (size_t i=0;i<geometry->count;++i) {
        const AxWorldRegion *region=&geometry->regions[i];
        if (region->first_index>geometry->index_count || region->index_count>geometry->index_count-region->first_index ||
            region->vertex_offset<0 || (size_t)region->vertex_offset>geometry->vertex_count || !isfinite(region->extent) || region->extent<=0) return false;
        for (size_t j=region->first_index;j<(size_t)region->first_index+region->index_count;++j)
            if (geometry->indices[j]>=geometry->vertex_count-(size_t)region->vertex_offset) return false;
    }
    const AxSurfaceVertex dummy_vertex={0};
    const uint32_t dummy_index=0;
    return ax_buffer_upload(r,vertices,geometry->vertex_count ? geometry->vertices : &dummy_vertex,
               (geometry->vertex_count ? geometry->vertex_count : 1)*sizeof(AxSurfaceVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
           ax_buffer_upload(r,indices,geometry->index_count ? geometry->indices : &dummy_index,
               (geometry->index_count ? geometry->index_count : 1)*sizeof(uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}
/** Prepares a complete cut, including its own draw table, so it outlives the geometry it was built from. */
static bool prepare_cut(AxRenderer *r,const AxWorldGeometry *geometry,bool edges,Cut *cut);
/** Uploads portable triangle-edge indices without requiring optional polygon-mode features. */
static bool upload_edges(AxRenderer *r,const AxWorldGeometry *geometry,AxBuffer *edges)
{
    if (geometry->index_count>UINT32_MAX/2 || geometry->index_count>SIZE_MAX/(2*sizeof(uint32_t))) return false;
    size_t count=geometry->index_count*2;
    uint32_t *data=malloc((count ? count : 1)*sizeof(*data));
    if (!data) return false;
    for (size_t i=0;i<geometry->index_count;i+=3) for (size_t k=0;k<3;++k) {
        data[i*2+k*2]=geometry->indices[i+k]; data[i*2+k*2+1]=geometry->indices[i+(k+1)%3];
    }
    if (!count) data[0]=0;
    bool ok=ax_buffer_upload(r,edges,data,(count ? count : 1)*sizeof(*data),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    free(data); return ok;
}
static bool prepare_cut(AxRenderer *r,const AxWorldGeometry *geometry,bool edges,Cut *cut)
{
    *cut=(Cut){0};
    if (!upload(r,geometry,&cut->vertices,&cut->indices) || (edges && !upload_edges(r,geometry,&cut->edges))) { destroy_cut(r,cut); return false; }
    cut->regions=malloc(geometry->count*sizeof(*cut->regions));
    if (!cut->regions) { destroy_cut(r,cut); return false; }
    memcpy(cut->regions,geometry->regions,geometry->count*sizeof(*cut->regions));
    cut->count=geometry->count;
    return true;
}
/** Frees a device-owned world node, accepting incomplete preparation. */
static void destroy_device(AxRenderer *r,void *user,void *state)
{
    (void)user;
    Device *gpu=state;
    if (!gpu) return;
    if (gpu->wireframe) vkDestroyPipeline(r->device,gpu->wireframe,NULL);
    if (gpu->depth_only) vkDestroyPipeline(r->device,gpu->depth_only,NULL);
    if (gpu->terrain) vkDestroyPipeline(r->device,gpu->terrain,NULL);
    if (gpu->shadow_pipeline) vkDestroyPipeline(r->device,gpu->shadow_pipeline,NULL);
    if (gpu->sky) vkDestroyPipeline(r->device,gpu->sky,NULL);
    if (gpu->composite) vkDestroyPipeline(r->device,gpu->composite,NULL);
    if (gpu->layout) vkDestroyPipelineLayout(r->device,gpu->layout,NULL);
    if (gpu->pool) vkDestroyDescriptorPool(r->device,gpu->pool,NULL);
    if (gpu->descriptor_layout) vkDestroyDescriptorSetLayout(r->device,gpu->descriptor_layout,NULL);
    if (gpu->color_sampler) vkDestroySampler(r->device,gpu->color_sampler,NULL);
    if (gpu->shadow_sampler) vkDestroySampler(r->device,gpu->shadow_sampler,NULL);
    destroy_target(r,&gpu->depth); destroy_target(r,&gpu->shadow);
    for (size_t slot=0; slot<SLOT_COUNT; ++slot) {
        destroy_target(r,&gpu->color[slot]); destroy_cut(r,&gpu->cuts[slot]);
        for (size_t i=0; i<AX_RENDER_FRAMES; ++i) ax_buffer_destroy(r,&gpu->regions[slot][i]);
    }
    free(gpu);
}
/** Recreates all world pipelines and CPU-derived geometry during startup/device migration. */
static bool create_device(AxRenderer *r,void *user,void **state)
{
    AxWorldRenderer *world=user;
    Device *gpu=calloc(1,sizeof(*gpu));
    *state=gpu;
    world->fade=0;
    if (!gpu || !prepare_cut(r,world->geometry,world->view==AX_WORLD_VIEW_WIREFRAME,&gpu->cuts[SLOT_CURRENT])) return false;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(r->physical,VK_FORMAT_D32_SFLOAT,&format);
    if ((format.optimalTilingFeatures&(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))!=
        (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return false;
    VkDescriptorSetLayoutBinding bindings[]={
        {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=2,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=3,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
    VkDescriptorSetLayoutCreateInfo set_layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
    if (vkCreateDescriptorSetLayout(r->device,&set_layout,NULL,&gpu->descriptor_layout)!=VK_SUCCESS) return false;
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,SLOT_COUNT*AX_RENDER_FRAMES},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,SLOT_COUNT*AX_RENDER_FRAMES*3}};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=SLOT_COUNT*AX_RENDER_FRAMES,.poolSizeCount=2,.pPoolSizes=sizes};
    if (vkCreateDescriptorPool(r->device,&pool,NULL,&gpu->pool)!=VK_SUCCESS) return false;
    VkDescriptorSetLayout layouts[SLOT_COUNT*AX_RENDER_FRAMES];
    for (size_t i=0; i<SLOT_COUNT*AX_RENDER_FRAMES; ++i) layouts[i]=gpu->descriptor_layout;
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=gpu->pool,
        .descriptorSetCount=SLOT_COUNT*AX_RENDER_FRAMES,.pSetLayouts=layouts};
    if (vkAllocateDescriptorSets(r->device,&allocate,&gpu->sets[0][0])!=VK_SUCCESS) return false;
    for (size_t slot=0; slot<SLOT_COUNT; ++slot) for (size_t i=0; i<AX_RENDER_FRAMES; ++i) {
        if (!ax_buffer_create(r,&gpu->regions[slot][i],AX_WORLD_MAX_REGIONS*sizeof(GpuRegion),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)) return false;
        VkDescriptorBufferInfo buffer={gpu->regions[slot][i].handle,0,VK_WHOLE_SIZE};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=gpu->sets[slot][i],.dstBinding=0,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer};
        vkUpdateDescriptorSets(r->device,1,&write,0,NULL);
    }
    VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(View)};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,
        .pSetLayouts=&gpu->descriptor_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if (vkCreatePipelineLayout(r->device,&layout,NULL,&gpu->layout)!=VK_SUCCESS) return false;
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (vkCreateSampler(r->device,&sampler,NULL,&gpu->color_sampler)!=VK_SUCCESS) return false;
    sampler.compareEnable=VK_TRUE; sampler.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler.addressModeU=sampler.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; sampler.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(r->device,&sampler,NULL,&gpu->shadow_sampler)!=VK_SUCCESS) return false;
    if (!create_target(r,VK_FORMAT_D32_SFLOAT,(VkExtent2D){AX_SHADOW_SIZE,AX_SHADOW_SIZE},
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,&gpu->shadow)) return false;
    gpu->terrain=pipeline(r,gpu,terrain_vertex,sizeof(terrain_vertex),terrain_fragment,sizeof(terrain_fragment),true,false,true,0);
    gpu->shadow_pipeline=pipeline(r,gpu,terrain_vertex,sizeof(terrain_vertex),shadow_fragment,sizeof(shadow_fragment),true,true,true,0);
    gpu->sky=pipeline(r,gpu,fullscreen_vertex,sizeof(fullscreen_vertex),sky_fragment,sizeof(sky_fragment),false,false,true,0);
    gpu->composite=pipeline(r,gpu,fullscreen_vertex,sizeof(fullscreen_vertex),composite_fragment,sizeof(composite_fragment),false,false,false,0);
    gpu->wireframe=pipeline(r,gpu,terrain_vertex,sizeof(terrain_vertex),terrain_fragment,sizeof(terrain_fragment),true,false,true,PIPELINE_LINES);
    gpu->depth_only=pipeline(r,gpu,terrain_vertex,sizeof(terrain_vertex),shadow_fragment,sizeof(shadow_fragment),true,false,true,PIPELINE_DEPTH);
    return gpu->wireframe && gpu->depth_only && gpu->terrain && gpu->shadow_pipeline && gpu->sky && gpu->composite;
}
/** Stages resize attachments before retiring old targets and rewriting descriptors while idle. */
static bool ensure_targets(AxWorldRenderer *world,Device *gpu,VkExtent2D extent)
{
    if (gpu->extent.width==extent.width && gpu->extent.height==extent.height) return true;
    AxRenderer *r=world->renderer;
    Target color[SLOT_COUNT]={{0}},depth={0};
    bool created=create_target(r,VK_FORMAT_D32_SFLOAT,extent,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,&depth);
    for (size_t slot=0; created && slot<SLOT_COUNT; ++slot)
        created=create_target(r,r->format,extent,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT,&color[slot]);
    if (!created || vkDeviceWaitIdle(r->device)!=VK_SUCCESS) {
        destroy_target(r,&depth);
        for (size_t slot=0; slot<SLOT_COUNT; ++slot) destroy_target(r,&color[slot]);
        return false;
    }
    destroy_target(r,&gpu->depth); gpu->depth=depth;
    for (size_t slot=0; slot<SLOT_COUNT; ++slot) { destroy_target(r,&gpu->color[slot]); gpu->color[slot]=color[slot]; }
    gpu->extent=extent; gpu->previous_readable=false;
    for (size_t slot=0; slot<SLOT_COUNT; ++slot) for (size_t i=0; i<AX_RENDER_FRAMES; ++i) {
        VkDescriptorImageInfo images[]={
            {gpu->shadow_sampler,gpu->shadow.view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
            {gpu->color_sampler,gpu->color[SLOT_CURRENT].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gpu->color_sampler,gpu->color[SLOT_PREVIOUS].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[3];
        for (uint32_t j=0; j<3; ++j) writes[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=gpu->sets[slot][i],.dstBinding=j+1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&images[j]};
        vkUpdateDescriptorSets(r->device,3,writes,0,NULL);
    }
    return true;
}
/** Writes camera-relative translations only into the current fence-protected frame slot. */
static bool update_regions(const AxWorldRenderer *world,Device *gpu,size_t slot,uint32_t frame)
{
    AxPosition eye=world->view_origin;
    double move[3]={world->camera->position[0],world->camera->position[1],world->camera->position[2]};
    if (!ax_position_move(&eye,move)) return false;
    GpuRegion *out=gpu->regions[slot][frame].mapped;
    const Cut *cut=&gpu->cuts[slot];
    for (size_t i=0; i<cut->count; ++i) {
        const AxWorldRegion *region=&cut->regions[i];
        AxPosition local=eye;
        double shift[3],relative[3];
        for (size_t k=0; k<3; ++k) shift[k]=world->frame_center[k]-region->center[k];
        if (!ax_position_move(&local,shift)) return false;
        ax_position_difference(&region->origin,&local,relative);
        for (size_t k=0; k<3; ++k) out[i].offset_extent[k]=(float)relative[k];
        out[i].offset_extent[3]=region->extent;
        out[i].identity[0]=(uint32_t)region->planet; out[i].identity[1]=(uint32_t)(region->planet>>32);
        out[i].identity[2]=out[i].identity[3]=0;
    }
    return true;
}
/** Creates shared camera, lighting, atmosphere, shadow and infinite reverse-Z projection constants.
 * Light-plane offsets keep shadow texels fixed in the world, so shadow edges do not crawl as the camera moves.
 */
static View view_constants(const AxWorldRenderer *world,VkExtent2D extent)
{
    View view={0};
    ax_camera_basis(world->camera,view.forward,view.right,view.up);
    view.forward[3]=tanf(world->camera->fov_y*0.5f); view.right[3]=(float)extent.width/extent.height; view.up[3]=0.05f;
    memcpy(view.sun,world->sun,3*sizeof(float)); view.sun[3]=world->atmosphere_height;
    memcpy(view.local_up,world->local_up,3*sizeof(float)); view.local_up[3]=world->altitude;
    float horizontal=hypotf(world->sun[0],world->sun[2]);
    if (horizontal<1e-6f) { view.light_right[0]=1; }
    else { view.light_right[0]=world->sun[2]/horizontal; view.light_right[2]=-world->sun[0]/horizontal; }
    view.light_up[0]=world->sun[1]*view.light_right[2];
    view.light_up[1]=world->sun[2]*view.light_right[0]-world->sun[0]*view.light_right[2];
    view.light_up[2]=-world->sun[1]*view.light_right[0];
    view.info[1]=(uint32_t)AX_SHADOW_RANGE;
    AxPosition eye=world->view_origin;
    double move[3]={world->camera->position[0],world->camera->position[1],world->camera->position[2]},center[3];
    const AxPosition zero={0};
    if (ax_position_move(&eye,move)) {
        ax_position_difference(&eye,&zero,center);
        double texel=2.0*AX_SHADOW_RANGE/AX_SHADOW_SIZE,across=0,along=0;
        for (size_t k=0;k<3;++k) { across+=center[k]*view.light_right[k]; along+=center[k]*view.light_up[k]; }
        view.light_right[3]=(float)(across-round(across/texel)*texel);
        view.light_up[3]=(float)(along-round(along/texel)*texel);
    }
    view.info[3]=(world->shadows_enabled ? 1u : 0u)|((uint32_t)world->view<<1);
    return view;
}
/** Records an explicit image barrier between an attachment and shader reads. */
static void barrier(VkCommandBuffer command,Target *target,VkImageAspectFlags aspect,VkImageLayout old,VkImageLayout next,
                    VkAccessFlags src,VkAccessFlags dst,VkPipelineStageFlags from,VkPipelineStageFlags to)
{
    VkImageMemoryBarrier image={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,.oldLayout=old,.newLayout=next,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=target->image,.subresourceRange={aspect,0,1,0,1}};
    vkCmdPipelineBarrier(command,from,to,0,0,NULL,0,NULL,1,&image);
}
/** Binds one pipeline, frame descriptor and target viewport. */
static void bind(const AxRenderFrame *frame,Device *gpu,VkPipeline pipeline,VkExtent2D extent,size_t slot)
{
    VkViewport viewport={0,0,(float)extent.width,(float)extent.height,0,1};
    VkRect2D scissor={{0,0},extent};
    vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdSetViewport(frame->command,0,1,&viewport); vkCmdSetScissor(frame->command,0,1,&scissor);
    vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,gpu->layout,0,1,&gpu->sets[slot][frame->frame_index],0,NULL);
}
/** Draws one cut's disjoint chunk ranges using triangle or portable line-list indices. */
static void draw_meshes(const AxRenderFrame *frame,const Cut *cut,Device *gpu,View *view,bool lines)
{
    VkDeviceSize offset=0;
    vkCmdBindVertexBuffers(frame->command,0,1,&cut->vertices.handle,&offset);
    vkCmdBindIndexBuffer(frame->command,lines ? cut->edges.handle : cut->indices.handle,0,VK_INDEX_TYPE_UINT32);
    for (size_t i=0; i<cut->count; ++i) {
        const AxWorldRegion *region=&cut->regions[i];
        if (!region->index_count) continue;
        view->info[0]=(uint32_t)i;
        vkCmdPushConstants(frame->command,gpu->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(*view),view);
        vkCmdDrawIndexed(frame->command,region->index_count*(lines ? 2u : 1u),1,region->first_index*(lines ? 2u : 1u),region->vertex_offset,0);
    }
}
/** Renders one cut's own shadow depth, then its depth-tested world, into that slot's color target. */
static bool render_cut(const AxRenderFrame *frame,AxWorldRenderer *world,Device *gpu,size_t slot)
{
    if (!update_regions(world,gpu,slot,frame->frame_index)) return false;
    View view=view_constants(world,frame->extent);
    const Cut *cut=&gpu->cuts[slot];
    barrier(frame->command,&gpu->shadow,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    VkRenderingAttachmentInfoKHR depth={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,.imageView=gpu->shadow.view,
        .imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp=VK_ATTACHMENT_STORE_OP_STORE,.clearValue={.depthStencil={1,0}}};
    VkRenderingInfoKHR rendering={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,.renderArea={{0,0},{AX_SHADOW_SIZE,AX_SHADOW_SIZE}},
        .layerCount=1,.pDepthAttachment=&depth};
    frame->begin_rendering(frame->command,&rendering);
    bind(frame,gpu,gpu->shadow_pipeline,(VkExtent2D){AX_SHADOW_SIZE,AX_SHADOW_SIZE},slot);
    view.info[2]=1; if (world->shadows_enabled) draw_meshes(frame,cut,gpu,&view,false);
    frame->end_rendering(frame->command);
    barrier(frame->command,&gpu->shadow,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    barrier(frame->command,&gpu->color[slot],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    barrier(frame->command,&gpu->depth,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    VkRenderingAttachmentInfoKHR color={.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,.imageView=gpu->color[slot].view,
        .imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE};
    depth.imageView=gpu->depth.view; depth.clearValue.depthStencil.depth=0;
    rendering.renderArea.extent=frame->extent; rendering.colorAttachmentCount=1; rendering.pColorAttachments=&color;
    frame->begin_rendering(frame->command,&rendering);
    bind(frame,gpu,gpu->sky,frame->extent,slot);
    view.info[2]=0;
    vkCmdPushConstants(frame->command,gpu->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(view),&view);
    vkCmdDraw(frame->command,3,1,0,0);
    if (world->view==AX_WORLD_VIEW_WIREFRAME) {
        bind(frame,gpu,gpu->depth_only,frame->extent,slot); draw_meshes(frame,cut,gpu,&view,false);
        bind(frame,gpu,gpu->wireframe,frame->extent,slot); draw_meshes(frame,cut,gpu,&view,true);
    } else { bind(frame,gpu,gpu->terrain,frame->extent,slot); draw_meshes(frame,cut,gpu,&view,false); }
    frame->end_rendering(frame->command);
    barrier(frame->command,&gpu->color[slot],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    return true;
}
/** Renders the current cut and, during a fade, the outgoing cut, before the shared swapchain/UI scope opens. */
static bool prepass(const AxRenderFrame *frame,void *user)
{
    AxWorldRenderer *world=user;
    Device *gpu=frame->device_state;
    if (!gpu || !ensure_targets(world,gpu,frame->extent)) return false;
    if (!gpu->previous_readable) {
        barrier(frame->command,&gpu->color[SLOT_PREVIOUS],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            0,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        gpu->previous_readable=true;
    }
    if (!render_cut(frame,world,gpu,SLOT_CURRENT)) return false;
    return !(world->fade>0 && gpu->cuts[SLOT_PREVIOUS].count) || render_cut(frame,world,gpu,SLOT_PREVIOUS);
}
/** Composites the completed world image into the swapchain before UI, blending in the outgoing cut during a fade. */
static bool record(const AxRenderFrame *frame,void *user)
{
    const AxWorldRenderer *world=user;
    Device *gpu=frame->device_state;
    if (!gpu || !gpu->color[SLOT_CURRENT].view) return false;
    float previous=gpu->cuts[SLOT_PREVIOUS].count ? world->fade : 0;
    bind(frame,gpu,gpu->composite,frame->extent,SLOT_CURRENT);
    vkCmdPushConstants(frame->command,gpu->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(previous),&previous);
    vkCmdDraw(frame->command,3,1,0,0);
    return true;
}
bool ax_world_renderer_create(AxWorldRenderer *world,AxRenderer *renderer,const AxWorldGeometry *geometry,const AxCamera *camera)
{
    if (!world || !renderer || !renderer->device || !geometry || !camera) return false;
    *world=(AxWorldRenderer){.renderer=renderer,.camera=camera,.geometry=geometry,.shadows_enabled=true,.local_up={0,1,0},.sun={0.42f,0.78f,0.46f}};
    float length=sqrtf(0.42f*0.42f+0.78f*0.78f+0.46f*0.46f);
    for (size_t i=0; i<3; ++i) world->sun[i]/=length;
    AxRenderNode node={.name="world",.record=record,.prepass=prepass,.user=world,.enabled=true,.create_device=create_device,.destroy_device=destroy_device};
    if (ax_renderer_register_node(renderer,&node)) return true;
    *world=(AxWorldRenderer){0}; return false;
}
/** Installs prepared geometry between frames; keeps the current cut if preparation fails.
 * @param fade Retains the current cut as the outgoing side of a blend; otherwise it is released at once.
 */
static bool publish(AxWorldRenderer *world,const AxWorldGeometry *geometry,bool fade)
{
    if (!world || !world->renderer) return false;
    AxRenderer *r=world->renderer;
    if (r->recording || r->changing || r->failed) return false;
    Device *gpu=NULL;
    for (size_t i=0; i<r->node_count; ++i) if (r->nodes[i].user==world && r->nodes[i].record==record) gpu=r->nodes[i].device_state;
    if (!gpu) return false;
    Cut next;
    if (!prepare_cut(r,geometry,world->view==AX_WORLD_VIEW_WIREFRAME,&next)) return false;
    if (vkDeviceWaitIdle(r->device)!=VK_SUCCESS) { destroy_cut(r,&next); return false; }
    destroy_cut(r,&gpu->cuts[SLOT_PREVIOUS]);
    if (fade) gpu->cuts[SLOT_PREVIOUS]=gpu->cuts[SLOT_CURRENT];
    else destroy_cut(r,&gpu->cuts[SLOT_CURRENT]);
    gpu->cuts[SLOT_CURRENT]=next;
    world->geometry=geometry;
    world->fade=fade ? 1 : 0;
    return true;
}
bool ax_world_renderer_set_geometry(AxWorldRenderer *world,const AxWorldGeometry *geometry)
{
    return publish(world,geometry,false);
}
bool ax_world_renderer_fade_geometry(AxWorldRenderer *world,const AxWorldGeometry *geometry)
{
    return publish(world,geometry,world && world->view==AX_WORLD_VIEW_DEFAULT);
}
void ax_world_renderer_advance(AxWorldRenderer *world,double dt)
{
    if (!world || !(world->fade>0) || !isfinite(dt) || dt<=0) return;
    world->fade=fmaxf(0,world->fade-fminf((float)dt,AX_WORLD_FADE_MAX_STEP)/AX_WORLD_FADE_SECONDS);
}
void ax_world_renderer_destroy(AxWorldRenderer *world)
{
    if (!world || !world->renderer) return;
    ax_renderer_remove_node(world->renderer,"world"); *world=(AxWorldRenderer){0};
}

bool ax_world_renderer_set_view(AxWorldRenderer *world,AxWorldView view)
{
    if (!world || !world->renderer || view<AX_WORLD_VIEW_DEFAULT || view>AX_WORLD_VIEW_NORMALS) return false;
    AxRenderer *r=world->renderer;
    if (r->recording || r->changing || r->failed) return false;
    Device *gpu=NULL;
    for (size_t i=0;i<r->node_count;++i) if (r->nodes[i].user==world && r->nodes[i].record==record) gpu=r->nodes[i].device_state;
    if (!gpu) return false;
    if (view==AX_WORLD_VIEW_WIREFRAME && !gpu->cuts[SLOT_CURRENT].edges.handle && !upload_edges(r,world->geometry,&gpu->cuts[SLOT_CURRENT].edges)) return false;
    world->view=view; world->fade=0; return true;
}
