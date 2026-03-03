package dev.thatredox.chunkynative.opencl.renderer;

import static org.jocl.CL.*;

import dev.thatredox.chunkynative.opencl.context.ContextManager;
import dev.thatredox.chunkynative.opencl.renderer.ClSceneLoader;
import dev.thatredox.chunkynative.opencl.renderer.kernel.DispatchParams;
import dev.thatredox.chunkynative.opencl.renderer.kernel.KernelBindings;
import dev.thatredox.chunkynative.opencl.renderer.kernel.PathTraceKernel;
import dev.thatredox.chunkynative.opencl.renderer.kernel.SceneConstants;
import dev.thatredox.chunkynative.opencl.renderer.scene.*;
import dev.thatredox.chunkynative.opencl.ui.OpenClRenderTimer;
import org.jocl.*;

import se.llbit.chunky.main.Chunky;
import se.llbit.chunky.renderer.*;
import se.llbit.chunky.renderer.scene.Scene;
import se.llbit.util.TaskTracker;

import java.util.Arrays;
import java.util.Random;
import java.util.concurrent.ForkJoinTask;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.BooleanSupplier;


public class OpenClPathTracingRenderer implements Renderer {

    private BooleanSupplier postRender = () -> true;

    @Override
    public String getId() {
        return "ChunkyClRenderer";
    }

    @Override
    public String getName() {
        return "ChunkyClRenderer";
    }

    @Override
    public String getDescription() {
        return "ChunkyClRenderer";
    }

    @Override
    public void setPostRender(BooleanSupplier callback) {
        postRender = callback;
    }

    @Override
    public void render(DefaultRenderManager manager) throws InterruptedException {
        ContextManager context = ContextManager.get();
        ClSceneLoader sceneLoader = context.sceneLoader;

        OpenClRenderTimer.start();
        try {
            ReentrantLock renderLock = new ReentrantLock();
            Scene scene = manager.bufferedScene;

            double[] sampleBuffer = scene.getSampleBuffer();
            float[] passBuffer = new float[sampleBuffer.length];

            // Ensure the scene is loaded
            sceneLoader.ensureLoad(manager.bufferedScene);

            try (ClCamera camera = new ClCamera(scene, context.context);
                 GpuSceneResources gpu = new GpuSceneResources(context.context, scene, passBuffer);
                 PathTraceKernel kernel = new PathTraceKernel(context.renderer.kernel, context.context.queue)) {
                RenderScheduler scheduler = new RenderScheduler(context.context.queue);
                // Generate initial camera rays
                camera.generate(renderLock, true);
                kernel.setStaticArgs(new KernelBindings(camera, sceneLoader, gpu, SceneConstants.fromScene(scene)));

                int bufferSppReal = 0;
                int logicalSpp = scene.spp;
                final int[] sceneSpp = {scene.spp};
                long lastCallback = 0;

                Random rand = new Random(0);

                ForkJoinTask<?> cameraGenTask = Chunky.getCommonThreads().submit(() -> 0);
                ForkJoinTask<?> bufferMergeTask = Chunky.getCommonThreads().submit(() -> 0);

                // This is the main rendering loop. This deals with dispatching rendering tasks. The majority of time is spent
                // waiting for the OpenCL renderer to complete.
                while (logicalSpp < scene.getTargetSpp()) {
                    renderLock.lock();
                    kernel.setPerDispatchArgs(new DispatchParams(rand.nextInt(), bufferSppReal));
                    cl_event renderEvent = kernel.dispatch(passBuffer.length / 3, null, null);
                    scheduler.waitFor(renderEvent);
                    renderLock.unlock();
                    bufferSppReal += 1;
                    scene.spp += 1;

                    if (camera.needGenerate && cameraGenTask.isDone()) {
                        cameraGenTask = Chunky.getCommonThreads().submit(() -> camera.generate(renderLock, true));
                    }

                    boolean saveEvent = isSaveEvent(manager.getSnapshotControl(), scene, logicalSpp + bufferSppReal);
                    if (bufferMergeTask.isDone() || saveEvent) {
                        if (!scene.shouldFinalizeBuffer() && !saveEvent) {
                            long time = System.currentTimeMillis();
                            if (time - lastCallback > 100 && !manager.shouldFinalize()) {
                                lastCallback = time;
                                if (postRender.getAsBoolean()) break;
                            }
                            if (bufferSppReal < 1024)
                                continue;
                        }

                        bufferMergeTask.join();
                        if (postRender.getAsBoolean()) break;
                        clEnqueueReadBuffer(context.context.queue, gpu.getBuffer().get(), CL_TRUE, 0,
                                (long) Sizeof.cl_float * passBuffer.length, Pointer.to(passBuffer),
                                0, null, null);
                        int sampSpp = sceneSpp[0];
                        int passSpp = bufferSppReal;
                        double sinv = 1.0 / (sampSpp + passSpp);
                        bufferSppReal = 0;

                        bufferMergeTask = Chunky.getCommonThreads().submit(() -> {
                            Arrays.parallelSetAll(sampleBuffer, i -> (sampleBuffer[i] * sampSpp + passBuffer[i] * passSpp) * sinv);
                            sceneSpp[0] += passSpp;
                            scene.postProcessFrame(TaskTracker.Task.NONE);
                            manager.redrawScreen();
                        });
                        logicalSpp += passSpp;
                        if (saveEvent) {
                            bufferMergeTask.join();
                            if (postRender.getAsBoolean()) break;
                        }
                    }
                }

                cameraGenTask.join();
                bufferMergeTask.join();
            }

        } finally {
            OpenClRenderTimer.stop();
        }
    }

    private boolean isSaveEvent(SnapshotControl control, Scene scene, int spp) {
        return control.saveSnapshot(scene, spp) || control.saveRenderDump(scene, spp);
    }

    @Override
    public boolean autoPostProcess() {
        return false;
    }

    @Override
    public void sceneReset(DefaultRenderManager manager, ResetReason reason, int resetCount) {
        boolean fullClear = reason == ResetReason.SCENE_LOADED || reason == ResetReason.MATERIALS_CHANGED;
        synchronized (manager.bufferedScene) {
            Arrays.fill(manager.bufferedScene.getSampleBuffer(), 0.0);
            manager.bufferedScene.spp = 0;
            manager.bufferedScene.renderTime = 0;
            if (fullClear) {
                Arrays.fill(manager.bufferedScene.getBackBuffer().data, 0);
                manager.bufferedScene.postProcessFrame(TaskTracker.Task.NONE);
            }
        }
        if (fullClear) {
            manager.redrawScreen();
        }
        ContextManager.get().sceneLoader.load(resetCount, reason, manager.bufferedScene);
    }
}
