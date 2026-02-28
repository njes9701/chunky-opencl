# OpenCL Plugin

## Note: These fixes are generated via Codex vibe coding. 100% stability is not guaranteed, and crashes may occur.

## Unofficial Fix Plugin - Current Status

### Fixed / Improved

- Parallel projection Y-clip / large-octree ray origin issue
- Glass / stained glass / glass pane rendering
- Glass exit-face handling when rays start inside the block
- Water blocks now render, including flowing water shapes (geometry only)
- Invisible `LightBlock` emitters are currently disabled on purpose for stability

### Current Limitations

- This is still an unofficial fix build
- 100% CPU parity is not guaranteed
- Invisible `LightBlock` emitters are ignored for now
- Water optics (underwater visibility, absorption, reflections) are not implemented yet
- Entity-based emitters such as campfires / candles are not yet fully matched to CPU behavior
- Some advanced lighting behavior is still experimental

###  Preview

![Water Preview 1](@photo)
![Water Preview 2](@photo)

This is a plugin for [Chunky](https://github.com/chunky-dev/chunky) which harnesses the power of the GPU with OpenCL 1.2+ to accelerate rendering.

#### This is currently a work in progress and does not support many features. The core renderer itself is still under development so render results may change drastically between versions.

## Installation

### Note: This requires the `2.5.0` snapshots.
Download the [latest plugin release](https://github.com/chunky-dev/chunky-opencl/releases) and extract it. In the Chunky Launcher, expand `Advanced Settings` and click on `Manage plugins`. In the `Plugin Manager` window click on `Add` and select the `.jar` file in the extracted zip file. Click on `Save` and start Chunky as usual.

![image](https://user-images.githubusercontent.com/42661490/116319916-28ef2580-a76c-11eb-9f93-86d444a349fd.png)

Select `ChunkyCL` as your renderer for the scene in the `Advanced` tab.

![image](https://user-images.githubusercontent.com/42661490/122492084-fc040580-cf99-11eb-9b08-b166dc25db41.png)

## Performance

Rough performance with a RTX 2070 is around 400 times that of the traditional CPU renderer as of 2022-01-27.

Some settings have been added to improve render performance.
* Indoor scenes should disable sunlight under `Lighting`
* Draw depth may be adjusted under `Advanced`
* Draw entities may be unchecked under `Advanced`
* OpenCL Device selector under `Advanced`

## Compatibility

* Not compatible with the Denoising Plugin.

---

## Development
This project is setup to work with IntelliJ and CLion. The base directory is intended to be opened in IntelliJ and the `src/main/opencl` directory in CLion.

For hot reloading, add `-DchunkyClHotReload="<src/main/opencl directory>"` as a JVM option.

## Copyright & License
ChunkyCL is Copyright (c) 2021 - 2024, [ThatRedox](https://github.com/ThatRedox) and contributors.

Permission to modify and redistribute is granted under the terms of the GPLv3 license. See the file `LICENSE` for the full license.

ChunkyCL uses the following 3rd party libraries:
* [Chunky](https://github.com/chunky-dev/chunky/)
* [JOCL](http://www.jocl.org/)
* [OpenCL header from the LLVM Project](https://llvm.org)
