#include "octree.h"
#include <stdlib.h>
#include <string.h>
#define AX_CHILD_FAR 0x10000u
#define AX_VALUE_POINTER_LIMIT 0xffffffu
typedef enum { CHILD_EMPTY, CHILD_LEAF, CHILD_NODE } ChildKind;
typedef struct { ChildKind kind; uint32_t color, node; } Child;
typedef struct {
    uint32_t children[8], colors[8];
    uint8_t valid, nonleaf;
} Node;
typedef struct {
    const AxVoxelGrid *grid;
    Node *nodes;
    uint64_t *subtree;
    uint8_t *far_slots;
    uint32_t count, capacity;
    uint64_t value_count;
    AxOctree *octree;
    uint32_t value_cursor;
    bool failed;
} Builder;
/** Counts set bits in a child mask. */
static uint32_t popcount8(uint32_t mask)
{
    mask &= 0xffu;
    mask = mask - ((mask >> 1) & 0x55u);
    mask = (mask & 0x33u) + ((mask >> 2) & 0x33u);
    return (mask + (mask >> 4)) & 0x0fu;
}
bool ax_voxel_grid_create(AxVoxelGrid *grid, uint32_t size)
{
    if (!grid) return false;
    *grid = (AxVoxelGrid){0};
    if (size < 2 || size > (1u << AX_OCTREE_MAX_DEPTH) || (size & (size - 1))) return false;
    size_t edge = size;
    if (edge > SIZE_MAX / edge || edge * edge > SIZE_MAX / edge) return false;
    grid->cells = calloc(edge * edge * edge, 1);
    if (!grid->cells) return false;
    grid->size = size;
    return true;
}
void ax_voxel_grid_destroy(AxVoxelGrid *grid)
{
    if (!grid) return;
    free(grid->cells);
    *grid = (AxVoxelGrid){0};
}
/** Returns the linear cell index for in-bounds coordinates. */
static size_t cell_index(const AxVoxelGrid *grid, int64_t x, int64_t y, int64_t z)
{
    size_t edge = grid->size;
    return ((size_t)z * edge + (size_t)y) * edge + (size_t)x;
}
/** Tests that coordinates fall inside the grid. */
static bool in_bounds(uint32_t size, int64_t x, int64_t y, int64_t z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < size && y < size && z < size;
}
void ax_voxel_grid_set(AxVoxelGrid *grid, int64_t x, int64_t y, int64_t z, uint8_t material)
{
    if (grid && grid->cells && in_bounds(grid->size, x, y, z)) grid->cells[cell_index(grid, x, y, z)] = material;
}
uint8_t ax_voxel_grid_get(const AxVoxelGrid *grid, int64_t x, int64_t y, int64_t z)
{
    return grid && grid->cells && in_bounds(grid->size, x, y, z) ? grid->cells[cell_index(grid, x, y, z)] : 0;
}
/** Appends an interior node, growing storage geometrically. */
static uint32_t add_node(Builder *b, const Node *node)
{
    if (b->count == b->capacity) {
        uint32_t capacity = b->capacity ? b->capacity * 2 : 4096;
        if (capacity <= b->capacity || (size_t)capacity > SIZE_MAX / sizeof(Node)) { b->failed = true; return 0; }
        Node *nodes = realloc(b->nodes, (size_t)capacity * sizeof(Node));
        if (!nodes) { b->failed = true; return 0; }
        b->nodes = nodes;
        b->capacity = capacity;
    }
    b->nodes[b->count] = *node;
    b->value_count += popcount8(node->valid);
    return b->count++;
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
/** Classifies a cubic region bottom-up, emitting interior nodes in post-order (children before parents). */
static Child classify(Builder *b, uint32_t x, uint32_t y, uint32_t z, uint32_t size)
{
    if (size == 1) {
        uint8_t material = b->grid->cells[cell_index(b->grid, x, y, z)];
        return material ? (Child){CHILD_LEAF, b->grid->palette[material] | 0xff000000u, 0} : (Child){CHILD_EMPTY, 0, 0};
    }
    uint32_t half = size / 2;
    Child children[8];
    bool empty = true, uniform = true;
    for (uint32_t i = 0; i < 8; ++i) {
        children[i] = classify(b, x + (i & 1u) * half, y + ((i >> 1) & 1u) * half, z + ((i >> 2) & 1u) * half, half);
        if (b->failed) return (Child){CHILD_EMPTY, 0, 0};
        empty &= children[i].kind == CHILD_EMPTY;
        uniform &= children[i].kind == CHILD_LEAF && children[i].color == children[0].color;
    }
    if (empty) return (Child){CHILD_EMPTY, 0, 0};
    if (uniform) return children[0];
    Node node = {0};
    for (uint32_t i = 0; i < 8; ++i) {
        if (children[i].kind == CHILD_EMPTY) continue;
        node.valid |= (uint8_t)(1u << i);
        node.colors[i] = children[i].color;
        if (children[i].kind == CHILD_NODE) {
            node.nonleaf |= (uint8_t)(1u << i);
            node.children[i] = children[i].node;
        }
    }
    uint32_t index = add_node(b, &node);
    return (Child){CHILD_NODE, average_color(children), index};
}
/**
 * Computes descriptor counts below every node and the far slots each child group needs.
 * A child group is laid out as [members][far slots][member subtrees in slot order].
 */
static bool measure(Builder *b)
{
    b->subtree = calloc(b->count, sizeof(*b->subtree));
    b->far_slots = calloc(b->count, 1);
    if (!b->subtree || !b->far_slots) return false;
    for (uint32_t n = 0; n < b->count; ++n) {
        const Node *node = &b->nodes[n];
        uint32_t members = popcount8(node->nonleaf), far = 0, needed;
        do {
            needed = 0;
            uint64_t prefix = 0;
            for (uint32_t slot = 0, k = 0; slot < 8; ++slot) {
                if (!(node->nonleaf & (1u << slot))) continue;
                uint64_t child = b->subtree[node->children[slot]];
                if (child && members + far - k + prefix > AX_OCTREE_NEAR_LIMIT) ++needed;
                prefix += child;
                ++k;
            }
            if (needed > far) far = needed;
            else break;
        } while (true);
        uint64_t total = members + far;
        for (uint32_t slot = 0; slot < 8; ++slot)
            if (node->nonleaf & (1u << slot)) total += b->subtree[node->children[slot]];
        if (total > UINT32_MAX) return false;
        b->subtree[n] = total;
        b->far_slots[n] = (uint8_t)far;
    }
    return true;
}
/** Writes one descriptor plus its attachment lookup entry and color values. */
static void write_descriptor(Builder *b, uint32_t index, const Node *node, uint32_t pointer, bool far)
{
    uint32_t *words = b->octree->words;
    words[index * 2] = (pointer << 17) | (far ? AX_CHILD_FAR : 0u) | ((uint32_t)node->valid << 8) | node->nonleaf;
    words[index * 2 + 1] = 0;
    words[b->octree->lookup_offset + index] = (b->value_cursor << 8) | node->valid;
    for (uint32_t slot = 0; slot < 8; ++slot)
        if (node->valid & (1u << slot)) words[b->octree->value_offset + b->value_cursor++] = node->colors[slot];
}
/** Emits the child group of a node starting at a descriptor index, then recurses into member subtrees. */
static void emit_group(Builder *b, uint32_t parent, uint32_t start)
{
    const Node *node = &b->nodes[parent];
    uint32_t members = popcount8(node->nonleaf), far = b->far_slots[parent], used_far = 0;
    uint32_t targets[8] = {0}, nodes[8] = {0};
    uint64_t prefix = 0;
    for (uint32_t slot = 0, k = 0; slot < 8; ++slot) {
        if (!(node->nonleaf & (1u << slot))) continue;
        uint32_t child = node->children[slot], index = start + k;
        uint32_t target = (uint32_t)(start + members + far + prefix);
        uint32_t pointer = 0;
        bool is_far = false;
        if (b->subtree[child]) {
            uint32_t offset = target - index;
            if (offset <= AX_OCTREE_NEAR_LIMIT) pointer = offset;
            else {
                uint32_t far_index = start + members + used_far++;
                b->octree->words[far_index * 2] = offset;
                pointer = far_index - index;
                is_far = true;
                ++b->octree->far_count;
            }
        }
        write_descriptor(b, index, &b->nodes[child], pointer, is_far);
        targets[k] = target;
        nodes[k] = child;
        prefix += b->subtree[child];
        ++k;
    }
    for (uint32_t k = 0; k < members; ++k)
        if (b->subtree[nodes[k]]) emit_group(b, nodes[k], targets[k]);
}
bool ax_octree_build(AxOctree *octree, const AxVoxelGrid *grid)
{
    if (!octree) return false;
    *octree = (AxOctree){0};
    if (!grid || !grid->cells || grid->size < 2 || grid->size > (1u << AX_OCTREE_MAX_DEPTH) || (grid->size & (grid->size - 1))) return false;
    Builder b = {.grid = grid, .octree = octree};
    Child top = classify(&b, 0, 0, 0, grid->size);
    bool success = false;
    if (b.failed) goto cleanup;
    if (top.kind != CHILD_NODE) {
        Node root = {0};
        if (top.kind == CHILD_LEAF) {
            root.valid = 0xff;
            for (int i = 0; i < 8; ++i) root.colors[i] = top.color;
        }
        top.node = add_node(&b, &root);
        if (b.failed) goto cleanup;
    }
    if (top.node != b.count - 1 || b.value_count > AX_VALUE_POINTER_LIMIT || !measure(&b)) goto cleanup;
    uint64_t descriptors = 1 + b.subtree[top.node];
    uint64_t words = descriptors * 3 + b.value_count;
    if (words > UINT32_MAX || words > SIZE_MAX / sizeof(uint32_t)) goto cleanup;
    octree->words = calloc((size_t)words, sizeof(uint32_t));
    if (!octree->words) goto cleanup;
    octree->word_count = (size_t)words;
    octree->descriptor_count = (uint32_t)descriptors;
    octree->lookup_offset = (uint32_t)(descriptors * 2);
    octree->value_offset = (uint32_t)(descriptors * 3);
    octree->value_count = (uint32_t)b.value_count;
    octree->size = grid->size;
    for (uint32_t size = grid->size; size > 1; size >>= 1) ++octree->depth;
    write_descriptor(&b, 0, &b.nodes[top.node], b.subtree[top.node] ? 1u : 0u, false);
    if (b.subtree[top.node]) emit_group(&b, top.node, 1);
    success = b.value_cursor == octree->value_count;
cleanup:
    free(b.nodes);
    free(b.subtree);
    free(b.far_slots);
    if (!success) ax_octree_destroy(octree);
    return success;
}
void ax_octree_destroy(AxOctree *octree)
{
    if (!octree) return;
    free(octree->words);
    *octree = (AxOctree){0};
}
bool ax_octree_sample(const AxOctree *octree, int64_t x, int64_t y, int64_t z, uint32_t *color)
{
    if (!octree || !octree->words || !in_bounds(octree->size, x, y, z)) return false;
    uint32_t index = 0;
    for (uint32_t half = octree->size >> 1; half; half >>= 1) {
        if (index >= octree->descriptor_count) return false;
        uint32_t slot = ((uint32_t)x & half ? 1u : 0u) | ((uint32_t)y & half ? 2u : 0u) | ((uint32_t)z & half ? 4u : 0u);
        uint32_t bit = 1u << slot, descriptor = octree->words[index * 2];
        if (!((descriptor >> 8) & bit)) return false;
        if (!(descriptor & bit)) {
            uint32_t lookup = octree->words[octree->lookup_offset + index];
            if (!(lookup & bit)) return false;
            uint32_t value = (lookup >> 8) + popcount8(lookup & (bit - 1u));
            if (value >= octree->value_count) return false;
            if (color) *color = octree->words[octree->value_offset + value];
            return true;
        }
        uint32_t offset = descriptor >> 17;
        if (descriptor & AX_CHILD_FAR) {
            if (index + offset >= octree->descriptor_count) return false;
            offset = octree->words[(index + offset) * 2];
        }
        index += offset + popcount8(descriptor & (bit - 1u));
    }
    return false;
}
