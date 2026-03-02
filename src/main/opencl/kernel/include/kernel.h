#ifndef CHUNKYCL_KERNEL_H
#define CHUNKYCL_KERNEL_H

#include "../opencl.h"
#include "rt.h"
#include "octree.h"
#include "block.h"
#include "constants.h"
#include "bvh.h"
#include "sky.h"

typedef struct {
    __global const int* meta;
    __global const int* cells;
    __global const int* indexes;
    __global const int* emitters;
} EmitterGrid;

EmitterGrid EmitterGrid_new(
        __global const int* meta,
        __global const int* cells,
        __global const int* indexes,
        __global const int* emitters
) {
    EmitterGrid g;
    g.meta = meta;
    g.cells = cells;
    g.indexes = indexes;
    g.emitters = emitters;
    return g;
}

bool EmitterGrid_hasData(EmitterGrid self) {
    return self.meta[0] > 0;
}

bool EmitterGrid_cellRange(EmitterGrid self, int3 worldPos, int* start, int* count) {
    if (!EmitterGrid_hasData(self)) {
        *start = 0;
        *count = 0;
        return false;
    }

    int cellSize = self.meta[0];
    int offsetX = self.meta[1];
    int sizeX = self.meta[2];
    int offsetY = self.meta[3];
    int sizeY = self.meta[4];
    int offsetZ = self.meta[5];
    int sizeZ = self.meta[6];

    int gridX = worldPos.x / cellSize;
    int gridY = worldPos.y / cellSize;
    int gridZ = worldPos.z / cellSize;

    if (gridX < offsetX || gridX >= offsetX + sizeX ||
            gridY < offsetY || gridY >= offsetY + sizeY ||
            gridZ < offsetZ || gridZ >= offsetZ + sizeZ) {
        *start = 0;
        *count = 0;
        return false;
    }

    int cellIndex = (((gridY - offsetY) * sizeX) + (gridX - offsetX)) * sizeZ + (gridZ - offsetZ);
    *start = self.cells[cellIndex * 2];
    *count = self.cells[cellIndex * 2 + 1];
    return *count > 0;
}

int4 EmitterGrid_getEmitter(EmitterGrid self, int emitterIndex) {
    int base = emitterIndex * 4;
    return (int4)(
            self.emitters[base + 0],
            self.emitters[base + 1],
            self.emitters[base + 2],
            self.emitters[base + 3]
    );
}

typedef struct {
    Octree octree;
    Octree waterOctree;
    Bvh worldBvh;
    Bvh actorBvh;
    BlockPalette blockPalette;
    MaterialPalette materialPalette;
    EmitterGrid emitterGrid;
    int drawDepth;
} Scene;


bool closestIntersect(Scene self, image2d_array_t atlas, Ray ray, IntersectionRecord* record, MaterialSample* sample, Material* mat) {
    bool hit = false;
    
    // 1. 優先測試 Octree (通常是場景中最密集的物體)
    if (Octree_octreeIntersect(self.octree, atlas, self.blockPalette, self.materialPalette, self.drawDepth, ray, record, sample)) {
        hit = true;
    }
    
    // 2. 測試水面 Octree (只有在距離比目前撞到的更短時才有意義)
    if (Octree_octreeIntersect(self.waterOctree, atlas, self.blockPalette, self.materialPalette, self.drawDepth, ray, record, sample)) {
        hit = true;
    }

    // 3. 測試 BVH (同樣只在更短的情況下更新 hit)
    // 注意：如果場景沒有實體，這部分會很快返回
    if (Bvh_intersect(self.worldBvh, atlas, self.materialPalette, ray, record, sample)) {
        hit = true;
    }
    
    if (Bvh_intersect(self.actorBvh, atlas, self.materialPalette, ray, record, sample)) {
        hit = true;
    }

    if (hit) {
        *mat = Material_get(self.materialPalette, record->material);
        return true;
    }
    
    return false;
}

void intersectSky(image2d_t skyTexture, float skyIntensity, Sun sun, image2d_array_t atlas, Ray ray, MaterialSample* sample) {
    Sky_intersect(skyTexture, skyIntensity, ray, sample);
    Sun_intersect(sun, atlas, ray, sample);
}

#endif
