#include "geometry.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
bool ax_world_geometry_build(const AxScene *scene, const AxSurfaceRegion *regions, size_t count, AxWorldGeometry *geometry)
{
    if (!scene || !regions || !geometry || !count || count>AX_WORLD_MAX_REGIONS) return false;
    AxWorldGeometry next={0};
    for (size_t i=0; i<count; ++i) {
        const AxSurfaceRegion *r=&regions[i];
        if (!isfinite(r->extent) || r->extent<0 || (r->mesh.vertex_count && !r->mesh.vertices) ||
            (r->mesh.index_count && !r->mesh.indices) || r->mesh.index_count%3) return false;
        if (r->extent==0) {
            if (r->mesh.vertex_count || r->mesh.index_count) return false;
            continue;
        }
        for (size_t j=0;j<r->mesh.index_count;++j) if (r->mesh.indices[j]>=r->mesh.vertex_count) return false;
        const AxPlanet *planet=ax_scene_planet(scene,r->planet);
        if (!planet || r->mesh.vertex_count>INT32_MAX-next.vertex_count || r->mesh.index_count>UINT32_MAX-next.index_count) return false;
        AxWorldRegion *out=&next.regions[next.count++];
        *out=(AxWorldRegion){.origin=r->origin,.planet=r->planet,.extent=r->extent,.first_index=(uint32_t)next.index_count,
            .index_count=(uint32_t)r->mesh.index_count,.vertex_offset=(int32_t)next.vertex_count};
        memcpy(out->center,ax_planet_description(planet)->center,sizeof(out->center));
        next.vertex_count+=r->mesh.vertex_count; next.index_count+=r->mesh.index_count;
    }
    if (!next.count || next.vertex_count>SIZE_MAX/sizeof(AxSurfaceVertex) || next.index_count>SIZE_MAX/sizeof(uint32_t)) return false;
    next.vertices=malloc((next.vertex_count ? next.vertex_count : 1)*sizeof(AxSurfaceVertex));
    next.indices=malloc((next.index_count ? next.index_count : 1)*sizeof(uint32_t));
    if (!next.vertices || !next.indices) { ax_world_geometry_destroy(&next); return false; }
    size_t vertex=0,index=0;
    for (size_t i=0; i<count; ++i) {
        const AxSurfaceMesh *mesh=&regions[i].mesh;
        if (mesh->vertex_count) memcpy(next.vertices+vertex,mesh->vertices,mesh->vertex_count*sizeof(AxSurfaceVertex));
        if (mesh->index_count) memcpy(next.indices+index,mesh->indices,mesh->index_count*sizeof(uint32_t));
        vertex+=mesh->vertex_count; index+=mesh->index_count;
    }
    ax_world_geometry_destroy(geometry); *geometry=next;
    return true;
}
void ax_world_geometry_destroy(AxWorldGeometry *geometry)
{
    if (!geometry) return;
    free(geometry->vertices); free(geometry->indices);
    *geometry=(AxWorldGeometry){0};
}
