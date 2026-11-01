#pragma once
#include "math/noise.h"
#include <stdbool.h>
/** Generation identities are persisted; waves retain existing saved-world geometry. */
typedef enum { AX_TERRAIN_WAVES,AX_TERRAIN_CONTINENTAL,AX_TERRAIN_CRATERED,AX_TERRAIN_COUNT } AxTerrainKind;
/** Immutable composed field settings, independent of scene ownership and render LOD. */
typedef struct {
    AxTerrainKind kind;
    AxNoise broad,landforms,detail;
    double radius,relief,frequency,cave_depth,phase[3];
} AxTerrain;
/** Surface classification data, evaluated only when requested. */
typedef struct { double elevation,temperature,moisture; } AxTerrainClimate;
/** Validates and initializes immutable recipe state; invalid controls leave terrain unchanged. */
bool ax_terrain_init(AxTerrain *terrain,AxTerrainKind kind,uint64_t seed,double radius,double relief,double frequency,double cave_depth);
/** Returns bounded radial displacement; position is planet-local meters. climate may be NULL. */
double ax_terrain_height(const AxTerrain *terrain,const double position[3],AxTerrainClimate *climate);
/** Samples negative-inside solid density, including bounded subsurface caves. */
double ax_terrain_density(const AxTerrain *terrain,const double position[3],AxTerrainClimate *climate);
