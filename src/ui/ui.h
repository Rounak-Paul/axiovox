#pragma once
#include "console.h"
#include "hud.h"
#include "renderer/memory.h"
#define AX_UI_VERTICES 65536
typedef struct {
    AxRenderer *renderer;
    AxConsole *console;
    AxFont font;
    AxHud hud;
    VkImage atlas;
    VkDeviceMemory atlas_memory;
    VkImageView atlas_view;
    VkSampler sampler;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    AxBuffer vertices[AX_RENDER_FRAMES];
} AxUi;
/** Creates the atlas/pipeline/buffers and registers the final UI render node. */
bool ax_ui_create(AxUi *ui, AxRenderer *renderer, AxConsole *console);
/** Destroys UI resources after the renderer is idle and before renderer destruction. */
void ax_ui_destroy(AxUi *ui);

/** Updates HUD input and feedback before recording a frame; owner-thread only. */
void ax_ui_update(AxUi *ui, double time);

/** Connects menu editing/persistence to the application's config; all arguments are borrowed. */
void ax_ui_bind_settings(AxUi *ui, AxConfig *config, const char *settings_path);
