#include "../opencl.h"
#include "octree.h"
#include "rt.h"
#include "block.h"
#include "material.h"
#include "kernel.h"
#include "camera.h"
#include "bvh.h"
#include "sky.h"

Ray ray_to_camera(
        const __global int* projectorType,
        const __global float* cameraSettings,
        const __global int* canvasConfig,
        int gid,
        Random random
) {
    Ray ray;
    if (*projectorType != -1) {
        float3 cameraPos = vload3(0, cameraSettings);
        float3 m1s = vload3(1, cameraSettings);
        float3 m2s = vload3(2, cameraSettings);
        float3 m3s = vload3(3, cameraSettings);

        int width = canvasConfig[0];
        int height = canvasConfig[1];
        int fullWidth = canvasConfig[2];
        int fullHeight = canvasConfig[3];
        int cropX = canvasConfig[4];
        int cropY = canvasConfig[5];

        float halfWidth = fullWidth / (2.0 * fullHeight);
        float invHeight = 1.0 / fullHeight;
        float x = -halfWidth + ((gid % width) + Random_nextFloat(random) + cropX) * invHeight;
        float y = -0.5 + ((gid / width) + Random_nextFloat(random) + cropY) * invHeight;

        switch (*projectorType) {
            case 0:
                ray = Camera_pinHole(x, y, random, cameraSettings + 12);
                break;
        }

        ray.direction = normalize((float3) (
                dot(m1s, ray.direction),
                        dot(m2s, ray.direction),
                        dot(m3s, ray.direction)
        ));
        ray.origin = (float3) (
                dot(m1s, ray.origin),
                        dot(m2s, ray.origin),
                        dot(m3s, ray.origin)
        );

        ray.origin += cameraPos;
    } else {
        ray = Camera_preGenerated(cameraSettings, gid);
    }
    return ray;
}

void initialize_ray_medium(Scene scene, Ray* ray) {
    int3 blockPos = intFloorFloat3(ray->origin);
    int block = Octree_get(&scene.octree, blockPos.x, blockPos.y, blockPos.z);
    if (block == 0) {
        int waterBlock = Octree_get(&scene.waterOctree, blockPos.x, blockPos.y, blockPos.z);
        if (waterBlock != 0) {
            int waterMaterial = BlockPalette_primaryMaterial(scene.blockPalette, waterBlock);
            Material waterMat = Material_get(scene.materialPalette, waterMaterial);
            if (!Material_isOpaque(waterMat)) {
                ray->prevMaterial = 0;
                ray->currentMaterial = waterMaterial;
                ray->prevBlock = 0;
                ray->currentBlock = waterBlock;
                return;
            }
        }
        ray->prevMaterial = 0;
        ray->currentMaterial = 0;
        ray->prevBlock = 0;
        ray->currentBlock = 0;
        return;
    }

    int material = BlockPalette_primaryMaterial(scene.blockPalette, block);
    Material currentMat = Material_get(scene.materialPalette, material);
    if (Material_isRefractive(currentMat) && !Material_isOpaque(currentMat)) {
        ray->prevMaterial = 0;
        ray->currentMaterial = material;
        ray->prevBlock = 0;
        ray->currentBlock = block;
    } else {
        ray->prevMaterial = 0;
        ray->currentMaterial = 0;
        ray->prevBlock = 0;
        ray->currentBlock = 0;
    }
}

float computeDiffuseProbability(float4 color, bool fancierTranslucency) {
    if (fancierTranslucency) {
        float maxComponent = fmax(color.x, fmax(color.y, color.z));
        return 1.0f - pow(1.0f - color.w, maxComponent);
    }
    return color.w;
}

float computeAbsorption(float4 color, float pDiffuse, bool fancierTranslucency) {
    if (fancierTranslucency) {
        return 1.0f - (1.0f - color.w) / (1.0f - pDiffuse + EPS);
    }
    return color.w;
}

float4 getDirectLightAttenuation(
        Scene scene,
        image2d_array_t textureAtlas,
        Ray ray,
        bool strictDirectLight
) {
    float4 attenuation = (float4) (1.0f, 1.0f, 1.0f, 1.0f);

    while (attenuation.w > 0.0f) {
        ray.origin += ray.direction * OFFSET;

        IntersectionRecord record = IntersectionRecord_new();
        MaterialSample sample;
        Material material;

        if (!closestIntersect(scene, textureAtlas, ray, &record, &sample, &material)) {
            break;
        }

        ray.prevMaterial = ray.currentMaterial;
        ray.prevBlock = ray.currentBlock;
        ray.currentMaterial = record.material;
        ray.currentBlock = record.block;

        float mult = 1.0f - sample.color.w;
        attenuation.x *= sample.color.x * sample.color.w + mult;
        attenuation.y *= sample.color.y * sample.color.w + mult;
        attenuation.z *= sample.color.z * sample.color.w + mult;
        attenuation.w *= mult;

        if (strictDirectLight) {
            Material prevMat = Material_get(scene.materialPalette, ray.prevMaterial);
            Material currentMat = Material_get(scene.materialPalette, ray.currentMaterial);
            if (fabs(Material_ior(prevMat) - Material_ior(currentMat)) >= EPS) {
                attenuation.w = 0.0f;
            }
        }

        ray.origin += ray.direction * record.distance;
    }

    return attenuation;
}

float3 sampleEmitterFace(
        Scene scene,
        image2d_array_t textureAtlas,
        float3 hitPoint,
        float3 shadingNormal,
        int4 emitter,
        int face,
        float faceScaler,
        float emitterIntensity,
        bool fancierTranslucency,
        float transmissivityCap,
        Random random
) {
    float3 localPos;
    float3 emitterNormal;
    float emitterArea;
    float2 uv = (float2)(Random_nextFloat(random), Random_nextFloat(random));
    if (!BlockPalette_sampleEmitterFace(scene.blockPalette, emitter.w, face, uv, &localPos, &emitterNormal, &emitterArea)) {
        return (float3)(0.0f);
    }

    float3 target = convert_float3((int3)(emitter.x, emitter.y, emitter.z)) + localPos;
    float3 toEmitter = target - hitPoint;
    float distance = length(toEmitter);
    if (distance <= EPS) {
        return (float3)(0.0f);
    }

    float3 direction = toEmitter / distance;
    if (dot(direction, shadingNormal) <= 0.0f) {
        return (float3)(0.0f);
    }

    Ray shadowRay;
    shadowRay.origin = hitPoint + direction * OFFSET;
    shadowRay.direction = direction;
    shadowRay.prevMaterial = 0;
    shadowRay.currentMaterial = 0;
    shadowRay.prevBlock = 0;
    shadowRay.currentBlock = 0;
    shadowRay.flags = RAY_INDIRECT;

    float traveled = 0.0f;
    float3 attenuation = (float3)(1.0f, 1.0f, 1.0f);
    while (traveled < distance) {
        IntersectionRecord record = IntersectionRecord_new();
        MaterialSample sample;
        Material material;
        if (!closestIntersect(scene, textureAtlas, shadowRay, &record, &sample, &material)) {
            return (float3)(0.0f);
        }

        traveled += record.distance;
        if (traveled >= distance - (2.0f * OFFSET)) {
            if (record.block != emitter.w || sample.emittance <= EPS) {
                return (float3)(0.0f);
            }
            float e = fabs(dot(direction, record.normal));
            e /= fmax(distance * distance, 1.0f);
            e *= emitterArea;
            e *= sample.emittance;
            e *= emitterIntensity;
            e *= faceScaler;
            return attenuation * sample.color.xyz * e;
        }

        float3 transmittance = Material_translucentTransmission(
                sample,
                sample.color.w,
                transmissivityCap,
                fancierTranslucency
        );
        if (Material_isRefractive(material) && !Material_isOpaque(material) && sample.color.w > EPS) {
            float colorAvg = (sample.color.x + sample.color.y + sample.color.z) / 3.0f;
            if (colorAvg > EPS) {
                float3 tintBoost = clamp(sample.color.xyz / colorAvg, (float3)(0.0f), (float3)(4.0f));
                float tintMix = clamp(sample.color.w * 2.25f, 0.0f, 1.0f);
                transmittance *= mix((float3)(1.0f), tintBoost, tintMix);
            }
        }
        attenuation *= transmittance;
        if (fmax(attenuation.x, fmax(attenuation.y, attenuation.z)) <= 1.0e-3f) {
            return (float3)(0.0f);
        }

        shadowRay.prevMaterial = shadowRay.currentMaterial;
        shadowRay.prevBlock = shadowRay.currentBlock;
        shadowRay.currentMaterial = record.material;
        shadowRay.currentBlock = record.block;
        shadowRay.origin += shadowRay.direction * (record.distance + OFFSET);
        traveled += OFFSET;
    }

    return (float3)(0.0f);
}

float3 sampleEmitters(
        Scene scene,
        image2d_array_t textureAtlas,
        float3 hitPoint,
        float3 shadingNormal,
        int strategy,
        float emitterIntensity,
        bool fancierTranslucency,
        float transmissivityCap,
        Random random
) {
    int start;
    int count;
    if (!EmitterGrid_cellRange(scene.emitterGrid, intFloorFloat3(hitPoint), &start, &count) || count <= 0) {
        return (float3)(0.0f);
    }

    float3 result = (float3)(0.0f);
    switch (strategy) {
        case 1:
        case 2: {
            int emitterListIndex = start + (int)(Random_nextFloat(random) * count);
            if (emitterListIndex >= start + count) {
                emitterListIndex = start + count - 1;
            }
            int emitterIndex = scene.emitterGrid.indexes[emitterListIndex];
            int4 emitter = EmitterGrid_getEmitter(scene.emitterGrid, emitterIndex);
            int faceCount = BlockPalette_emitterFaceCount(scene.blockPalette, emitter.w);
            if (faceCount <= 0) {
                return (float3)(0.0f);
            }
            if (strategy == 1) {
                int face = (int)(Random_nextFloat(random) * faceCount);
                if (face >= faceCount) {
                    face = faceCount - 1;
                }
                result += sampleEmitterFace(scene, textureAtlas, hitPoint, shadingNormal, emitter, face, M_PI_F, emitterIntensity, fancierTranslucency, transmissivityCap, random);
            } else {
                float scaler = M_PI_F / (float) faceCount;
                for (int face = 0; face < faceCount; face++) {
                    result += sampleEmitterFace(scene, textureAtlas, hitPoint, shadingNormal, emitter, face, scaler, emitterIntensity, fancierTranslucency, transmissivityCap, random);
                }
            }
            break;
        }
        case 3: {
            float emitterScaler = M_PI_F / (float) count;
            for (int i = 0; i < count; i++) {
                int emitterIndex = scene.emitterGrid.indexes[start + i];
                int4 emitter = EmitterGrid_getEmitter(scene.emitterGrid, emitterIndex);
                int faceCount = BlockPalette_emitterFaceCount(scene.blockPalette, emitter.w);
                if (faceCount <= 0) {
                    continue;
                }
                float faceScaler = emitterScaler / (float) faceCount;
                for (int face = 0; face < faceCount; face++) {
                    result += sampleEmitterFace(scene, textureAtlas, hitPoint, shadingNormal, emitter, face, faceScaler, emitterIntensity, fancierTranslucency, transmissivityCap, random);
                }
            }
            break;
        }
        default:
            break;
    }
    return result;
}

__kernel void render(
    __global const int* projectorType,
    __global const float* cameraSettings,

    __global const int* octreeDepth,
    __global const int* octreeData,
    __global const int* waterOctreeDepth,
    __global const int* waterOctreeData,

    __global const int* bPalette,
    __global const int* quadModels,
    __global const int* aabbModels,
    __global const int* waterModels,

    __global const int* worldBvhData,
    __global const int* actorBvhData,
    __global const int* bvhTrigs,

    image2d_array_t textureAtlas,
    __global const int* matPalette,
    __global const int* emitterGridMeta,
    __global const int* emitterGridCells,
    __global const int* emitterGridIndexes,
    __global const int* emitterGridEmitters,

    image2d_t skyTexture,
    __global const float* skyIntensity,
    __global const int* sunData,

    __global const int* randomSeed,
    __global const int* bufferSpp,
    __global const int* canvasConfig,
    __global const int* rayDepth,
    __global const float* sceneSettings,
    int emittersEnabled,
    float emitterIntensity,
    int emitterSamplingStrategy,
    int preventNormalEmitterWithSampling,
    __global float* res

) {
    int gid = get_global_id(0);

    Scene scene;
    scene.materialPalette = MaterialPalette_new(matPalette);
    scene.octree = Octree_create(octreeData, *octreeDepth);
    scene.waterOctree = Octree_create(waterOctreeData, *waterOctreeDepth);
    scene.worldBvh = Bvh_new(worldBvhData, bvhTrigs, &scene.materialPalette);
    scene.actorBvh = Bvh_new(actorBvhData, bvhTrigs, &scene.materialPalette);
    scene.blockPalette = BlockPalette_new(bPalette, quadModels, aabbModels, waterModels, &scene.materialPalette);
    scene.emitterGrid = EmitterGrid_new(emitterGridMeta, emitterGridCells, emitterGridIndexes, emitterGridEmitters);
    scene.drawDepth = 256;

    Sun sun = Sun_new(sunData);

    unsigned int randomState = *randomSeed + gid;
    Random random = &randomState;
    Random_nextState(random);
    Ray ray = ray_to_camera(projectorType, cameraSettings, canvasConfig, gid, random);

    initialize_ray_medium(scene, &ray);
    ray.flags = 0;

    float3 color = (float3) (0.0);
    float3 throughput = (float3) (1.0);
    float transmissivityCap = sceneSettings[0];
    bool fancierTranslucency = sceneSettings[1] > 0.5f;
    bool doSunSampling = sceneSettings[2] > 0.5f;
    bool sunLuminosity = sceneSettings[3] > 0.5f;
    bool strictDirectLight = sceneSettings[4] > 0.5f;
    int effectiveEmitterSamplingStrategy = emittersEnabled != 0 && emitterSamplingStrategy == 0 ? 2 : emitterSamplingStrategy;

    for (int depth = 0; depth < *rayDepth; depth++) {
        IntersectionRecord record = IntersectionRecord_new();
        MaterialSample sample;
        Material material;

        if (closestIntersect(scene, textureAtlas, ray, &record, &sample, &material)) {
            ray.prevMaterial = ray.currentMaterial;
            ray.prevBlock = ray.currentBlock;
            ray.currentMaterial = record.material;
            ray.currentBlock = record.block;

            Material currentMat = Material_get(scene.materialPalette, ray.currentMaterial);
            Material prevMat = Material_get(scene.materialPalette, ray.prevMaterial);
            float pSpecular = sample.specular;
            float pDiffuse = computeDiffuseProbability(sample.color, fancierTranslucency);
            float pAbsorb = computeAbsorption(sample.color, pDiffuse, fancierTranslucency);
            float n1 = Material_ior(prevMat);
            float n2 = Material_ior(currentMat);
            float3 hitPoint = ray.origin + ray.direction * record.distance;
            if (sample.color.w + pSpecular < EPS && fabs(n1 - n2) < EPS) {
                ray.origin = hitPoint + ray.direction * OFFSET;
                continue;
            }

            bool didSpecularBounce = true;
            bool doMetal = sample.metalness > EPS && sample.metalness > Random_nextFloat(random);
            if (doMetal) {
                throughput *= sample.color.xyz;
                ray.origin = hitPoint;
                ray.direction = _Material_specularReflection(record, sample, ray, random);
                ray.origin += ray.direction * OFFSET;
                ray.currentMaterial = ray.prevMaterial;
                ray.currentBlock = ray.prevBlock;
            } else if (pSpecular > EPS && pSpecular > Random_nextFloat(random)) {
                ray.origin = hitPoint;
                ray.direction = _Material_specularReflection(record, sample, ray, random);
                ray.origin += ray.direction * OFFSET;
                ray.currentMaterial = ray.prevMaterial;
                ray.currentBlock = ray.prevBlock;
            } else if (Random_nextFloat(random) < pDiffuse) {
                bool allowNormalEmitter = emittersEnabled != 0 &&
                        (!preventNormalEmitterWithSampling || effectiveEmitterSamplingStrategy == 0 || depth == 0);
                if (allowNormalEmitter && sample.emittance > EPS) {
                    color += throughput * sample.color.xyz * sample.color.xyz * sample.emittance * emitterIntensity;
                } else if (emittersEnabled != 0 &&
                        effectiveEmitterSamplingStrategy != 0 &&
                        sample.emittance <= EPS &&
                        Material_isOpaque(currentMat) &&
                        !Material_isRefractive(currentMat)) {
                    float3 emitterLight = sampleEmitters(
                            scene,
                            textureAtlas,
                            hitPoint,
                            record.normal,
                            effectiveEmitterSamplingStrategy,
                            emitterIntensity,
                            fancierTranslucency,
                            transmissivityCap,
                            random
                    );
                    color += throughput * sample.color.xyz * emitterLight;
                }

                if (doSunSampling) {
                    Ray sunRay = ray;
                    sunRay.origin = hitPoint;
                    sunRay.currentMaterial = ray.prevMaterial;
                    sunRay.currentBlock = ray.prevBlock;
                    sunRay.prevMaterial = ray.prevMaterial;
                    sunRay.prevBlock = ray.prevBlock;

                    if (Sun_sampleDirection(sun, &sunRay, random)) {
                        float frontLight = dot(sunRay.direction, record.normal);
                        if (frontLight > 0.0f) {
                            float4 attenuation = getDirectLightAttenuation(
                                    scene,
                                    textureAtlas,
                                    sunRay,
                                    strictDirectLight
                            );
                            if (attenuation.w > 0.0f) {
                                float mult = fabs(frontLight) * (sunLuminosity ? sun.luminosity : 1.0f);
                                float3 directLight = attenuation.xyz * attenuation.w * mult;
                                color += throughput * sample.color.xyz * directLight * Sun_emittance(sun);
                            }
                        }
                    }
                }

                throughput *= sample.color.xyz;
                ray.origin = hitPoint;
                ray.direction = _Material_diffuseReflection(record, random);
                ray.origin += ray.direction * OFFSET;
                ray.currentMaterial = ray.prevMaterial;
                ray.currentBlock = ray.prevBlock;
                didSpecularBounce = false;
            } else if (fabs(n1 - n2) >= EPS) {
                bool doRefraction = Material_isRefractive(currentMat) || Material_isRefractive(prevMat);
                float n1n2 = n1 / n2;
                float cosTheta = -dot(record.normal, ray.direction);
                float radicand = 1 - n1n2 * n1n2 * (1 - cosTheta * cosTheta);

                if (doRefraction && radicand < EPS) {
                    ray.origin = hitPoint;
                    ray.direction = _Material_specularReflection(record, sample, ray, random);
                    ray.origin += ray.direction * OFFSET;
                    ray.currentMaterial = ray.prevMaterial;
                    ray.currentBlock = ray.prevBlock;
                } else {
                    float a = n1n2 - 1;
                    float b = n1n2 + 1;
                    float R0 = (a * a) / (b * b);
                    float c = 1 - cosTheta;
                    float Rtheta = R0 + (1 - R0) * c * c * c * c * c;

                    if (Random_nextFloat(random) < Rtheta) {
                        ray.origin = hitPoint;
                        ray.direction = _Material_specularReflection(record, sample, ray, random);
                        ray.origin += ray.direction * OFFSET;
                        ray.currentMaterial = ray.prevMaterial;
                        ray.currentBlock = ray.prevBlock;
                    } else {
                        throughput *= Material_translucentTransmission(sample, pAbsorb, transmissivityCap, fancierTranslucency);
                        ray.origin = hitPoint;
                        if (doRefraction) {
                            ray.direction = Material_refractDirection(record, ray, n1, n2);
                        }
                        ray.origin += ray.direction * OFFSET;
                    }
                }
            } else {
                throughput *= Material_translucentTransmission(sample, pAbsorb, transmissivityCap, fancierTranslucency);
                ray.origin = hitPoint + ray.direction * OFFSET;
            }

            if (!didSpecularBounce) {
                ray.flags |= RAY_INDIRECT;
            }
        } else {
            intersectSky(skyTexture, *skyIntensity, sun, textureAtlas, ray, &sample);
            throughput *= sample.color.xyz;
            color += sample.emittance * throughput;
            break;
        }
    }

    int spp = *bufferSpp;
    float3 bufferColor = vload3(gid, res);
    bufferColor = (bufferColor * spp + color) / (spp + 1);
    vstore3(bufferColor, gid, res);
}

__kernel void preview(
    __global const int* projectorType,
    __global const float* cameraSettings,

    __global const int* octreeDepth,
    __global const int* octreeData,
    __global const int* waterOctreeDepth,
    __global const int* waterOctreeData,

    __global const int* bPalette,
    __global const int* quadModels,
    __global const int* aabbModels,
    __global const int* waterModels,

    __global const int* worldBvhData,
    __global const int* actorBvhData,
    __global const int* bvhTrigs,

    image2d_array_t textureAtlas,
    __global const int* matPalette,

    image2d_t skyTexture,
    __global const float* skyIntensity,
    __global const int* sunData,

    __global const int* canvasConfig,
    __global int* res
) {
    int gid = get_global_id(0);

    int px = gid % canvasConfig[0] + canvasConfig[4];
    int py = gid / canvasConfig[0] + canvasConfig[5];

    // Crosshairs?
    if ((px == canvasConfig[2] / 2 && (py >= canvasConfig[3] / 2 - 5 && py <= canvasConfig[3] / 2 + 5)) ||
        (py == canvasConfig[3] / 2 && (px >= canvasConfig[2] / 2 - 5 && px <= canvasConfig[2] / 2 + 5))) {
        res[gid] = 0xFFFFFFFF;
        return;
    }

    Scene scene;
    scene.materialPalette = MaterialPalette_new(matPalette);
    scene.octree = Octree_create(octreeData, *octreeDepth);
    scene.waterOctree = Octree_create(waterOctreeData, *waterOctreeDepth);
    scene.worldBvh = Bvh_new(worldBvhData, bvhTrigs, &scene.materialPalette);
    scene.actorBvh = Bvh_new(actorBvhData, bvhTrigs, &scene.materialPalette);
    scene.blockPalette = BlockPalette_new(bPalette, quadModels, aabbModels, waterModels, &scene.materialPalette);
    scene.emitterGrid = EmitterGrid_new(bPalette, bPalette, bPalette, bPalette);
    scene.drawDepth = 256;

    Sun sun = Sun_new(sunData);

    unsigned int randomState = 0;
    Random random = &randomState;
    Random_nextState(random);

    Ray ray = ray_to_camera(projectorType, cameraSettings, canvasConfig, gid, random);

    IntersectionRecord record = IntersectionRecord_new();
    MaterialSample sample;
    Material material;

    initialize_ray_medium(scene, &ray);
    ray.flags = RAY_PREVIEW;

    float3 color;
    if (closestIntersect(scene, textureAtlas, ray, &record, &sample, &material)) {
        float shading = dot(record.normal, (float3) (0.25, 0.866, 0.433));
        shading = fmax(0.3f, shading);
        color = sample.color.xyz * shading;
    } else {
        intersectSky(skyTexture, *skyIntensity, sun, textureAtlas, ray, &sample);
        color = sample.color.xyz;
    }

    color = sqrt(color);
    int3 rgb = intFloorFloat3(clamp(color * 255.0f, 0.0f, 255.0f));
    res[gid] = 0xFF000000 | (rgb.x << 16) | (rgb.y << 8) | rgb.z;
}
