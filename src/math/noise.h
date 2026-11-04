#pragma once
#include <stdint.h>
/** Immutable seeded permutation table; build once, share freely between sampling workers. */
typedef struct { uint8_t permutation[512]; uint64_t seed; } AxNoise;
/** Coherent sample and analytic derivatives with respect to input coordinates. */
typedef struct { double value, gradient[3]; } AxNoiseSample;
/** Deterministically scrambles an unsigned seed or lattice identity. */
uint64_t ax_noise_hash64(uint64_t value);
/** Initializes noise from seed without shared RNG state. */
void ax_noise_init(AxNoise *noise, uint64_t seed);
/** Returns continuous gradient noise in [-1,1]; invalid/out-of-range coordinates return zero. */
AxNoiseSample ax_noise_gradient3(const AxNoise *noise, const double position[3]);
/** Returns quintic-interpolated value noise in [-1,1] at position. */
double ax_noise_value3(const AxNoise *noise, const double position[3]);
/** Returns normalized octave noise; ridged selects 1-abs(noise), octaves is bounded to 1..8. */
double ax_noise_fractal3(const AxNoise *noise, const double position[3], unsigned octaves,
                         double lacunarity, double gain, int ridged);
/** Returns nearest feature-point distance for seeded 3D cellular noise; invalid positions return zero. */
double ax_noise_cellular3(const AxNoise *noise, const double position[3]);
/** Warps position by three decorrelated coherent channels, bounded by amplitude per axis. */
void ax_noise_warp3(const AxNoise *noise, const double position[3], double amplitude, double warped[3]);

/** Returns bounded [0,1] erosion-inspired ridges at position; coarse gradients steer up to eight finer octaves. */
double ax_noise_erosion3(const AxNoise *noise,const double position[3],unsigned octaves);
