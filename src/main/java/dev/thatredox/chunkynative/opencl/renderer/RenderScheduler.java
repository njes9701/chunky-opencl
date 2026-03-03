package dev.thatredox.chunkynative.opencl.renderer;

import static org.jocl.CL.*;

import org.jocl.cl_command_queue;
import org.jocl.cl_event;
import org.jocl.cl_kernel;

public class RenderScheduler {
    private final cl_command_queue queue;

    public RenderScheduler(cl_command_queue queue) {
        this.queue = queue;
    }

    public cl_event enqueue(cl_kernel kernel, long globalSize) {
        cl_event event = new cl_event();
        clEnqueueNDRangeKernel(queue, kernel, 1, null, new long[] { globalSize }, null, 0, null, event);
        return event;
    }

    public void waitFor(cl_event event) {
        clWaitForEvents(1, new cl_event[] { event });
        clReleaseEvent(event);
    }
}
