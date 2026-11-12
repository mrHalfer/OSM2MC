#include "export/playgroundset.h"
#include <QFile>

//! The playground structures (playground_templates.bin, PGP1 from extract_playgrounds.py): magic + the Qt-qUncompress
//! payload (see the format description in extract_playgrounds.py). The state strings of the palette are deduplicated
//! against the cave-state registry (cs.states) exactly like the cars and the Pale Garden donor do.
bool PlaygroundSet::load(const QString& path, CaveSet& cs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray file = f.readAll();
    if (file.size() < 16 || qstrncmp(file.constData(), "PGP1", 4) != 0) return false;
    const QByteArray raw = qUncompress(reinterpret_cast<const uchar*>(file.constData()) + 4, int(file.size() - 4));
    if (raw.size() < 4) return false;
    const uchar* d = reinterpret_cast<const uchar*>(raw.constData());
    int o = 0;
    auto need = [&](int n) { return o + n <= raw.size(); };
    auto u16 = [&]() -> int { const int v = (d[o] << 8) | d[o + 1]; o += 2; return v; };
    auto u32 = [&]() -> quint32 { const quint32 v = (quint32(d[o]) << 24) | (quint32(d[o + 1]) << 16) | (quint32(d[o + 2]) << 8) | d[o + 3]; o += 4; return v; };

    const int count = int(u32());
    if (count <= 0 || count > 64) return false;
    QHash<QString, int> byKey;
    for (int i = 0; i < cs.states.size(); ++i) byKey.insert(caveStateKey(cs.states[i]), i);

    items.clear();
    for (int c = 0; c < count; ++c)
    {
        if (!need(2)) return false;
        const int nl = u16();
        if (!need(nl)) return false;
        Item it;
        it.name = QString::fromUtf8(raw.constData() + o, nl);
        o += nl;
        if (!need(6)) return false;
        it.sx = u16(); it.sy = u16(); it.sz = u16();
        if (it.sx <= 0 || it.sy <= 0 || it.sz <= 0 || it.sx > 64 || it.sy > 64 || it.sz > 64) return false;
        if (!need(2)) return false;
        const int palN = u16();
        if (palN <= 0 || palN > 20000) return false;
        QVector<quint16> idMap(palN);
        for (int i = 0; i < palN; ++i)
        {
            if (!need(2)) return false;
            const int l = u16();
            if (!need(l)) return false;
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
            auto sit = byKey.constFind(key);
            int idx;
            if (sit == byKey.constEnd())
            {
                idx = cs.states.size();
                cs.states.append(st);
                cs.idOf.append(caveStateToId(st, idx));
                byKey.insert(key, idx);
            }
            else idx = sit.value();
            idMap[i] = cs.idOf[idx];
        }
        if (!need(1)) return false;
        const int rotN = d[o++];
        if (rotN != 4) return false;                                        // the format carries the four quarter turns
        for (int r = 0; r < 4; ++r)
        {
            if (!need(4)) return false;
            const quint32 n = u32();
            if (n == 0 || n > 1000000 || !need(int(n) * 5)) return false;
            const int boxX = (r % 2) ? it.sz : it.sx, boxZ = (r % 2) ? it.sx : it.sz;
            Rot& rot = it.rots[r];
            rot.cells.clear();
            for (quint32 k = 0; k < n; ++k)
            {
                Cell cell{ d[o], d[o + 1], d[o + 2], quint16((d[o + 3] << 8) | d[o + 4]) };
                o += 5;
                if (cell.x >= boxX || cell.y >= it.sy || cell.z >= boxZ || cell.id >= palN) return false;
                cell.id = idMap[cell.id];
                rot.cells.append(cell);
            }
        }
        items.append(it);
    }
    return o == raw.size();
}
