#ifndef CHUNKYCLPLUGIN_BIOME_H
#define CHUNKYCLPLUGIN_BIOME_H

#include "../opencl.h"

#define BIOME_META_MIN_X 0
#define BIOME_META_MIN_Z 1
#define BIOME_META_SIZE_X 2
#define BIOME_META_SIZE_Z 3
#define BIOME_META_ORIGIN_X 4
#define BIOME_META_ORIGIN_Z 5
#define BIOME_META_DEFAULT_GRASS 6
#define BIOME_META_DEFAULT_FOLIAGE 9
#define BIOME_META_DEFAULT_DRY_FOLIAGE 12
#define BIOME_META_DEFAULT_WATER 15

typedef struct {
    __global const int* meta;
    __global const int* grid;
    __global const float* grass;
    __global const float* foliage;
    __global const float* dryFoliage;
    __global const float* water;
} BiomeColors;

BiomeColors BiomeColors_new(
        __global const int* meta,
        __global const int* grid,
        __global const float* grass,
        __global const float* foliage,
        __global const float* dryFoliage,
        __global const float* water
) {
    BiomeColors b;
    b.meta = meta;
    b.grid = grid;
    b.grass = grass;
    b.foliage = foliage;
    b.dryFoliage = dryFoliage;
    b.water = water;
    return b;
}

bool BiomeColors_hasData(BiomeColors self) {
    return self.meta[BIOME_META_SIZE_X] > 0 && self.meta[BIOME_META_SIZE_Z] > 0;
}

inline int biome_floor_div(int a, int b) {
    int q = a / b;
    int r = a % b;
    return (r != 0 && a < 0) ? (q - 1) : q;
}

inline float3 biome_default_color(BiomeColors self, int offset) {
    return (float3)(
            as_float(self.meta[offset + 0]),
            as_float(self.meta[offset + 1]),
            as_float(self.meta[offset + 2])
    );
}

inline float3 biome_sample(__global const float* data, int chunkIndex, int worldX, int worldZ) {
    int localX = worldX & 15;
    int localZ = worldZ & 15;
    int offset = (chunkIndex * 256 + localZ * 16 + localX) * 3;
    return (float3)(data[offset + 0], data[offset + 1], data[offset + 2]);
}

inline int biome_chunk_index(BiomeColors self, int worldX, int worldZ) {
    if (!BiomeColors_hasData(self)) {
        return -1;
    }

    int chunkX = biome_floor_div(worldX, 16);
    int chunkZ = biome_floor_div(worldZ, 16);

    int sizeX = self.meta[BIOME_META_SIZE_X];
    int sizeZ = self.meta[BIOME_META_SIZE_Z];
    int ix = chunkX - self.meta[BIOME_META_MIN_X];
    int iz = chunkZ - self.meta[BIOME_META_MIN_Z];
    if (ix < 0 || iz < 0 || ix >= sizeX || iz >= sizeZ) {
        return -1;
    }

    int gridIndex = iz * sizeX + ix;
    return self.grid[gridIndex];
}

float3 BiomeColors_getGrass(BiomeColors self, int3 worldPos) {
    int worldX = worldPos.x + self.meta[BIOME_META_ORIGIN_X];
    int worldZ = worldPos.z + self.meta[BIOME_META_ORIGIN_Z];
    int chunkIndex = biome_chunk_index(self, worldX, worldZ);
    if (chunkIndex < 0) {
        return biome_default_color(self, BIOME_META_DEFAULT_GRASS);
    }
    return biome_sample(self.grass, chunkIndex, worldX, worldZ);
}

float3 BiomeColors_getFoliage(BiomeColors self, int3 worldPos) {
    int worldX = worldPos.x + self.meta[BIOME_META_ORIGIN_X];
    int worldZ = worldPos.z + self.meta[BIOME_META_ORIGIN_Z];
    int chunkIndex = biome_chunk_index(self, worldX, worldZ);
    if (chunkIndex < 0) {
        return biome_default_color(self, BIOME_META_DEFAULT_FOLIAGE);
    }
    return biome_sample(self.foliage, chunkIndex, worldX, worldZ);
}

float3 BiomeColors_getDryFoliage(BiomeColors self, int3 worldPos) {
    int worldX = worldPos.x + self.meta[BIOME_META_ORIGIN_X];
    int worldZ = worldPos.z + self.meta[BIOME_META_ORIGIN_Z];
    int chunkIndex = biome_chunk_index(self, worldX, worldZ);
    if (chunkIndex < 0) {
        return biome_default_color(self, BIOME_META_DEFAULT_DRY_FOLIAGE);
    }
    return biome_sample(self.dryFoliage, chunkIndex, worldX, worldZ);
}

float3 BiomeColors_getWater(BiomeColors self, int3 worldPos) {
    int worldX = worldPos.x + self.meta[BIOME_META_ORIGIN_X];
    int worldZ = worldPos.z + self.meta[BIOME_META_ORIGIN_Z];
    int chunkIndex = biome_chunk_index(self, worldX, worldZ);
    if (chunkIndex < 0) {
        return biome_default_color(self, BIOME_META_DEFAULT_WATER);
    }
    return biome_sample(self.water, chunkIndex, worldX, worldZ);
}

#endif
