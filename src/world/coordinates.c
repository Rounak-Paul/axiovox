#include "coordinates.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
bool ax_position_normalize(AxPosition *position)
{
    if (!position) return false;
    AxPosition next = *position;
    for (size_t i = 0; i < 3; ++i) {
        double carry = floor(next.offset[i] / AX_CHUNK_METERS);
        if (!isfinite(carry) || carry < -0x1p63 || carry >= 0x1p63) return false;
        int64_t shift = (int64_t)carry;
        if ((shift > 0 && next.chunk[i] > INT64_MAX - shift) ||
            (shift < 0 && next.chunk[i] < INT64_MIN - shift)) return false;
        next.chunk[i] += shift;
        next.offset[i] -= carry * AX_CHUNK_METERS;
        if (!(next.offset[i] >= 0.0 && next.offset[i] < AX_CHUNK_METERS)) return false;
    }
    *position = next;
    return true;
}
bool ax_position_from_meters(AxPosition *position, const double meters[3])
{
    if (!position || !meters) return false;
    AxPosition next = {0};
    memcpy(next.offset, meters, sizeof(next.offset));
    if (!ax_position_normalize(&next)) return false;
    *position = next;
    return true;
}
bool ax_position_move(AxPosition *position, const double delta[3])
{
    if (!position || !delta) return false;
    AxPosition next = *position;
    for (size_t i = 0; i < 3; ++i) next.offset[i] += delta[i];
    if (!ax_position_normalize(&next)) return false;
    *position = next;
    return true;
}
void ax_position_difference(const AxPosition *a, const AxPosition *b, double meters[3])
{
    for (size_t i = 0; i < 3; ++i) {
        bool negative = a->chunk[i] < b->chunk[i];
        uint64_t magnitude = negative ? (uint64_t)b->chunk[i] - (uint64_t)a->chunk[i] :
                                        (uint64_t)a->chunk[i] - (uint64_t)b->chunk[i];
        meters[i] = (negative ? -(double)magnitude : (double)magnitude) * AX_CHUNK_METERS + a->offset[i] - b->offset[i];
    }
}
