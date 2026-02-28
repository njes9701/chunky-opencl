# Water Render Fixes

## 中文說明

### 問題現象

在這個插件裡，水原本幾乎等於「沒接上」：

1. 世界方塊是從 `worldOctree` 讀。
2. 但 Chunky 的水不是放在 `worldOctree`，而是放在獨立的 `waterOctree`。
3. 同時，水也不是普通 `full block`、`AABB model` 或 `Quad model`。
4. 流動水有自己的四角高度資料，需要專用幾何。

所以結果不是只有「材質不對」，而是：

- GPU 根本沒有讀到水那棵 octree。
- 就算讀到 palette 裡有 `minecraft:water`，原本的 block export 也沒有水專用模型去表達流動形狀。

### 根因

這次確認到的根因有兩個：

1. `waterOctree` 沒有被導出到 OpenCL。
2. 水需要獨立的模型類型，不能直接走普通方塊路徑。

Chunky CPU 端的水方塊是 `se.llbit.chunky.block.minecraft.Water`，而且是 `localIntersect=true` 的特殊方塊。  
它的流動外形來自四個角的高度資料，不是固定立方體。

### 這次的修法

這次修的是「先讓水顯示出來」，不包含完整水下光學。

#### 1. 新增水專用打包模型

新增：

- [PackedWaterModel.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\models\PackedWaterModel.java)

這個結構目前只打包兩件事：

- 水材質 reference
- `Water.data`

其中 `Water.data` 會保留：

- `full block` 狀態
- 四角高度資料

這樣 GPU 端就能根據同一份資料重建靜水與流動水形狀。

#### 2. 在 block export 裡給水一個新 model type

修改：

- [PackedBlock.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\primitives\PackedBlock.java)

原本 `PackedBlock` 只認：

- `0 = invisible`
- `1 = full block`
- `2 = AABB`
- `3 = quad`
- `4 = invisible light block`

現在新增：

- `5 = water model`

當 block 是 `Water` 時，直接走 `PackedWaterModel`，而不是誤塞進一般方塊路徑。

#### 3. 同時導出 world octree 和 water octree

修改：

- [AbstractSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\AbstractSceneLoader.java)
- [ClSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\opencl\renderer\ClSceneLoader.java)

原本只會載入：

- `scene.getWorldOctree()`

現在會同時載入：

- `scene.getWorldOctree()`
- `scene.getWaterOctree()`

而且兩棵樹會各自傳進 OpenCL：

- `octreeDepth / octreeData`
- `waterOctreeDepth / waterOctreeData`

#### 4. OpenCL Scene 同時和兩棵 octree 求交

修改：

- [kernel.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\kernel.h)

`Scene` 結構現在新增：

- `Octree waterOctree`

`closestIntersect(...)` 現在會同時測：

- `world octree`
- `water octree`
- `world BVH`
- `actor BVH`

這一步很重要，因為如果只傳了 `waterOctree` 但不參與最近命中比較，水仍然不會正確出現在畫面上。

#### 5. 在 kernel 裡重建水幾何

修改：

- [block.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\block.h)

這次沒有去抄別的實作，而是直接按 Chunky 本地水資料做一個最小可用版：

- `full block` 水：走整塊 AABB 命中
- 非 `full block` 水：用四角高度重建 top/bottom/四側三角面

目前 kernel 會根據 `Water.data` 解析：

- `corner SW`
- `corner SE`
- `corner NE`
- `corner NW`
- `full block` flag

然後在 GPU 端做三角形求交，讓流動水的斜面形狀先能被畫出來。

#### 6. 相機起點若在水裡，也會先帶入當前材質

修改：

- [rayTracer.c](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\rayTracer.c)

`initialize_ray_medium(...)` 原本只檢查 `world octree`。  
現在如果世界樹沒有 block，也會再檢查 `waterOctree`，讓相機起點在水裡時至少不會完全失去介質狀態。

### 這次刻意沒有做的事

這次是「先顯示出來」版本，所以刻意沒有把下面幾件事一起做：

1. 水中光線行走
2. 正確水面反射 / Fresnel
3. 水下可見距離與吸收
4. 水面法線擾動與波紋
5. water plane
6. 完整 CPU 對齊的 `isInWater(...)` / `enterBlock(...)` / `exitWater(...)` 路徑

也就是說，這版目標只有：

- 水要能看到
- 流動水形狀要先成立

而不是：

- 水已經完整和 CPU 視覺一致

### 目前已知限制

目前這版水渲染仍有這些限制：

1. 先以「顯示幾何」為主，外觀還不會完全像 CPU。
2. 沒有完整水下散射、吸收與視距。
3. 沒有真正的水面波紋 normal/displacement。
4. 水與其他透明介質之間的細節還沒完整對齊。

### 涉及檔案

- [PackedWaterModel.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\models\PackedWaterModel.java)
- [PackedBlock.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\primitives\PackedBlock.java)
- [AbstractSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\AbstractSceneLoader.java)
- [ClSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\opencl\renderer\ClSceneLoader.java)
- [OpenClPathTracingRenderer.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\opencl\renderer\OpenClPathTracingRenderer.java)
- [OpenClPreviewRenderer.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\opencl\renderer\OpenClPreviewRenderer.java)
- [kernel.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\kernel.h)
- [block.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\block.h)
- [rayTracer.c](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\rayTracer.c)

## English Summary

### Problem

Water was not merely shaded incorrectly. It was effectively missing from the GPU path for two separate reasons:

1. Chunky stores water in a separate `waterOctree`, not in the main `worldOctree`.
2. Water is not a normal cube/AABB/quad block. Flowing water uses per-corner height data and needs custom geometry.

### Root Cause

The previous OpenCL plugin only loaded the main world octree and had no dedicated water model type.  
That meant:

- the GPU never traversed the actual water tree,
- and even if a water block existed in the palette, there was no representation for flowing water shape.

### Fix Strategy

This fix intentionally targets only the first milestone: make water visible with the correct basic block shape.

It does not yet implement full water optics.

### What Was Added

#### 1. Dedicated packed water model

Added:

- [PackedWaterModel.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\models\PackedWaterModel.java)

This stores:

- a packed water material reference,
- the original `Water.data` field.

That `data` preserves both the full-block flag and the four corner height values used by flowing water.

#### 2. New block model type for water

Updated:

- [PackedBlock.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\primitives\PackedBlock.java)

Water now uses a dedicated `modelType = 5` instead of falling through the generic block path.

#### 3. Export both octrees

Updated:

- [AbstractSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\common\export\AbstractSceneLoader.java)
- [ClSceneLoader.java](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\java\dev\thatredox\chunkynative\opencl\renderer\ClSceneLoader.java)

The renderer now uploads both:

- world octree
- water octree

instead of only the world octree.

#### 4. Scene intersection now includes water octree

Updated:

- [kernel.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\kernel.h)

`closestIntersect(...)` now checks the water octree alongside the world octree and BVHs, so water can actually compete as the nearest visible surface.

#### 5. Water geometry reconstruction in the kernel

Updated:

- [block.h](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\block.h)

The kernel now reconstructs:

- full water blocks as a full AABB,
- flowing water as top/bottom/side triangles derived from the four corner heights.

This is the key step that makes flowing water shapes appear instead of rendering everything as a cube or not rendering it at all.

#### 6. Camera-start medium initialization also checks water

Updated:

- [rayTracer.c](C:\Users\njes9\IdeaProjects\chunky-opencl\src\main\opencl\kernel\include\rayTracer.c)

If the camera starts inside water, the initial medium state can now be picked up from the water octree as well.

### Deliberately Out Of Scope

This patch does not yet implement:

1. underwater light transport,
2. proper water reflection/Fresnel,
3. underwater visibility/absorption,
4. water surface normal/displacement shading,
5. water plane support,
6. full CPU-equivalent water traversal behavior.

### Current Goal

The goal of this patch is intentionally narrow:

- make water render,
- make flowing water shape render,
- postpone full water optics for a later pass.
