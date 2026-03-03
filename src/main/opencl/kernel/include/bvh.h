#ifndef CHUNKYCL_PLUGIN_BVH
#define CHUNKYCL_PLUGIN_BVH

#include "../opencl.h"
#include "material.h"
#include "primitives.h"

typedef struct {
    __global const int* bvh;
    __global const int* trigs;
    MaterialPalette* materialPalette;
} Bvh;

Bvh Bvh_new(__global const int* bvh, __global const int* trigs, MaterialPalette* materialPalette) {
    Bvh b;
    b.bvh = bvh;
    b.trigs = trigs;
    b.materialPalette = materialPalette;
    return b;
}

bool Bvh_intersect(Bvh self, image2d_array_t atlas, MaterialPalette palette, Ray ray, IntersectionRecord* record, MaterialSample* sample);

#endif
