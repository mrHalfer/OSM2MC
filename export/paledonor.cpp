#include "export/paledonor.h"
#include <QDataStream>
#include <QFile>
#include <QStringList>

//! The random_spread salt (spacing is a power of two, separation=0) at which the potential start chunk of cell (cx,cz) is exactly (cx,cz).
//! It repeats java.util.Random / WorldgenRandom.setLargeFeatureWithSalt / RandomSpreadStructurePlacement (checked against the vanilla city of seed 777).
int findPlacementSalt(qint64 seed, int cx, int cz, int spacing)
{
    if (spacing < 2 || (spacing & (spacing - 1)) != 0) return 0;      // the implementation is correct only for spacing = 2^n (nextInt by a power of two) and separation = 0
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const qint64 i = floorDiv(cx, spacing), j = floorDiv(cz, spacing);
    const quint64 wantX = quint64(cx - i * spacing), wantZ = quint64(cz - j * spacing);
    const quint64 MASK = (1ull << 48) - 1;
    const int shift = 31 - [&] { int b = 0; while ((1 << b) < spacing) ++b; return b; }();
    const quint64 base = quint64(i * 341873128712ll + j * 132897987541ll + seed);
    for (qint64 salt = 1; salt < 2147483647ll; ++salt)
    {
        quint64 s = ((base + quint64(salt)) ^ 0x5DEECE66Dull) & MASK;
        s = (s * 0x5DEECE66Dull + 0xBull) & MASK;
        if (((s >> 17) >> shift) != wantX) continue;
        s = (s * 0x5DEECE66Dull + 0xBull) & MASK;
        if (((s >> 17) >> shift) == wantZ) return int(salt);
    }
    return 0;
}

bool PaleDonor::load(const QString& path, CaveSet& cs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray file = f.readAll();
    const bool v2 = file.size() >= 16 && qstrncmp(file.constData(), "PGD2", 4) == 0;
    const bool v3 = file.size() >= 16 && qstrncmp(file.constData(), "PGD3", 4) == 0;
    if (file.size() < 16 || (!v2 && !v3 && qstrncmp(file.constData(), "PGD1", 4) != 0)) return false;
    const QByteArray raw = qUncompress(reinterpret_cast<const uchar*>(file.constData()) + 4, int(file.size() - 4));
    if (raw.size() < 12) return false;
    const uchar* d = reinterpret_cast<const uchar*>(raw.constData());
    auto r32 = [&](int o) -> quint32 { return (quint32(d[o]) << 24) | (quint32(d[o + 1]) << 16) | (quint32(d[o + 2]) << 8) | d[o + 3]; };
    const int w = int(r32(0)), h = int(r32(4)), palN = int(r32(8));
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || palN <= 0 || palN > 20000) return false;
    int o = 12;
    QHash<QString, int> byKey;
    for (int i = 0; i < cs.states.size(); ++i) byKey.insert(caveStateKey(cs.states[i]), i);
    QVector<quint16> idMap(palN), idMapDry(palN);
    for (int i = 0; i < palN; ++i)
    {
        if (o + 2 > raw.size()) return false;
        const int l = (d[o] << 8) | d[o + 1];
        o += 2;
        if (o + l > raw.size()) return false;
        const QStringList parts = QString::fromUtf8(raw.constData() + o, l).split(QLatin1Char('|'));
        o += l;
        CaveState st;
        st.name = parts.value(0);
        for (int k = 1; k < parts.size(); ++k)
        {
            const int eq = parts[k].indexOf(QLatin1Char('='));
            if (eq > 0) st.props.append(qMakePair(parts[k].left(eq), parts[k].mid(eq + 1)));
        }
        const QString key = caveStateKey(st);
        auto it = byKey.constFind(key);
        int idx;
        if (it == byKey.constEnd())
        {
            idx = cs.states.size();
            cs.states.append(st);
            cs.idOf.append(caveStateToId(st, idx));
            byKey.insert(key, idx);
        }
        else idx = it.value();
        idMap[i] = cs.idOf[idx];
        idMapDry[i] = idMap[i];
        {   // the variant without waterlogged
            bool wl = false;
            CaveState sd = st;
            for (auto& pr : sd.props) if (pr.first == QLatin1String("waterlogged") && pr.second == QLatin1String("true")) { pr.second = QStringLiteral("false"); wl = true; }
            if (wl)
            {
                const QString kd = caveStateKey(sd);
                auto itd = byKey.constFind(kd);
                int idd;
                if (itd == byKey.constEnd())
                {
                    idd = cs.states.size();
                    cs.states.append(sd);
                    cs.idOf.append(caveStateToId(sd, idd));
                    byKey.insert(kd, idd);
                }
                else idd = itd.value();
                idMapDry[i] = cs.idOf[idd];
            }
        }
        if (st.name == QLatin1String("minecraft:creaking_heart")) { heartIds.insert(idMap[i]); heartId = idMap[i]; }
        static const char* const kMansionNames[] = { "minecraft:dark_oak_planks", "minecraft:cobblestone", "minecraft:glass_pane", "minecraft:dark_oak_stairs",
                                                     "minecraft:cobblestone_stairs", "minecraft:cobblestone_wall", "minecraft:polished_andesite",
                                                     "minecraft:smooth_stone_slab", "minecraft:dark_oak_fence" };
        for (const char* mn : kMansionNames) if (st.name == QLatin1String(mn)) mansionIds.insert(idMap[i]);
        if (st.name == QLatin1String("minecraft:pale_oak_log") && (st.props.isEmpty() || (st.props.size() == 1 && st.props[0].second == QLatin1String("y"))))
            logId = idMap[i];
    }
    hd.resize(w * h); surf.resize(w * h); off.resize(w * h + 1);
    ady.clear(); aid.clear(); aidDry.clear();
    uoff.clear(); udy.clear(); uid.clear();
    if (v3) uoff.resize(w * h + 1);
    double sum = 0.0;
    for (int i = 0; i < w * h; ++i)
    {
        if (o + 6 > raw.size()) return false;
        hd[i] = qint16((d[o] << 8) | d[o + 1]);
        const int si = (d[o + 2] << 8) | d[o + 3];
        const int n = (d[o + 4] << 8) | d[o + 5];
        o += 6;
        surf[i] = si < palN ? idMap[si] : quint16(IdGrass);
        off[i] = quint32(ady.size());
        const int esz = v2 ? 4 : 3;
        if (o + esz * n > raw.size()) return false;
        for (int k = 0; k < n; ++k)
        {
            const int ii = v2 ? ((d[o + 2] << 8) | d[o + 3]) : ((d[o + 1] << 8) | d[o + 2]);
            ady.append(v2 ? qint16((d[o] << 8) | d[o + 1]) : qint16(d[o]));
            aid.append(ii < palN ? idMap[ii] : quint16(IdAir));
            aidDry.append(ii < palN ? idMapDry[ii] : quint16(IdAir));
            o += esz;
        }
        if (v3)
        {   // the underground cells of a column: a count + (depth u8, palette index u16)
            if (o + 1 > raw.size()) return false;
            const int nu = d[o++];
            if (o + 3 * nu > raw.size()) return false;
            uoff[i] = quint32(udy.size());
            for (int k = 0; k < nu; ++k)
            {
                const int ii = (d[o + 1] << 8) | d[o + 2];
                udy.append(d[o]);
                uid.append(ii < palN ? idMap[ii] : quint16(IdAir));
                o += 3;
            }
        }
        sum += hd[i];
    }
    off[w * h] = quint32(ady.size());
    if (v3) uoff[w * h] = quint32(udy.size());
    W = w; H = h;
    if (v2)
    {   // block entities and the building box
        if (o + 4 > raw.size()) return false;
        const int nbe = int(r32(o));
        o += 4;
        for (int k = 0; k < nbe; ++k)
        {
            if (o + 11 > raw.size()) return false;
            PatchBe b;
            b.x = (d[o] << 8) | d[o + 1]; b.z = (d[o + 2] << 8) | d[o + 3]; b.dy = qint16((d[o + 4] << 8) | d[o + 5]); b.kind = d[o + 6];
            const int l = (d[o + 7] << 8) | d[o + 8];
            o += 9;
            if (o + l > raw.size()) return false;
            b.str = QString::fromUtf8(raw.constData() + o, l);
            o += l;
            bes.append(b);
        }
        if (o + 8 <= raw.size())
        {
            bbx0 = (d[o] << 8) | d[o + 1]; bbz0 = (d[o + 2] << 8) | d[o + 3]; bbx1 = (d[o + 4] << 8) | d[o + 5]; bbz1 = (d[o + 6] << 8) | d[o + 7];
        }
    }
    // the mansion columns: there is a block from mansionIds above the terrain; the box and an extension by 4 blocks (the yard and porch of the mansion are part of it too)
    mansion.fill(0, w * h);
    mx0 = w; mx1 = -1; mz0 = h; mz1 = -1;
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
        {
            bool hit = false;
            for (quint32 k = off[z * w + x]; k < off[z * w + x + 1] && !hit; ++k) hit = mansionIds.contains(aid[int(k)]);
            if (!hit) continue;
            mx0 = std::min(mx0, x); mx1 = std::max(mx1, x); mz0 = std::min(mz0, z); mz1 = std::max(mz1, z);
        }
    if (mx1 >= mx0)
    {
        mx0 = std::max(0, mx0 - 4); mz0 = std::max(0, mz0 - 4); mx1 = std::min(w - 1, mx1 + 4); mz1 = std::min(h - 1, mz1 + 4);
        for (int z = mz0; z <= mz1; ++z)
            for (int x = mx0; x <= mx1; ++x) mansion[z * w + x] = 1;
    }
    return true;
}


