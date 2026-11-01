#pragma once
#include <stdbool.h>
#include <stdint.h>
/** Coordinate addressing unit, preserved by the save format; independent of terrain sampling resolution. */
#define AX_CHUNK_METERS 2.0
/** Planet-local position: signed chunk address plus meters in [0, AX_CHUNK_METERS). */
typedef struct { int64_t chunk[3]; double offset[3]; } AxPosition;
/** Normalizes chunk/offset without losing low-order bits; rejects overflow/nonfinite input atomically. */
bool ax_position_normalize(AxPosition *position);
/** Converts finite planet-local meters to a normalized position; rejects unrepresentable addresses. */
bool ax_position_from_meters(AxPosition *position, const double meters[3]);
/** Adds a finite displacement in meters; leaves position unchanged on overflow. */
bool ax_position_move(AxPosition *position, const double delta[3]);
/** Computes a-b in meters, subtracting chunk addresses before converting to floating point. */
void ax_position_difference(const AxPosition *a, const AxPosition *b, double meters[3]);
