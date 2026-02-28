# Parallel Projection Y-Clip Bugfix

## Symptom

In `ProjectionMode.PARALLEL`, rendering became sparse/corrupted when the scene Y clip range crossed certain thresholds.

Observed examples:

- `min:0 max:512` was OK, but `min:0 max:513` broke.
- `min:1 max:513` was still broken.
- `min:16 max:513` became OK again.
- Similar behavior repeated around `528`, `544`, etc.

This matched octree depth changes caused by the clipped scene height.

## Root Cause

The important transition was not the literal values `513`, `529`, etc. by themselves, but the fact that those ranges caused Chunky to rebuild the world octree at a larger depth.

When that happened:

- Parallel primary rays were still generated correctly from Chunky's camera/projector logic.
- But many of those rays started very far away from the octree.
- In larger octrees (`depth >= 10`), the GPU path had to march from those far-away origins using `float` precision.
- That precision loss produced sparse/missing geometry in the OpenCL result.

The debugging logs showed that once the Y clip range crossed the problematic threshold, many important parallel rays had octree entry distances in the thousands of units before reaching the scene.

## Final Fix

The fix is implemented in:

- [ClCamera.java](E:\chunky%20opencl\chunky-opencl-main\src\main\java\dev\thatredox\chunkynative\opencl\renderer\scene\ClCamera.java)

For parallel projection only:

1. Generate the ray normally with `camera.calcViewRay(...)`.
2. Translate it into octree space with `ray.o.sub(scene.getOrigin())`.
3. If the ray actually intersects the octree bounds, move the ray origin forward to just inside the octree entry point.
4. Upload that adjusted ray to the GPU.

This keeps the same viewing direction and projection behavior, but avoids tracing from extremely distant origins in GPU `float` space.

## Why This Fix Was Kept

This approach was kept because it:

- preserves normal parallel projection behavior,
- fixes the sparse/corrupted rendering for the problematic Y-clip ranges,
- avoids invasive changes to the OpenCL octree traversal logic,
- avoids the broken approaches that pushed all parallel rays using a generic CPU-style `fixRay(...)`.

## Related Notes

Several attempted fixes were discarded because they caused regressions:

- forcing parallel rays through the pinhole kernel path,
- applying CPU-style parallel `fixRay(...)` too early or too aggressively,
- changing octree traversal offsets in ways that made the whole image sparse.

The final retained change is intentionally narrow: it only moves parallel rays that truly enter the octree, and only up to the octree entry point.

## Current State

The final plugin jar keeps:

- the Java 17 build alignment,
- the direct dependency on the local Chunky core jar,
- the OpenCL draw depth fix in `rayTracer.c`,
- the parallel-ray octree-entry adjustment in `ClCamera.java`.
