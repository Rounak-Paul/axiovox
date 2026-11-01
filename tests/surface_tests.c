#include "world/surface.h"
#include "world/mesh_optimizer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); goto cleanup; } } while (0)
typedef struct { int64_t a[3],b[3]; int direction; } Edge;
/** Orders quantized geometric edges independently of mesh vertex allocation and winding. */
static int compare_edge(const void *a,const void *b)
{
    const Edge *x=a,*y=b;
    for (size_t i=0;i<3;++i) if (x->a[i]!=y->a[i]) return x->a[i]<y->a[i] ? -1 : 1;
    for (size_t i=0;i<3;++i) if (x->b[i]!=y->b[i]) return x->b[i]<y->b[i] ? -1 : 1;
    return 0;
}
/** Collects surface edges, omitting coarse triangles owned by an aligned finer cube. */
static size_t edges(const AxSurfaceRegion *region,const double origin[3],const double *exclude,Edge *out)
{
    size_t count=0;
    for (size_t i=0;i<region->mesh.index_count;i+=3) {
        double p[3][3],center[3]={0};
        for (size_t j=0;j<3;++j) for (size_t k=0;k<3;++k) {
            p[j][k]=region->mesh.vertices[region->mesh.indices[i+j]].position[k]+origin[k];
            center[k]+=p[j][k]/3;
        }
        bool inside=exclude!=NULL;
        if (exclude) for (size_t k=0;k<3;++k) if (center[k]<exclude[k] || center[k]>exclude[k]+16) inside=false;
        if (inside) continue;
        for (size_t j=0;j<3;++j) {
            Edge edge;
            for (size_t k=0;k<3;++k) { edge.a[k]=llround(p[j][k]*10000); edge.b[k]=llround(p[(j+1)%3][k]*10000); }
            bool reverse=false;
            for (size_t k=0;k<3;++k) if (edge.a[k]!=edge.b[k]) { reverse=edge.a[k]>edge.b[k]; break; }
            edge.direction=reverse ? -1 : 1;
            if (reverse) for (size_t k=0;k<3;++k) { int64_t swap=edge.a[k]; edge.a[k]=edge.b[k]; edge.b[k]=swap; }
            out[count++]=edge;
        }
    }
    return count;
}
/** Checks manifold closure after geometric welding, including transition triangles on the shared LOD planes. */
static bool closed(Edge *items,size_t count)
{
    qsort(items,count,sizeof(*items),compare_edge);
    for (size_t i=0;i<count;) {
        size_t j=i+1;
        while (j<count && !compare_edge(&items[i],&items[j])) ++j;
        if (j-i!=2 || items[i].direction+items[i+1].direction!=0) {
            fprintf(stderr,"unpaired edge (%lld,%lld,%lld)-(%lld,%lld,%lld), count %zu\n",
                (long long)items[i].a[0],(long long)items[i].a[1],(long long)items[i].a[2],
                (long long)items[i].b[0],(long long)items[i].b[1],(long long)items[i].b[2],j-i);
            return false;
        }
        i=j;
    }
    return true;
}
/** Checks disjoint adaptive leaves against coarser neighbors on positive and negative faces. */
static bool adaptive_seams(const AxPlanet *planet,unsigned refined)
{
    AxSurfaceRegion regions[15]={0}; Edge *all=NULL; size_t count=0,total=0; bool ok=false;
    for (unsigned octant=0;octant<8;++octant) {
        unsigned children=octant==refined ? 8 : 1;
        for (unsigned child=0;child<children;++child) {
            double minimum[3]; uint8_t mask=0;
            for (unsigned k=0;k<3;++k) {
                minimum[k]=(octant&(1u<<k)) ? 0 : -16;
                if (children==8) {
                    if (child&(1u<<k)) minimum[k]+=8;
                    if (((octant>>k)&1)!=((child>>k)&1)) mask|=(uint8_t)(1u<<(2*k+((octant&(1u<<k)) ? 0 : 1)));
                }
            }
            AxPosition origin;
            if (!ax_position_from_meters(&origin,minimum) || !ax_surface_build(planet,&origin,children==8 ? 8 : 16,16,mask,&regions[count])) goto cleanup;
            AxMeshStats stats;
            if (!ax_mesh_optimize(&regions[count].mesh,regions[count].extent,regions[count].extent/16,NULL,&stats)) goto cleanup;
            total+=regions[count++].mesh.index_count;
        }
    }
    all=malloc(total*sizeof(*all)); if (!all) goto cleanup;
    size_t n=0; const AxPosition zero={0};
    for (size_t i=0;i<count;++i) { double minimum[3]; ax_position_difference(&regions[i].origin,&zero,minimum); n+=edges(&regions[i],minimum,NULL,all+n); }
    ok=closed(all,n);
cleanup:
    free(all); for (size_t i=0;i<15;++i) ax_surface_destroy(&regions[i]); return ok;
}
/** Validates indexed manifold extraction, smooth normals, exact-plane cases, LOD seams and failure retention. */
int main(void)
{
    int result=EXIT_FAILURE;
    AxScene scene={0};
    AxSurfaceRegion coarse={0},fine={0},empty={0};
    Edge *all=NULL;
    AxPlanetDesc desc={.id=1,.radius=10};
    CHECK(ax_scene_add_planet(&scene,&desc));
    const AxPlanet *planet=ax_scene_planet(&scene,1);
    AxPosition origin;
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,-16,-16}));
    CHECK(ax_surface_build(planet,&origin,32,32,false,&coarse));
    CHECK(coarse.mesh.index_count>0 && coarse.mesh.vertex_count<coarse.mesh.index_count/2);
    for (size_t i=0;i<coarse.mesh.index_count;++i) CHECK(coarse.mesh.indices[i]<coarse.mesh.vertex_count);
    for (size_t i=0;i<coarse.mesh.vertex_count;++i) {
        const AxSurfaceVertex *v=&coarse.mesh.vertices[i];
        double length=0;
        for (size_t k=0;k<3;++k) { CHECK(isfinite(v->position[k]) && isfinite(v->normal[k])); length+=v->normal[k]*v->normal[k]; }
        CHECK(fabs(length-1)<1e-5);
    }
    all=malloc(coarse.mesh.index_count*sizeof(*all)); CHECK(all);
    CHECK(closed(all,edges(&coarse,(double[3]){-16,-16,-16},NULL,all)));
    free(all); all=NULL;
    const double offset[3]={-7,-5,-3};
    CHECK(ax_position_from_meters(&origin,offset));
    CHECK(ax_surface_build(planet,&origin,16,32,AX_SURFACE_ALL_FACES,&fine));
    all=malloc((coarse.mesh.index_count+fine.mesh.index_count)*sizeof(*all)); CHECK(all);
    size_t n=edges(&coarse,(double[3]){-16,-16,-16},offset,all);
    n+=edges(&fine,offset,NULL,all+n);
    CHECK(closed(all,n));
    CHECK(adaptive_seams(planet,0) && adaptive_seams(planet,7));
    AxVolumeEdit cut={.half_extent={3.25,3.25,3.25},.material=AX_MATERIAL_ROCK,.remove=true};
    CHECK(ax_position_from_meters(&cut.position,(double[3]){-7,5,0}));
    CHECK(ax_scene_edit(&scene,1,&cut));
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,-16,-16}));
    CHECK(ax_surface_build(planet,&origin,32,32,false,&coarse));
    CHECK(ax_position_from_meters(&origin,offset));
    CHECK(ax_surface_build(planet,&origin,16,32,AX_SURFACE_ALL_FACES,&fine));
    free(all); all=malloc((coarse.mesh.index_count+fine.mesh.index_count)*sizeof(*all)); CHECK(all);
    n=edges(&coarse,(double[3]){-16,-16,-16},offset,all);
    n+=edges(&fine,offset,NULL,all+n);
    CHECK(closed(all,n));
    AxMeshStats coarse_stats,fine_stats;
    CHECK(ax_mesh_optimize(&coarse.mesh,32,1,(float[4]){9,11,13,16},&coarse_stats));
    CHECK(ax_mesh_optimize(&fine.mesh,16,0.5f,NULL,&fine_stats));
    CHECK(coarse_stats.triangles<=coarse_stats.visible_triangles && fine_stats.triangles<=fine_stats.visible_triangles);
    free(all); all=malloc((coarse.mesh.index_count+fine.mesh.index_count)*sizeof(*all)); CHECK(all);
    n=edges(&coarse,(double[3]){-16,-16,-16},NULL,all);
    n+=edges(&fine,offset,NULL,all+n);
    CHECK(closed(all,n));
    uint32_t *previous=fine.mesh.indices;
    CHECK(!ax_surface_build(planet,&origin,16,3,AX_SURFACE_ALL_FACES,&fine) && fine.mesh.indices==previous);
    CHECK(ax_position_from_meters(&origin,(double[3]){100,100,100}));
    CHECK(ax_surface_build(planet,&origin,16,32,AX_SURFACE_ALL_FACES,&empty) && !empty.mesh.index_count && empty.extent==16);
    AxPlanetDesc flat_desc={.id=2,.radius=1000000};
    const AxPlanet *flat=ax_scene_add_planet(&scene,&flat_desc); CHECK(flat);
    CHECK(ax_position_from_meters(&origin,(double[3]){-16,999984,-16}));
    CHECK(ax_surface_build(flat,&origin,32,64,false,&empty));
    AxMeshStats flat_stats;
    CHECK(ax_mesh_optimize(&empty.mesh,32,0.5f,NULL,&flat_stats));
    CHECK(flat_stats.triangles<flat_stats.visible_triangles/4);
    uint32_t *intact=empty.mesh.indices;
    CHECK(!ax_mesh_optimize(&empty.mesh,NAN,0.5f,NULL,&flat_stats) && empty.mesh.indices==intact);
    printf("flat terrain: %zu -> %zu triangles\n",flat_stats.visible_triangles,flat_stats.triangles);
    result=EXIT_SUCCESS;
cleanup:
    free(all); ax_surface_destroy(&coarse); ax_surface_destroy(&fine); ax_surface_destroy(&empty); ax_scene_destroy(&scene);
    return result;
}
