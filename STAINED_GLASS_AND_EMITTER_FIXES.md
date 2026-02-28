# Stained Glass And Emitter Fixes

## 目的

這份文件記錄這次對 `chunky-opencl` 插件中：

1. 普通玻璃 / 染色玻璃 / 玻璃板
2. 可見發光方塊與 Emitter 行為

所做的修復、原因與目前保留的限制。

---

## 一、玻璃渲染修復

### 1. 補上玻璃材質資料導出

修復前，GPU 端雖然有拿到玻璃幾何，但材質資訊不完整，導致玻璃容易被當成一般表面而不是折射介質。

這次補上的重點：

- `PackedMaterial` 現在會正確帶出：
  - `refractive`
  - `opaque`
  - `ior`
- 對 `opaque=false` 且 `ior` 明顯不是空氣的材質，也會視為有效折射材質。
- 透明 / 折射材質強制保留逐像素 color texture，避免因單色平均貼圖丟失 alpha。

相關檔案：

- `src/main/java/dev/thatredox/chunkynative/common/export/primitives/PackedMaterial.java`

### 2. 修復整塊玻璃從內部出射時抓錯面

這是玻璃透明度問題的核心修復。

原本 OpenCL AABB 相交邏輯在 ray 從方塊內部出發時，仍優先取 `tmin`，這對整塊玻璃會抓到負的入口面，而不是正確的出口面，結果就是：

- 背面丟失
- 透射路徑不成立
- 整塊玻璃看起來像實心或黑框

修復後，若 ray 起點在 AABB 內部，會改取 `tmax` 作為有效命中面。

相關檔案：

- `src/main/opencl/kernel/include/primitives.h`

### 3. 修復透明像素命中時的背面黑框

普通玻璃貼圖有大量 `alpha=0` 的透明像素。

原本 OpenCL 端在 `allowTransparentHit=true` 時，即使像素是完全透明，也會把原始 `rgb=0,0,0` 帶進材質採樣，結果背面邊框被當成黑色表面累積出來。

修復後：

- 透明像素若仍需保留幾何命中，會改成
  - `rgb=1,1,1`
  - `alpha=0`
- 這樣可以保留介質界面，但不再渲出黑色背面框線。

相關檔案：

- `src/main/opencl/kernel/include/material.h`
- `src/main/opencl/kernel/include/block.h`

### 4. 初始化相機所在介質

原本相機 ray 一律從空氣開始，導致當攝影機已經位於玻璃內部時，折射界面順序錯誤。

修復後：

- 在 ray 生成後，會先檢查相機所在 octree block
- 若起點落在折射且非 opaque 的材質中，會先把 `currentMaterial/currentBlock` 初始化為該介質

這能修正：

- 相機在玻璃內部時的錯誤反射/折射

相關檔案：

- `src/main/opencl/kernel/include/block.h`
- `src/main/opencl/kernel/include/rayTracer.c`

### 5. 抑制同材質玻璃之間的內部界面

相鄰同材質玻璃方塊之間，CPU 會透過 `isSameMaterial(...)` 類似邏輯避免把內部接縫視為新表面。

GPU 端新增了相同方向的處理：

- 若 ray 已處於某材質中
- 且下一次命中的材質與目前材質相同
- 會跳過這個內部界面繼續 march

這能減少：

- 染色玻璃大面積鋪設時過度明顯的表面網格

相關檔案：

- `src/main/opencl/kernel/include/octree.h`

### 6. 稍微加強染色玻璃表面紋路

為了讓染色玻璃的表面紋理更接近 CPU 視覺，對折射且非 opaque 的半透明玻璃像素，額外做了小幅 alpha 提升。

注意：

- 只影響 `0 < alpha < 1` 的表面紋理像素
- 完全透明區域不會被改成可見
- 主要目的是讓表面紋路更容易看見

相關檔案：

- `src/main/opencl/kernel/include/material.h`

---

## 二、Emitter / 發光方塊修復

### 1. 先釐清原本狀態

原本插件其實已經有基本發光：

- `material.emittance` 會被打包到 GPU 材質
- ray 直接命中發光材質時會把光加到結果裡

但這只代表：

- 發光方塊本身會亮

不代表：

- 它會像 CPU 一樣穩定地照亮別的表面
- 或透過染色玻璃產生明顯的有色透光

相關檔案：

- `src/main/java/dev/thatredox/chunkynative/common/export/primitives/PackedMaterial.java`
- `src/main/opencl/kernel/include/material.h`
- `src/main/opencl/kernel/include/rayTracer.c`

### 2. 導出 Emitter Grid

為了讓可見發光方塊能像 CPU 那樣被附近表面直接採樣，新增了 emitter grid 的導出鏈路。

目前會從 Chunky 的 `Scene.getEmitterGrid()` 取出：

- `cellSize`
- `offsetX/Y/Z`
- `sizeX/Y/Z`
- 每格 cell 的 `(start, count)`
- emitter 索引陣列
- emitter 位置與對應 block

相關檔案：

- `src/main/java/dev/thatredox/chunkynative/common/export/AbstractSceneLoader.java`
- `src/main/java/dev/thatredox/chunkynative/opencl/renderer/ClSceneLoader.java`
- `src/main/opencl/kernel/include/kernel.h`

### 3. 實作可見發光方塊的 face sampling

GPU 端新增了對 emitter block 幾何面的採樣：

- full block: 六個面
- AABB model: 每個有效面
- Quad model: 每個 quad

這些資料用來建立從「受光表面」到「發光方塊面」的 shadow ray。

相關檔案：

- `src/main/opencl/kernel/include/block.h`

### 4. 實作 emitter shadow / direct sampling

新增 `sampleEmitterFace(...)` 與 `sampleEmitters(...)`：

- 對 emitter face 做隨機點採樣
- 從目前 diffuse 命中點打一條 shadow ray 到 emitter
- 以距離平方、面積、emittance、策略縮放估算貢獻

目前支援的策略方向：

- `ONE`
- `ONE_BLOCK`
- `ALL`

相關檔案：

- `src/main/opencl/kernel/include/rayTracer.c`

### 5. 只在安全範圍內啟用 emitter direct sampling

為了避免之前出現的黑點與玻璃表面噪聲，這條 emitter sampling 目前只對以下情況啟用：

- 當前命中是 diffuse 分支
- 命中表面本身不是發光材質
- 命中表面是 `opaque`
- 命中表面不是折射材質

也就是說：

- 不會直接在玻璃表面上跑 emitter direct sampling
- 這是刻意限制，用來避免黑點回來

相關檔案：

- `src/main/opencl/kernel/include/rayTracer.c`

### 6. 暫時忽略隱形光源 `LightBlock`

因為隱形光源會引入額外的行為干擾，且你當前需求集中在可見發光方塊，所以目前先把 `LightBlock` 完全忽略：

- 不參與 preview
- 不參與 path tracing 命中

相關檔案：

- `src/main/opencl/kernel/include/block.h`

---

## 三、染色玻璃 + 發光方塊的增強版

### 1. 為什麼需要增強版

對照 `chunky-master` 後可以確認：

- CPU 的 emitter sampling 並不是專門為「發光方塊光線穿過染色玻璃後的強烈彩色透光」設計
- 你希望的是更明顯的效果

所以這部分不是單純還原 CPU，而是額外做了增強版。

### 2. 增強版內容

在 emitter shadow ray 穿過折射且非 opaque 的染色玻璃時：

- 先用 `Material_translucentTransmission(...)` 累積基本透光
- 再對染色玻璃做額外 tint boost
- 讓 emitter 光穿過染色玻璃後的顏色更明顯

這個增強只套用在：

- 可見發光方塊的 emitter direct sampling 路徑

不會影響：

- 太陽光路徑
- 一般玻璃主折射路徑

相關檔案：

- `src/main/opencl/kernel/include/rayTracer.c`

---

## 四、目前仍保留的限制

### 1. 這不是完全 CPU 原樣

玻璃主渲染大方向已經盡量向 CPU 靠攏，但：

- 「發光方塊穿過染色玻璃後的強烈彩色透光」

這部分目前是有意加強過的增強版，不是單純 CPU 原樣。

### 2. 隱形光源目前被關掉

`LightBlock` 目前刻意忽略，所以：

- 隱形光源不會生效

### 3. emitter sampling 仍只做在保守範圍

為了避免黑點問題回來，目前不在以下表面上跑 emitter direct sampling：

- 折射表面
- 非 opaque 表面
- 發光材質表面本身

### 4. campfire / candle / flame 類 entity emitter 尚未完整對齊

目前優先完成的是一般可見發光方塊這條主路徑。
像 campfire、candle、cake with candle 這種依賴 entity / flame geometry 的 emitter，還不是完整 CPU parity。

---

## 五、實際修改檔案

主要涉及：

- `src/main/java/dev/thatredox/chunkynative/common/export/AbstractSceneLoader.java`
- `src/main/java/dev/thatredox/chunkynative/common/export/primitives/PackedMaterial.java`
- `src/main/java/dev/thatredox/chunkynative/opencl/renderer/ClSceneLoader.java`
- `src/main/java/dev/thatredox/chunkynative/opencl/renderer/OpenClPathTracingRenderer.java`
- `src/main/opencl/kernel/include/block.h`
- `src/main/opencl/kernel/include/kernel.h`
- `src/main/opencl/kernel/include/material.h`
- `src/main/opencl/kernel/include/octree.h`
- `src/main/opencl/kernel/include/primitives.h`
- `src/main/opencl/kernel/include/rayTracer.c`

---

## 六、總結

這次修復可以分成兩部分：

1. 玻璃本身修正
   - 材質資料補齊
   - 內部出射面修正
   - 背面黑框修正
   - 同材質內部界面抑制
   - 相機位於玻璃內時的介質初始化

2. 發光方塊與染色玻璃增強
   - emitter grid 導出
   - 可見發光方塊 direct sampling
   - 隱形光源暫時停用
   - 染色玻璃對 emitter 光的 tint boost

目前版本的目標不是嚴格一比一還原 CPU 每個細節，而是：

- 保持玻璃主渲染穩定
- 避免黑點回來
- 讓可見發光方塊穿過染色玻璃時有更明顯的彩色透光

---

## English Summary

This document describes the recent fixes made to the `chunky-opencl` plugin for:

1. glass / stained glass / glass panes
2. visible emissive blocks and emitter behavior

### Glass fixes

- Exported refractive material data correctly to the GPU:
  - `refractive`
  - `opaque`
  - `ior`
- Fixed full-block AABB intersection when a ray starts inside glass:
  the renderer now uses the exit face instead of the invalid entry face.
- Fixed black back-face artifacts caused by fully transparent glass texels being sampled as black.
- Initialized the ray medium correctly when the camera starts inside refractive glass.
- Reduced internal seams between adjacent blocks of the same glass material.
- Slightly strengthened stained-glass surface texture visibility without making fully transparent texels visible.

### Emitter fixes

- Confirmed that basic emissive materials were already exported, but direct emitter sampling was incomplete.
- Added emitter-grid export from Chunky scene data to the OpenCL side.
- Added face sampling support for visible emissive blocks:
  - full blocks
  - AABB models
  - quad models
- Added a conservative direct-emitter sampling path for visible emissive blocks.
- Temporarily disabled invisible `LightBlock` handling to avoid unstable behavior.

### Stained glass + emitter enhancement

- CPU Chunky does not strongly emphasize “colored light passing through stained glass from emissive blocks” by default.
- Because you wanted this effect to be more visible, an enhanced emitter-through-stained-glass path was added.
- This enhancement is intentionally limited to visible emissive block sampling, so it does not broadly change all glass rendering paths.

### Current limitations

- This is not a strict 1:1 copy of every CPU lighting detail.
- Invisible `LightBlock` emitters are currently ignored on purpose.
- Direct emitter sampling is still restricted to a conservative subset of surfaces to prevent black-dot artifacts from returning.
- Entity-based emitters such as campfires / candles are not yet fully matched to CPU behavior.
