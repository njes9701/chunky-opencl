package dev.thatredox.chunkynative.opencl.ui;

public final class OpenClRenderTimer {
    private static volatile boolean running = false;
    private static volatile long startNanos = 0L;
    private static volatile long lastElapsedNanos = 0L;

    private OpenClRenderTimer() {}

    public static void start() {
        running = true;
        startNanos = System.nanoTime();
        lastElapsedNanos = 0L;
    }

    public static void stop() {
        if (running) {
            lastElapsedNanos = System.nanoTime() - startNanos;
            running = false;
        }
    }

    public static boolean isRunning() {
        return running;
    }

    public static long getElapsedMillis() {
        long elapsedNanos = running ? (System.nanoTime() - startNanos) : lastElapsedNanos;
        return elapsedNanos / 1_000_000L;
    }
}
