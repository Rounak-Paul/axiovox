#include "surface.h"
#include "transvoxel/tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    const AxPlanet *planet;
    AxPosition origin;
    double step;
    uint32_t cells, side;
    uint8_t transition_faces;
    float *density;
    uint32_t *cache;
    AxSurfaceMesh mesh;
    size_t vertex_capacity, index_capacity;
} Builder;
/** Grows mesh storage without overflowing host or GPU index ranges. */
static bool reserve(void **data, size_t *capacity, size_t count, size_t extra, size_t item)
{
    if (extra > UINT32_MAX - count) return false;
    size_t needed = count + extra;
    if (needed <= *capacity) return true;
    size_t next = *capacity ? *capacity : 1024;
    while (next < needed) {
        if (next > UINT32_MAX / 2) { next = needed; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / item) return false;
    void *allocation = realloc(*data, next * item);
    if (!allocation) return false;
    *data = allocation; *capacity = next;
    return true;
}
/** Samples bounded mesh-local coordinates through the precise planet coordinate system. */
static double sample(const Builder *b, const double p[3], AxMaterial *material)
{
    if (material) *material=AX_MATERIAL_AIR;
    AxPosition position = b->origin;
    if (!ax_position_move(&position, p)) return INFINITY;
    return ax_planet_sample(b->planet, &position, material);
}
/** Addresses the two-cell density halo surrounding the meshing domain. */
static size_t grid_index(const Builder *b,int x,int y,int z)
{
    return ((size_t)(z+2)*b->side+(size_t)(y+2))*b->side+(size_t)(x+2);
}
/** Resolves a lattice point into the shared sample/edge cache. */
static size_t lattice(const Builder *b,const uint32_t p[3])
{
    return grid_index(b,(int)p[0],(int)p[1],(int)p[2]);
}
/** Interpolates cached central differences instead of resampling procedural noise per mesh vertex. */
static void cached_gradient(const Builder *b,const double p[3],int stride,double normal[3])
{
    int cell[3]; double t[3];
    for (size_t k=0;k<3;++k) {
        cell[k]=(int)fmax(0,fmin(b->cells-stride,floor(p[k]/b->step/stride)*stride));
        t[k]=(p[k]/b->step-cell[k])/stride; normal[k]=0;
    }
    for (unsigned i=0;i<8;++i) {
        int q[3]={cell[0]+(int)(i&1)*stride,cell[1]+(int)((i>>1)&1)*stride,cell[2]+(int)((i>>2)&1)*stride};
        double weight=((i&1) ? t[0] : 1-t[0])*((i&2) ? t[1] : 1-t[1])*((i&4) ? t[2] : 1-t[2]);
        for (size_t k=0;k<3;++k) {
            int a[3]={q[0],q[1],q[2]},c[3]={q[0],q[1],q[2]}; a[k]+=stride; c[k]-=stride;
            normal[k]+=weight*(b->density[grid_index(b,a[0],a[1],a[2])]-b->density[grid_index(b,c[0],c[1],c[2])])/(2*b->step*stride);
        }
    }
}
/** Matches the coarser neighbor's normal filter on transition planes, blending within two fine cells. */
static void cached_normal(const Builder *b,const double p[3],double normal[3])
{
    cached_gradient(b,p,1,normal);
    double blend=0;
    for (unsigned face=0;face<6;++face) if (b->transition_faces&(1u<<face)) {
        double distance=(face&1) ? b->cells*b->step-p[face/2] : p[face/2];
        blend=fmax(blend,fmax(0,1-distance/(2*b->step)));
    }
    if (blend>0) {
        double coarse[3]; cached_gradient(b,p,2,coarse);
        for (size_t k=0;k<3;++k) normal[k]+=blend*(coarse[k]-normal[k]);
    }
}
/** Creates a smooth vertex from the cached field and resolves its exposed material without Newton resampling. */
static bool append_vertex(Builder *b,const double p[3],uint32_t *index)
{
    if (!reserve((void **)&b->mesh.vertices,&b->vertex_capacity,b->mesh.vertex_count,1,sizeof(AxSurfaceVertex))) return false;
    AxSurfaceVertex vertex={0};
    double normal[3]; cached_normal(b,p,normal);
    double length=hypot(hypot(normal[0],normal[1]),normal[2]);
    if (!isfinite(length)) return false;
    if (length<1e-12) { normal[0]=0; normal[1]=1; normal[2]=0; length=1; }
    for (size_t k=0;k<3;++k) { normal[k]/=length; vertex.position[k]=(float)p[k]; vertex.normal[k]=(float)normal[k]; }
    AxPosition position=b->origin;
    if (!ax_position_move(&position,p)) return false;
    AxMaterial material=ax_planet_surface_material(b->planet,&position,normal);
    const uint32_t colors[AX_MATERIAL_COUNT]={0xff777777,0xff888b90,0xff42678b,0xff4c9364,0xffb7af94,0xff82b5c9,0xffe5e8ed};
    vertex.color=colors[material];
    *index=(uint32_t)b->mesh.vertex_count;
    b->mesh.vertices[b->mesh.vertex_count++]=vertex;
    return true;
}
/** Interpolates an edge in canonical endpoint order, making neighboring cells bit-identical. */
static bool edge_vertex(Builder *b, const uint32_t a[3], const uint32_t c[3], bool shared, uint32_t *index)
{
    const uint32_t *lo = a, *hi = c;
    size_t ia = lattice(b,a), ic = lattice(b,c);
    if (ia > ic) { lo = c; hi = a; size_t swap = ia; ia = ic; ic = swap; }
    double da = b->density[ia], dc = b->density[ic];
    double t = da == dc ? 0.5 : da / (da - dc);
    if (!isfinite(t)) return false;
    t = fmax(0, fmin(1,t));
    size_t key = SIZE_MAX;
    if (t == 0 || t == 1) key = (t == 0 ? ia : ic) * 4 + 3;
    else if (shared) {
        size_t axis = lo[0] != hi[0] ? 0 : lo[1] != hi[1] ? 1 : 2;
        key = ia * 4 + axis;
    }
    if (key != SIZE_MAX && b->cache[key] != UINT32_MAX) { *index = b->cache[key]; return true; }
    double p[3];
    for (size_t k = 0; k < 3; ++k) p[k] = ((double)lo[k] + ((double)hi[k] - lo[k]) * t) * b->step;
    if (!append_vertex(b,p,index)) return false;
    if (key != SIZE_MAX) b->cache[key] = *index;
    return true;
}
/** Emits a nondegenerate indexed triangle with outward regular-cell winding. */
static bool triangle(Builder *b, uint32_t a, uint32_t c, uint32_t d)
{
    if (a == c || a == d || c == d) return true;
    const float *p = b->mesh.vertices[a].position, *q = b->mesh.vertices[c].position, *r = b->mesh.vertices[d].position;
    double u[3], v[3], cross[3];
    for (size_t k = 0; k < 3; ++k) { u[k] = (double)q[k] - p[k]; v[k] = (double)r[k] - p[k]; }
    cross[0] = u[1]*v[2] - u[2]*v[1]; cross[1] = u[2]*v[0] - u[0]*v[2]; cross[2] = u[0]*v[1] - u[1]*v[0];
    if (cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2] == 0) return true;
    if (!reserve((void **)&b->mesh.indices,&b->index_capacity,b->mesh.index_count,3,sizeof(uint32_t))) return false;
    uint32_t *out = b->mesh.indices + b->mesh.index_count;
    out[0] = a; out[1] = c; out[2] = d;
    b->mesh.index_count += 3;
    return true;
}
/** Extracts modified marching-cubes cells, sharing all regular lattice-edge vertices. */
static bool regular_cells(Builder *b)
{
    for (uint32_t z=0; z<b->cells; ++z) for (uint32_t y=0; y<b->cells; ++y) for (uint32_t x=0; x<b->cells; ++x) {
        uint32_t corners[8][3], code = 0;
        for (uint32_t i=0; i<8; ++i) {
            corners[i][0]=x+(i&1); corners[i][1]=y+((i>>1)&1); corners[i][2]=z+((i>>2)&1);
            if (b->density[lattice(b,corners[i])] < 0) code |= 1u << i;
        }
        const RegularCellData *data = &regularCellData[regularCellClass[code]];
        uint32_t vertices[12];
        for (uint32_t i=0; i<(uint32_t)(data->geometryCounts>>4); ++i) {
            uint16_t edge = regularVertexData[code][i];
            if (!edge_vertex(b,corners[(edge>>4)&15],corners[edge&15],true,&vertices[i])) return false;
        }
        for (uint32_t i=0; i<(uint32_t)(data->geometryCounts&15)*3; i+=3)
            if (!triangle(b,vertices[data->vertexIndex[i]],vertices[data->vertexIndex[i+1]],vertices[data->vertexIndex[i+2]])) return false;
    }
    return true;
}
/** Stitches each fine cube face to the coarser face contour using the published 512 transition cases.
 * Both contours lie on the shared plane: zero-depth cells need no displacement of regular mesh vertices.
 */
static bool build_transitions(Builder *b)
{
    const unsigned order[9] = {0,1,2,5,8,7,6,3,4};
    for (uint32_t face=0; face<6; ++face) {
        if (!(b->transition_faces&(1u<<face))) continue;
        uint32_t axis=face/2, u=(axis+1)%3, v=(axis+2)%3;
        for (uint32_t y=0; y<b->cells; y+=2) for (uint32_t x=0; x<b->cells; x+=2) {
            uint32_t corners[13][3] = {{0}}, code=0;
            for (uint32_t i=0; i<9; ++i) {
                corners[i][axis]=(face&1) ? b->cells : 0;
                corners[i][u]=x+i%3; corners[i][v]=y+i/3;
            }
            memcpy(corners[9],corners[0],sizeof(corners[0])); memcpy(corners[10],corners[2],sizeof(corners[0]));
            memcpy(corners[11],corners[6],sizeof(corners[0])); memcpy(corners[12],corners[8],sizeof(corners[0]));
            for (uint32_t i=0; i<9; ++i) if (b->density[lattice(b,corners[order[i]])] < 0) code |= 1u<<i;
            uint32_t cell_class = transitionCellClass[code];
            const TransitionCellData *data = &transitionCellData[cell_class&127];
            uint32_t vertices[12];
            for (uint32_t i=0; i<(uint32_t)(data->geometryCounts>>4); ++i) {
                uint16_t edge = transitionVertexData[code][i];
                uint32_t a=(edge>>4)&15, c=edge&15;
                bool shared = a<9 && c<9;
                if (!edge_vertex(b,corners[a],corners[c],shared,&vertices[i])) return false;
            }
            bool reverse = ((cell_class&128)!=0) == ((face&1)!=0);
            for (uint32_t i=0; i<(uint32_t)(data->geometryCounts&15)*3; i+=3) {
                uint32_t a=vertices[data->vertexIndex[i]], c=vertices[data->vertexIndex[i+1]], d=vertices[data->vertexIndex[i+2]];
                if (!triangle(b,a,reverse ? d : c,reverse ? c : d)) return false;
            }
        }
    }
    return true;
}
bool ax_surface_build(const AxPlanet *planet, const AxPosition *origin, float extent,
                      uint32_t cells, uint8_t transition_faces, AxSurfaceRegion *region)
{
    if (!planet || !origin || !region || !isfinite(extent) || extent<=0 || cells<2 || cells>128 || (cells&1) || transition_faces>AX_SURFACE_ALL_FACES) return false;
    Builder b = {.planet=planet,.origin=*origin,.step=(double)extent/cells,.cells=cells,.side=cells+5,.transition_faces=transition_faces};
    if (!ax_position_normalize(&b.origin)) return false;
    AxPosition end=b.origin;
    if (!ax_position_move(&end,(double[3]){extent,extent,extent})) return false;
    const AxPlanetDesc *desc=ax_planet_description(planet);
    double minimum,maximum;
    if (!ax_planet_density_bounds(planet,&b.origin,extent,&minimum,&maximum)) return false;
    if (minimum>0 || maximum<0) {
        ax_surface_destroy(region);
        *region=(AxSurfaceRegion){.planet=desc->id,.origin=b.origin,.revision=ax_planet_revision(planet),
            .extent=extent,.cells=cells,.transition_faces=transition_faces};
        return true;
    }
    size_t count=(size_t)b.side*b.side*b.side;
    b.density=malloc(count*sizeof(float)); b.cache=malloc(count*4*sizeof(uint32_t));
    bool success=false;
    if (!b.density || !b.cache) goto cleanup;
    memset(b.cache,255,count*4*sizeof(uint32_t));
    for (int z=-2;z<=(int)cells+2;++z) for (int y=-2;y<=(int)cells+2;++y) for (int x=-2;x<=(int)cells+2;++x) {
        double p[3]={x*b.step,y*b.step,z*b.step};
        double value=sample(&b,p,NULL);
        if (!isfinite(value) || fabs(value)>3e38) goto cleanup;
        b.density[grid_index(&b,x,y,z)]=(float)value;
    }
    if (!regular_cells(&b) || (transition_faces && !build_transitions(&b))) goto cleanup;
    ax_surface_destroy(region);
    *region=(AxSurfaceRegion){.mesh=b.mesh,.planet=ax_planet_description(planet)->id,.origin=b.origin,
        .revision=ax_planet_revision(planet),.extent=extent,.cells=cells,.transition_faces=transition_faces};
    b.mesh=(AxSurfaceMesh){0};
    success=true;
cleanup:
    free(b.density); free(b.cache); free(b.mesh.vertices); free(b.mesh.indices);
    return success;
}
void ax_surface_destroy(AxSurfaceRegion *region)
{
    if (!region) return;
    free(region->mesh.vertices); free(region->mesh.indices);
    *region=(AxSurfaceRegion){0};
}
