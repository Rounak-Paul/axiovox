#include "octree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHILD_FAR 0x10000u
#define SPLIT_LEVEL 3
#define LEAF_MARGIN 0.12f
#define NODE_MARGIN 0.01f
#define USEFUL_SLAB 0.95f
typedef struct { float n[3], pos, thick; } Slab;
typedef enum { CHILD_EMPTY, CHILD_LEAF, CHILD_NODE } ChildKind;
typedef struct {
    ChildKind kind;
    uint32_t color, node, contour;
    bool has_contour;
    Slab slab;
} Child;
typedef struct { uint32_t color, contour, node; } Entry;
typedef struct { uint32_t first, subtree; uint8_t valid, nonleaf, contour_mask, far_slots; } Node;
typedef struct {
    Node *nodes;
    Entry *entries;
    uint32_t node_count, node_capacity, entry_count, entry_capacity;
    uint64_t values, contours;
    bool failed;
} Arena;
typedef struct { int32_t cell[3]; uint32_t level; } Region;
typedef struct { const AxVoxelSource *source; float voxel; } Sampler;
typedef struct { const Sampler *sampler; Region region; Arena arena; Child result; } Task;
typedef struct { AxOctree *octree; const Arena *arena; uint32_t value_cursor, contour_cursor; } Writer;
static const Child empty_child = {CHILD_EMPTY, 0, 0, 0, false, {{0, 0, 0}, 0, 0}};
/** Counts set bits in a child mask. */
static uint32_t popcount8(uint32_t mask)
{
    mask &= 0xffu;
    mask = mask - ((mask >> 1) & 0x55u);
    mask = (mask & 0x33u) + ((mask >> 2) & 0x33u);
    return (mask + (mask >> 4)) & 0x0fu;
}
/** Returns the dot product of two 3-vectors. */
static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
/** Computes a region's side length and minimum corner in world units. */
static float region_bounds(const Sampler *sm, const Region *r, float min[3])
{
    for (int i = 0; i < 3; ++i) min[i] = (float)r->cell[i] * sm->voxel;
    return (float)(1u << (sm->source->depth - r->level)) * sm->voxel;
}
/** Returns the region of child slot i (bit 0 = x, bit 1 = y, bit 2 = z). */
static Region child_region(const Sampler *sm, const Region *r, uint32_t slot)
{
    int32_t half = (int32_t)(1u << (sm->source->depth - r->level - 1));
    Region child = {{r->cell[0] + (int32_t)(slot & 1u) * half, r->cell[1] + (int32_t)((slot >> 1) & 1u) * half,
        r->cell[2] + (int32_t)((slot >> 2) & 1u) * half}, r->level + 1};
    return child;
}
/** Quantizes a direction into 6-bit signed components scaled to use the full range; false if degenerate. */
static bool quantize_normal(const float g[3], int32_t bits[3], float n[3])
{
    float m = fmaxf(fabsf(g[0]), fmaxf(fabsf(g[1]), fabsf(g[2])));
    if (!(m > 1e-20f) || !isfinite(m)) return false;
    for (int i = 0; i < 3; ++i) {
        long b = lroundf(g[i] / m * 31.0f);
        bits[i] = (int32_t)(b < -32 ? -32 : b > 31 ? 31 : b);
        n[i] = (float)bits[i] / 32.0f;
    }
    return true;
}
/**
 * Encodes the tightest representable slab enclosing [hmin, hmax] along n (rounded outward).
 * @return false when the slab cannot be represented or would not cut the cube meaningfully.
 */
static bool encode_slab(const int32_t bits[3], const float n[3], float hmin, float hmax, float s, uint32_t *value, Slab *slab)
{
    if (!(hmin <= hmax) || !isfinite(hmin) || !isfinite(hmax)) return false;
    float unit_pos = 1.5f * s / 64.0f, unit_thick = 0.75f * s / 64.0f;
    long pos_bits = lroundf((hmin + hmax) * 0.5f / unit_pos);
    pos_bits = pos_bits < -64 ? -64 : pos_bits > 63 ? 63 : pos_bits;
    float pos = (float)pos_bits * unit_pos;
    float need = fmaxf(hmax - pos, pos - hmin);
    float thick_bits = ceilf(need / unit_thick);
    if (thick_bits < 1.0f) thick_bits = 1.0f;
    if (!(thick_bits <= 127.0f)) return false;
    float thick = thick_bits * unit_thick;
    float width = s * (fabsf(n[0]) + fabsf(n[1]) + fabsf(n[2]));
    if (2.0f * thick >= USEFUL_SLAB * width) return false;
    *value = ((uint32_t)thick_bits << 25) | (((uint32_t)pos_bits & 0x7fu) << 18) |
             (((uint32_t)bits[0] & 0x3fu) << 12) | (((uint32_t)bits[1] & 0x3fu) << 6) | ((uint32_t)bits[2] & 0x3fu);
    *slab = (Slab){{n[0], n[1], n[2]}, pos, thick};
    return true;
}
/** Decodes a contour value for a voxel of side s, matching the shader. */
static Slab decode_slab(uint32_t value, float s)
{
    int32_t word = (int32_t)value;
    Slab slab = {{(float)((int32_t)((uint32_t)word << 14) >> 26) / 32.0f, (float)((int32_t)((uint32_t)word << 20) >> 26) / 32.0f,
        (float)((int32_t)((uint32_t)word << 26) >> 26) / 32.0f},
        (float)((int32_t)((uint32_t)word << 7) >> 25) * 1.5f * s / 64.0f, (float)(value >> 25) * 0.75f * s / 64.0f};
    return slab;
}
/**
 * Computes the extent of dir.(x - ref) over the cube optionally intersected with a slab,
 * by enumerating the polytope's candidate vertices (inside corners and edge-plane crossings).
 * @return false when the polytope is empty.
 */
static bool polytope_extent(const float min[3], float s, const Slab *slab, const float dir[3], const float ref[3], float *lo, float *hi)
{
    float corners[8][3], center[3] = {min[0] + s * 0.5f, min[1] + s * 0.5f, min[2] + s * 0.5f}, plane[8];
    bool found = false;
    *lo = INFINITY; *hi = -INFINITY;
    for (uint32_t k = 0; k < 8; ++k) {
        for (int i = 0; i < 3; ++i) corners[k][i] = min[i] + ((k >> i) & 1u ? s : 0.0f);
        float rel[3] = {corners[k][0] - center[0], corners[k][1] - center[1], corners[k][2] - center[2]};
        plane[k] = slab ? dot3(slab->n, rel) - slab->pos : 0.0f;
        if (slab && fabsf(plane[k]) > slab->thick * 1.00001f + 1e-7f * s) continue;
        float value = dir[0] * (corners[k][0] - ref[0]) + dir[1] * (corners[k][1] - ref[1]) + dir[2] * (corners[k][2] - ref[2]);
        *lo = fminf(*lo, value); *hi = fmaxf(*hi, value); found = true;
    }
    if (!slab) return found;
    for (uint32_t k = 0; k < 8; ++k)
        for (uint32_t axis = 1; axis < 8; axis <<= 1) {
            if (k & axis) continue;
            uint32_t j = k | axis;
            for (int side = -1; side <= 1; side += 2) {
                float a0 = plane[k] - (float)side * slab->thick, a1 = plane[j] - (float)side * slab->thick;
                if ((a0 < 0.0f) == (a1 < 0.0f) || a0 == a1) continue;
                float t = a0 / (a0 - a1), value = 0.0f;
                for (int i = 0; i < 3; ++i) value += dir[i] * (corners[k][i] + (corners[j][i] - corners[k][i]) * t - ref[i]);
                *lo = fminf(*lo, value); *hi = fmaxf(*hi, value); found = true;
            }
        }
    return found;
}
/** Evaluates the source distance; non-finite results are treated as far away. */
static float distance_at(const Sampler *sm, const float p[3])
{
    float d = sm->source->distance(sm->source->user, p);
    return isfinite(d) ? d : INFINITY;
}
/**
 * Classifies one finest-level voxel from its corners and center, fitting a contour around the
 * Newton-projected surface samples. Voxels whose slab misses the cube are dropped.
 */
static Child leaf(const Sampler *sm, const Region *r, float center_distance)
{
    float min[3], s = region_bounds(sm, r, min), q[9][3], f[9], fmin = INFINITY, fmax = -INFINITY;
    float c[3] = {min[0] + s * 0.5f, min[1] + s * 0.5f, min[2] + s * 0.5f};
    for (uint32_t k = 0; k < 9; ++k) {
        for (int i = 0; i < 3; ++i) q[k][i] = k == 8 ? c[i] : min[i] + ((k >> i) & 1u ? s : 0.0f);
        f[k] = k == 8 ? center_distance : distance_at(sm, q[k]);
        fmin = fminf(fmin, f[k]); fmax = fmaxf(fmax, f[k]);
    }
    if (!isfinite(fmin) || !isfinite(fmax)) return empty_child;
    float g[3] = {0, 0, 0};
    for (uint32_t k = 0; k < 8; ++k)
        for (int i = 0; i < 3; ++i) g[i] += ((k >> i) & 1u ? f[k] : -f[k]) / (4.0f * s);
    float length = sqrtf(dot3(g, g));
    bool crossing = fmin <= 0.0f && fmax >= 0.0f, near = false;
    float unit[3] = {0.0f, 1.0f, 0.0f}, n[3], hmin = INFINITY, hmax = -INFINITY;
    int32_t bits[3];
    bool oriented = length > 1e-12f && quantize_normal(g, bits, n);
    if (oriented) {
        for (int i = 0; i < 3; ++i) unit[i] = g[i] / length;
        for (uint32_t k = 0; k < 9; ++k) {
            float offset = f[k] / length, x[3], rel[3];
            if (fabsf(offset) > s) continue;
            bool inside = true;
            for (int i = 0; i < 3; ++i) {
                x[i] = q[k][i] - unit[i] * offset;
                inside &= x[i] >= min[i] - 0.15f * s && x[i] <= min[i] + 1.15f * s;
                rel[i] = x[i] - c[i];
            }
            if (!inside) continue;
            float h = dot3(n, rel);
            hmin = fminf(hmin, h); hmax = fmaxf(hmax, h);
            near |= fabsf(offset) <= 0.25f * s;
        }
    }
    if (!crossing && !near) return empty_child;
    Child child = {CHILD_LEAF, 0, 0, 0, false, {{0, 0, 0}, 0, 0}};
    if (AX_WORLD_CONTOURS && oriented && hmin <= hmax) {
        float margin = LEAF_MARGIN * s * sqrtf(dot3(n, n));
        child.has_contour = encode_slab(bits, n, hmin - margin, hmax + margin, s, &child.contour, &child.slab);
        float lo, hi;
        if (child.has_contour && !polytope_extent(min, s, &child.slab, n, c, &lo, &hi)) return empty_child;
    }
    if (!child.has_contour && !crossing) return empty_child;
    float surface[3], offset = oriented ? f[8] / length : 0.0f;
    for (int i = 0; i < 3; ++i) {
        surface[i] = c[i] - unit[i] * offset;
        surface[i] = fminf(fmaxf(surface[i], min[i]), min[i] + s);
    }
    child.color = sm->source->color(sm->source->user, surface, unit, r->cell) | 0xff000000u;
    return child;
}
/** Appends to a growable array, flagging the arena on overflow or allocation failure. */
static bool reserve(Arena *arena, void **data, uint32_t *capacity, uint32_t count, uint32_t extra, size_t size)
{
    if (count <= UINT32_MAX - extra && count + extra <= *capacity) return true;
    if (count > UINT32_MAX - extra) { arena->failed = true; return false; }
    uint64_t wanted = *capacity ? (uint64_t)*capacity * 2 : 4096;
    while (wanted < (uint64_t)count + extra) wanted *= 2;
    if (wanted > UINT32_MAX) wanted = UINT32_MAX;
    if (wanted > SIZE_MAX / size) { arena->failed = true; return false; }
    void *grown = realloc(*data, (size_t)wanted * size);
    if (!grown) { arena->failed = true; return false; }
    *data = grown;
    *capacity = (uint32_t)wanted;
    return true;
}
/** Averages valid child colors per channel into an opaque ABGR color. */
static uint32_t average_color(const Child children[8])
{
    uint32_t sum[3] = {0}, count = 0;
    for (int i = 0; i < 8; ++i) {
        if (children[i].kind == CHILD_EMPTY) continue;
        for (int c = 0; c < 3; ++c) sum[c] += (children[i].color >> (8 * c)) & 0xffu;
        ++count;
    }
    return 0xff000000u | ((sum[2] / count) << 16) | ((sum[1] / count) << 8) | (sum[0] / count);
}
/**
 * Computes the far slots a child group needs. A group is laid out as [members][far slots][member subtrees].
 * @return false when the subtree exceeds 32-bit descriptor addressing.
 */
static bool measure(const Arena *arena, const Child children[8], uint32_t *far_slots, uint32_t *subtree)
{
    uint32_t members = 0, far = 0, needed;
    for (int i = 0; i < 8; ++i) members += children[i].kind == CHILD_NODE;
    do {
        needed = 0;
        uint64_t prefix = 0;
        for (uint32_t slot = 0, k = 0; slot < 8; ++slot) {
            if (children[slot].kind != CHILD_NODE) continue;
            uint64_t size = arena->nodes[children[slot].node].subtree;
            if (size && members + far - k + prefix > AX_OCTREE_NEAR_LIMIT) ++needed;
            prefix += size;
            ++k;
        }
        if (needed <= far) break;
        far = needed;
    } while (true);
    uint64_t total = members + far;
    for (int i = 0; i < 8; ++i)
        if (children[i].kind == CHILD_NODE) total += arena->nodes[children[i].node].subtree;
    if (total > UINT32_MAX) return false;
    *far_slots = far;
    *subtree = (uint32_t)total;
    return true;
}
/**
 * Emits an interior node for a region and fits the region's own contour so that it encloses every
 * child's cube-slab polytope; children are referenced in the same arena.
 */
static Child make_node(Arena *arena, const Sampler *sm, const Region *r, const Child children[8])
{
    uint32_t valid = 0, nonleaf = 0, contours = 0, count = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        if (children[i].kind == CHILD_EMPTY) continue;
        valid |= 1u << i; ++count;
        if (children[i].kind == CHILD_NODE) nonleaf |= 1u << i;
        if (children[i].has_contour) contours |= 1u << i;
    }
    if (!valid || arena->failed) return empty_child;
    uint32_t far = 0, subtree = 0;
    if (!measure(arena, children, &far, &subtree)) { arena->failed = true; return empty_child; }
    if (!reserve(arena, (void **)&arena->entries, &arena->entry_capacity, arena->entry_count, count, sizeof(Entry)) ||
        !reserve(arena, (void **)&arena->nodes, &arena->node_capacity, arena->node_count, 1, sizeof(Node))) return empty_child;
    Node node = {arena->entry_count, subtree, (uint8_t)valid, (uint8_t)nonleaf, (uint8_t)contours, (uint8_t)far};
    for (uint32_t i = 0; i < 8; ++i)
        if (valid & (1u << i)) arena->entries[arena->entry_count++] = (Entry){children[i].color, children[i].contour, children[i].node};
    uint32_t index = arena->node_count;
    arena->nodes[arena->node_count++] = node;
    arena->values += count;
    arena->contours += popcount8(contours);

    Child result = {CHILD_NODE, average_color(children), index, 0, false, {{0, 0, 0}, 0, 0}};
    if (!AX_WORLD_CONTOURS) return result;
    float min[3], s = region_bounds(sm, r, min), c[3] = {min[0] + s * 0.5f, min[1] + s * 0.5f, min[2] + s * 0.5f};
    float g[3], step = s * 0.25f, n[3], hmin = INFINITY, hmax = -INFINITY;
    for (int i = 0; i < 3; ++i) {
        float a[3] = {c[0], c[1], c[2]}, b[3] = {c[0], c[1], c[2]};
        a[i] += step; b[i] -= step;
        g[i] = distance_at(sm, a) - distance_at(sm, b);
    }
    int32_t bits[3];
    if (!quantize_normal(g, bits, n)) return result;
    for (uint32_t i = 0; i < 8; ++i) {
        if (!(valid & (1u << i))) continue;
        Region cr = child_region(sm, r, i);
        float cmin[3], cs = region_bounds(sm, &cr, cmin), lo, hi;
        if (!polytope_extent(cmin, cs, children[i].has_contour ? &children[i].slab : NULL, n, c, &lo, &hi)) continue;
        hmin = fminf(hmin, lo); hmax = fmaxf(hmax, hi);
    }
    float margin = NODE_MARGIN * s * sqrtf(dot3(n, n));
    result.has_contour = encode_slab(bits, n, hmin - margin, hmax + margin, s, &result.contour, &result.slab);
    return result;
}
/** Recursively builds a region bottom-up, culling regions the surface cannot reach via the Lipschitz bound. */
static Child visit(Arena *arena, const Sampler *sm, const Region *r)
{
    if (arena->failed) return empty_child;
    float min[3], s = region_bounds(sm, r, min), c[3] = {min[0] + s * 0.5f, min[1] + s * 0.5f, min[2] + s * 0.5f};
    float distance = distance_at(sm, c);
    if (!(fabsf(distance) <= sm->source->lipschitz * s * 0.8660254f)) return empty_child;
    if (r->level == sm->source->depth) return leaf(sm, r, distance);
    Child children[8];
    for (uint32_t i = 0; i < 8; ++i) {
        Region cr = child_region(sm, r, i);
        children[i] = visit(arena, sm, &cr);
        if (arena->failed) return empty_child;
    }
    return make_node(arena, sm, r, children);
}
/** Worker entry: builds one split-level subtree into the task's private arena. */
static void run_task(void *user)
{
    Task *task = user;
    task->result = visit(&task->arena, task->sampler, &task->region);
}
/** Concatenates task arenas into one, rebasing node and entry indices so children still precede parents. */
static bool merge(Arena *merged, Task *tasks, uint32_t task_count)
{
    uint64_t nodes = 0, entries = 0;
    for (uint32_t i = 0; i < task_count; ++i) {
        if (tasks[i].arena.failed) return false;
        nodes += tasks[i].arena.node_count; entries += tasks[i].arena.entry_count;
    }
    if (nodes >= UINT32_MAX - 4096 || entries >= UINT32_MAX - 4096) return false;
    if (!reserve(merged, (void **)&merged->nodes, &merged->node_capacity, 0, (uint32_t)nodes + 1, sizeof(Node)) ||
        !reserve(merged, (void **)&merged->entries, &merged->entry_capacity, 0, (uint32_t)entries + 1, sizeof(Entry))) return false;
    for (uint32_t t = 0; t < task_count; ++t) {
        Arena *a = &tasks[t].arena;
        uint32_t node_base = merged->node_count, entry_base = merged->entry_count;
        for (uint32_t n = 0; n < a->node_count; ++n) {
            Node node = a->nodes[n];
            for (uint32_t slot = 0, k = 0; slot < 8; ++slot) {
                if (!(node.valid & (1u << slot))) continue;
                if (node.nonleaf & (1u << slot)) a->entries[node.first + k].node += node_base;
                ++k;
            }
            node.first += entry_base;
            merged->nodes[merged->node_count++] = node;
        }
        if (a->entry_count) memcpy(merged->entries + entry_base, a->entries, (size_t)a->entry_count * sizeof(Entry));
        merged->entry_count += a->entry_count;
        merged->values += a->values; merged->contours += a->contours;
        if (tasks[t].result.kind == CHILD_NODE) tasks[t].result.node += node_base;
    }
    return true;
}
/** Builds levels above the split level on the owner thread from the merged task results. */
static Child combine(Arena *arena, const Sampler *sm, const Region *r, const Task *tasks, uint32_t split)
{
    if (r->level == split) {
        uint32_t shift = sm->source->depth - split, side = 1u << split;
        uint32_t x = (uint32_t)r->cell[0] >> shift, y = (uint32_t)r->cell[1] >> shift, z = (uint32_t)r->cell[2] >> shift;
        return tasks[(x * side + y) * side + z].result;
    }
    Child children[8];
    for (uint32_t i = 0; i < 8; ++i) {
        Region cr = child_region(sm, r, i);
        children[i] = combine(arena, sm, &cr, tasks, split);
        if (arena->failed) return empty_child;
    }
    return make_node(arena, sm, r, children);
}
/** Writes one descriptor with its lookup entry, color values, and contour values. */
static void write_descriptor(Writer *w, uint32_t index, const Node *node, uint32_t pointer, bool far)
{
    uint32_t *words = w->octree->words;
    words[index * 2] = (pointer << 17) | (far ? CHILD_FAR : 0u) | ((uint32_t)node->valid << 8) | node->nonleaf;
    words[index * 2 + 1] = (w->contour_cursor << 8) | node->contour_mask;
    words[w->octree->lookup_offset + index] = (w->value_cursor << 8) | node->valid;
    for (uint32_t slot = 0, k = 0; slot < 8; ++slot) {
        if (!(node->valid & (1u << slot))) continue;
        const Entry *entry = &w->arena->entries[node->first + k++];
        words[w->octree->value_offset + w->value_cursor++] = entry->color;
        if (node->contour_mask & (1u << slot)) words[w->octree->contour_offset + w->contour_cursor++] = entry->contour;
    }
}
/** Emits the child group of a node starting at a descriptor index, then recurses into member subtrees. */
static void emit_group(Writer *w, uint32_t parent, uint32_t start)
{
    const Node *node = &w->arena->nodes[parent];
    uint32_t members = popcount8(node->nonleaf), used_far = 0, targets[8] = {0}, nodes[8] = {0};
    uint64_t prefix = 0;
    for (uint32_t slot = 0, k = 0, e = 0; slot < 8; ++slot) {
        if (!(node->valid & (1u << slot))) continue;
        const Entry *entry = &w->arena->entries[node->first + e++];
        if (!(node->nonleaf & (1u << slot))) continue;
        uint32_t child = entry->node, index = start + k;
        uint32_t target = (uint32_t)(start + members + node->far_slots + prefix), pointer = 0;
        bool is_far = false;
        if (w->arena->nodes[child].subtree) {
            uint32_t offset = target - index;
            if (offset <= AX_OCTREE_NEAR_LIMIT) pointer = offset;
            else {
                uint32_t far_index = start + members + used_far++;
                w->octree->words[far_index * 2] = offset;
                pointer = far_index - index;
                is_far = true;
                ++w->octree->far_count;
            }
        }
        write_descriptor(w, index, &w->arena->nodes[child], pointer, is_far);
        targets[k] = target;
        nodes[k] = child;
        prefix += w->arena->nodes[child].subtree;
        ++k;
    }
    for (uint32_t k = 0; k < members; ++k)
        if (w->arena->nodes[nodes[k]].subtree) emit_group(w, nodes[k], targets[k]);
}
/** Runs every split-level task on the pool, falling back to the calling thread when the queue is full or absent. */
static void run_tasks(Task *tasks, uint32_t count, AxJobs *jobs)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (jobs && ax_jobs_submit(jobs, run_task, &tasks[i])) continue;
        if (jobs) {
            ax_jobs_wait(jobs);
            if (ax_jobs_submit(jobs, run_task, &tasks[i])) continue;
        }
        run_task(&tasks[i]);
    }
    if (jobs) ax_jobs_wait(jobs);
}
bool ax_octree_build(AxOctree *octree, const AxVoxelSource *source, AxJobs *jobs)
{
    if (!octree) return false;
    *octree = (AxOctree){0};
    if (!source || !source->distance || !source->color || !isfinite(source->extent) || !(source->extent > 0.0f) ||
        source->depth < 1 || source->depth > AX_OCTREE_MAX_DEPTH || !isfinite(source->lipschitz) || !(source->lipschitz > 0.0f)) return false;
    Sampler sampler = {source, source->extent / (float)(1u << source->depth)};
    uint32_t split = source->depth > SPLIT_LEVEL + 1 ? SPLIT_LEVEL : 0, task_count = 1u << (3 * split);
    uint32_t side = 1u << split, shift = source->depth - split;
    Task *tasks = calloc(task_count, sizeof(*tasks));
    Arena arena = {0};
    bool success = false;
    if (!tasks) return false;
    for (uint32_t i = 0; i < task_count; ++i) {
        uint32_t x = i / (side * side), y = (i / side) % side, z = i % side;
        tasks[i] = (Task){.sampler = &sampler, .region = {{(int32_t)(x << shift), (int32_t)(y << shift), (int32_t)(z << shift)}, split}};
    }
    run_tasks(tasks, task_count, jobs);
    if (!merge(&arena, tasks, task_count)) goto cleanup;
    Region root_region = {{0, 0, 0}, 0};
    Child root = combine(&arena, &sampler, &root_region, tasks, split);
    if (arena.failed) goto cleanup;
    if (root.kind != CHILD_NODE) {
        if (!reserve(&arena, (void **)&arena.nodes, &arena.node_capacity, arena.node_count, 1, sizeof(Node))) goto cleanup;
        root.node = arena.node_count;
        arena.nodes[arena.node_count++] = (Node){arena.entry_count, 0, 0, 0, 0, 0};
    }
    if (root.node != arena.node_count - 1 || arena.values > AX_OCTREE_POINTER_LIMIT || arena.contours > AX_OCTREE_POINTER_LIMIT) goto cleanup;
    const Node *top = &arena.nodes[root.node];
    uint64_t descriptors = 1 + (uint64_t)top->subtree;
    uint64_t words = descriptors * 3 + arena.values + arena.contours;
    if (words > UINT32_MAX || words > SIZE_MAX / sizeof(uint32_t)) goto cleanup;
    octree->words = calloc((size_t)words, sizeof(uint32_t));
    if (!octree->words) goto cleanup;
    octree->word_count = (size_t)words;
    octree->descriptor_count = (uint32_t)descriptors;
    octree->lookup_offset = (uint32_t)(descriptors * 2);
    octree->value_offset = (uint32_t)(descriptors * 3);
    octree->contour_offset = (uint32_t)(descriptors * 3 + arena.values);
    octree->value_count = (uint32_t)arena.values;
    octree->contour_count = (uint32_t)arena.contours;
    octree->depth = source->depth;
    octree->size = 1u << source->depth;
    octree->extent = source->extent;
    Writer writer = {octree, &arena, 0, 0};
    write_descriptor(&writer, 0, top, top->subtree ? 1u : 0u, false);
    if (top->subtree) emit_group(&writer, root.node, 1);
    success = writer.value_cursor == octree->value_count && writer.contour_cursor == octree->contour_count;
cleanup:
    for (uint32_t i = 0; i < task_count; ++i) { free(tasks[i].arena.nodes); free(tasks[i].arena.entries); }
    free(tasks);
    free(arena.nodes);
    free(arena.entries);
    if (!success) ax_octree_destroy(octree);
    return success;
}
void ax_octree_destroy(AxOctree *octree)
{
    if (!octree) return;
    free(octree->words);
    *octree = (AxOctree){0};
}
bool ax_octree_sample(const AxOctree *octree, const float p[3], uint32_t *color)
{
    if (!octree || !octree->words || !p) return false;
    for (int i = 0; i < 3; ++i)
        if (!(p[i] >= 0.0f && p[i] < octree->extent)) return false;
    uint32_t index = 0;
    float min[3] = {0, 0, 0}, size = octree->extent;
    for (uint32_t level = 0; level < octree->depth; ++level) {
        if (index >= octree->descriptor_count) return false;
        float half = size * 0.5f;
        uint32_t slot = 0;
        for (int i = 0; i < 3; ++i)
            if (p[i] >= min[i] + half) { slot |= 1u << i; min[i] += half; }
        uint32_t bit = 1u << slot, descriptor = octree->words[index * 2], contour = octree->words[index * 2 + 1];
        if (!((descriptor >> 8) & bit)) return false;
        if (contour & bit) {
            uint32_t at = (contour >> 8) + popcount8(contour & (bit - 1u));
            if (at >= octree->contour_count) return false;
            Slab slab = decode_slab(octree->words[octree->contour_offset + at], half);
            float rel[3] = {p[0] - min[0] - half * 0.5f, p[1] - min[1] - half * 0.5f, p[2] - min[2] - half * 0.5f};
            if (fabsf(dot3(slab.n, rel) - slab.pos) > slab.thick + 1e-5f * half) return false;
        }
        if (!(descriptor & bit)) {
            uint32_t lookup = octree->words[octree->lookup_offset + index];
            if (!(lookup & bit)) return false;
            uint32_t value = (lookup >> 8) + popcount8(lookup & (bit - 1u));
            if (value >= octree->value_count) return false;
            if (color) *color = octree->words[octree->value_offset + value];
            return true;
        }
        uint32_t offset = descriptor >> 17;
        if (descriptor & CHILD_FAR) {
            if (index + offset >= octree->descriptor_count) return false;
            offset = octree->words[(index + offset) * 2];
        }
        index += offset + popcount8(descriptor & (bit - 1u));
        size = half;
    }
    return false;
}
