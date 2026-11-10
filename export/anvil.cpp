#include "export/anvil.h"
#include "export/blocks.h"
#include "export/ground.h"
#include "export/format/bytes.h"
#include "export/format/nbtwriter.h"
#include <QDateTime>
#include <QMap>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Writing a chunk into NBT (the 1.13–1.16.5 format)
// ---------------------------------------------------------------------------

//! Packing values of width bits into a long array in the "sparse" 1.16.2+ format (DataVersion 2529+):
//! entriesPerLong = floor(64/bits), an entry does NOT cross a long boundary, the high unused bits
//! of each long are padding. The same format that vanilla applies to Heightmaps too.
static void packSparse(const QVector<quint32>& values, int bits, QVector<qint64>& out)
{
    const int perLong = 64 / bits;
    out.clear();
    out.fill(0, int((values.size() + perLong - 1) / perLong));
    for (int i = 0; i < values.size(); ++i)
    {
        const int li = i / perLong;
        const int bitOff = (i % perLong) * bits;
        out[li] |= qint64(values[i]) << bitOff;
    }
}

//! Chunk NBT in the NATIVE Minecraft 26.x format (DataVersion 5023): sections Y=-4..19 (the world -64..319),
//! the palettes are STRING LISTS ("minecraft:stone"), a unary palette has no data written; we do not write light and heightmaps
//! at all — the reader computes them itself (checked with a live 26.3 server: such a chunk loads,
//! the blocks are preserved, the game re-saves them with light/heightmaps). The format was checked against reference
//! chunks created by a vanilla 26.3 server (packSparse 1.16.2+ matches their packing).
//! Legacy (1.21.x): the same layout, but the block_states palette is a List<Compound>{Name} (we do not
//! write Properties — the reader substitutes the defaults), DataVersion 4671. Checked against a chunk of a 1.21.11 world.
QByteArray buildChunkNbt(int chunkX, int chunkZ, const QHash<int, QVector<quint16>>& sections,
                                McWorldExporter::Format format,
                                const QVector<ChestSite>& chestsHere,
                                const GroundChunk& ground,
                                const CaveSet* caves,
                                const QVector<PaintingSite>& paintsHere,
                                const QVector<SpawnerSite>& spawnersHere,
                                const QVector<FluidSite>& fluidsHere,
                                const QVector<SignSite>& signsHere,
                                const QVector<BellSite>& bellsHere,
                                const QVector<BannerSite>& bannersHere,
                                const QVector<LairZone>& zonesHere,
                                const LairSet* lairs,
                                const QHash<ColKey, quint8>* biomeCells,
                                const QVector<CaveBe>& extraBes,
                                const QVector<QPoint3>& heartsHere,
                                bool blendEdge,
                                const QVector<PortalStartSite>& portalsHere,
                                const QHash<ColKey, QVector<QPair<qint16, quint16>>>* paleUnder)
{
    const bool legacy = (format == McWorldExporter::Format::Legacy121x);
    NbtWriter w;
    w.beginCompound("");                    // the root compound — without a name (the root must be a named compound)
    w.writeString("Status", QStringLiteral("minecraft:full"));
    w.writeInt("xPos", chunkX);
    w.writeInt("yPos", -4);                 // the lower boundary of the world -64/16
    w.writeInt("zPos", chunkZ);
    w.writeLong("LastUpdate", 0);
    w.beginCompound("structures");
    const QVector<LairSet::Start> lairStarts = lairs ? lairs->startsTouching(chunkX, chunkZ) : QVector<LairSet::Start>();
    QVector<CaveSet::WorldStart> tplHere;                              // the structure starts of the cave template (mines, trial chambers, donor cities)
    if (caves && !caves->empty()) caves->startsAt(chunkX, chunkZ, tplHere);
    if (zonesHere.isEmpty() && lairStarts.isEmpty() && portalsHere.isEmpty() && tplHere.isEmpty())
    {
        w.beginList("References", Nbt::Compound, 0);
        w.beginList("starts", Nbt::Compound, 0);
    }
    else
    {   // a false "pillager outpost": the game spawns pillagers inside the box of the start structure (spawn_overrides pillager_outpost).
        // The box = our zone (inside the building). The start lies in the zone center chunk, references to it are in all chunks the zone touches.
        QVector<qint64> refs;
        const LairZone* startZ = nullptr;
        for (const LairZone& z : zonesHere)
        {
            const int scx = ((z.x0 + z.x1) / 2) >> 4, scz = ((z.z0 + z.z1) / 2) >> 4;
            refs.append(qint64(quint32(scx)) | (qint64(quint32(scz)) << 32));
            if (scx == chunkX && scz == chunkZ && !startZ) startZ = &z;
        }
        // references to starts merged by structure id (keys in a compound must not repeat)
        QMap<QString, QVector<qint64>> refMap;
        if (!zonesHere.isEmpty()) refMap[QStringLiteral("minecraft:pillager_outpost")] += refs;
        for (const LairSet::Start& ls : lairStarts)
            refMap[ls.sid].append(qint64(quint32(ls.scx)) | (qint64(quint32(ls.scz)) << 32));
        for (const PortalStartSite& ps : portalsHere)
            refMap[QStringLiteral("minecraft:ruined_portal")].append(qint64(quint32(ps.cx)) | (qint64(quint32(ps.cz)) << 32));
        for (const CaveSet::WorldStart& tw : tplHere)
            refMap[tw.id].append(qint64(quint32(tw.startCx)) | (qint64(quint32(tw.startCz)) << 32));
        w.beginCompound("References");
        for (auto rit = refMap.constBegin(); rit != refMap.constEnd(); ++rit)
            w.writeLongArray(qPrintable(rit.key()), rit.value());
        w.endCompound();
        w.beginCompound("starts");
        for (const CaveSet::WorldStart& tw : tplHere)
        {   // the starts of template structures (the real mines and trial chambers of the donor) — /locate and advancements work off them, for example for entering trial chambers
            if (!tw.here) continue;
            bool dup = false;
            for (const LairSet::Start& ls : lairStarts) if (ls.sid == tw.id && ls.scx == chunkX && ls.scz == chunkZ) dup = true;
            if (dup) continue;
            w.beginCompound(qPrintable(tw.id));
            w.writeString("id", tw.id);
            w.writeInt("ChunkX", chunkX); w.writeInt("ChunkZ", chunkZ);
            w.writeInt("references", 1);
            w.beginList("Children", Nbt::Compound, tw.pieces.size());
            for (const CaveSet::TplPiece& pc : tw.pieces)
            {
                w.beginCompound(nullptr);
                w.writeString("id", pc.id);
                w.writeIntArray("BB", { pc.bb[0], pc.bb[1], pc.bb[2], pc.bb[3], pc.bb[4], pc.bb[5] });
                w.writeInt("GD", 0); w.writeInt("O", -1);
                if (pc.id == QLatin1String("minecraft:jigsaw"))
                {   // a jigsaw piece: the box as is, the template is the original one (the game does not use it on load), without junctions
                    w.writeInt("PosX", pc.bb[0]); w.writeInt("PosY", pc.bb[1]); w.writeInt("PosZ", pc.bb[2]);
                    w.writeInt("ground_level_delta", 1);
                    w.writeString("rotation", QStringLiteral("NONE"));
                    w.beginCompound("pool_element");
                    w.writeString("element_type", QStringLiteral("minecraft:legacy_single_pool_element"));
                    w.writeString("location", pc.loc.isEmpty() ? QStringLiteral("minecraft:empty") : pc.loc);
                    w.writeString("projection", QStringLiteral("rigid"));
                    w.beginCompound("processors");
                    w.beginList("processors", Nbt::Compound, 0);
                    w.endCompound();
                    w.endCompound();
                    w.beginList("junctions", Nbt::Compound, 0);
                }
                else
                    w.writeInt("MST", 0);                                    // mines: an ordinary type (the game takes the other piece fields by default)
                w.endCompound();
            }
            w.endCompound();                // the start
        }
        for (const PortalStartSite& ps : portalsHere)
        {   // the start of a ruined portal (the format was checked against a vanilla 26.3 world: one piece rupo with the template ruined_portal/portal_2)
            if (ps.cx != chunkX || ps.cz != chunkZ) continue;
            w.beginCompound("minecraft:ruined_portal");
            w.writeString("id", QStringLiteral("minecraft:ruined_portal"));
            w.writeInt("ChunkX", chunkX); w.writeInt("ChunkZ", chunkZ);
            w.writeInt("references", int(portalsHere.size()));
            w.beginList("Children", Nbt::Compound, 1);
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:rupo"));
            w.writeIntArray("BB", { ps.bb[0], ps.bb[1], ps.bb[2], ps.bb[3], ps.bb[4], ps.bb[5] });
            w.writeString("VerticalPlacement", QStringLiteral("on_land_surface"));
            w.writeInt("TPX", ps.bb[0]); w.writeInt("TPY", ps.bb[1]); w.writeInt("TPZ", ps.bb[2]);
            w.beginCompound("Properties");
            w.writeByte("overgrown", 0);
            w.writeFloat("mossiness", 0.2f);
            w.writeByte("replace_with_blackstone", 0);
            w.writeByte("vines", 0);
            w.writeByte("cold", 0);
            w.writeByte("air_pocket", 0);
            w.endCompound();
            w.writeInt("GD", 0);
            w.writeString("Rotation", QStringLiteral("NONE"));
            w.writeString("Mirror", QStringLiteral("NONE"));
            w.writeInt("O", 2);
            w.writeString("Template", QStringLiteral("minecraft:ruined_portal/portal_2"));
            w.endCompound();                // the piece
            w.endCompound();                // the start
        }
        for (const LairSet::Start& ls : lairStarts)
        {   // the start of a stronghold / an Ancient City: one "stub" piece with the box of the main hall (the hall itself already lies as template blocks)
            if (ls.scx != chunkX || ls.scz != chunkZ) continue;
            w.beginCompound(qPrintable(ls.sid));
            w.writeString("id", ls.sid);
            w.writeInt("ChunkX", chunkX); w.writeInt("ChunkZ", chunkZ);
            w.writeInt("references", 1);
            w.beginList("Children", Nbt::Compound, 1);
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:jigsaw"));
            w.writeIntArray("BB", { ls.bb[0], ls.bb[1], ls.bb[2], ls.bb[3], ls.bb[4], ls.bb[5] });
            w.writeInt("PosX", ls.bb[0]); w.writeInt("PosY", ls.bb[1]); w.writeInt("PosZ", ls.bb[2]);
            w.writeInt("GD", 0); w.writeInt("O", -1); w.writeInt("ground_level_delta", 1);
            w.writeString("rotation", QStringLiteral("NONE"));
            w.beginCompound("pool_element");
            w.writeString("element_type", QStringLiteral("minecraft:legacy_single_pool_element"));
            w.writeString("location", QStringLiteral("minecraft:pillager_outpost/base_plate"));
            w.writeString("projection", QStringLiteral("rigid"));
            w.beginCompound("processors");
            w.beginList("processors", Nbt::Compound, 0);
            w.endCompound();
            w.endCompound();
            w.beginList("junctions", Nbt::Compound, 0);
            w.endCompound();                // the piece
            w.endCompound();                // the start
        }
        if (startZ)
        {
            w.beginCompound("minecraft:pillager_outpost");
            w.writeString("id", QStringLiteral("minecraft:pillager_outpost"));
            w.writeInt("ChunkX", chunkX); w.writeInt("ChunkZ", chunkZ);
            w.writeInt("references", int(refs.size()));
            // The pieces: a feature_logs tile (a 6×3×7 template) on each floor inside the zone rectangle — the game takes the piece box from the template size
            // (BB = Pos + size), so instead of a single 16×30×16 (it would stick out onto the roof) we lay low tiles on the floors of the stories.
            struct Tile { int x, y, z; };
            QVector<Tile> tiles;
            for (int fy : startZ->floorsY)
                for (int tz = startZ->z0; tz + 6 <= startZ->z1; tz += 7)
                    for (int tx = startZ->x0; tx + 5 <= startZ->x1; tx += 6)
                        if (tiles.size() < 160) tiles.append(Tile{ tx, fy, tz });
            if (tiles.isEmpty()) tiles.append(Tile{ startZ->x0, startZ->floorsY.first(), startZ->z0 });
            w.beginList("Children", Nbt::Compound, tiles.size());
            for (const Tile& tl : tiles)
            {
                w.beginCompound(nullptr);
                w.writeString("id", QStringLiteral("minecraft:jigsaw"));
                w.writeIntArray("BB", { tl.x, tl.y, tl.z, tl.x + 5, tl.y + 2, tl.z + 6 });
                w.writeInt("PosX", tl.x); w.writeInt("PosY", tl.y); w.writeInt("PosZ", tl.z);
                w.writeInt("GD", 0); w.writeInt("O", -1); w.writeInt("ground_level_delta", 1);
                w.writeString("rotation", QStringLiteral("NONE"));
                w.beginCompound("pool_element");
                w.writeString("element_type", QStringLiteral("minecraft:legacy_single_pool_element"));
                w.writeString("location", QStringLiteral("minecraft:pillager_outpost/feature_logs"));
                w.writeString("projection", QStringLiteral("rigid"));
                w.beginCompound("processors");
                w.beginList("processors", Nbt::Compound, 0);
                w.endCompound();
                w.endCompound();
                w.beginList("junctions", Nbt::Compound, 0);
                w.endCompound();
            }
            w.endCompound();
        }
        w.endCompound();
    }
    w.endCompound();
    w.beginCompound("Heightmaps");          // empty — the game recomputes them itself
    w.endCompound();
    if (blendEdge)
    {   // The export boundary: the blending_data tag turns on vanilla terrain smoothing (the same as when upgrading a world from 1.17 to 1.18) —
        // the neighboring chunks that the game will still generate adjust the height and density to our surface
        w.beginCompound("blending_data");
        w.writeInt("min_section", -4);
        w.writeInt("max_section", 20);
        w.endCompound();
    }
    w.writeLong("InhabitedTime", 0);

    // The sections: the object blocks are taken from the store, the GROUND is completed analytically from the column metadata
    // (it is not in the store). Entirely stone sections (below stoneFloor, without objects) are written
    // with a unary palette without materializing 4096 cells — on the apron this is ~8 of the 24 sections of a chunk.
    const bool hasGround = !ground.isEmpty();
    int groundMaxTop = 0;                   // the highest ground top in the chunk (above it — air only)
    if (hasGround)
        groundMaxTop = *std::max_element(ground.top.constBegin(), ground.top.constEnd());
    // The cave template of a chunk (y=-64..63 = sections -4..3). Under the soil strip the cells are taken from
    // the cast (the cast air = a cave); above y=63 and in the 3 top rows of a column — analytics as before.
    CaveTemplate ctOwn;
    const CaveTemplate* ct = nullptr;
    if (hasGround && caves && !caves->empty() && ground.caveTpl >= 0)
    {
        ctOwn = caves->get(chunkX, chunkZ);        // a continuous donor block, mirrored tiling by the chunk coordinates
        if (!ctOwn.blocks.isEmpty()) ct = &ctOwn;
    }
    bool lairDeep = false;                  // the chunk touches the Ancient City: the lower sections have the deep_dark biome
    if (ct && lairs && !lairs->placed.isEmpty())
        lairs->apply(chunkX, chunkZ, ctOwn, &lairDeep);
    // The Pale Garden: its own landscape — the cave templates do not cut through the top 28 blocks of ground under it (there is solid rock/earth), they start deeper; other columns have 3 blocks.
    // the donor underground cells under the Pale Garden (pits, sinkholes, voids near the surface): a column -> a list of (y, id)
    const QVector<QPair<qint16, quint16>>* underCol[256];
    for (int col = 0; col < 256; ++col)
    {
        underCol[col] = nullptr;
        if (!paleUnder) continue;
        const auto uit = paleUnder->constFind(pairKey(chunkX * 16 + (col & 15), chunkZ * 16 + (col >> 4)));
        if (uit != paleUnder->constEnd()) underCol[col] = &uit.value();
    }
    int caveCap[256];
    for (int col = 0; col < 256; ++col)
    {
        caveCap[col] = 3;
        if (biomeCells && biomeCells->value(pairKey((chunkX * 16 + (col & 15)) >> 2, (chunkZ * 16 + (col >> 4)) >> 2), quint8(0)) == 1) caveCap[col] = 28;
    }
    // The Pale Garden: lava from the cave templates near the surface (within 16 blocks underground) is replaced with rock — it is not needed in the forest; deeper (lava lakes in dungeons) we do not touch.
    if (ct && biomeCells)
    {
        bool paleChunk = false;
        for (int z4 = 0; z4 < 4 && !paleChunk; ++z4)
            for (int x4 = 0; x4 < 4; ++x4)
                if (biomeCells->value(pairKey(chunkX * 4 + x4, chunkZ * 4 + z4), quint8(0)) == 1) { paleChunk = true; break; }
        if (paleChunk)
        {
            QHash<quint16, char> lavaId;
            auto isLavaB = [&](quint16 b) -> bool
            {
                if (b == IdLava) return true;
                if (b < IdCaveBase) return false;
                auto it = lavaId.find(b);
                if (it == lavaId.end()) it = lavaId.insert(b, blockName(b) == QLatin1String("minecraft:lava") ? char(1) : char(0));
                return it.value() != 0;
            };
            for (int col = 0; col < 256; ++col)
            {
                const int topG = ground.top[col];
                for (int ty = 127; ty >= 0; --ty)
                {
                    if (ty - 64 < topG - 16) break;
                    quint16& bl = ctOwn.blocks[ty * 256 + col];
                    if (isLavaB(bl)) bl = IdStone;
                }
            }
        }
    }
    // Underground lakes right at the surface: the donor ceiling is flat (the water stands at one level) — we bite out the top layer of water with rock
    // in flakes (and sometimes a second one) so that the ceiling above a lake is uneven. Only where it is <= 18 blocks from the lake to the ground surface.
    if (ct)
    {
        quint32 hs = quint32(chunkX) * 73856093u ^ quint32(chunkZ) * 19349663u ^ 0x1AEEu;
        auto nextH = [&hs]() { hs ^= hs << 13; hs ^= hs >> 17; hs ^= hs << 5; return hs; };
        for (int col = 0; col < 256; ++col)
        {
            const int topG = ground.top[col];
            for (int ty = 126; ty >= 0; --ty)
            {
                const quint16 here = ctOwn.blocks[ty * 256 + col];
                if (here != IdWater) continue;
                const quint16 above = ctOwn.blocks[(ty + 1) * 256 + col];
                if (above == IdAir || above == IdWater) break;            // the water surface is open — not a ceiling
                if (topG - (ty - 64) <= 18 && nextH() % 100u < 42u)
                {
                    ctOwn.blocks[ty * 256 + col] = IdStone;
                    if (ty > 0 && ctOwn.blocks[(ty - 1) * 256 + col] == IdWater && nextH() % 100u < 30u)
                        ctOwn.blocks[(ty - 1) * 256 + col] = IdStone;
                }
                break;
            }
        }
        for (int sec = 0; sec < 8; ++sec)
        {
            const quint16 first = ctOwn.blocks[sec * 4096];
            bool uni = true;
            for (int j = 1; j < 4096 && uni; ++j)
                if (ctOwn.blocks[sec * 4096 + j] != first) uni = false;
            ctOwn.uniId[sec] = (uni && first != IdAir) ? first : quint16(0);
        }
    }

    // The "Pale Garden" biome (the Pale Garden forest): cells of 4×4 columns inside the chosen forest (paleCells, the key is pairKey(x>>2, z>>2)).
    // Biomes by cells of 4×4 columns (one pattern at all heights): 0 plains, 1 pale_garden (the Pale Garden), 2 taiga (spruce forests), 3 forest
    // (other forests with height), 4 birch_forest (around schools), 5 dappled_forest (the motley forest around kindergartens; 1.21.x does not have it — flower_forest).
    // This is needed so that at the edge of the map the game generation smoothly (blending) continues the same type of forest.
    int cellBiome[16];
    bool anyOther = false;
    for (int z4 = 0; z4 < 4; ++z4)
        for (int x4 = 0; x4 < 4; ++x4)
        {
            int v = 0;
            if (biomeCells) v = biomeCells->value(pairKey(chunkX * 4 + x4, chunkZ * 4 + z4), quint8(0));
            cellBiome[z4 * 4 + x4] = v;
            if (v) anyOther = true;
        }
    const char* const* kBiomeNames = g_biomeName;                          // depend on the climate zone
    const bool zoneBiome = std::strcmp(g_biomeName[0], "minecraft:plains") != 0;
    w.beginList("sections", Nbt::Compound, 24);     // all sections Y=-4..19, as vanilla writes
    for (int si = -4; si <= 19; ++si)
    {
        const auto sit = sections.constFind(si);
        const bool hasObj = (sit != sections.constEnd());
        const int secBotY = si << 4;
        const bool caveSec = ct && si >= -4 && si <= 3;
        const quint16 tplUni = caveSec ? ct->uniId[si + 4] : quint16(0);

        QVector<quint16> merged;            // the "objects + ground" section for the lower palette code
        const QVector<quint16>* secP = nullptr;
        quint16 unaryId = 0;
        if (!hasGround)
        {
            if (hasObj) secP = &sit.value();
        }
        else if (!hasObj && secBotY + 15 <= ground.stoneFloor && (!caveSec || tplUni))
            // Entirely under the ground: a uniform section of the cast — its block (usually deepslate), otherwise stone.
            // Careful: the unary tplUni must be tied to stoneFloor — otherwise a uniform section
            // of the template in a flat world would overwrite the surface (si=0 = y0..15 = the turf layer).
            unaryId = tplUni ? tplUni : IdStone;
        else if (hasObj || secBotY <= groundMaxTop || caveSec)
        {   // the section touches objects and/or the ground surface — we assemble the cells
            merged.resize(4096);
            for (int i = 0; i < 4096; ++i)
            {
                const int col = ((i >> 4) & 15) * 16 + (i & 15);        // lz*16+lx (the index i = (y<<8)|(z<<4)|x)
                const int y = secBotY | ((i >> 8) & 15);
                quint16 blk;
                // "y < top-3": the ground crust (turf + 2 dirt + stone) is not bitten through by a cave —
                // otherwise at the chamfer (top=-1) the template gnawed air out in the 3 blocks under the surface,
                // and the lowered column looked like a hole (the seal check of verify).
                if (ct && y >= -64 && y <= 63 && y < ground.top[col] - caveCap[col] && ground.wd[col] == 0)
                    blk = ct->blocks[(y + 64) * 256 + col];             // the underground — a cave cast
                else
                    blk = groundBlockAt(ground.top[col], ground.surf[col], ground.wd[col], y, chunkX * 16 + (col & 15), chunkZ * 16 + (col >> 4), !ground.art.isEmpty() && ground.art[col] != 0);
                if (underCol[col] && ground.wd[col] == 0)
                    for (const QPair<qint16, quint16>& ue : *underCol[col])
                        if (ue.first == y) { blk = ue.second; break; }
                if (hasObj && sit.value()[i]) blk = sit.value()[i];     // an object takes priority over the ground
                merged[i] = blk;
            }
            secP = &merged;
        }

        // The palette: air is added only if it is really present in the section — fully filled
        // sections get a unary palette without data (as vanilla writes, ~100 bytes instead of 2 KB).
        QHash<quint16, int> pal;
        QVector<quint16> palIds;
        QVector<quint32> palIdx;
        const QVector<quint16> emptySec;
        const QVector<quint16>& sec = secP ? *secP : emptySec;
        if (unaryId)
            palIds << unaryId;
        else if (sec.size() == 4096)
        {
            palIdx.fill(0, 4096);
            for (int i = 0; i < 4096; ++i)
            {
                const quint16 id = sec[i];
                int pi = pal.value(id, -1);
                if (pi < 0) { pi = palIds.size(); pal.insert(id, pi); palIds << id; }
                palIdx[i] = quint32(pi);
            }
        }
        if (palIds.isEmpty())
            palIds << IdAir;                // an empty section: a single [air]
        const bool single = (palIds.size() == 1 || sec.size() != 4096);
        int bits = 4;
        while ((1 << bits) < palIds.size()) ++bits;
        QVector<qint64> packed;
        if (!single)
            packSparse(palIdx, bits, packed);

        w.beginCompound(nullptr);
        w.writeByte("Y", qint8(si));
        w.beginCompound("block_states");
        if (legacy)
        {
            w.beginList("palette", Nbt::Compound, palIds.size());
            for (quint16 id : palIds)
            {
                QVector<QPair<QString, QString>> props;
                blockProperties(id, props);
                w.beginCompound(nullptr);
                {
                    QString nm = blockName(id);
                    if (nm.contains(QLatin1String("poplar_leaves"))) nm = QStringLiteral("minecraft:oak_leaves");        // 1.21.x: no poplar
                    else if (nm == QLatin1String("minecraft:poplar_log")) nm = QStringLiteral("minecraft:oak_log");
                    w.writeString("Name", nm);
                }
                if (!props.isEmpty())
                {
                    w.beginCompound("Properties");            // the values are strings (checked against a 1.21.11 world)
                    for (const auto& kv : props)
                        w.writeString(qPrintable(kv.first), kv.second);
                    w.endCompound();
                }
                w.endCompound();
            }
        }
        else
        {
            // 26.x: a palette without properties is a List<String> (vanilla writes it that way). But if a section has
            // even one block with properties, the WHOLE palette becomes a List<Compound>, and each element
            // must have the key "id" (without properties — a compound with only "id"). Before, an element
            // without properties was written with an EMPTY name — the 26.3 server read such an element as AIR and lost whole
            // walls around torches/stairs (hence the "floating" lights in the game).
            bool anyProps = false;
            for (quint16 id : palIds)
            {
                QVector<QPair<QString, QString>> p;
                blockProperties(id, p);
                if (!p.isEmpty()) { anyProps = true; break; }
            }
            if (!anyProps)
            {
                w.beginList("palette", Nbt::String, palIds.size());
                for (quint16 id : palIds)
                    w.writeStringElem(blockName(id));
            }
            else
            {
                w.beginList("palette", Nbt::Compound, palIds.size());
                for (quint16 id : palIds)
                {
                    QVector<QPair<QString, QString>> props;
                    blockProperties(id, props);
                    w.beginCompound(nullptr);
                    w.writeString("id", blockName(id));
                    if (!props.isEmpty())
                    {
                        w.beginCompound("properties");
                        for (const auto& kv : props)
                            w.writeString(qPrintable(kv.first), kv.second);
                        w.endCompound();
                    }
                    w.endCompound();
                }
            }
        }
        if (!single)
            w.writeLongArray("data", packed);
        w.endCompound();                    // block_states
        w.beginCompound("biomes");          // uniform plains — a unary palette without data
        if ((anyOther || zoneBiome) && !(lairDeep && si <= -1))
        {
            int pidx[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };          // value -> index in the palette
            QVector<int> usedVals;
            for (int vv = 0; vv < 10; ++vv)
                for (int ci = 0; ci < 16; ++ci)
                    if (cellBiome[ci] == vv) { pidx[vv] = usedVals.size(); usedVals.append(vv); break; }
            w.beginList("palette", Nbt::String, usedVals.size());
            for (int vv : usedVals)
            {
                // legacy (1.21.x): no dappled_forest (it is flower_forest there); the swamp name is the same in both formats
                QString nm = QLatin1String(kBiomeNames[vv]);
                if (legacy && vv == 5) nm = QStringLiteral("minecraft:flower_forest");
                w.writeStringElem(nm);
            }
            if (usedVals.size() > 1)
            {
                int bits = 1;
                while ((1 << bits) < usedVals.size()) ++bits;               // the palette <= 10 -> 1..4 bits; the values do not cross a long boundary
                const int per = 64 / bits, nLongs = (64 + per - 1) / per;
                QVector<qint64> longs(nLongs, 0);
                for (int y4 = 0; y4 < 4; ++y4)
                    for (int z4 = 0; z4 < 4; ++z4)
                        for (int x4 = 0; x4 < 4; ++x4)
                        {
                            const int i = (y4 * 4 + z4) * 4 + x4;
                            longs[i / per] |= qint64(quint64(pidx[cellBiome[z4 * 4 + x4]]) << ((i % per) * bits));
                        }
                w.writeLongArray("data", longs);
            }
        }
        else
        {
            w.beginList("palette", Nbt::String, 1);
            w.writeStringElem(lairDeep && si <= -1 ? QStringLiteral("minecraft:deep_dark") : QStringLiteral("minecraft:plains"));
        }
        w.endCompound();                    // biomes
        w.endCompound();                    // section
    }

    // The chests, pots, paintings and spawners of this chunk (a common list of block_entities).
    // Chests: the BE key is "LootTable" (NOT "Loot" — that is the format of structure files, in
    // the chunk block_entities the game silently loses it; checked on the 26.3 server: only LootTable
    // survives loading/saving). The key is the same in 1.21.x and 26.x. A vanilla table, the trick
    // is that the game generates the contents itself on first opening (it survives a non-existent table
    // too — the chest is simply empty). We do not write LootTableSeed — a random one is used on opening.
    // Pots (the user's remark "flowers do not grow in them"): flower_pot has NO flower property,
    // the kind of flower lives only in the pot's BE. We write the key variants of all format generations —
    // the server ignores unknown ones: "display"{id} (1.20.5+), "DisplayBlock"{Name} (<=1.20.4),
    // a string "flower" (our spare one; which one took root is decided by a `data get block` probe).
    if (!heartsHere.isEmpty())
    {   // a "creaking heart": without a scheduled tick a block lifted from disk stays dormant and creakings do not appear —
        // a tick on the next game tick makes the heart recompute its state (awake at night) and start spawning
        w.beginList("block_ticks", Nbt::Compound, heartsHere.size());
        for (const QPoint3& hp : heartsHere)
        {
            w.beginCompound(nullptr);
            w.writeString("i", QStringLiteral("minecraft:creaking_heart"));
            w.writeInt("p", 0);
            w.writeInt("t", 1);
            w.writeInt("x", hp.x);
            w.writeInt("y", hp.y);
            w.writeInt("z", hp.z);
            w.endCompound();
        }
    }
    if (!fluidsHere.isEmpty())
    {   // fluid ticks: the sources on the chamfer spread when the chunk is loaded (water faster than lava)
        w.beginList("fluid_ticks", Nbt::Compound, fluidsHere.size());
        for (const FluidSite& fs : fluidsHere)
        {
            w.beginCompound(nullptr);
            w.writeString("i", fs.lava ? QStringLiteral("minecraft:lava") : QStringLiteral("minecraft:water"));
            w.writeInt("p", 0);
            w.writeInt("t", fs.lava ? 30 : 5);
            w.writeInt("x", fs.x);
            w.writeInt("y", fs.y);
            w.writeInt("z", fs.z);
            w.endCompound();
        }
    }
    // The BEs of donor structures (chests/barrels with loot, spawners): only those whose block really stayed in place —
    // below the ground crust, not in a pond and not covered by a city object.
    QVector<CaveBe> caveBes;
    if (ct)
        for (const CaveBe& b : ct->bes)
        {
            const int col = b.lz * 16 + b.lx;
            if (b.y < -64 || b.y > 63 || b.y >= ground.top[col] - caveCap[col] || ground.wd[col] != 0) continue;
            const auto os = sections.constFind(b.y >> 4);
            if (os != sections.constEnd() && os.value().size() == 4096
                && os.value()[((b.y & 15) << 8) | (b.lz << 4) | b.lx] != 0) continue;
            if (legacy && b.kind == 5 && b.raw.contains("minecraft:decorated_pot")) continue;   // 1.21.x: a different format of pot sherds — the pot stays without a block entity
            caveBes.append(b);
        }
    caveBes += extraBes;                                  // the chests of outposts and camps
    const int beN = chestsHere.size() + spawnersHere.size() + caveBes.size() + signsHere.size() + bellsHere.size() + bannersHere.size() + heartsHere.size();   // paintings are entities, not BEs
    if (beN > 0)
    {
        w.beginList("block_entities", Nbt::Compound, beN);
        for (const ChestSite& cs : chestsHere)
        {
            w.beginCompound(nullptr);
            if (cs.stove > 0)
            {   // a working stove of a private house (a user request): fuel in slot 1, sometimes food in slot 0
                // (the item format of 1.20.5+: id/count; Slot — a byte, as a vanilla container dumps it)
                static const char* const kStoveFood[6] = { "minecraft:raw_beef", "minecraft:raw_porkchop", "minecraft:raw_chicken",
                                                           "minecraft:raw_mutton", "minecraft:potato", "minecraft:carrot" };
                const int s = cs.stove - 1;
                const bool food = (s & 2) != 0;
                w.writeString("id", QStringLiteral("minecraft:furnace"));
                w.writeInt("x", cs.x);
                w.writeInt("y", cs.y);
                w.writeInt("z", cs.z);
                w.beginList("Items", Nbt::Compound, food ? 2 : 1);
                if (food)
                {
                    w.beginCompound(nullptr);
                    w.writeByte("Slot", 0);
                    w.writeString("id", QLatin1String(kStoveFood[(s >> 3) % 6]));
                    w.writeByte("count", 1);
                    w.endCompound();
                }
                {
                    w.beginCompound(nullptr);
                    w.writeByte("Slot", 1);
                    w.writeString("id", (s & 1) ? QStringLiteral("minecraft:coal") : QStringLiteral("minecraft:charcoal"));
                    w.writeByte("count", qint8(1 + ((s >> 6) & 3)));
                    w.endCompound();
                }
                w.endCompound();                              // a list element
                continue;
            }
            if (cs.lectern >= 0)
            {   // a lectern with a written book (the item format of 1.20.5+: id/count/components)
                const BookText& bk = cs.lectern >= 1000 ? kSciBooks[(cs.lectern - 1000) % kSciBooksCount] : kBooks[cs.lectern % kBooksCount];
                w.writeString("id", QStringLiteral("minecraft:lectern"));
                w.writeInt("x", cs.x);
                w.writeInt("y", cs.y);
                w.writeInt("z", cs.z);
                w.beginCompound("Book");
                w.writeString("id", QStringLiteral("minecraft:written_book"));
                w.writeInt("count", 1);
                w.beginCompound("components");
                w.beginCompound("minecraft:written_book_content");
                w.beginCompound("title");
                w.writeString("raw", QString::fromUtf8(bk.title));
                w.endCompound();
                w.writeString("author", QString::fromUtf8(cs.lectern >= 1000 ? "Кафедра физики" : "Настоятель"));
                w.beginList("pages", Nbt::Compound, 2);
                for (const char* pg : { bk.page1, bk.page2 })
                {
                    w.beginCompound(nullptr);
                    w.writeString("raw", QString::fromUtf8(pg));
                    w.endCompound();
                }
                w.endCompound();                              // written_book_content
                w.endCompound();                              // components
                w.endCompound();                              // Book
                w.writeInt("Page", 0);
                w.endCompound();                              // a list element
                continue;
            }
            w.writeString("id", cs.barrel ? QStringLiteral("minecraft:barrel") : QStringLiteral("minecraft:chest"));
            w.writeInt("x", cs.x);
            w.writeInt("y", cs.y);
            w.writeInt("z", cs.z);
            w.writeString("LootTable", cs.barrel ? QLatin1String(kBarrelLoot[cs.tbl % kBarrelLootCount])
                                              : QLatin1String(kChestLoot[cs.tbl % kChestLootCount]));
            w.endCompound();                                  // a list element
        }
        for (const BannerSite& bn : bannersHere)
        {   // an ominous banner: a white base + a pattern (rhombus, stripe_bottom, stripe_center, border, half_horizontal, circle, border)
            static const char* const kPat[7] = { "minecraft:rhombus", "minecraft:stripe_bottom", "minecraft:stripe_center", "minecraft:border",
                                                 "minecraft:half_horizontal", "minecraft:circle", "minecraft:border" };
            static const char* const kCol[7] = { "cyan", "light_gray", "gray", "light_gray", "black", "light_gray", "black" };
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:banner"));
            w.writeInt("x", bn.x); w.writeInt("y", bn.y); w.writeInt("z", bn.z);
            w.beginList("patterns", Nbt::Compound, 7);
            for (int k = 0; k < 7; ++k)
            {
                w.beginCompound(nullptr);
                w.writeString("color", QLatin1String(kCol[k]));
                w.writeString("pattern", QLatin1String(kPat[k]));
                w.endCompound();
            }
            w.endCompound();
        }
        for (const QPoint3& hp : heartsHere)
        {   // the block entity of a "creaking heart": without a record in the BE list the game creates it only on the first touch of the block — which means
            // the heart does not tick and creakings do not appear (checked on the server: at night it stayed dormant)
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:creaking_heart"));
            w.writeInt("x", hp.x); w.writeInt("y", hp.y); w.writeInt("z", hp.z);
            w.endCompound();
        }
        for (const BellSite& bl : bellsHere)
        {
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:bell"));
            w.writeInt("x", bl.x); w.writeInt("y", bl.y); w.writeInt("z", bl.z);
            w.endCompound();
        }
        for (const SignSite& sg : signsHere)
        {   // a sign: the text is ordinary strings (since 1.21.5 a string in messages is a text component), wax — so that it cannot be edited
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:sign"));
            w.writeInt("x", sg.x); w.writeInt("y", sg.y); w.writeInt("z", sg.z);
            w.writeByte("is_waxed", 1);                       // wax: the inscription does not change in the game (this is by design)
            for (int side = 0; side < 2; ++side)
            {
                w.beginCompound(side == 0 ? "front_text" : "back_text");
                w.writeByte("has_glowing_text", 0);
                w.writeString("color", QStringLiteral("black"));
                // legacy (1.21.x): the string is a JSON component {"text":"…"}, as clients before 1.21.5 read it (including 1.21.1);
                // 26.x and newer: an ordinary string = a text component.
                auto lineText = [legacy](const QString& t) -> QString
                {
                    if (!legacy) return t;
                    QString e = t;
                    e.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
                    return QStringLiteral("{\"text\":\"%1\"}").arg(e);
                };
                w.beginList("messages", Nbt::String, 4);
                w.writeStringElem(lineText(side == 0 ? sg.l1.left(30) : QString()));
                w.writeStringElem(lineText(side == 0 ? sg.l2.left(30) : QString()));
                w.writeStringElem(lineText(side == 0 ? sg.l3.left(30) : QString()));
                w.writeStringElem(lineText(QString()));
                w.endCompound();
            }
            w.endCompound();                                  // a list element
        }
        for (const CaveBe& cb : caveBes)
        {
            if (cb.kind == 5)
            {   // a donor BE as is (trial_spawner, vault, dispenser, pot, suspicious sand…): the NBT fields + the coordinates
                w.beginCompound(nullptr);
                w.appendRaw(cb.raw);
                w.writeInt("x", chunkX * 16 + cb.lx);
                w.writeInt("y", cb.y);
                w.writeInt("z", chunkZ * 16 + cb.lz);
                w.endCompound();
                continue;
            }
            static const char* const kIds[4] = { "minecraft:chest", "minecraft:trapped_chest",
                                                 "minecraft:barrel", "minecraft:mob_spawner" };
            w.beginCompound(nullptr);
            w.writeString("id", QLatin1String(kIds[std::max(1, std::min(4, cb.kind)) - 1]));
            w.writeInt("x", chunkX * 16 + cb.lx);
            w.writeInt("y", cb.y);
            w.writeInt("z", chunkZ * 16 + cb.lz);
            if (cb.kind == 4)
            {   // a structure spawner: the mob is in SpawnData.entity.id (+ SpawnPotentials) — as for our spawners
                w.beginCompound("SpawnData");
                w.beginCompound("entity");
                w.writeString("id", cb.str);
                w.endCompound();
                w.endCompound();
                w.beginList("SpawnPotentials", Nbt::Compound, 1);
                w.beginCompound(nullptr);
                w.writeInt("weight", 1);
                w.beginCompound("data");
                w.beginCompound("entity");
                w.writeString("id", cb.str);
                w.endCompound();
                w.endCompound();
                w.endCompound();
            }
            else
                w.writeString("LootTable", cb.str);
            w.endCompound();                                  // a list element
        }
        for (const SpawnerSite& sp : spawnersHere)
        {
            const QLatin1String mob{ kSpawnerTypes[sp.type % kSpawnerTypesCount] };
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:mob_spawner"));   // the BE type id (the block is called minecraft:spawner); with "minecraft:spawner" the game skipped the record and the spawner stayed empty
            w.writeInt("x", sp.x);
            w.writeInt("y", sp.y);
            w.writeInt("z", sp.z);
            // The vanilla 1.18+ format: the monster lies in SpawnData.entity.id (not SpawnData.id — such a
            // spawner was loaded by the game "empty", without a mob) + we duplicate it in SpawnPotentials[0].data.entity;
            // the other parameters are the vanilla values of the default spawner.
            w.beginCompound("SpawnData");
            w.beginCompound("entity");
            w.writeString("id", mob);
            w.endCompound();
            w.endCompound();
            w.beginList("SpawnPotentials", Nbt::Compound, 1);
            w.beginCompound(nullptr);
            w.writeInt("weight", 1);
            w.beginCompound("data");
            w.beginCompound("entity");
            w.writeString("id", mob);
            w.endCompound();
            w.endCompound();
            w.endCompound();
            w.writeShort("Delay", 20);
            w.writeShort("MinSpawnDelay", 200);
            w.writeShort("MaxSpawnDelay", 800);
            w.writeShort("SpawnCount", 4);
            w.writeShort("MaxNearbyEntities", 6);
            w.writeShort("RequiredPlayerRange", 16);
            w.writeShort("SpawnRange", 4);
            w.writeString("EntityId", mob);
            w.endCompound();                                  // a list element
        }
    }

    // We do NOT write the mall mannequins into the chunk: since 1.17 the server does NOT read the Entities tag from region chunks
    // (the 26.x/1.21.11 smoke: armor_stands from a chunk are not loaded and would not be loaded SILENTLY) —
    // vanilla stores entities in separate entities/r.*.mca. See buildEntityChunkNbt.

    w.writeInt("DataVersion", legacy ? 4671 : 5023);
    w.endCompound(); // root
    return w.out;
}

//! The mannequin armor damage (the minecraft:damage component): the durability ALWAYS stays between 2 and 36% of the item maximum — the armor is
//! "worn". The maximums by material (kArmorMats) and slot (boots, leggings, chestplate, helmet).
static int armorDamage(const MannequinSite& ms, int slot)
{
    static const int kMax[5][4] = {
        { 65, 75, 80, 55 },        // leather
        { 195, 225, 240, 165 },    // chainmail
        { 195, 225, 240, 165 },    // iron
        { 91, 105, 112, 77 },      // gold
        { 429, 495, 528, 363 } };  // diamond
    const int mx = kMax[ms.mat % 5][slot & 3];
    quint32 h = quint32(ms.x) * 374761393u ^ quint32(ms.z) * 668265263u ^ quint32(ms.y) * 2246822519u ^ quint32(slot) * 3266489917u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const int leftPct = 2 + int(h % 35u);                       // 2..36 % remains
    int dmg = mx - int(std::lround(mx * leftPct / 100.0));
    dmg = std::max(0, std::min(dmg, mx - 1));                   // at least 1 unit of durability
    return dmg;
}

//! The payload of one cell of entities/r.<rx>.<rz>.mca. The format was CHECKED against dumps of real worlds
//! of both smoke servers (26.x and 1.21.11 — the structures are identical, only
//! DataVersion differs): a root compound {Position: an INT_ARRAY of the absolute chunk coordinates,
//! DataVersion, Entities: LIST<Compound>}; the region container is a standard anvil — it is
//! assembled by buildRegionFile (the directory path is set by the caller).
//! A mannequin = an armor_stand (a remark: "sometimes mannequins may wear pieces
//! of armor" — armor is worn only by an armor stand, it cannot be laid out as a block). Pos/Rotation
//! — List<Double>/List<Float>; ArmorItems — 4 slots [boots, leggings, chestplate,
//! helmet], an empty slot is an empty compound (1.20.5+ — an item {id,count}, components by
//! default). Disabled/Invulnerable — so that a player cannot move or break the showcase.
QByteArray buildEntityChunkNbt(int chunkX, int chunkZ,
                                      const QVector<MannequinSite>& mannequinsHere, bool legacy,
                                      const QVector<NpcSite>& npcsHere,
                                      const QVector<PaintingSite>& paintsHere,
                                      const QVector<FrameSite>& framesHere)
{
    NbtWriter w;
    w.beginCompound("");                        // the root is a named compound
    w.writeIntArray("Position", { chunkX, chunkZ });
    w.writeInt("DataVersion", legacy ? 4671 : 5023);
    w.beginList("Entities", Nbt::Compound, mannequinsHere.size() + npcsHere.size() + paintsHere.size() + framesHere.size());
    {
        auto pos3 = [&w](double x, double y, double z)
        {
            w.beginList("Pos", Nbt::Double, 3);
            w.writeDoubleElem(x); w.writeDoubleElem(y); w.writeDoubleElem(z);
            w.beginList("Motion", Nbt::Double, 3);
            w.writeDoubleElem(0.0); w.writeDoubleElem(0.0); w.writeDoubleElem(0.0);
        };
        for (const NpcSite& n : npcsHere)
        {   // a villager / a wandering trader / a trader llama (the formats were taken with `data get entity` from the 26.3 server)
            w.beginCompound(nullptr);
            w.writeString("id", n.kind == 0 ? QStringLiteral("minecraft:villager")
                                : n.kind == 1 ? QStringLiteral("minecraft:wandering_trader")
                                : n.kind == 2 ? QStringLiteral("minecraft:trader_llama")
                                : n.kind == 5 ? n.animal
                                : n.kind == 4 ? (n.variant == 0 ? QStringLiteral("minecraft:zombie")
                                                : n.variant == 1 ? QStringLiteral("minecraft:skeleton")
                                                : n.variant == 2 ? QStringLiteral("minecraft:spider")
                                                : n.variant == 3 ? QStringLiteral("minecraft:witch")
                                                : n.variant == 4 ? QStringLiteral("minecraft:pillager")
                                                : n.variant == 5 ? QStringLiteral("minecraft:vindicator")
                                                : n.variant == 6 ? QStringLiteral("minecraft:evoker")
                                                : n.variant == 8 ? QStringLiteral("minecraft:slime")
                                                : n.variant == 9 ? QStringLiteral("minecraft:creeper")
                                                                 : QStringLiteral("minecraft:enderman"))
                                              : QStringLiteral("minecraft:iron_golem"));
            pos3(n.x + 0.5, double(n.y) + n.yoff, n.z + 0.5);
            w.beginList("Rotation", Nbt::Float, 2);
            w.writeFloatElem(float(n.yaw)); w.writeFloatElem(0.f);
            w.writeByte("PersistenceRequired", 1);
            w.writeByte("Invulnerable", (n.kind == 4 || n.kind == 5) ? 0 : 1);         // zombies from spawners do not touch villagers; basement mobs are mortal
            if (n.kind == 0)
            {
                w.beginCompound("VillagerData");
                w.writeString("profession", QStringLiteral("minecraft:") + QLatin1String(kProfs[n.prof % 15]));
                w.writeInt("level", n.baby ? 1 : n.level);
                w.writeString("type", QStringLiteral("minecraft:") + QLatin1String(kVTypes[n.vtype % 7]));
                w.endCompound();
                w.writeInt("Age", n.baby ? -24000 : 0);
            }
            else if (n.kind == 3)
                w.writeByte("PlayerCreated", 0);                      // an iron golem (guards the site)
            else if (n.kind == 5)
            {   // an animal: without extra fields (it does not despawn — PersistenceRequired above); for a cat we set the coat explicitly
                if (!n.petVariant.isEmpty()) w.writeString("variant", n.petVariant);
                else if (n.animal == QLatin1String("minecraft:tropical_fish")) w.writeInt("BucketVariantTag", n.variant & 0xffffff);  // the fish colour pattern
                else if (n.animal == QLatin1String("minecraft:pufferfish")) w.writeInt("PufferFishSize", n.variant ? 2 : 1);          // a half-grown pufferfish
                else if (n.animal == QLatin1String("minecraft:axolotl")) w.writeInt("Variant", n.variant % 5);                        // 0 wild,1 leucistic,2 albino,3 melanoid,4 golden
            }
            else if (n.kind != 4)
            {
                w.writeInt("DespawnDelay", 2000000000);               // without this the trader disappears after ~40 minutes
                if (n.kind == 2) w.writeInt("Variant", n.variant);
            }
            else if (n.variant == 8)
                w.writeInt("Size", std::max(0, std::min(3, n.level)));       // a slime: the size 0..3
            else if (n.variant == 4 || n.variant == 5)
            {   // armed raiders: a pillager — a crossbow, a vindicator — an iron axe (both formats: HandItems before 1.21.4, equipment since 1.21.5)
                const QString hand = (n.variant == 4) ? QStringLiteral("minecraft:crossbow") : QStringLiteral("minecraft:iron_axe");
                // the standard-bearer: an ominous banner (a white_banner with a pattern) on the head, PatrolLeader — the captain
                auto writeBanner = [&w]()
                {
                    static const char* const kPat[7] = { "minecraft:rhombus", "minecraft:stripe_bottom", "minecraft:stripe_center", "minecraft:border",
                                                         "minecraft:half_horizontal", "minecraft:circle", "minecraft:border" };
                    static const char* const kCol[7] = { "cyan", "light_gray", "gray", "light_gray", "black", "light_gray", "black" };
                    w.writeString("id", QStringLiteral("minecraft:white_banner"));
                    w.writeInt("count", 1);
                    w.beginCompound("components");
                    w.beginList("minecraft:banner_patterns", Nbt::Compound, 7);
                    for (int q = 0; q < 7; ++q)
                    {
                        w.beginCompound(nullptr);
                        w.writeString("color", QLatin1String(kCol[q]));
                        w.writeString("pattern", QLatin1String(kPat[q]));
                        w.endCompound();
                    }
                    w.endCompound();
                };
                if (n.captain) w.writeByte("PatrolLeader", 1);
                if (legacy)
                {
                    w.beginList("HandItems", Nbt::Compound, 2);
                    w.beginCompound(nullptr);
                    w.writeString("id", hand);
                    w.writeInt("count", 1);
                    w.endCompound();
                    w.beginCompound(nullptr);
                    w.endCompound();
                    if (n.captain)
                    {
                        w.beginList("ArmorItems", Nbt::Compound, 4);
                        for (int q = 0; q < 3; ++q) { w.beginCompound(nullptr); w.endCompound(); }
                        w.beginCompound(nullptr);
                        writeBanner();
                        w.endCompound();
                    }
                }
                w.beginCompound("equipment");
                w.beginCompound("mainhand");
                w.writeString("id", hand);
                w.writeInt("count", 1);
                w.endCompound();
                if (n.captain)
                {
                    w.beginCompound("head");
                    writeBanner();
                    w.endCompound();
                }
                w.endCompound();
            }
            w.endCompound();
        }
        for (const FrameSite& fr : framesHere)
        {   // an item frame (the format was taken with `data get entity` from the 26.3 server): Facing — a byte 2=N 3=S 4=W 5=E
            static const int kFacing[4] = { 2, 5, 3, 4 };               // our face 0=N,1=E,2=S,3=W
            static const double kDx[4] = { 0, 1, 0, -1 }, kDz[4] = { -1, 0, 1, 0 };
            static const int kYaw[4] = { 180, 270, 0, 90 };
            const bool up = (fr.face == 4);                              // 4 — lies on a block, looks up
            const int f = fr.face & 3;
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:item_frame"));
            if (up) pos3(fr.x + 0.5, fr.y + 0.03125, fr.z + 0.5);
            else    pos3(fr.x + 0.5 - kDx[f] * 0.46875, fr.y + 0.5, fr.z + 0.5 - kDz[f] * 0.46875);
            w.beginList("Rotation", Nbt::Float, 2);
            w.writeFloatElem(up ? 0.f : float(kYaw[f])); w.writeFloatElem(up ? -90.f : 0.f);
            w.writeByte("Facing", qint8(up ? 1 : kFacing[f]));
            w.writeIntArray("block_pos", { fr.x, fr.y, fr.z });
            w.writeInt("TileX", fr.x); w.writeInt("TileY", fr.y); w.writeInt("TileZ", fr.z);
            w.writeByte("ItemRotation", 0);
            w.writeByte("Invulnerable", 1);
            w.beginCompound("Item");
            QString itemId;
            if (fr.kind == 5) itemId = QStringLiteral("minecraft:golden_apple");
            else if (fr.kind == 6) itemId = QStringLiteral("minecraft:golden_carrot");
            else itemId = QStringLiteral("minecraft:%1_%2").arg(QLatin1String(kToolMatNames[fr.mat % 5]),
                                                               QLatin1String(kToolKindNames[fr.kind % 5]));
            w.writeString("id", itemId);
            w.writeInt("count", 1);
            if (fr.kind < 5)
            {
                w.beginCompound("components");
                w.writeInt("minecraft:damage", fr.dmg);
                w.endCompound();
            }
            w.endCompound();
            w.endCompound();
        }
        for (const PaintingSite& pt : paintsHere)
        {   // a painting entity: block_pos — the cell in front of the wall, facing 0=S 1=W 2=N 3=E (from the wall into the room)
            static const int kFaceByte[4] = { 2, 3, 0, 1 };           // our face 0=N,1=E,2=S,3=W -> a byte
            static const double kDx[4] = { 0, 1, 0, -1 }, kDz[4] = { -1, 0, 1, 0 };
            const int f = pt.face & 3;
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:painting"));
            // Pos: the cell center, shifted toward the wall by 15/32 of a block (as the game places it: z+0.96875 for facing=north)
            pos3(pt.x + 0.5 - kDx[f] * 0.46875, pt.y + 0.5, pt.z + 0.5 - kDz[f] * 0.46875);
            w.beginList("Rotation", Nbt::Float, 2);
            w.writeFloatElem(float(kFaceByte[f] * 90));            // S=0 W=90 N=180 E=270
            w.writeFloatElem(0.f);
            w.writeByte("facing", qint8(kFaceByte[f]));
            w.writeString("variant", QLatin1String(kPaintMotifs[pt.motif % kPaintMotifsCount]));
            w.writeIntArray("block_pos", { pt.x, pt.y, pt.z });
            w.writeInt("TileX", pt.x); w.writeInt("TileY", pt.y); w.writeInt("TileZ", pt.z);   // the keys of old versions
            w.writeByte("Invulnerable", 1);
            w.endCompound();
        }
        static const char* const kPart[4] = { "boots", "leggings", "chestplate", "helmet" };
        for (const MannequinSite& ms : mannequinsHere)
        {
            w.beginCompound(nullptr);
            w.writeString("id", QStringLiteral("minecraft:armor_stand"));
            w.beginList("Pos", Nbt::Double, 3);
            w.writeDoubleElem(ms.x + 0.5);
            w.writeDoubleElem(double(ms.y));
            w.writeDoubleElem(ms.z + 0.5);
            w.beginList("Motion", Nbt::Double, 3);
            w.writeDoubleElem(0.0);
            w.writeDoubleElem(0.0);
            w.writeDoubleElem(0.0);
            w.beginList("Rotation", Nbt::Float, 2);
            w.writeFloatElem(float(ms.yaw));
            w.writeFloatElem(0.f);
            w.writeByte("NoGravity", 1);
            w.writeByte("Small", 0);
            w.writeByte("Arms", 1);                          // arms — they are visible on a mannequin
            w.writeByte("Disabled", 1);
            w.writeByte("Invulnerable", 1);
            // Armor in BOTH formats (the tags do not interfere with each other — each version reads its own, the extra is ignored):
            //  · ArmorItems — up to and including 1.21.4 (1.20.5–1.21.4: id + count(Int) + components); legacy worlds opened by an old client;
            //  · equipment — since 1.21.5 (feet/legs/chest/head). 26.x reads only equipment, so ArmorItems is written only in legacy.
            if (legacy)
            {
                w.beginList("ArmorItems", Nbt::Compound, 4);
                for (int s = 0; s < 4; ++s)
                {
                    w.beginCompound(nullptr);
                    if (ms.armor & (1 << s))
                    {
                        w.writeString("id", QStringLiteral("minecraft:%1_%2")
                                                .arg(QLatin1String(kArmorMats[ms.mat % kArmorMatsCount]),
                                                     QLatin1String(kPart[s])));
                        w.writeInt("count", 1);
                        w.beginCompound("components");
                        w.writeInt("minecraft:damage", armorDamage(ms, s));
                        w.endCompound();
                    }
                    w.endCompound();                              // an ArmorItems element
                }
            }
            // Since 1.21.5 the equipment of entities is the compound "equipment" (feet/legs/chest/head); without it the mannequins stood naked.
            if (ms.armor)
            {
                static const char* const kSlot[4] = { "feet", "legs", "chest", "head" };
                w.beginCompound("equipment");
                for (int s = 0; s < 4; ++s)
                {
                    if (!(ms.armor & (1 << s))) continue;
                    w.beginCompound(kSlot[s]);
                    w.writeString("id", QStringLiteral("minecraft:%1_%2")
                                            .arg(QLatin1String(kArmorMats[ms.mat % kArmorMatsCount]),
                                                 QLatin1String(kPart[s])));
                    w.writeInt("count", 1);
                    w.beginCompound("components");
                    w.writeInt("minecraft:damage", armorDamage(ms, s));
                    w.endCompound();
                    w.endCompound();
                }
                w.endCompound();
            }
            w.endCompound();                                  // a list element
        }
    }
    w.endCompound(); // root
    return w.out;
}

// ---------------------------------------------------------------------------
// level.dat and world_gen_settings.dat (gzip NBT, the 26.x and 1.21.x formats)
// ---------------------------------------------------------------------------

static void writeDimensions(NbtWriter& w, int groundTopY, const McWorldExporter::WorldOptions& world);   // below in the file (the common part of the generation)

//! level.dat in the NATIVE Minecraft 26.x format (checked field by field against a level.dat created by a
//! vanilla 26.3 server — see dump_lvldat.py). The key point: Data.version is now an INT ("storage
//! version" 19133) and the game strictly validates it when opening a world (LevelVersion.parse);
//! DataVersion=5023; the generation is moved into data/minecraft/world_gen_settings.dat.
//! Legacy (1.21.x, checked against the level.dat of a world created by the 1.21.11 client — dump121.py):
//! DataVersion/Version.Id=4671, Name "1.21.11", the generation INSIDE level.dat (WorldGenSettings),
//! an ordinary (not compound) difficulty; version=Int 19133 is already there in 1.21.11 too — the field is the same.
QByteArray buildLevelDat(const QString& levelName, int spawnX, int spawnY, int spawnZ,
                                McWorldExporter::Format format, int groundTopY,
                                const McWorldExporter::WorldOptions& world)
{
    const bool legacy = (format == McWorldExporter::Format::Legacy121x);
    NbtWriter w;
    w.beginCompound("");
    w.beginCompound("Data");
    w.writeInt("version", 19133);                     // storage version — MANDATORILY an INT (it is in 1.21.11 too)!
    w.beginCompound("Version");
    w.writeByte("Snapshot", 0);
    w.writeString("Series", QStringLiteral("main"));
    w.writeInt("Id", legacy ? 4671 : 5023);
    w.writeString("Name", legacy ? QStringLiteral("1.21.11") : QStringLiteral("26.3"));
    w.endCompound();
    w.writeInt("DataVersion", legacy ? 4671 : 5023);
    if (!legacy)
    {
        w.beginList("version_history", Nbt::Int, 1);  // only in 26.x
        w.writeIntElem(5023);
    }
    w.writeString("LevelName", levelName);
    w.writeInt("GameType", 1);                        // creative — no fall damage from the spawn
    w.writeByte("allowCommands", 1);                  // in 26.x this is a Byte, not an Int
    w.writeByte("hardcore", 0);
    if (legacy)
    {
        w.writeByte("Difficulty", 2);                 // normal: mobs in basements and spawners work (peaceful removed them)
        w.writeByte("DifficultyLocked", 0);
        w.beginCompound("DragonFight");               // otherwise the server log has ERROR "key missing: DragonFight"
        w.writeByte("NeedsStateScanning", 1);         // (it is in the vanilla 1.21.11 world — dump121.py)
        w.writeByte("DragonKilled", 0);
        w.writeByte("PreviouslyKilled", 0);
        w.endCompound();
        w.beginCompound("WorldGenSettings");          // the generation INSIDE level.dat (in 26.x — a separate file)
        w.writeByte("bonus_chest", 0);
        w.writeLong("seed", qint64(world.seed));
        // structures/villages are turned on only in the "normal" surroundings — a flat one does not need them
        // (and otherwise the very first chunk beyond the export border would shell us with a village on a cliff).
        w.writeByte("generate_features", world.normalSurroundings ? 1 : 0);   // the field name in 1.21.x (in 26.x — generate_structures)
        writeDimensions(w, groundTopY, world);
        w.endCompound();
    }
    else
    {
        w.beginCompound("difficulty_settings");
        w.writeString("difficulty", QStringLiteral("normal"));     // mobs in basements and spawners work (peaceful removed them)
        w.writeByte("hardcore", 0);
        w.writeByte("locked", 0);
        w.endCompound();
    }
    w.writeLong("Time", 6000);
    w.writeLong("LastPlayed", QDateTime::currentMSecsSinceEpoch());
    w.writeByte("initialized", 1);
    w.writeByte("WasModded", 0);
    w.beginCompound("spawn");                         // the new replacement of SpawnX/Y/Z
    w.writeIntArray("pos", QVector<qint32>{ spawnX, spawnY, spawnZ });  // above the center of mass
    w.writeFloat("pitch", 0.0f);                     // of objects: the player falls onto their scene
    w.writeString("dimension", QStringLiteral("minecraft:overworld"));
    w.writeFloat("yaw", 0.0f);
    w.endCompound();
    w.beginList("ServerBrands", Nbt::String, 1);
    w.writeStringElem(QStringLiteral("vanilla"));
    w.beginCompound("DataPacks");
    w.beginList("Enabled", Nbt::String, 1);
    w.writeStringElem(QStringLiteral("vanilla"));
    w.beginList("Disabled", Nbt::String, 3);
    w.writeStringElem(QStringLiteral("minecart_improvements"));
    w.writeStringElem(QStringLiteral("redstone_experiments"));
    w.writeStringElem(QStringLiteral("trade_rebalance"));
    w.endCompound();                                  // DataPacks
    w.endCompound(); // Data
    w.endCompound(); // root
    return gzipWrap(w.out);
}

//! dimensions{overworld(flat), the_nether, the_end} — the COMMON part for world_gen_settings.dat (26.x)
//! and the WorldGenSettings compound inside level.dat (1.21.x): the content in both formats is identical
//! (checked against the 26.3 and 1.21.11 worlds). The top of the flat plane is groundTopY (the median of our ground crust),
//! below — stone down to the bottom; structures are turned off.
static void writeDimensions(NbtWriter& w, int groundTopY, const McWorldExporter::WorldOptions& world)
{
    groundTopY = std::max(0, std::min(groundTopY, 300));
    const int stoneH = groundTopY + 65 - 4;           // total layers from y=-64 to the top: top+65

    w.beginCompound("dimensions");

    w.beginCompound("minecraft:overworld");
    w.beginCompound("generator");
    if (world.normalSurroundings)
    {
        // A normal seed-based world: the terrain is the native noise generator (terrain, caves, ores), the biomes are the
        // standard multi_noise preset of the overworld, we write no settings — the game takes the vanilla ones by
        // DataVersion. OUR chunks (status=full) are never touched by the generator; cliffs at the
        // export border are a deliberate price (smoothing the joint is a separate future task).
        w.beginCompound("biome_source");
        w.writeString("preset", QStringLiteral("minecraft:overworld"));
        w.writeString("type", QStringLiteral("minecraft:multi_noise"));
        w.endCompound();                                  // biome_source
        // settings is MANDATORY: without it 26.x crashes with "Overworld settings missing"
        // (WorldDimensions), the default by DataVersion is not substituted.
        w.writeString("settings", QStringLiteral("minecraft:overworld"));
        w.writeString("type", QStringLiteral("minecraft:noise"));
    }
    else
    {
        w.beginCompound("settings");
        w.writeByte("features", 0);
        w.writeString("biome", QStringLiteral("minecraft:plains"));
        w.beginList("layers", Nbt::Compound, 4);
        w.beginCompound(nullptr); w.writeString("block", QStringLiteral("minecraft:bedrock"));      w.writeInt("height", 1); w.endCompound();
        w.beginCompound(nullptr); w.writeString("block", QStringLiteral("minecraft:stone"));         w.writeInt("height", stoneH); w.endCompound();
        w.beginCompound(nullptr); w.writeString("block", QStringLiteral("minecraft:dirt"));          w.writeInt("height", 2); w.endCompound();
        w.beginCompound(nullptr); w.writeString("block", QStringLiteral("minecraft:grass_block"));   w.writeInt("height", 1); w.endCompound();
        w.beginList("structure_overrides", Nbt::String, 2);
        w.writeStringElem(QStringLiteral("minecraft:strongholds"));
        w.writeStringElem(QStringLiteral("minecraft:villages"));
        w.writeByte("lakes", 0);
        w.endCompound();                                  // settings
        w.writeString("type", QStringLiteral("minecraft:flat"));
    }
    w.endCompound();                                  // generator
    w.writeString("type", QStringLiteral("minecraft:overworld"));
    w.endCompound();                                  // minecraft:overworld

    w.beginCompound("minecraft:the_nether");
    w.beginCompound("generator");
    w.writeString("settings", QStringLiteral("minecraft:nether"));
    w.beginCompound("biome_source");
    w.writeString("preset", QStringLiteral("minecraft:nether"));
    w.writeString("type", QStringLiteral("minecraft:multi_noise"));
    w.endCompound();
    w.writeString("type", QStringLiteral("minecraft:noise"));
    w.endCompound();
    w.writeString("type", QStringLiteral("minecraft:the_nether"));
    w.endCompound();

    w.beginCompound("minecraft:the_end");
    w.beginCompound("generator");
    w.writeString("settings", QStringLiteral("minecraft:end"));
    w.beginCompound("biome_source");
    w.writeString("type", QStringLiteral("minecraft:the_end"));
    w.endCompound();
    w.writeString("type", QStringLiteral("minecraft:noise"));
    w.endCompound();
    w.writeString("type", QStringLiteral("minecraft:the_end"));
    w.endCompound();

    w.endCompound();                                  // dimensions
}

//! data/minecraft/world_gen_settings.dat — a superflat generator: the neighbors of our chunks need
//! flat ground; we put its top at the median height of our own "ground crust" so that the joint of chunks
//! is almost a step of a couple of blocks. Structure generation is turned off. Nether/End — by the standard
//! presets (the game creates their chunks itself). The format is a slice of a vanilla 26.3 world.
QByteArray buildWorldGenSettings(int groundTopY, const McWorldExporter::WorldOptions& world)
{
    NbtWriter w;
    w.beginCompound("");
    w.beginCompound("data");
    w.writeByte("bonus_chest", 0);
    w.writeLong("seed", qint64(world.seed));
    w.writeByte("generate_structures", world.normalSurroundings ? 1 : 0);
    writeDimensions(w, groundTopY, world);
    w.endCompound();                                  // data
    w.writeInt("DataVersion", 5023);
    w.endCompound(); // root
    return gzipWrap(w.out);
}

// ---------------------------------------------------------------------------
// the region file
// ---------------------------------------------------------------------------

//! The chunks of one region -> .mca. order — the positions (localX, localZ) in writing order; payloadByPos —
//! the chunk NBT before compression by the same key pair.
QByteArray buildRegionFile(const QVector<QPair<int, int>>& order,
                                  const QHash<ColKey, QByteArray>& payloadByPos, bool precompressed)
{
    struct Loc { int sectors = 0; int offset = 0; };
    Loc locs[32 * 32];

    // First we compress ALL chunks: compression id 2 = a zlib stream (CMF/FLG+deflate+adler, without the Qt
    // size prefix); the number of sectors must be counted by the COMPRESSED size.
    // precompressed — the chunks are already compressed (zlibStream) in the worker threads of chunk assembly: only the layout by sectors remains here.
    QHash<ColKey, QByteArray> compressedOwn;
    if (!precompressed)
        for (const QPair<int, int>& p : order)
            compressedOwn.insert(pairKey(p.first, p.second), zlibStream(payloadByPos.value(pairKey(p.first, p.second))));
    const QHash<ColKey, QByteArray>& compressed = precompressed ? payloadByPos : compressedOwn;

    int cursor = 2; // after the two header sectors
    for (const QPair<int, int>& p : order)
    {
        const QByteArray comp = compressed.value(pairKey(p.first, p.second));
        const int dataSectors = (comp.size() + 1 + 4 + 4095) / 4096;   // payload = [4 of length][1 of type][data]
        if (dataSectors > 255) continue;   // sectorCount lacks a byte — we skip the chunk rather than spoil the region
        Loc& l = locs[p.second * 32 + p.first];
        l.offset = cursor;
        l.sectors = dataSectors;
        cursor += dataSectors;
    }

    QByteArray file;
    file.reserve(cursor * 4096);
    QByteArray header(8192, char(0));
    const quint32 now = quint32(QDateTime::currentSecsSinceEpoch());
    char* h = header.data();
    for (int lz = 0; lz < 32; ++lz)
        for (int lx = 0; lx < 32; ++lx)
        {
            const Loc& l = locs[lz * 32 + lx];
            if (!l.sectors) continue;
            const int pos = (lz * 32 + lx) * 4;
            // The coordinate table (the first header sector): ONE 4-byte BE word per chunk —
            // the high 3 bytes = the offset in 4KiB sectors, the low byte = sectorCount (max 255).
            // The compression type is NOT stored in the header — it is 1 byte after the length in the chunk data itself.
            const quint32 locWord = (quint32(l.offset) << 8) | (quint32(l.sectors) & 0xFFu);
            h[pos]     = char((locWord >> 24) & 0xFF);
            h[pos + 1] = char((locWord >> 16) & 0xFF);
            h[pos + 2] = char((locWord >> 8) & 0xFF);
            h[pos + 3] = char(locWord & 0xFF);
            // mtime in the second table (offset 4096 + pos)
            h[4096 + pos]     = char((now >> 24) & 0xFF);
            h[4096 + pos + 1] = char((now >> 16) & 0xFF);
            h[4096 + pos + 2] = char((now >> 8) & 0xFF);
            h[4096 + pos + 3] = char(now & 0xFF);
        }
    file.append(header);

    for (const QPair<int, int>& p : order)
    {
        const Loc& l = locs[p.second * 32 + p.first];
        if (!l.sectors) continue;
        const QByteArray comp = compressed.value(pairKey(p.first, p.second));
        QByteArray chunk;
        chunk.reserve(4 + comp.size());
        putBE32(chunk, quint32(comp.size() + 1));             // length = payload + the type byte
        chunk.append(char(2));
        chunk.append(comp);
        const int pad = l.sectors * 4096 - chunk.size();      // alignment to sectors
        if (pad > 0) chunk.append(QByteArray(pad, char(0)));
        file.append(chunk);
    }
    return file;
}
