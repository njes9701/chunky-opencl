package dev.thatredox.chunkynative.common.export.models;

import dev.thatredox.chunkynative.common.export.Packer;
import dev.thatredox.chunkynative.common.export.ResourcePalette;
import dev.thatredox.chunkynative.common.export.primitives.PackedMaterial;
import dev.thatredox.chunkynative.common.export.texture.AbstractTextureLoader;
import it.unimi.dsi.fastutil.ints.IntArrayList;
import se.llbit.chunky.block.minecraft.Water;
import se.llbit.chunky.model.Tint;
import se.llbit.chunky.resources.Texture;

public class PackedWaterModel implements Packer {
    public final int material;
    public final int data;

    public PackedWaterModel(Water water, AbstractTextureLoader textureLoader,
                            ResourcePalette<PackedMaterial> materialPalette) {
        this.material = materialPalette.put(new PackedMaterial(
                Texture.water,
                Tint.BIOME_WATER,
                0.0f,
                water.specular,
                water.metalness,
                water.roughness,
                textureLoader));
        this.data = water.data;
    }

    @Override
    public IntArrayList pack() {
        IntArrayList out = new IntArrayList(2);
        out.add(material);
        out.add(data);
        return out;
    }
}
