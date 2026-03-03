package dev.thatredox.chunkynative.opencl.renderer.kernel;

import se.llbit.chunky.renderer.scene.Scene;

public class SceneConstants {
    private final int emittersEnabled;
    private final float emitterIntensity;
    private final int emitterSamplingStrategy;
    private final int preventNormalEmitterWithSampling;

    private SceneConstants(int emittersEnabled, float emitterIntensity, int emitterSamplingStrategy, int preventNormalEmitterWithSampling) {
        this.emittersEnabled = emittersEnabled;
        this.emitterIntensity = emitterIntensity;
        this.emitterSamplingStrategy = emitterSamplingStrategy;
        this.preventNormalEmitterWithSampling = preventNormalEmitterWithSampling;
    }

    public static SceneConstants fromScene(Scene scene) {
        return new SceneConstants(
                scene.getEmittersEnabled() ? 1 : 0,
                (float) scene.getEmitterIntensity(),
                scene.getEmitterSamplingStrategy().ordinal(),
                scene.isPreventNormalEmitterWithSampling() ? 1 : 0
        );
    }

    public int getEmittersEnabled() {
        return emittersEnabled;
    }

    public float getEmitterIntensity() {
        return emitterIntensity;
    }

    public int getEmitterSamplingStrategy() {
        return emitterSamplingStrategy;
    }

    public int getPreventNormalEmitterWithSampling() {
        return preventNormalEmitterWithSampling;
    }
}
