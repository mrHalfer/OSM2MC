#include "export/exportrun.h"

/*!\file mcworldexporter.cpp
 * \brief Implementation of the export to a Minecraft Java 26.x save (DataVersion 5023, native format).
 *
 * The 26.x world format (checked with a live vanilla 26.3 server — a round trip without a single error):
 * - level.dat: Data.version = INT 19133 ("storage version", strictly validated on opening!),
 *   DataVersion 5023, a Version compound, a spawn compound instead of SpawnX/Y/Z, difficulty_settings;
 * - data/minecraft/world_gen_settings.dat — the generator (we write our own superflat, the top of the flat plane
 *   is adjusted to the median height of our ground crust); the game creates the other state files itself;
 * - layout: chunks in dimensions/minecraft/overworld/region/*.mca (there is no root region/ any more);
 * - chunk NBT: sections Y=-4..19, block_states/biomes with palettes as STRING LISTS ("minecraft:stone"),
 *   a unary palette has no data, light and Heightmaps are not written — the reader recomputes them itself.
 *
 * The formats written by hand here (minimally, but per the wiki spec and the behavior of Amulet Core):
 *  - NBT (big-endian) — tags 0..12, a root compound with a name;
 *  - region .mca — an 8192-byte header: a table of 1024 BE words (3 bytes of offset in 4KiB sectors |
 *    1 byte sectorCount) + a table of 1024 BE mtimes; then chunks in sectors of 4096, payload =
 *    [4 bytes of length][1 byte of compression type][data]; the compression is zlib (type 2), i.e. exactly what
 *    qCompress() returns (it is a standard zlib stream: a 2-byte header, deflate, adler32);
 *  - level.dat — GZIP NBT. Qt can only do zlib, so gzip is assembled by hand: a "raw" deflate is cut out of
 *    the qCompress() stream (the 2-byte zlib header and the 4-byte adler32 are removed),
 *    and a 10-byte gzip header and the CRC32+ISIZE trailer are appended to it;
 *  - zip — local headers + central directory + EOCD, the deflate method (the same "raw" deflate from
 *    qCompress(), the size is known in advance — everything is assembled in memory) with a fallback to store.
 *
 * IMPORTANT about the packing of BlockStates/Heightmaps: starting with DataVersion 2529 (1.16.2, see the interface
 * anvil_2529 in Amulet Core — "less dense": entries NO LONGER spill over a long boundary) long
 * arrays are packed "sparsely": entriesPerLong = floor(64/bits), the high bits of each long
 * are padding. For 4 bits both formats give an identical result, but at 5+ bits they differ, so
 * the sparse variant is used everywhere here. The minimum width of the block palette is 4 bits (MIN_BITS_PER_BLOCK,
 * vanilla 1.13+ cannot read a palette narrower than 4 bits).
 *
 * Voxelization: a triangle is cut by horizontal "slabs" Y∈[n,n+1] (Sutherland–Hodgman over a 3D
 * polygon), the (convex) section is projected onto XZ and rasterized over integer X columns
 * (clipping the strip [X,X+1] is again a convex clip, its Z range is taken). This is how the
 * "surface" voxels are obtained; if the mesh is closed — a 3D flood fill from the extent boundary marks the outer
 * air, the unreached cells = the body of the object.
 */

// The byte utilities, NBT and zip have been moved to export/format/


// ---------------------------------------------------------------------------
// The voxel storage and the per-column ground metadata moved to export/voxelstore.h
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// McWorldExporter::Export
// ---------------------------------------------------------------------------

McWorldExporter::Result McWorldExporter::Export(const QVector<McObject>& objectsIn,
                                                const HeightSampler& heightAt,
                                                const QString& path,
                                                const QString& levelName,
                                                const std::atomic_bool* cancel,
                                                Format format,
                                                const ProgressFn& progress,
                                                const WorldOptions& world)
{
    ExportRun run(objectsIn, heightAt, path, levelName, cancel, format, progress, world);
    return run.run();
}
