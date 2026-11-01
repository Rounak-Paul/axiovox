#include "world/world_renderer.h"
#include "world/mesh_optimizer.h"
#include "renderer/memory.h"
#include <string.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); goto cleanup; } } while (0)
/** Draws frames, pumping window events so resizes reach the renderer. */
static bool draw(AxRenderer *renderer, int frames)
{
    for (int i = 0; i < frames; ++i) {
        glfwPollEvents();
        if (!ax_renderer_draw(renderer)) return false;
    }
    return true;
}
typedef struct {
    AxRenderer *renderer;
    AxRenderNode world;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    AxBuffer pixels;
} Capture;
/** Renders the actual world pipeline into a readable image after its normal prepass. */
static bool capture_frame(const AxRenderFrame *frame, void *user)
{
    Capture *capture = user;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = capture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,NULL,0,NULL,1,&barrier);
    VkRenderingAttachmentInfoKHR attachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = capture->view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfoKHR rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = {{0,0}, frame->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attachment};
    frame->begin_rendering(frame->command, &rendering);
    AxRenderFrame target = *frame;
    target.device_state = capture->world.device_state;
    bool success = capture->world.record(&target, capture->world.user);
    frame->end_rendering(frame->command);
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageExtent = {frame->extent.width, frame->extent.height, 1}};
    vkCmdCopyImageToBuffer(frame->command, capture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture->pixels.handle,1,&copy);
    VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    return success;
}
/** Keeps the capture node in the ordered pipeline; all work happens in its prepass. */
static bool capture_record(const AxRenderFrame *frame, void *user)
{
    (void)frame;
    (void)user;
    return true;
}
/** Creates an offscreen target matching the real world pipeline and framebuffer. */
static bool create_capture(Capture *capture, AxRenderer *r)
{
    capture->renderer = r;
    for (size_t i=0;i<r->node_count;++i) if (!strcmp(r->nodes[i].name,"world")) capture->world = r->nodes[i];
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = r->format, .extent = {r->extent.width,r->extent.height,1}, .mipLevels = 1,.arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateImage(r->device,&image,NULL,&capture->image) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(r->device,capture->image,&requirements);
    uint32_t type = ax_memory_type(r->physical,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size,.memoryTypeIndex=type};
    if (vkAllocateMemory(r->device,&allocation,NULL,&capture->memory) != VK_SUCCESS ||
        vkBindImageMemory(r->device,capture->image,capture->memory,0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=capture->image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=r->format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    return vkCreateImageView(r->device,&view,NULL,&capture->view) == VK_SUCCESS &&
        ax_buffer_create(r,&capture->pixels,(VkDeviceSize)r->extent.width*r->extent.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true) &&
        ax_renderer_register_node(r,&(AxRenderNode){.name="capture",.user=capture,.enabled=true,.prepass=capture_frame,.record=capture_record});
}
/** Releases offscreen validation resources after completion. */
static void destroy_capture(Capture *capture)
{
    AxRenderer *r = capture->renderer;
    if (!r) return;
    ax_renderer_remove_node(r,"capture");
    if (capture->view) vkDestroyImageView(r->device,capture->view,NULL);
    if (capture->image) vkDestroyImage(r->device,capture->image,NULL);
    if (capture->memory) vkFreeMemory(r->device,capture->memory,NULL);
    ax_buffer_destroy(r,&capture->pixels);
    *capture = (Capture){0};
}
/** Verifies rasterized planets, edits, targets, depth, resize, atomic publication, and GPU migration. */
int main(void)
{
    AxEvents events={0};
    AxWindow window={0};
    AxRenderer renderer={0};
    AxWorldRenderer world={0};
    AxCamera camera;
    Capture capture={0};
    AxScene scene={0};
    AxSurfaceRegion bodies[3]={0};
    AxWorldGeometry geometry={0},refined={0};
    int status=EXIT_FAILURE;
    AxMeshStats mesh_stats;
    AxPlanetDesc terra={.id=1,.radius=10};
    AxPlanetDesc luna={.id=2,.center={25,0,0},.radius=4,.surface_material=AX_MATERIAL_ROCK};
    CHECK(ax_scene_add_planet(&scene,&terra) && ax_scene_add_planet(&scene,&luna));
    AxPosition origin;
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,-16,-16}));
    CHECK(ax_surface_build(ax_scene_planet(&scene,1),&origin,32,32,false,&bodies[0]));
    CHECK(ax_mesh_optimize(&bodies[0].mesh,32,1,NULL,&mesh_stats));
    CHECK(ax_position_from_meters(&origin,(double[3]){-8,-8,-8}));
    CHECK(ax_surface_build(ax_scene_planet(&scene,2),&origin,16,32,false,&bodies[1]));
    CHECK(ax_mesh_optimize(&bodies[1].mesh,16,0.5f,NULL,&mesh_stats));
    CHECK(ax_world_geometry_build(&scene,bodies,2,&geometry));
    ax_camera_init(&camera,(float[3]){0,0,0},0,0,NULL);
    CHECK(ax_window_create(&window,&events,320,240));
    CHECK(ax_renderer_create(&renderer,&window,&events,-1));
    CHECK(ax_world_renderer_create(&world,&renderer,&geometry,&camera));
    CHECK(ax_position_from_meters(&world.view_origin,(double[3]){0,0,40}));
    CHECK(draw(&renderer,3));
    VkExtent2D before=renderer.extent;
    glfwSetWindowSize(window.handle,400,300);
    CHECK(draw(&renderer,4));
    CHECK(renderer.extent.width!=before.width && renderer.extent.height!=before.height);
    CHECK(!ax_world_renderer_set_geometry(&world,&(AxWorldGeometry){0}));
    CHECK(world.geometry==&geometry);
    CHECK(create_capture(&capture,&renderer));
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    uint8_t *pixels=capture.pixels.mapped;
    size_t center=((size_t)renderer.extent.height/2*renderer.extent.width+renderer.extent.width/2)*4;
    CHECK(pixels[center]+pixels[center+1]+pixels[center+2]>90);
    CHECK(pixels[0]+pixels[1]+pixels[2]<90);
    float tangent=tanf(camera.fov_y*0.5f);
    size_t moon_x=(size_t)((0.625f/(tangent*renderer.extent.width/renderer.extent.height)+1)*0.5f*renderer.extent.width);
    size_t moon=((size_t)renderer.extent.height/2*renderer.extent.width+moon_x)*4;
    CHECK(moon_x<renderer.extent.width && pixels[moon]+pixels[moon+1]+pixels[moon+2]>90);
    int surface_green=2*pixels[center+1]-pixels[center]-pixels[center+2];
    CHECK(surface_green>30);
    double floor_x=-1.0,floor_y=-2.0,floor_z=sqrt(100-floor_x*floor_x-floor_y*floor_y);
    double distance=40-floor_z;
    size_t shadow_x=(size_t)((floor_x/(distance*tangent*renderer.extent.width/renderer.extent.height)+1)*0.5*renderer.extent.width);
    size_t shadow_y=(size_t)((-floor_y/(distance*tangent)+1)*0.5*renderer.extent.height);
    size_t shadow_pixel=(shadow_y*renderer.extent.width+shadow_x)*4;
    AxVolumeEdit block={.half_extent={1,1,1},.shape=AX_BRUSH_BOX,.material=AX_MATERIAL_BUILDING};
    CHECK(ax_position_from_meters(&block.position,(double[3]){0,0,11}));
    CHECK(ax_scene_edit(&scene,1,&block));
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,-16,-16}));
    CHECK(ax_surface_build(ax_scene_planet(&scene,1),&origin,32,32,false,&bodies[0]));
    CHECK(ax_mesh_optimize(&bodies[0].mesh,32,1,NULL,&mesh_stats));
    CHECK(ax_world_geometry_build(&scene,bodies,2,&refined));
    CHECK(ax_world_renderer_set_geometry(&world,&refined));
    ax_world_geometry_destroy(&geometry); geometry=refined; refined=(AxWorldGeometry){0}; world.geometry=&geometry;
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    int shadowed=pixels[shadow_pixel]+pixels[shadow_pixel+1]+pixels[shadow_pixel+2];
    world.shadows_enabled=false;
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    int unshadowed=pixels[shadow_pixel]+pixels[shadow_pixel+1]+pixels[shadow_pixel+2];
    world.shadows_enabled=true;
    CHECK(shadowed+25<unshadowed);
    AxVolumeEdit excavation={.half_extent={3,3,3},.material=AX_MATERIAL_ROCK,.remove=true};
    CHECK(ax_position_from_meters(&excavation.position,(double[3]){0,0,10}));
    CHECK(ax_scene_edit(&scene,1,&excavation));
    CHECK(ax_position_from_meters(&origin,(double[3]){-4,-4,6}));
    CHECK(ax_surface_build(ax_scene_planet(&scene,1),&origin,8,16,true,&bodies[2]));
    CHECK(ax_mesh_optimize(&bodies[2].mesh,8,0.5f,NULL,&mesh_stats));
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,-16,-16}));
    CHECK(ax_surface_build(ax_scene_planet(&scene,1),&origin,32,32,false,&bodies[0]));
    CHECK(ax_mesh_optimize(&bodies[0].mesh,32,1,(float[4]){12,12,22,8},&mesh_stats));
    CHECK(ax_world_geometry_build(&scene,bodies,3,&refined));
    CHECK(ax_world_renderer_set_geometry(&world,&refined));
    ax_world_geometry_destroy(&geometry); geometry=refined; refined=(AxWorldGeometry){0}; world.geometry=&geometry;
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    int excavated_green=2*pixels[center+1]-pixels[center]-pixels[center+2];
    CHECK(abs(excavated_green)<30 && excavated_green<surface_green);
    CHECK(ax_position_from_meters(&world.view_origin,(double[3]){0,0,14}));
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    CHECK(pixels[center]+pixels[center+1]+pixels[center+2]>90);
    ax_camera_init(&camera,NULL,0,-1.56f,(double[3]){1,0,0});
    CHECK(ax_position_from_meters(&world.view_origin,(double[3]){40,0,0}));
    CHECK(draw(&renderer,2) && vkDeviceWaitIdle(renderer.device)==VK_SUCCESS);
    CHECK(pixels[center]+pixels[center+1]+pixels[center+2]>90);
    destroy_capture(&capture);
    CHECK(ax_renderer_switch_device(&renderer,-1) && draw(&renderer,3));
    status=EXIT_SUCCESS;
cleanup:
    if (renderer.device) vkDeviceWaitIdle(renderer.device);
    destroy_capture(&capture);
    ax_world_renderer_destroy(&world);
    ax_renderer_destroy(&renderer);
    ax_window_destroy(&window);
    ax_world_geometry_destroy(&geometry); ax_world_geometry_destroy(&refined);
    for (size_t i=0;i<3;++i) ax_surface_destroy(&bodies[i]);
    ax_scene_destroy(&scene);
    return status;
}
