package dev.thatredox.chunkynative.opencl.renderer.kernel;

import dev.thatredox.chunkynative.opencl.renderer.ClSceneLoader;
import dev.thatredox.chunkynative.opencl.renderer.GpuSceneResources;
import dev.thatredox.chunkynative.opencl.renderer.scene.ClCamera;

public class KernelBindings {
    private final ClCamera camera;
    private final ClSceneLoader sceneLoader;
    private final GpuSceneResources gpu;
    private final SceneConstants sceneConstants;

    public KernelBindings(ClCamera camera, ClSceneLoader sceneLoader, GpuSceneResources gpu, SceneConstants sceneConstants) {
        this.camera = camera;
        this.sceneLoader = sceneLoader;
        this.gpu = gpu;
        this.sceneConstants = sceneConstants;
    }

    public ClCamera getCamera() {
        return camera;
    }

    public ClSceneLoader getSceneLoader() {
        return sceneLoader;
    }

    public GpuSceneResources getGpu() {
        return gpu;
    }

    public SceneConstants getSceneConstants() {
        return sceneConstants;
    }
}
