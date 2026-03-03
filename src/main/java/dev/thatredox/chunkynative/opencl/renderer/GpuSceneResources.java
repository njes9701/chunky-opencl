package dev.thatredox.chunkynative.opencl.renderer;

import static org.jocl.CL.*;

import dev.thatredox.chunkynative.opencl.context.ClContext;
import dev.thatredox.chunkynative.opencl.ui.ChunkyClTab;
import dev.thatredox.chunkynative.opencl.util.ClIntBuffer;
import dev.thatredox.chunkynative.opencl.util.ClMemory;
import dev.thatredox.chunkynative.util.Reflection;
import org.jocl.Pointer;
import org.jocl.Sizeof;
import se.llbit.chunky.renderer.scene.Scene;

public class GpuSceneResources implements AutoCloseable {
    private final ClContext context;
    private final ClMemory buffer;
    private final ClMemory randomSeed;
    private final ClMemory bufferSpp;
    private final ClIntBuffer canvasConfig;
    private final ClIntBuffer rayDepth;
    private final ClMemory sceneSettings;

    public GpuSceneResources(ClContext context, Scene scene, float[] passBuffer) {
        this.context = context;

        this.buffer = new ClMemory(clCreateBuffer(context.context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                (long) Sizeof.cl_float * passBuffer.length, Pointer.to(passBuffer), null));
        this.randomSeed = new ClMemory(clCreateBuffer(context.context, CL_MEM_READ_ONLY, Sizeof.cl_int, null, null));
        this.bufferSpp = new ClMemory(clCreateBuffer(context.context, CL_MEM_READ_ONLY, Sizeof.cl_int, null, null));

        this.canvasConfig = new ClIntBuffer(new int[] {
                scene.canvasConfig.getWidth(), scene.canvasConfig.getHeight(),
                scene.canvasConfig.getCropWidth(), scene.canvasConfig.getCropHeight(),
                scene.canvasConfig.getCropX(), scene.canvasConfig.getCropY()
        }, context);
        this.rayDepth = new ClIntBuffer(scene.getRayDepth(), context);

        this.sceneSettings = new ClMemory(
                clCreateBuffer(context.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                        (long) Sizeof.cl_float * 7,
                        Pointer.to(new float[] {
                                ((Double) Reflection.getFieldValue(scene, "transmissivityCap", Double.class)).floatValue(),
                                ((Boolean) Reflection.getFieldValue(scene, "fancierTranslucency", Boolean.class)) ? 1.0f : 0.0f,
                                scene.getSunSamplingStrategy().doSunSampling() ? 1.0f : 0.0f,
                                scene.getSunSamplingStrategy().isSunLuminosity() ? 1.0f : 0.0f,
                                scene.getSunSamplingStrategy().isStrictDirectLight() ? 1.0f : 0.0f,
                                ChunkyClTab.russianRouletteThreshold,
                                (float) ChunkyClTab.virtualDepth
                        }), null));
    }

    public ClContext getContext() {
        return context;
    }

    public ClMemory getBuffer() {
        return buffer;
    }

    public ClMemory getRandomSeed() {
        return randomSeed;
    }

    public ClMemory getBufferSpp() {
        return bufferSpp;
    }

    public ClIntBuffer getCanvasConfig() {
        return canvasConfig;
    }

    public ClIntBuffer getRayDepth() {
        return rayDepth;
    }

    public ClMemory getSceneSettings() {
        return sceneSettings;
    }

    @Override
    public void close() {
        sceneSettings.close();
        rayDepth.close();
        canvasConfig.close();
        bufferSpp.close();
        randomSeed.close();
        buffer.close();
    }
}
