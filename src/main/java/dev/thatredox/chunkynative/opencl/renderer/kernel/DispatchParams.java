package dev.thatredox.chunkynative.opencl.renderer.kernel;

public class DispatchParams {
    private final int rngSeed;
    private final int bufferSpp;

    public DispatchParams(int rngSeed, int bufferSpp) {
        this.rngSeed = rngSeed;
        this.bufferSpp = bufferSpp;
    }

    public int getRngSeed() {
        return rngSeed;
    }

    public int getBufferSpp() {
        return bufferSpp;
    }
}
