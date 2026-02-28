package dev.thatredox.chunkynative.opencl.renderer;

import dev.thatredox.chunkynative.common.export.AbstractSceneLoader;
import dev.thatredox.chunkynative.common.export.ResourcePalette;
import dev.thatredox.chunkynative.common.export.models.PackedAabbModel;
import dev.thatredox.chunkynative.common.export.models.PackedQuadModel;
import dev.thatredox.chunkynative.common.export.models.PackedTriangleModel;
import dev.thatredox.chunkynative.common.export.models.PackedWaterModel;
import dev.thatredox.chunkynative.common.export.primitives.PackedBlock;
import dev.thatredox.chunkynative.common.export.primitives.PackedMaterial;
import dev.thatredox.chunkynative.common.export.primitives.PackedSun;
import dev.thatredox.chunkynative.common.export.texture.AbstractTextureLoader;
import dev.thatredox.chunkynative.common.state.SkyState;
import dev.thatredox.chunkynative.opencl.context.ClContext;
import dev.thatredox.chunkynative.opencl.context.ContextManager;
import dev.thatredox.chunkynative.opencl.renderer.export.ClPackedResourcePalette;
import dev.thatredox.chunkynative.opencl.renderer.export.ClTextureLoader;
import dev.thatredox.chunkynative.opencl.renderer.scene.ClSky;
import dev.thatredox.chunkynative.opencl.util.ClIntBuffer;
import dev.thatredox.chunkynative.util.FunctionCache;
import dev.thatredox.chunkynative.util.Reflection;
import se.llbit.chunky.renderer.ResetReason;
import se.llbit.chunky.renderer.scene.Scene;
import se.llbit.math.Grid;

import java.util.List;
import java.util.Arrays;

public class ClSceneLoader extends AbstractSceneLoader {
    protected final FunctionCache<int[], ClIntBuffer> clWorldBvh;
    protected final FunctionCache<int[], ClIntBuffer> clActorBvh;
    protected final FunctionCache<PackedSun, ClIntBuffer> clPackedSun;
    protected ClSky clSky = null;
    protected SkyState skyState = null;

    protected ClIntBuffer octreeData = null;
    protected ClIntBuffer octreeDepth = null;
    protected ClIntBuffer waterOctreeData = null;
    protected ClIntBuffer waterOctreeDepth = null;
    protected ClIntBuffer emitterGridMeta = null;
    protected ClIntBuffer emitterGridCells = null;
    protected ClIntBuffer emitterGridIndexes = null;
    protected ClIntBuffer emitterGridEmitters = null;
    private final ClContext context;

    public ClSceneLoader(ClContext context) {
        this.context = context;
        this.clWorldBvh = new FunctionCache<>(i -> new ClIntBuffer(i, context), ClIntBuffer::close, null);
        this.clActorBvh = new FunctionCache<>(i -> new ClIntBuffer(i, context), ClIntBuffer::close, null);
        this.clPackedSun = new FunctionCache<>(i -> new ClIntBuffer(i, context), ClIntBuffer::close, null);
    }

    @Override
    public boolean ensureLoad(Scene scene) {
        return this.ensureLoad(scene, clSky == null);
    }

    @Override
    public boolean load(int modCount, ResetReason resetReason, Scene scene) {
        boolean loadSuccess = super.load(modCount, resetReason, scene);
        if (this.modCount != modCount) {
            SkyState newSky = new SkyState(scene.sky(), scene.sun());
            if (!newSky.equals(skyState)) {
                if (clSky != null) clSky.close();
                clSky = new ClSky(scene, context);
                skyState = newSky;
                packedSun = new PackedSun(scene.sun(), getTexturePalette());
            }
            loadEmitterGrid(scene);
        }
        return loadSuccess;
    }

    private void loadEmitterGrid(Scene scene) {
        if (emitterGridMeta != null) emitterGridMeta.close();
        if (emitterGridCells != null) emitterGridCells.close();
        if (emitterGridIndexes != null) emitterGridIndexes.close();
        if (emitterGridEmitters != null) emitterGridEmitters.close();

        Grid grid = scene.getEmitterGrid();
        if (grid == null || blockMapping == null) {
            emitterGridMeta = new ClIntBuffer(new int[] {0, 0, 0, 0, 0, 0, 0}, context);
            emitterGridCells = new ClIntBuffer(new int[] {0, 0}, context);
            emitterGridIndexes = new ClIntBuffer(new int[] {0}, context);
            emitterGridEmitters = new ClIntBuffer(new int[] {0, 0, 0, 0}, context);
            return;
        }

        int cellSize = Reflection.getFieldValue(grid, "cellSize", Integer.class);
        int offsetX = Reflection.getFieldValue(grid, "offsetX", Integer.class);
        int sizeX = Reflection.getFieldValue(grid, "sizeX", Integer.class);
        int offsetY = Reflection.getFieldValue(grid, "offsetY", Integer.class);
        int sizeY = Reflection.getFieldValue(grid, "sizeY", Integer.class);
        int offsetZ = Reflection.getFieldValue(grid, "offsetZ", Integer.class);
        int sizeZ = Reflection.getFieldValue(grid, "sizeZ", Integer.class);
        int[] constructedGrid = Reflection.getFieldValue(grid, "constructedGrid", int[].class);
        int[] positionIndexes = Reflection.getFieldValue(grid, "positionIndexes", int[].class);
        @SuppressWarnings("unchecked")
        List<Grid.EmitterPosition> positions = Reflection.getFieldValue(grid, "emitterPositions", List.class);

        int[] emitters = new int[Math.max(positions.size(), 1) * 4];
        for (int i = 0; i < positions.size(); i++) {
            Grid.EmitterPosition pos = positions.get(i);
            int paletteIndex = scene.getPalette().getPalette().indexOf(pos.block);
            int packedBlock = paletteIndex >= 0 && paletteIndex < blockMapping.length ? blockMapping[paletteIndex] : 0;
            emitters[i * 4] = pos.x;
            emitters[i * 4 + 1] = pos.y;
            emitters[i * 4 + 2] = pos.z;
            emitters[i * 4 + 3] = packedBlock;
        }

        emitterGridMeta = new ClIntBuffer(new int[] {
                cellSize,
                offsetX, sizeX,
                offsetY, sizeY,
                offsetZ, sizeZ
        }, context);
        emitterGridCells = new ClIntBuffer(constructedGrid == null || constructedGrid.length == 0 ? new int[] {0, 0} : constructedGrid, context);
        emitterGridIndexes = new ClIntBuffer(positionIndexes == null || positionIndexes.length == 0 ? new int[] {0} : positionIndexes, context);
        emitterGridEmitters = new ClIntBuffer(emitters, context);
    }

    private int[] mapOctree(int[] octree, int[] blockMapping) {
        return Arrays.stream(octree)
                .map(i -> i > 0 || -i >= blockMapping.length ? i : -blockMapping[-i])
                .toArray();
    }

    @Override
    protected boolean loadWorldOctree(int[] octree, int depth, int[] blockMapping, ResourcePalette<PackedBlock> blockPalette) {
        if (octreeData != null) octreeData.close();
        if (octreeDepth != null) octreeDepth.close();

        int[] mappedOctree = mapOctree(octree, blockMapping);
        octreeData = new ClIntBuffer(mappedOctree, context);
        octreeDepth = new ClIntBuffer(depth, context);
        return true;
    }

    @Override
    protected boolean loadWaterOctree(int[] octree, int depth, int[] blockMapping, ResourcePalette<PackedBlock> blockPalette) {
        if (waterOctreeData != null) waterOctreeData.close();
        if (waterOctreeDepth != null) waterOctreeDepth.close();

        int[] mappedOctree = mapOctree(octree, blockMapping);
        waterOctreeData = new ClIntBuffer(mappedOctree, context);
        waterOctreeDepth = new ClIntBuffer(depth, context);
        return true;
    }

    @Override
    protected AbstractTextureLoader createTextureLoader() {
        return new ClTextureLoader(context);
    }

    @Override
    protected ResourcePalette<PackedBlock> createBlockPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    @Override
    protected ResourcePalette<PackedMaterial> createMaterialPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    @Override
    protected ResourcePalette<PackedAabbModel> createAabbModelPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    @Override
    protected ResourcePalette<PackedQuadModel> createQuadModelPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    @Override
    protected ResourcePalette<PackedWaterModel> createWaterModelPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    @Override
    protected ResourcePalette<PackedTriangleModel> createTriangleModelPalette() {
        return new ClPackedResourcePalette<>(context);
    }

    public ClIntBuffer getOctreeData() {
        assert octreeData != null;
        return octreeData;
    }

    public ClIntBuffer getOctreeDepth() {
        assert octreeDepth != null;
        return octreeDepth;
    }

    public ClIntBuffer getWaterOctreeData() {
        assert waterOctreeData != null;
        return waterOctreeData;
    }

    public ClIntBuffer getWaterOctreeDepth() {
        assert waterOctreeDepth != null;
        return waterOctreeDepth;
    }

    public ClTextureLoader getTexturePalette() {
        assert texturePalette instanceof ClTextureLoader;
        return (ClTextureLoader) texturePalette;
    }

    public ClPackedResourcePalette<PackedBlock> getBlockPalette() {
        assert blockPalette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedBlock>) blockPalette;
    }

    public ClPackedResourcePalette<PackedMaterial> getMaterialPalette() {
        assert materialPalette.palette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedMaterial>) materialPalette.palette;
    }

    public ClPackedResourcePalette<PackedAabbModel> getAabbPalette() {
        assert aabbPalette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedAabbModel>) aabbPalette;
    }

    public ClPackedResourcePalette<PackedQuadModel> getQuadPalette() {
        assert quadPalette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedQuadModel>) quadPalette;
    }

    public ClPackedResourcePalette<PackedWaterModel> getWaterPalette() {
        assert waterPalette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedWaterModel>) waterPalette;
    }

    public ClPackedResourcePalette<PackedTriangleModel> getTrigPalette() {
        assert trigPalette instanceof ClPackedResourcePalette;
        return (ClPackedResourcePalette<PackedTriangleModel>) trigPalette;
    }

    public ClIntBuffer getWorldBvh() {
        return clWorldBvh.apply(this.worldBvh);
    }

    public ClIntBuffer getActorBvh() {
        return clActorBvh.apply(this.actorBvh);
    }

    public ClSky getSky() {
        assert clSky != null;
        return clSky;
    }

    public ClIntBuffer getSun() {
        return clPackedSun.apply(packedSun);
    }

    public ClIntBuffer getEmitterGridMeta() {
        assert emitterGridMeta != null;
        return emitterGridMeta;
    }

    public ClIntBuffer getEmitterGridCells() {
        assert emitterGridCells != null;
        return emitterGridCells;
    }

    public ClIntBuffer getEmitterGridIndexes() {
        assert emitterGridIndexes != null;
        return emitterGridIndexes;
    }

    public ClIntBuffer getEmitterGridEmitters() {
        assert emitterGridEmitters != null;
        return emitterGridEmitters;
    }
}
