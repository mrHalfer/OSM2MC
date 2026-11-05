#include "export/caveset.h"
#include <QDataStream>
#include <QFile>
#include <QStringList>


QString caveStateKey(const CaveState& s)
{
    QVector<QPair<QString, QString>> p = s.props;
    std::sort(p.begin(), p.end());
    QString k = s.name;
    for (const auto& kv : p) k += QLatin1Char('|') + kv.first + QLatin1Char('=') + kv.second;
    return k;
}

//! The mirror of a block state: fx — reflection along the X axis (east<->west), fz — along Z (north<->south).
CaveState mirrorCaveState(const CaveState& s, bool fx, bool fz)
{
    auto swapDir = [&](QString v) -> QString
    {
        if (fx) { if (v == QLatin1String("east")) v = QStringLiteral("west"); else if (v == QLatin1String("west")) v = QStringLiteral("east"); }
        if (fz) { if (v == QLatin1String("north")) v = QStringLiteral("south"); else if (v == QLatin1String("south")) v = QStringLiteral("north"); }
        return v;
    };
    const bool single = (fx != fz);       // one reflection changes the "handedness" (left<->right)
    auto swapLR = [](QString v) -> QString
    {
        if (v.contains(QLatin1String("left"))) return v.replace(QLatin1String("left"), QLatin1String("right"));
        if (v.contains(QLatin1String("right"))) return v.replace(QLatin1String("right"), QLatin1String("left"));
        return v;
    };
    QHash<QString, QString> m;
    for (const auto& kv : s.props) m.insert(kv.first, kv.second);
    QHash<QString, QString> o = m;
    for (const auto& kv : s.props)
    {
        const QString& k = kv.first; const QString& v = kv.second;
        if (k == QLatin1String("east") || k == QLatin1String("west") || k == QLatin1String("north") || k == QLatin1String("south"))
        {   // connections (fence, glass, wall, vine...): the value goes to the mirrored side
            const QString nk = swapDir(k);
            o[nk] = m.value(k);
        }
        else if (k == QLatin1String("facing"))
            o[k] = swapDir(v);
        else if (k == QLatin1String("shape"))
        {
            if (v.contains(QLatin1String("inner")) || v.contains(QLatin1String("outer")))
                o[k] = single ? swapLR(v) : v;                       // stairs: corners
            else
            {   // rails: north_south, east_west, ascending_*, north_east...
                QStringList parts = v.split(QLatin1Char('_'));
                for (QString& pt : parts) pt = swapDir(pt);
                o[k] = parts.join(QLatin1Char('_'));
            }
        }
        else if (k == QLatin1String("hinge") || k == QLatin1String("type"))
            o[k] = (single && (v == QLatin1String("left") || v == QLatin1String("right"))) ? swapLR(v) : v;
        else if (k == QLatin1String("rotation"))
        {
            int r = v.toInt();
            if (fx) r = (16 - r) % 16;
            if (fz) r = ((8 - r) % 16 + 16) % 16;
            o[k] = QString::number(r);
        }
    }
    CaveState out;
    out.name = s.name;
    for (const auto& kv : s.props) out.props.append(qMakePair(kv.first, o.value(kv.first)));
    return out;
}

quint16 caveStateToId(const CaveState& st, int paletteIdx)
{
    if (st.props.isEmpty())
    {
        if (st.name == QLatin1String("minecraft:air"))   return IdAir;
        if (st.name == QLatin1String("minecraft:stone")) return IdStone;
        const int idx = paletteIndexOf(st.name.toLatin1().constData());
        if (idx >= 0) return quint16(IdPaletteStart + idx);
    }
    else if (st.props.size() == 1 && st.props[0].first == QLatin1String("level") && st.props[0].second == QLatin1String("0"))
    {
        if (st.name == QLatin1String("minecraft:water")) return IdWater;
        if (st.name == QLatin1String("minecraft:lava"))  return IdLava;
    }
    const int dyn = int(IdCaveBase) + paletteIdx;
    return dyn < 65535 ? quint16(dyn) : quint16(IdStone);
}

bool CaveSet::load(const QString& path, quint64 seed, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (err) *err = QObject::tr("Could not open the cave template file: %1").arg(path);
        return false;
    }
    file = f.readAll();
    auto bad = [err](const char* why) -> bool
    { if (err) *err = QObject::tr("Cave templates: %1").arg(QLatin1String(why)); return false; };
    const uchar* d = reinterpret_cast<const uchar*>(file.constData());
    const qint64 n = file.size();
    if (n < 24 || qstrncmp(file.constData(), "MCW2", 4) != 0)
        return bad("a file of the MCW2 format is needed (rebuild with extract_cave_world.py)");
    auto rd32 = [&](qint64 pos) -> quint32
    { return (quint32(d[pos]) << 24) | (quint32(d[pos + 1]) << 16) | (quint32(d[pos + 2]) << 8) | d[pos + 3]; };
    const quint32 ver = rd32(4), w = rd32(8), h = rd32(12), palN = rd32(16);
    if (ver != 2 || w == 0 || h == 0 || w > 1024 || h > 1024 || palN == 0 || palN > 20000)
        return bad("unsupported version/header sizes");
    qint64 pos = 20;
    states.clear();
    QHash<QString, int> byKey;
    for (quint32 i = 0; i < palN; ++i)
    {
        if (pos + 2 > n) return bad("the palette is truncated");
        const int l = (d[pos] << 8) | d[pos + 1];
        pos += 2;
        if (pos + l > n) return bad("the palette is truncated");
        const QStringList parts = QString::fromUtf8(file.constData() + pos, l).split(QLatin1Char('|'));
        pos += l;
        CaveState st;
        st.name = parts.value(0);
        for (int k = 1; k < parts.size(); ++k)
        {
            const int eq = parts[k].indexOf(QLatin1Char('='));
            if (eq > 0) st.props.append(qMakePair(parts[k].left(eq), parts[k].mid(eq + 1)));
        }
        byKey.insert(caveStateKey(st), states.size());
        states.append(st);
    }
    baseN = states.size();
    const qint64 nChunks = qint64(w) * h;
    if (pos + nChunks * 8 > n) return bad("the chunk index is truncated");
    off.resize(int(nChunks)); len.resize(int(nChunks));
    for (qint64 i = 0; i < nChunks; ++i)
    {
        off[int(i)] = rd32(pos + i * 8);
        len[int(i)] = rd32(pos + i * 8 + 4);
        if (qint64(off[int(i)]) + len[int(i)] > n) return bad("a chunk blob goes beyond the file");
    }
    // mirrored palette variants (by X, by Z, by both axes)
    for (int fi = 0; fi < 3; ++fi)
    {
        const bool fx = (fi == 0 || fi == 2), fz = (fi == 1 || fi == 2);
        flipOf[fi].resize(baseN);
        for (int i = 0; i < baseN; ++i)
        {
            const CaveState m = mirrorCaveState(states[i], fx, fz);
            const QString k = caveStateKey(m);
            auto it = byKey.constFind(k);
            if (it == byKey.constEnd())
            {
                it = byKey.insert(k, states.size());
                states.append(m);
            }
            flipOf[fi][i] = quint32(it.value());
        }
    }
    idOf.resize(states.size());
    for (int i = 0; i < states.size(); ++i)
    {
        idOf[i] = caveStateToId(states[i], i);
        const QString& nm = states[i].name;
        if (nm == QLatin1String("minecraft:water") || nm == QLatin1String("minecraft:kelp") || nm == QLatin1String("minecraft:kelp_plant")
            || nm == QLatin1String("minecraft:seagrass") || nm == QLatin1String("minecraft:tall_seagrass") || nm == QLatin1String("minecraft:bubble_column"))
            oceanIds.insert(idOf[i]);
    }
    W = int(w); H = int(h);
    // the 'STRT' trailer: the structure starts of the window (after the chunk blobs); absent — structures without starts
    tplStarts.clear(); tplStartsByChunk.clear();
    {
        qint64 endBlobs = 0;
        for (int i = 0; i < off.size(); ++i) endBlobs = std::max<qint64>(endBlobs, qint64(off[i]) + len[i]);
        if (n >= endBlobs + 8 && qstrncmp(file.constData() + endBlobs, "STRT", 4) == 0)
        {
            qint64 tp = endBlobs + 4;
            const quint32 ns = rd32(tp); tp += 4;
            auto ri32 = [&](qint64 q) -> int { return int(rd32(q)); };
            for (quint32 si = 0; si < ns && tp + 2 <= n; ++si)
            {
                TplStart st;
                int l = (d[tp] << 8) | d[tp + 1]; tp += 2;
                if (tp + l + 10 > n) break;
                st.id = QString::fromUtf8(file.constData() + tp, l); tp += l;
                st.sx = ri32(tp); st.sz = ri32(tp + 4);
                const int np = (d[tp + 8] << 8) | d[tp + 9]; tp += 10;
                bool ok = true;
                for (int pi = 0; pi < np && ok; ++pi)
                {
                    TplPiece pc;
                    if (tp + 2 > n) { ok = false; break; }
                    l = (d[tp] << 8) | d[tp + 1]; tp += 2;
                    if (tp + l + 24 + 2 > n) { ok = false; break; }
                    pc.id = QString::fromUtf8(file.constData() + tp, l); tp += l;
                    for (int k = 0; k < 6; ++k) pc.bb[k] = ri32(tp + 4 * k);
                    tp += 24;
                    l = (d[tp] << 8) | d[tp + 1]; tp += 2;
                    if (tp + l > n) { ok = false; break; }
                    pc.loc = QString::fromUtf8(file.constData() + tp, l); tp += l;
                    st.pieces.append(pc);
                }
                if (!ok) break;
                if (st.sx < 0 || st.sx >= int(w) || st.sz < 0 || st.sz >= int(h)) continue;
                const int idx = tplStarts.size();
                tplStarts.append(st);
                QSet<ColKey> cells;
                cells.insert(pairKey(st.sx, st.sz));
                for (const TplPiece& pc : st.pieces)
                    for (int cz2 = pc.bb[2] >> 4; cz2 <= (pc.bb[5] >> 4); ++cz2)
                        for (int cx2 = pc.bb[0] >> 4; cx2 <= (pc.bb[3] >> 4); ++cx2)
                            if (cx2 >= 0 && cx2 < int(w) && cz2 >= 0 && cz2 < int(h)) cells.insert(pairKey(cx2, cz2));
                for (const qint64 ck : cells) tplStartsByChunk[ck].append(idx);
            }
        }
    }
    // the tile offset: a different seed — a different donor area under the city
    quint64 hsh = seed * 0x9E3779B97F4A7C15ull;
    hsh ^= hsh >> 29;
    offX = int(hsh % quint64(2 * W));
    hsh *= 0xBF58476D1CE4E5B9ull; hsh ^= hsh >> 31;
    offZ = int(hsh % quint64(2 * H));
    g_caveStates = &states;
    return true;
}

void CaveSet::startsAt(int cx, int cz, QVector<WorldStart>& out) const
{
    out.clear();
    if (empty() || tplStarts.isEmpty()) return;
    const int px = ((cx + offX) % (2 * W) + 2 * W) % (2 * W), pz = ((cz + offZ) % (2 * H) + 2 * H) % (2 * H);
    const bool fx = px >= W, fz = pz >= H;
    const int u = fx ? 2 * W - 1 - px : px, v = fz ? 2 * H - 1 - pz : pz;
    const auto it = tplStartsByChunk.constFind(pairKey(u, v));
    if (it == tplStartsByChunk.constEnd()) return;
    const int baseCx = cx - px, baseCz = cz - pz;                      // the world chunk of the tile origin (for both halves)
    const int baseX = baseCx * 16, baseZ = baseCz * 16;
    for (const int si : it.value())
    {
        const TplStart& st = tplStarts[si];
        WorldStart ws;
        ws.id = st.id;
        ws.startCx = fx ? baseCx + 2 * W - 1 - st.sx : baseCx + st.sx;
        ws.startCz = fz ? baseCz + 2 * H - 1 - st.sz : baseCz + st.sz;
        ws.here = (u == st.sx && v == st.sz);
        if (ws.here)
            for (const TplPiece& pc : st.pieces)
            {
                TplPiece wp = pc;
                wp.bb[0] = fx ? baseX + 2 * W * 16 - 1 - pc.bb[3] : baseX + pc.bb[0];
                wp.bb[3] = fx ? baseX + 2 * W * 16 - 1 - pc.bb[0] : baseX + pc.bb[3];
                wp.bb[2] = fz ? baseZ + 2 * H * 16 - 1 - pc.bb[5] : baseZ + pc.bb[2];
                wp.bb[5] = fz ? baseZ + 2 * H * 16 - 1 - pc.bb[2] : baseZ + pc.bb[5];
                ws.pieces.append(wp);
            }
        out.append(ws);
    }
}

CaveTemplate CaveSet::get(int cx, int cz) const
{
    CaveTemplate ct;
    if (empty()) return ct;
    const int px = ((cx + offX) % (2 * W) + 2 * W) % (2 * W), pz = ((cz + offZ) % (2 * H) + 2 * H) % (2 * H);
    const bool fx = px >= W, fz = pz >= H;
    const int u = fx ? 2 * W - 1 - px : px, v = fz ? 2 * H - 1 - pz : pz;
    const int idx = v * W + u;
    const QByteArray raw = qUncompress(reinterpret_cast<const uchar*>(file.constData()) + off[idx], int(len[idx]));
    static constexpr int CELLS = 128 * 256;
    if (raw.size() < 4 + CELLS * 2) return ct;
    const uchar* r = reinterpret_cast<const uchar*>(raw.constData());
    const int beN = (r[0] << 24) | (r[1] << 16) | (r[2] << 8) | r[3];
    int pos = 4;
    for (int i = 0; i < beN && pos + 7 <= raw.size(); ++i)
    {
        CaveBe b;
        const int lx = qint8(r[pos]), lz = qint8(r[pos + 1]);
        b.y = qint16((r[pos + 2] << 8) | r[pos + 3]);
        b.kind = r[pos + 4];
        const int l = (r[pos + 5] << 8) | r[pos + 6];
        pos += 7;
        if (pos + l > raw.size()) break;
        if (b.kind == 5) b.raw = QByteArray(raw.constData() + pos, l);
        else b.str = QString::fromUtf8(raw.constData() + pos, l);
        pos += l;
        b.lx = fx ? 15 - lx : lx;
        b.lz = fz ? 15 - lz : lz;
        ct.bes.append(b);
    }
    if (raw.size() < pos + CELLS * 2) return ct;
    const uchar* c = r + pos;
    const int f = (fx && fz) ? 3 : (fz ? 2 : (fx ? 1 : 0));   // 0 none, 1 by X, 2 by Z, 3 by XZ
    const quint32* fm = f ? flipOf[f - 1].constData() : nullptr;
    ct.blocks.resize(CELLS);
    for (int ty = 0; ty < 128; ++ty)
        for (int lz = 0; lz < 16; ++lz)
        {
            const int sz = fz ? 15 - lz : lz;
            for (int lx = 0; lx < 16; ++lx)
            {
                const int sx = fx ? 15 - lx : lx;
                const int src = ty * 256 + sz * 16 + sx;
                quint32 p = (quint32(c[2 * src]) << 8) | c[2 * src + 1];
                if (p >= quint32(baseN)) p = 0;
                if (fm) p = fm[p];
                ct.blocks[ty * 256 + lz * 16 + lx] = idOf[int(p)];
            }
        }
    // The "donor ocean": a continuous body of water/seaweed from the very top of the template (y=63) downward is the seabed of the donor, while here it is land.
    // We fill it with stone: the ground is leveled to the top of the template, caves start under a real bottom, not under an ocean.
    // Other large bodies of water (underground lakes/an "ocean" under the bottom that did not reach the top of the template): connected water of 150 cells or more
    // inside a chunk is filled with stone; small puddles and cave streams remain.
    if (!oceanIds.isEmpty())
    {
        QVector<char> seen(128 * 256, 0);
        QVector<int> stack, comp;
        for (int start = 0; start < 128 * 256; ++start)
        {
            if (seen[start] || !oceanIds.contains(ct.blocks[start])) continue;
            comp.clear(); stack.clear();
            stack.append(start); seen[start] = 1;
            while (!stack.isEmpty())
            {
                const int c = stack.takeLast();
                comp.append(c);
                const int ty = c >> 8, lz = (c >> 4) & 15, lx = c & 15;
                const int nb[6] = { lx > 0 ? c - 1 : -1, lx < 15 ? c + 1 : -1, lz > 0 ? c - 16 : -1, lz < 15 ? c + 16 : -1,
                                    ty > 0 ? c - 256 : -1, ty < 127 ? c + 256 : -1 };
                for (int n : nb)
                    if (n >= 0 && !seen[n] && oceanIds.contains(ct.blocks[n])) { seen[n] = 1; stack.append(n); }
            }
            if (comp.size() >= 150)
                for (int c : comp) ct.blocks[c] = IdStone;
        }
    }
    // The same with the air "above the donor ground": where the donor terrain is below y=63 (valleys, slopes), open air stretched from the top of the template downward —
    // here there is ground there, and such a cavity turned into a huge void underground (especially at the joints of template tiles).
    for (int col = 0; col < 256; ++col)
        for (int ty = 127; ty >= 0; --ty)
        {
            const quint16 b = ct.blocks[ty * 256 + col];
            if (b != IdAir && !oceanIds.contains(b)) break;
            ct.blocks[ty * 256 + col] = IdStone;
        }
    for (int sec = 0; sec < 8; ++sec)
    {
        const quint16 first = ct.blocks[sec * 4096];
        bool uni = true;
        for (int j = 1; j < 4096 && uni; ++j)
            if (ct.blocks[sec * 4096 + j] != first) uni = false;
        ct.uniId[sec] = (uni && first != IdAir) ? first : quint16(0);
    }
    return ct;
}


