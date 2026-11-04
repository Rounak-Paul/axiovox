#include "app/runtime.h"
#include "core/command.h"
#include "core/config.h"
#include "core/jobs.h"
#include "core/files.h"
#include "core/log.h"
#include "renderer/renderer.h"
#include "ui/ui.h"
#include "world/stream.h"
#include "world/gravity.h"
#include <math.h>
#include <errno.h>
#include <inttypes.h>
#include "world/world_renderer.h"
#include <stdio.h>
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct {
    AxConfig config;
    AxWindow window;
    AxInput input;
    AxConsole console;
    AxCommands commands;
    AxRenderer renderer;
    AxUi ui;
    AxCamera camera;
    AxScene scene;
    AxWorldStream stream;
    AxWorldGeometry geometry;
    AxPosition view_origin;
    double altitude;
    AxGravity gravity;
    AxPlanetId active_planet, nearby_planet;
    float flight_speed;
    AxWorldRenderer world_renderer;
    AxJobs *jobs;
    AxRuntime runtime;
    char settings_path[AX_PATH_CAPACITY];
} Application;
/** Publishes disposable surface assets while preserving the authoritative scene and camera frame.
 * @param fade Blends over the outgoing layout for streaming updates; false shows it at once, as for a teleport.
 */
static bool publish_world(Application *app,bool fade)
{
    AxWorldGeometry next = {0};
    if (!ax_world_geometry_build(&app->scene, app->stream.regions, app->stream.count, &next)) return false;
    if (app->world_renderer.renderer && !(fade ? ax_world_renderer_fade_geometry : ax_world_renderer_set_geometry)(&app->world_renderer, &next)) {
        ax_world_geometry_destroy(&next);
        return false;
    }
    ax_world_geometry_destroy(&app->geometry);
    app->geometry = next;
    app->world_renderer.geometry = &app->geometry;
    app->stream.dirty = false;
    return true;
}
/** Visits a body within the same scene, staging all terrain assets before committing the camera pose. */
static bool visit_planet(Application *app, AxPlanetId id)
{
    const AxPlanet *planet = ax_scene_planet(&app->scene, id);
    if (!planet) return false;
    double radius = ax_planet_description(planet)->radius;
    AxPosition eye;
    double direction[3]={0.35,0.5,-sqrt(0.6275)};
    double base[3];
    for (size_t k=0;k<3;++k) base[k]=direction[k]*radius;
    double height=ax_planet_terrain_height(planet,base);
    float arrival_yaw=0,arrival_pitch=-0.15f;
    if (ax_planet_description(planet)->terrain_kind==AX_TERRAIN_CONTINENTAL) {
        double score=-INFINITY,relief=ax_planet_description(planet)->terrain_height;
        for (unsigned i=0;i<96;++i) {
            double angle=i*2.399963229728653,y=-0.6+1.2*(i+0.5)/96;
            double candidate[3]={sqrt(1-y*y)*cos(angle),y,sqrt(1-y*y)*sin(angle)};
            for (size_t k=0;k<3;++k) base[k]=candidate[k]*radius;
            double h=ax_planet_terrain_height(planet,base);
            if (h<relief*0.03 || h>relief*0.3) continue;
            AxCamera orientation;
            ax_camera_init(&orientation,NULL,0,0,candidate);
            for (unsigned heading=0;heading<8;++heading) {
                orientation.yaw=(float)(heading*0.7853981633974483);
                float forward[3]; ax_camera_basis(&orientation,forward,NULL,NULL);
                double ahead[3];
                double distance=fmin(18000,radius*0.04);
                for (size_t k=0;k<3;++k) ahead[k]=base[k]+forward[k]*distance;
                double peak=ax_planet_terrain_height(planet,ahead);
                double near_distance=fmin(100,distance*0.05);
                for (size_t k=0;k<3;++k) ahead[k]=base[k]+forward[k]*near_distance;
                double near_height=ax_planet_terrain_height(planet,ahead);
                double grade=fabs(near_height-h)/near_distance;
                if (grade>0.6) continue;
                double interest=peak-h-fabs(h-relief*0.10)*0.5-grade*relief*0.3;
                if (interest>score) {
                    score=interest; height=h; arrival_yaw=orientation.yaw; arrival_pitch=0.02f;
                    memcpy(direction,candidate,sizeof(direction));
                }
            }
        }
    }
    for (size_t k=0;k<3;++k) base[k]=direction[k]*(radius+height+12);
    if (!ax_position_from_meters(&eye,base)) return false;
    AxWorldStream next = {0};
    if (!ax_world_stream_create(&next, &app->scene, id, &eye, app->jobs)) return false;
    AxWorldStream previous = app->stream;
    app->stream = next;
    if (!publish_world(app,false)) {
        app->stream = previous;
        ax_world_stream_destroy(&next, app->jobs);
        return false;
    }
    ax_world_stream_destroy(&previous, app->jobs);
    app->active_planet = id;
    app->view_origin = eye;
    ax_camera_init(&app->camera, (float[3]){0,0,0}, arrival_yaw,arrival_pitch,direction);
    return true;
}
/** Reconciles a small float flight displacement with the precise planet frame and updates continuous LOD streaming. */
static bool update_world(Application *app)
{
    double move[3] = {app->camera.position[0], app->camera.position[1], app->camera.position[2]};
    if (!ax_position_move(&app->view_origin, move)) return false;
    memset(app->camera.position, 0, sizeof(app->camera.position));
    const AxPlanet *anchor = ax_scene_planet(&app->scene, app->active_planet);
    if (!anchor) return false;
    const AxPlanetDesc *frame = ax_planet_description(anchor);
    app->world_renderer.view_origin = app->view_origin;
    memcpy(app->world_renderer.frame_center, frame->center, sizeof(frame->center));
    app->world_renderer.ocean_radius=0;
    double nearest = INFINITY;
    for (size_t i = 0; i < app->scene.count; ++i) {
        const AxPlanetDesc *body = ax_planet_description(app->scene.planets[i]);
        if (app->world_renderer.ocean_radius==0 && body->terrain_kind==AX_TERRAIN_CONTINENTAL) {
            memcpy(app->world_renderer.ocean_center,body->center,sizeof(body->center));
            app->world_renderer.ocean_radius=body->radius;
        }
        AxPosition local = app->view_origin;
        double delta[3];
        for (size_t k = 0; k < 3; ++k) delta[k] = frame->center[k] - body->center[k];
        if (!ax_position_move(&local, delta)) return false;
        const AxPosition zero = {0};
        ax_position_difference(&local, &zero, delta);
        double length = hypot(hypot(delta[0], delta[1]), delta[2]);
        double altitude = length - body->radius;
        if (fabs(altitude) < nearest) {
            nearest = fabs(altitude);
            app->nearby_planet = body->id;
            app->altitude = altitude-ax_planet_terrain_height(app->scene.planets[i],delta);
            app->world_renderer.altitude = (float)altitude;
            app->world_renderer.atmosphere_height = body->id == 1 ? (float)(body->radius * 0.01) : 0;
            for (size_t k = 0; k < 3; ++k) app->world_renderer.local_up[k] = length > 0 ? (float)(delta[k] / length) : (k == 1 ? 1 : 0);
        }
    }
    if (!ax_gravity_sample(&app->scene,app->active_planet,&app->view_origin,&app->gravity)) return false;
    if (!ax_world_stream_update(&app->stream, &app->scene, app->active_planet, &app->view_origin, app->jobs)) return false;
    if (app->stream.dirty && !(app->world_renderer.fade>0) && !publish_world(app,true)) return false;
    return true;
}
/** Parses one finite bounded brush parameter without accepting trailing text. */
static bool brush_number(const char *text, double *value)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    return !errno && end != text && !*end && isfinite(*value) && *value > 0 && *value <= 32;
}
/** Applies validated global scale and hotbar rows to the current HUD. */
static bool configure_hud(Application *app)
{
    return ax_hud_configure(&app->ui.hud, ax_config_get(&app->config, "ui.scale")->value,
                            ax_config_get(&app->config, "ui.hotbar.rows")->value);
}
/** Applies presentation/logging side effects after the runtime transaction has committed. */
static void config_changed(const AxEvent *event, void *user)
{
    Application *app = user;
    if (event->data.config.restart_required) {
        ax_log(AX_LOG_INFO, "%s changed; restart required", event->data.config.key);
    } else if (!strcmp(event->data.config.key, "log.level")) {
        ax_log_set_level((AxLogLevel)ax_config_get(&app->config, "log.level")->value);
    } else if (!strncmp(event->data.config.key, "ui.", 3)) {
        if (!configure_hud(app)) ax_log(AX_LOG_ERROR, "Invalid HUD configuration");
    } else if (app->window.handle && (!strcmp(event->data.config.key, "window.width") || !strcmp(event->data.config.key, "window.height"))) {
        int width, height;
        glfwGetWindowSize(app->window.handle, &width, &height);
        int desired_width = ax_config_get(&app->config, "window.width")->value;
        int desired_height = ax_config_get(&app->config, "window.height")->value;
        if (width != desired_width || height != desired_height)
            glfwSetWindowSize(app->window.handle, desired_width, desired_height);
    }
}
/** Logs the successfully selected device after logical device creation. */
static void device_selected(const AxEvent *event, void *user)
{
    (void)user;
    ax_log(AX_LOG_INFO, "Selected GPU %u: %s", event->data.device.index, event->data.device.name);
}
/** Writes command results to the console and logger. */
static void output(Application *app, const char *message)
{
    ax_console_print(&app->console, message);
    ax_log(AX_LOG_INFO, "%s", message);
}
/** Dispatches dotted setting access and engine commands with explicit argument validation. */
static bool command(int argc, const char *const *argv, void *user)
{
    Application *app = user;
    const AxSetting *setting = ax_config_get(&app->config, argv[0]);
    if (setting) {
        if (argc == 2 && !ax_config_set(&app->config, argv[0], argv[1])) {
            if (ax_config_error(&app->config)[0]) output(app, ax_config_error(&app->config));
            return false;
        }
        if (argc != 1 && argc != 2) return false;
        char result[256];
        snprintf(result, sizeof(result), "%s = %d%s", setting->key, setting->value,
                 setting->restart_required && app->config.restart_pending ? " (restart pending)" : "");
        output(app, result);
        return true;
    }
    if (!strcmp(argv[0], "world.planets") && argc == 1) {
        for (size_t i = 0; i < app->scene.count; ++i) {
            const AxPlanetDesc *d = ax_planet_description(app->scene.planets[i]);
            char message[160];
            snprintf(message, sizeof(message), "%s (%" PRIu64 ") radius %.0f m%s", ax_planet_name(d->id), d->id, d->radius,
                     d->id == app->active_planet ? " (active)" : "");
            output(app, message);
        }
        return true;
    }
    if (!strcmp(argv[0], "world.visit") && argc == 2) {
        char *end;
        errno = 0;
        uint64_t id = strtoull(argv[1], &end, 10);
        return argv[1][0] != '-' && end != argv[1] && !*end && !errno && visit_planet(app, id);
    }
    if ((!strcmp(argv[0], "world.refresh") || !strcmp(argv[0], "world.recenter")) && argc == 1) return update_world(app);
    if (!strcmp(argv[0], "world.speed") && argc == 2) {
        char *end;
        errno = 0;
        double speed = strtod(argv[1], &end);
        if (errno || end == argv[1] || *end || !isfinite(speed) || speed <= 0 || speed > 1000000) return false;
        app->flight_speed = (float)speed;
        return true;
    }
    if (!strcmp(argv[0], "world.altitude") && argc == 2) {
        char *end;
        errno = 0;
        double altitude = strtod(argv[1], &end);
        const AxPlanet *planet = ax_scene_planet(&app->scene, app->active_planet);
        if (errno || end == argv[1] || *end || !isfinite(altitude) || altitude < 0 || altitude > 1e12 || !planet) return false;
        double up[3], delta[3], position[3];
        if (!ax_planet_up(&app->view_origin, up)) return false;
        const AxPosition zero = {0};
        ax_position_difference(&app->view_origin, &zero, position);
        double current = hypot(hypot(position[0], position[1]), position[2]) - ax_planet_description(planet)->radius - ax_planet_terrain_height(planet,position);
        for (size_t i = 0; i < 3; ++i) delta[i] = up[i] * (altitude - current);
        return ax_position_move(&app->view_origin, delta);
    }
    if (!strcmp(argv[0], "world.save") && argc == 2) return ax_scene_save(&app->scene, argv[1]);
    if (!strcmp(argv[0], "world.load") && argc == 2) {
        AxScene candidate = {0};
        if (!ax_scene_load(&candidate, argv[1]) || !candidate.count) { ax_scene_destroy(&candidate); return false; }
        AxScene previous = app->scene;
        AxPlanetId target = ax_scene_planet(&candidate, app->active_planet) ? app->active_planet :
                            ax_planet_description(candidate.planets[0])->id;
        app->scene = candidate;
        if (!visit_planet(app, target)) { app->scene = previous; ax_scene_destroy(&candidate); return false; }
        app->scene.events = previous.events;
        ax_scene_destroy(&previous);
        return true;
    }
    if ((!strcmp(argv[0], "world.carve") || !strcmp(argv[0], "world.build")) && (argc == 3 || argc == 4)) {
        double radius, distance;
        if (!brush_number(argv[1], &radius) || !brush_number(argv[2], &distance) ||
            (argc == 4 && strcmp(argv[3], "box") && strcmp(argv[3], "sphere"))) return false;
        if (!update_world(app)) return false;
        float forward[3];
        ax_camera_basis(&app->camera, forward, NULL, NULL);
        AxVolumeEdit edit = {.position = app->view_origin, .half_extent = {radius, radius, radius},
            .shape = argc == 4 && !strcmp(argv[3], "box") ? AX_BRUSH_BOX : AX_BRUSH_SPHERE,
            .material = AX_MATERIAL_BUILDING, .remove = !strcmp(argv[0], "world.carve")};
        double delta[3];
        for (size_t i = 0; i < 3; ++i) delta[i] = app->camera.position[i] + forward[i] * distance;
        const AxPlanetDesc *frame = ax_planet_description(ax_scene_planet(&app->scene, app->active_planet));
        const AxPlanetDesc *body = ax_planet_description(ax_scene_planet(&app->scene, app->nearby_planet));
        for (size_t i = 0; i < 3; ++i) delta[i] += frame->center[i] - body->center[i];
        return ax_position_move(&edit.position, delta) && ax_scene_edit(&app->scene, app->nearby_planet, &edit);
    }
    if (!strcmp(argv[0], "core.help") && argc == 1) {
        output(app, "name [value] reads/writes settings. Quotes and backslash escapes supported.");
        for (size_t i = 0; i < app->commands.count; ++i) output(app, app->commands.entries[i].name);
        return true;
    }
    if (!strcmp(argv[0], "core.config.save") && argc == 2) return ax_config_save(&app->config, argv[1]);
    if (!strcmp(argv[0], "core.config.load") && argc == 2) return ax_config_load(&app->config, argv[1]);
    if (!strcmp(argv[0], "core.quit") && argc == 1 && app->window.handle) {
        glfwSetWindowShouldClose(app->window.handle, GLFW_TRUE);
        return true;
    }
    if (!strcmp(argv[0], "renderer.view")) {
        const char *modes[]={"default","wireframe","normals"};
        if (argc==1) { output(app,modes[app->world_renderer.view]); return true; }
        if (argc==2) for (size_t i=0;i<3;++i) if (!strcmp(argv[1],modes[i]))
            return ax_world_renderer_set_view(&app->world_renderer,(AxWorldView)i);
        output(app,"Usage: /renderer.view default|wireframe|normals"); return false;
    }
    if (!strcmp(argv[0], "renderer.nodes") && argc == 1) {
        for (size_t i = 0; i < app->renderer.node_count; ++i) output(app, app->renderer.nodes[i].name);
        output(app, "ui (always last)");
        return true;
    }
    if (!strcmp(argv[0], "renderer.node.enable") && argc == 3) {
        if (strcmp(argv[2], "true") && strcmp(argv[2], "false")) return false;
        return ax_renderer_enable_node(&app->renderer, argv[1], !strcmp(argv[2], "true"));
    }
    return false;
}
/** Starts core services and renderer; accepts --config path, --command text, and --smoke-test. */
int main(int argc, char **argv)
{
    AxEvents events = {0};
    Application app = {0};
    int status = EXIT_FAILURE;
    bool smoke = false, custom_config = false;
    ax_config_init(&app.config, &events);
    if (!ax_settings_path(app.settings_path, sizeof(app.settings_path))) ax_log(AX_LOG_WARN, "User settings directory is unavailable");
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--smoke-test")) smoke = true;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            const char *path = argv[++i];
            if (strlen(path) >= sizeof(app.settings_path) || !ax_config_load(&app.config, path)) { ax_log(AX_LOG_ERROR, "Invalid config: %s", path); goto cleanup; }
            strcpy(app.settings_path, path);
            custom_config = true;
        } else if (!strcmp(argv[i], "--command") && i + 1 < argc) ++i;
        else { ax_log(AX_LOG_ERROR, "Usage: axiovox [--config path] [--command text] [--smoke-test]"); goto cleanup; }
    }
    if (!custom_config && app.settings_path[0] && ax_file_exists(app.settings_path) && !ax_config_load(&app.config, app.settings_path))
        ax_log(AX_LOG_WARN, "Could not load user settings; using defaults");
    ax_log_set_level((AxLogLevel)ax_config_get(&app.config, "log.level")->value);
    if (!ax_event_subscribe(&events, AX_EVENT_CONFIG_CHANGED, config_changed, &app) ||
        !ax_event_subscribe(&events, AX_EVENT_DEVICE_SELECTED, device_selected, NULL)) goto cleanup;
    const char *names[] = {"core.help", "core.quit", "core.config.save", "core.config.load", "renderer.nodes", "renderer.node.enable", "renderer.view",
                           "world.speed", "world.altitude", "world.refresh", "world.recenter", "world.planets", "world.visit", "world.carve", "world.build", "world.save", "world.load"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!ax_command_register(&app.commands, names[i], command, &app)) goto cleanup;
    for (size_t i = 0; i < sizeof(app.config.settings) / sizeof(app.config.settings[0]); ++i)
        if (!ax_command_register(&app.commands, app.config.settings[i].key, command, &app)) goto cleanup;
    app.input.events = &events;
    if (!ax_console_init(&app.console, &app.commands, &app.input, &events)) goto cleanup;
    app.jobs = ax_jobs_create((size_t)ax_config_get(&app.config, "jobs.workers")->value);
    if (!app.jobs) { ax_log(AX_LOG_ERROR, "Worker pool creation failed"); goto cleanup; }
    ax_scene_init(&app.scene, &events);
    AxPlanetDesc first = {.id = 1, .seed = 42, .radius = 1000000, .terrain_height = 10000, .terrain_frequency = 0.005, .terrain_kind = AX_TERRAIN_CONTINENTAL, .surface_gravity = 9.81, .cave_depth = 128};
    AxPlanetDesc second = {.id = 2, .seed = 917, .center = {1200000, 1800000, -2400000}, .radius = 270000, .terrain_height = 2500, .terrain_frequency = 0.001, .terrain_kind = AX_TERRAIN_CRATERED, .surface_gravity = 1.62, .surface_material = AX_MATERIAL_ROCK};
    if (!ax_scene_add_planet(&app.scene, &first) || !ax_scene_add_planet(&app.scene, &second)) goto cleanup;
    struct timespec build_begin, build_end;
    timespec_get(&build_begin, TIME_UTC);
    if (!visit_planet(&app, 1)) { ax_log(AX_LOG_ERROR, "World generation failed"); goto cleanup; }
    timespec_get(&build_end, TIME_UTC);
    app.flight_speed = 6;
    size_t source_triangles=0,visible_triangles=0;
    for (size_t i=0;i<app.stream.count;++i) { source_triangles+=app.stream.regions[i].source_triangles; visible_triangles+=app.stream.regions[i].visible_triangles; }
    ax_log(AX_LOG_INFO,"Terrain optimizer: %zu source, %zu visible -> %zu triangles",source_triangles,visible_triangles,app.geometry.index_count/3);
    ax_log(AX_LOG_INFO, "Terra/Luna: %zu mesh regions, %zu triangles, %.1f MiB built in %.0f ms", app.geometry.count, app.geometry.index_count / 3,
           (double)(app.geometry.vertex_count * sizeof(AxSurfaceVertex) + app.geometry.index_count * sizeof(uint32_t)) / (1024 * 1024),
           (double)(build_end.tv_sec - build_begin.tv_sec) * 1000.0 + (double)(build_end.tv_nsec - build_begin.tv_nsec) / 1e6);
    if (!ax_window_create(&app.window, &events, ax_config_get(&app.config, "window.width")->value, ax_config_get(&app.config, "window.height")->value)) goto cleanup;
    ax_window_set_input(&app.window, &app.input);
    if (!ax_renderer_create(&app.renderer, &app.window, &events, ax_config_get(&app.config, "renderer.device")->value)) goto cleanup;
    if (!ax_world_renderer_create(&app.world_renderer, &app.renderer, &app.geometry, &app.camera)) { ax_log(AX_LOG_ERROR, "World renderer initialization failed"); goto cleanup; }
    if (!update_world(&app)) goto cleanup;
    if (!ax_ui_create(&app.ui, &app.renderer, &app.console)) { ax_log(AX_LOG_ERROR, "UI initialization failed"); goto cleanup; }
    if (!configure_hud(&app)) goto cleanup;
    ax_ui_bind_settings(&app.ui, &app.config, app.settings_path);
    if (!ax_window_sync_config(&app.window, &app.config)) goto cleanup;
    ax_config_mark_applied(&app.config);
    if (!ax_runtime_bind(&app.runtime, &app.config, &app.renderer, app.jobs)) goto cleanup;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--config")) ++i;
        else if (!strcmp(argv[i], "--command")) {
            if (!ax_command_execute(&app.commands, argv[++i])) { ax_log(AX_LOG_ERROR, "Command failed: %s", argv[i]); goto cleanup; }
        }
    }
    if (smoke) ax_console_open(&app.console, true);
    unsigned frames = 0;
    double previous_time = glfwGetTime();
    while (!glfwWindowShouldClose(app.window.handle)) {
        ax_input_begin_frame(&app.input);
        glfwPollEvents();
        if (!ax_window_sync_config(&app.window, &app.config)) { ax_log(AX_LOG_ERROR, "Could not synchronize window settings"); goto cleanup; }
        if (smoke && frames == 2) {
            ax_console_open(&app.console, false);
            ax_input_key(&app.input, AX_KEY_ESCAPE, true, false, 0);
            ax_input_key(&app.input, AX_KEY_ESCAPE, false, false, 0);
        }
        if (smoke && frames == 4) ax_menu_open_settings(&app.ui.hud.menu);
        if (smoke && frames == 6) app.ui.hud.menu.category = AX_SETTINGS_VIDEO;
        double now = glfwGetTime();
        ax_ui_update(&app.ui, now);
        bool cruise = ax_input_down(&app.input, AX_KEY_LEFT_SHIFT) || ax_input_down(&app.input, AX_KEY_RIGHT_SHIFT);
        app.camera.move_speed = cruise ? (float)fmin(250000, app.flight_speed + fmax(app.altitude, 0) * 0.2) : app.flight_speed;
        ax_camera_align(&app.camera,app.gravity.up,app.gravity.alignment,now-previous_time);
        ax_camera_update(&app.camera, &app.input, now - previous_time);
        ax_world_renderer_advance(&app.world_renderer, now - previous_time);
        previous_time = now;
        if (!update_world(&app)) {
            if (!app.stream.failed) output(&app, "Terrain streaming update failed; existing planet coverage retained.");
            app.stream.failed = true;
        } else app.stream.failed = false;
        ax_window_set_cursor_locked(&app.window, !app.input.captured && glfwGetWindowAttrib(app.window.handle, GLFW_FOCUSED) &&
                                                 !glfwGetWindowAttrib(app.window.handle, GLFW_ICONIFIED));
        if (!app.runtime.healthy) { ax_log(AX_LOG_ERROR, "Runtime rollback failed"); goto cleanup; }
        if (!ax_renderer_draw(&app.renderer)) { ax_log(AX_LOG_ERROR, "Frame rendering failed"); goto cleanup; }
        if (smoke && ++frames == 8) break;
        int width, height;
        glfwGetFramebufferSize(app.window.handle, &width, &height);
        if (width <= 0 || height <= 0 || glfwGetWindowAttrib(app.window.handle, GLFW_ICONIFIED)) glfwWaitEventsTimeout(0.05);
    }
    status = EXIT_SUCCESS;
cleanup:
    ax_world_stream_destroy(&app.stream, app.jobs);
    ax_jobs_destroy(app.jobs);
    if (app.renderer.device) vkDeviceWaitIdle(app.renderer.device);
    ax_ui_destroy(&app.ui);
    ax_world_renderer_destroy(&app.world_renderer);
    ax_renderer_destroy(&app.renderer);
    ax_world_geometry_destroy(&app.geometry);
    ax_scene_destroy(&app.scene);
    ax_console_destroy(&app.console);
    ax_window_destroy(&app.window);
    return status;
}
