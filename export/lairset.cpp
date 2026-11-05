#include "export/lairset.h"
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>


bool LairSet::load(const QString& path, CaveSet& cs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray file = f.readAll();
    const uchar* d = reinterpret_cast<const uchar*>(file.constData());
    const qint64 n = file.size();
    auto rd32 = [&](qint64 pos) -> quint32
    { return (quint32(d[pos]) << 24) | (quint32(d[pos + 1]) << 16) | (quint32(d[pos + 2]) << 8) | d[pos + 3]; };
    if (n < 12 || qstrncmp(file.constData(), "LST1", 4) != 0) return false;
    const quint32 jl = rd32(4);
    if (8 + qint64(jl) > n) return false;
    const QJsonArray arr = QJsonDocument::fromJson(QByteArray(file.constData() + 8, int(jl))).object().value(QStringLiteral("structures")).toArray();
    QHash<QString, int> byKey;
    for (int i = 0; i < cs.states.size(); ++i) byKey.insert(caveStateKey(cs.states[i]), i);
    qint64 pos = 8 + qint64(jl);
    QVector<LairStruct> out;
    for (int si = 0; si < arr.size(); ++si)
    {
        if (pos + 4 > n) return false;
        const quint32 plen = rd32(pos);
        pos += 4;
        if (pos + plen > n || plen < 20 || qstrncmp(file.constData() + pos, "MCW2", 4) != 0) return false;
        const uchar* p = d + pos;
        auto r32 = [&](qint64 o) -> quint32 { return (quint32(p[o]) << 24) | (quint32(p[o + 1]) << 16) | (quint32(p[o + 2]) << 8) | p[o + 3]; };
        const int W = int(r32(8)), H = int(r32(12)), palN = int(r32(16));
        if (W <= 0 || H <= 0 || W > 64 || H > 64 || palN <= 0 || palN > 20000) return false;
        qint64 o = 20;
        QVector<quint16> idMap(palN);
        for (int i = 0; i < palN; ++i)
        {
            const int l = (p[o] << 8) | p[o + 1];
            o += 2;
            if (o + l > qint64(plen)) return false;
            const QStringList parts = QString::fromUtf8(reinterpret_cast<const char*>(p) + o, l).split(QLatin1Char('|'));
            o += l;
            CaveState stt;
            stt.name = parts.value(0);
            for (int k = 1; k < parts.size(); ++k)
            {
                const int eq = parts[k].indexOf(QLatin1Char('='));
                if (eq > 0) stt.props.append(qMakePair(parts[k].left(eq), parts[k].mid(eq + 1)));
            }
            const QString key = caveStateKey(stt);
            auto it = byKey.constFind(key);
            int idx;
            if (it == byKey.constEnd())
            {
                idx = cs.states.size();
                cs.states.append(stt);
                cs.idOf.append(caveStateToId(stt, idx));
                byKey.insert(key, idx);
            }
            else idx = it.value();
            idMap[i] = cs.idOf[idx];
        }
        const qint64 idxPos = o;
        o += qint64(W) * H * 8;
        if (o > qint64(plen)) return false;
        const QJsonObject jo = arr[si].toObject();
        LairStruct ls;
        ls.id = jo.value(QStringLiteral("id")).toString();
        ls.biome = jo.value(QStringLiteral("biome")).toString();
        ls.cx0 = jo.value(QStringLiteral("cx0")).toInt(); ls.cz0 = jo.value(QStringLiteral("cz0")).toInt();
        ls.W = W; ls.H = H;
        const QJsonArray en = jo.value(QStringLiteral("entry")).toArray();
        ls.ex = en.at(0).toInt(); ls.ez = en.at(1).toInt();
        ls.floorY = jo.value(QStringLiteral("floorY")).toInt(); ls.topY = jo.value(QStringLiteral("topY")).toInt();
        const QJsonArray rm = jo.value(QStringLiteral("room")).toArray();
        for (int k = 0; k < 6; ++k) ls.room[k] = rm.at(k).toInt();
        const QJsonArray yr = jo.value(QStringLiteral("yRange")).toArray();
        if (yr.size() == 2) { ls.yLo = std::max(-64, yr.at(0).toInt()); ls.yHi = std::min(63, yr.at(1).toInt()); }
        const int RW = W * 16, RH = H * 16;
        ls.cells.fill(quint16(IdAir), qint64(RW) * RH * 128);
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i)
            {
                const qint64 e = idxPos + qint64(j * W + i) * 8;
                const quint32 bo = r32(e), bl = r32(e + 4);
                if (qint64(bo) + bl > qint64(plen)) return false;
                const QByteArray raw = qUncompress(p + bo, int(bl));
                if (raw.size() < 4 + 32768 * 2) return false;
                const uchar* r = reinterpret_cast<const uchar*>(raw.constData());
                const int beN = (r[0] << 24) | (r[1] << 16) | (r[2] << 8) | r[3];
                int q = 4;
                for (int b = 0; b < beN && q + 7 <= raw.size(); ++b)
                {
                    LairBe be;
                    const int lx = qint8(r[q]), lz = qint8(r[q + 1]);
                    be.y = qint16((r[q + 2] << 8) | r[q + 3]);
                    be.kind = r[q + 4];
                    const int l = (r[q + 5] << 8) | r[q + 6];
                    q += 7;
                    if (q + l > raw.size()) break;
                    be.str = QString::fromUtf8(raw.constData() + q, l);
                    q += l;
                    be.x = (ls.cx0 + i) * 16 + lx; be.z = (ls.cz0 + j) * 16 + lz;
                    ls.bes.append(be);
                }
                if (raw.size() < q + 32768 * 2) return false;
                const uchar* c = r + q;
                for (int ty = 0; ty < 128; ++ty)
                    for (int lz = 0; lz < 16; ++lz)
                        for (int lx = 0; lx < 16; ++lx)
                        {
                            const int src = ty * 256 + lz * 16 + lx;
                            const int pi = (c[2 * src] << 8) | c[2 * src + 1];
                            ls.cells[(qint64(ty) * RH + (j * 16 + lz)) * RW + (i * 16 + lx)] = pi < palN ? idMap[pi] : quint16(IdAir);
                        }
            }
        if (ls.id.isEmpty()) return false;
        out.append(ls);
        pos += plen;
    }
    st = out;
    return !st.isEmpty();
}

QVector<LairSet::Start> LairSet::startsTouching(int cx, int cz) const
{
    QVector<Start> out;
    for (const LairPlace& pl : placed)
    {
        const LairStruct& s = st[pl.s];
        Start r;
        r.sid = QStringLiteral("minecraft:") + s.id;
        r.bb[0] = s.room[0] + pl.tx; r.bb[1] = s.room[1]; r.bb[2] = s.room[2] + pl.tz;
        r.bb[3] = s.room[3] + pl.tx; r.bb[4] = s.room[4]; r.bb[5] = s.room[5] + pl.tz;
        r.scx = ((r.bb[0] + r.bb[3]) / 2) >> 4; r.scz = ((r.bb[2] + r.bb[5]) / 2) >> 4;
        if (cx >= (r.bb[0] >> 4) && cx <= (r.bb[3] >> 4) && cz >= (r.bb[2] >> 4) && cz <= (r.bb[5] >> 4))
            out.append(r);
    }
    return out;
}

bool LairSet::apply(int cx, int cz, CaveTemplate& ct, bool* deepDark) const
{
    bool changed = false;
    const QRect chunk(cx * 16, cz * 16, 16, 16);
    for (const LairPlace& pl : placed)
    {
        const LairStruct& s = st[pl.s];
        const QRect r = rectOf(pl).intersected(chunk);
        if (r.isEmpty()) continue;
        if (deepDark && !s.biome.isEmpty()) *deepDark = true;
        const int RW = s.W * 16, RH = s.H * 16;
        const int ox = s.cx0 * 16 + pl.tx, oz = s.cz0 * 16 + pl.tz;          // the world corner of the rectangle
        for (int z = r.top(); z <= r.bottom(); ++z)
            for (int x = r.left(); x <= r.right(); ++x)
            {
                const int lx = x - cx * 16, lz = z - cz * 16;
                const int edgeXZ = std::min(std::min(x - ox, ox + RW - 1 - x), std::min(z - oz, oz + RH - 1 - z));
                for (int ty = s.yLo + 64; ty <= s.yHi + 64; ++ty)
                {
                    quint16 b = s.cells[(qint64(ty) * RH + (z - oz)) * RW + (x - ox)];
                    // the cut seam: at the boundary of the rectangle (and of the top/bottom of the range) a void of the donor, behind which in the world template there is rock,
                    // is replaced with stone — otherwise the cut cavity gives a huge "chasm". Where the cave continues in the same cell
                    // (air in the world template too) — the cut smoothly leads into the cave, we touch nothing.
                    if ((b == IdAir || b == IdWater) && (edgeXZ < 6 || ty - (s.yLo + 64) < 3 || (s.yHi + 64) - ty < 3))
                    {
                        const quint16 base = ct.blocks[ty * 256 + lz * 16 + lx];
                        if (base != IdAir && base != IdWater) b = IdStone;
                    }
                    ct.blocks[ty * 256 + lz * 16 + lx] = b;
                }
            }
        for (int i = ct.bes.size() - 1; i >= 0; --i)                          // the donor chests inside the overlapped area are replaced
            if (ct.bes[i].y >= s.yLo && ct.bes[i].y <= s.yHi && r.contains(cx * 16 + ct.bes[i].lx, cz * 16 + ct.bes[i].lz)) ct.bes.removeAt(i);
        for (const LairBe& b : s.bes)
        {
            const int wx = b.x + pl.tx, wz = b.z + pl.tz;
            if (!r.contains(wx, wz) || b.y < s.yLo || b.y > s.yHi) continue;
            CaveBe cb;
            cb.lx = wx - cx * 16; cb.lz = wz - cz * 16; cb.y = b.y; cb.kind = b.kind; cb.str = b.str;
            ct.bes.append(cb);
        }
        changed = true;
    }
    if (changed)
        for (int sec = 0; sec < 8; ++sec)
        {
            const quint16 first = ct.blocks[sec * 4096];
            bool uni = true;
            for (int j = 1; j < 4096 && uni; ++j)
                if (ct.blocks[sec * 4096 + j] != first) uni = false;
            ct.uniId[sec] = (uni && first != IdAir) ? first : quint16(0);
        }
    return changed;
}


