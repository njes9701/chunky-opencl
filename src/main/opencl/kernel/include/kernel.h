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
    float distance = 0;

    for (int i = 0; i < self.drawDepth; i++) {
        IntersectionRecord tempRecord = *record;
        tempRecord.distance = record->distance - distance;

        Ray tempRay = ray;
        tempRay.origin += distance * tempRay.direction;

        if (tempRecord.distance <= 0) {
            return false;
        }

        bool hit = false;
        hit |= Octree_octreeIntersect(self.octree, atlas, self.blockPalette, self.materialPalette, self.drawDepth, tempRay, &tempRecord, sample);
        hit |= Octree_octreeIntersect(self.waterOctree, atlas, self.blockPalette, self.materialPalette, self.drawDepth, tempRay, &tempRecord, sample);
        hit |= Bvh_intersect(self.worldBvh, atlas, self.materialPalette, tempRay, &tempRecord, sample);
        hit |= Bvh_intersect(self.actorBvh, atlas, self.materialPalette, tempRay, &tempRecord, sample);

        if (!hit) {
            return false;
        }

        distance += tempRecord.distance;

        *record = tempRecord;
        record->distance = distance;
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
