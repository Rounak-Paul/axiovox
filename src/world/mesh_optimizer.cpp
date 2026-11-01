#include "mesh_optimizer.h"
#include "meshoptimizer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>
static_assert(sizeof(AxSurfaceVertex)==28,"Deduplication requires initialized, padding-free attributes");
/** Prepares owned optimized arrays; published C allocations change only after every stage succeeds. */
extern "C" bool ax_mesh_optimize(AxSurfaceMesh *mesh,float extent,float cell_size,const float exclusion[4],AxMeshStats *stats)
{
    if (!mesh || !std::isfinite(extent) || extent<=0 || !std::isfinite(cell_size) || cell_size<=0 ||
        mesh->index_count%3 || mesh->vertex_count>UINT32_MAX || mesh->index_count>UINT32_MAX ||
        (mesh->vertex_count && !mesh->vertices) || (mesh->index_count && !mesh->indices)) return false;
    for (size_t i=0;i<mesh->index_count;++i) if (mesh->indices[i]>=mesh->vertex_count) return false;
    for (size_t i=0;i<mesh->vertex_count;++i) for (size_t k=0;k<3;++k)
        if (!std::isfinite(mesh->vertices[i].position[k]) || !std::isfinite(mesh->vertices[i].normal[k])) return false;
    AxMeshStats result={mesh->index_count/3,mesh->index_count/3,0,0};
    if (exclusion) {
        if (!std::isfinite(exclusion[3]) || exclusion[3]<=0) return false;
        for (size_t k=0;k<3;++k) {
            if (!std::isfinite(exclusion[k]) ||
                std::fabs(exclusion[k]/cell_size-std::round(exclusion[k]/cell_size))>1e-4f ||
                std::fabs((exclusion[k]+exclusion[3])/cell_size-std::round((exclusion[k]+exclusion[3])/cell_size))>1e-4f) return false;
        }
    }
    if (!mesh->index_count) {
        std::free(mesh->vertices); std::free(mesh->indices); *mesh=AxSurfaceMesh{};
        if (stats) *stats=result;
        return true;
    }
    try {
        std::vector<unsigned int> source;
        source.reserve(mesh->index_count);
        for (size_t i=0;i<mesh->index_count;i+=3) {
            bool inside=exclusion!=nullptr;
            for (size_t k=0;inside && k<3;++k) {
                double center=(double(mesh->vertices[mesh->indices[i]].position[k])+mesh->vertices[mesh->indices[i+1]].position[k]+
                    mesh->vertices[mesh->indices[i+2]].position[k])/3;
                inside=center>=exclusion[k] && center<=exclusion[k]+exclusion[3];
            }
            if (!inside) source.insert(source.end(),mesh->indices+i,mesh->indices+i+3);
        }
        result.visible_triangles=source.size()/3;
        if (source.empty()) {
            std::free(mesh->vertices); std::free(mesh->indices); *mesh=AxSurfaceMesh{};
            if (stats) *stats=result;
            return true;
        }
        std::vector<unsigned int> remap(mesh->vertex_count),indices(source.size());
        size_t count=meshopt_generateVertexRemap(remap.data(),source.data(),source.size(),
            mesh->vertices,mesh->vertex_count,sizeof(AxSurfaceVertex));
        std::vector<AxSurfaceVertex> vertices(count);
        meshopt_remapVertexBuffer(vertices.data(),mesh->vertices,mesh->vertex_count,sizeof(AxSurfaceVertex),remap.data());
        meshopt_remapIndexBuffer(indices.data(),source.data(),source.size(),remap.data());
        std::vector<unsigned char> locks(count,0);
        std::vector<float> attributes(count*6);
        for (size_t i=0;i<count;++i) {
            for (size_t k=0;k<3;++k) {
                if (vertices[i].position[k]<=cell_size*2 || vertices[i].position[k]>=extent-cell_size*2) locks[i]=meshopt_SimplifyVertex_Lock;
                attributes[i*6+k]=vertices[i].normal[k];
                attributes[i*6+3+k]=float((vertices[i].color>>(8*k))&255)/255;
            }
        }
        if (exclusion) for (size_t i=0;i<count;++i) {
            const float *p=vertices[i].position;
            float halo=cell_size*2;
            for (size_t axis=0;axis<3;++axis) {
                size_t a=(axis+1)%3,b=(axis+2)%3;
                bool near_face=std::fabs(p[axis]-exclusion[axis])<=halo || std::fabs(p[axis]-exclusion[axis]-exclusion[3])<=halo;
                if (near_face && p[a]>=exclusion[a]-halo && p[a]<=exclusion[a]+exclusion[3]+halo &&
                    p[b]>=exclusion[b]-halo && p[b]<=exclusion[b]+exclusion[3]+halo) locks[i]=meshopt_SimplifyVertex_Lock;
            }
        }
        for (size_t i=0;i<indices.size();i+=3) for (size_t k=0;k<3;++k) {
            unsigned int a=indices[i+k],b=indices[i+(k+1)%3];
            if (vertices[a].color!=vertices[b].color) locks[a]=locks[b]=meshopt_SimplifyVertex_Lock;
        }
        const float weights[6]={0.05f,0.05f,0.05f,0.1f,0.1f,0.1f};
        size_t target=std::max(size_t(3),indices.size()/20*3);
        std::vector<unsigned int> reduced(indices.size());
        size_t reduced_count=meshopt_simplifyWithAttributes(reduced.data(),indices.data(),indices.size(),vertices[0].position,
            count,sizeof(AxSurfaceVertex),attributes.data(),6*sizeof(float),weights,6,locks.data(),target,
            cell_size*0.15f,meshopt_SimplifyLockBorder|meshopt_SimplifyErrorAbsolute,&result.error);
        if (!reduced_count || reduced_count%3 || !std::isfinite(result.error)) return false;
        reduced.resize(reduced_count);
        meshopt_optimizeVertexCache(reduced.data(),reduced.data(),reduced.size(),count);
        count=meshopt_optimizeVertexFetch(vertices.data(),reduced.data(),reduced.size(),vertices.data(),count,sizeof(AxSurfaceVertex));
        std::unique_ptr<AxSurfaceVertex,decltype(&std::free)> output_vertices(
            static_cast<AxSurfaceVertex *>(std::malloc(count*sizeof(AxSurfaceVertex))),&std::free);
        std::unique_ptr<uint32_t,decltype(&std::free)> output_indices(
            static_cast<uint32_t *>(std::malloc(reduced.size()*sizeof(uint32_t))),&std::free);
        if (!output_vertices || !output_indices) return false;
        std::memcpy(output_vertices.get(),vertices.data(),count*sizeof(AxSurfaceVertex));
        std::memcpy(output_indices.get(),reduced.data(),reduced.size()*sizeof(uint32_t));
        std::free(mesh->vertices); std::free(mesh->indices);
        *mesh=AxSurfaceMesh{output_vertices.release(),output_indices.release(),count,reduced.size()};
        result.triangles=reduced.size()/3;
        if (stats) *stats=result;
        return true;
    } catch (const std::bad_alloc &) { return false; }
      catch (const std::length_error &) { return false; }
}
