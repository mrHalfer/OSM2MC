#include "export/buildingmerge.h"
#include "export/colkey.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <algorithm>
#include <functional>
#include <limits>

//! Industrial zone (44340000), churches (44319000) and all non-residential (44310000..44318000) are composite buildings (a base, domes, annexes…): touching/overlapping buildings of ONE
//! code are assembled into ONE object (the meshes are glued) so that a complex of parts
//! of different heights becomes one building with a common outline, common floors and partitions (the walls between parts are removed by the voxelizer by
//! the merged flag). A complex is limited to 140×140 blocks and 60 parts — giant clusters stay separate.
//! The codes whose parts are glued into one complex: industrial zone 44340000, church 44319000 and all non-residential 44310000..44318000
//! (non-residential, administrative, educational, children's, healthcare, scientific, culture, museum, shopping mall).
bool mergeableCode(int code)
{
    return code == 44340000 || code == 44319000 || (code >= 44310000 && code <= 44318000 && code % 1000 == 0);
}

//! The minimum height of a building part: low annexes and blocks (including those attached to the main building) are pulled upward so that the room inside is no lower than 3 blocks.
//! The whole mesh of the part is stretched (the bottom stays in place); sheds and private houses (their own geometry) and elevator shafts are not touched. It is called BEFORE gluing complexes: after it the parts can no longer be told apart.
void raiseLowBuildingParts(QVector<McWorldExporter::McObject>& objs, const VxTransform& tf, double minBlocks)
{
    using O = McWorldExporter::McObject;
    for (O& o : objs)
    {
        if (o.kind != O::Building || !o.closed || o.lift || o.vertices.size() < 3) continue;
        if (o.code == 44000000 || o.code == 44400002 || o.code == 44400100 || o.code == 44400110) continue;
        double zmin = 1e300, zmax = -1e300;
        for (const QVector3D& v : o.vertices) { zmin = std::min(zmin, double(v.z())); zmax = std::max(zmax, double(v.z())); }
        const double hb = (zmax - zmin) * tf.zScale;
        if (hb < 1e-6 || hb >= minBlocks) continue;
        const double k = minBlocks / hb;
        for (QVector3D& v : o.vertices) v.setZ(float(zmin + (double(v.z()) - zmin) * k));
        o.extrudeH *= k;
    }
}

QVector<McWorldExporter::McObject> mergeIndustrial(const QVector<McWorldExporter::McObject>& in, const VxTransform& tf,
                                                            const std::atomic_bool* cancel)
{
    using O = McWorldExporter::McObject;
    // Raised building parts (min_height) are glued with the building under them: the slab between them is removed and the interior is one (the floors and the stairs
    // are then laid by the ordinary pass over the whole stack). Not only the "complex" codes take part: a residential part over a residential building too, but only
    // when their plans overlap (the buildings that merely touch are not glued).
    struct Box { double x0, x1, z0, z1; };
    auto boxOf = [&](const O& o)
    {
        Box b{ 1e18, -1e18, 1e18, -1e18 };
        for (const QVector3D& v : o.vertices)
        {
            const double bx = toBlockX(tf, v.x()), bz = toBlockZ(tf, v.y());
            b.x0 = std::min(b.x0, bx); b.x1 = std::max(b.x1, bx); b.z0 = std::min(b.z0, bz); b.z1 = std::max(b.z1, bz);
        }
        return b;
    };
    QVector<Box> raisedBoxes;
    for (const O& o : in)
        if (o.kind == O::Building && o.closed && o.raised > 0.0 && !o.lift && o.vertices.size() >= 3) raisedBoxes << boxOf(o);
    auto underRaised = [&](const O& o)
    {
        if (raisedBoxes.isEmpty()) return false;
        const Box b = boxOf(o);
        for (const Box& r : raisedBoxes)
            if (b.x0 <= r.x1 && b.x1 >= r.x0 && b.z0 <= r.z1 && b.z1 >= r.z0) return true;
        return false;
    };
    QVector<int> cand;
    for (int i = 0; i < in.size(); ++i)
        if (in[i].kind == O::Building && in[i].closed && !in[i].lift && in[i].vertices.size() >= 3
            && (mergeableCode(in[i].code) || in[i].raised > 0.0 || (in[i].code == 44400000 && underRaised(in[i]))))
            cand << i;
    if (cand.size() < 2) return in;
    const int n = cand.size();
    QVector<QList<qint64>> cols(n);
    QHash<ColKey, int> owner;
    QSet<quint64> edges;
    auto addEdge = [&](int a, int b, bool overlap)
    {   // we glue only parts of one code (a church with a church, a workshop with a workshop)
        if (a == b) return;
        const O& A = in[cand[a]]; const O& B = in[cand[b]];
        if (A.code != B.code) return;
        if (!mergeableCode(A.code) && (!overlap || (A.raised <= 0.0 && B.raised <= 0.0))) return;       // a residential one — only a raised part over its building
        edges.insert((quint64(std::min(a, b)) << 32) | quint32(std::max(a, b)));
    };
    QVector<int> bx0(n, std::numeric_limits<int>::max()), bx1(n, std::numeric_limits<int>::min());
    QVector<int> bz0(n, std::numeric_limits<int>::max()), bz1(n, std::numeric_limits<int>::min());
    for (int k = 0; k < n; ++k)
    {
        if (cancel && cancel->load()) return in;
        footprintColumns(in[cand[k]], tf, cols[k], cancel);
        for (qint64 ck : cols[k])
        {
            const int x = keyFirst(ck), z = keySecond(ck);
            bx0[k] = std::min(bx0[k], x); bx1[k] = std::max(bx1[k], x);
            bz0[k] = std::min(bz0[k], z); bz1[k] = std::max(bz1[k], z);
            auto it = owner.find(ck);
            if (it != owner.end()) addEdge(it.value(), k, true); else owner.insert(ck, k);
        }
    }
    static const int ddx[4] = { 1, -1, 0, 0 }, ddz[4] = { 0, 0, 1, -1 };
    for (int k = 0; k < n; ++k)
        for (qint64 ck : cols[k])
        {
            const int x = keyFirst(ck), z = keySecond(ck);
            for (int d = 0; d < 4; ++d)
            {
                const auto it = owner.constFind(pairKey(x + ddx[d], z + ddz[d]));
                if (it != owner.constEnd()) addEdge(it.value(), k, false);
            }
        }
    if (edges.isEmpty()) return in;
    QVector<int> par(n);
    for (int i = 0; i < n; ++i) par[i] = i;
    QVector<int> cnt(n, 1);
    std::function<int(int)> find = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
    QList<quint64> el = edges.values();
    std::sort(el.begin(), el.end());
    for (quint64 e : el)
    {
        const int a = find(int(e >> 32)), b = find(int(e & 0xFFFFFFFFu));
        if (a == b) continue;
        const int nx0 = std::min(bx0[a], bx0[b]), nx1 = std::max(bx1[a], bx1[b]);
        const int nz0 = std::min(bz0[a], bz0[b]), nz1 = std::max(bz1[a], bz1[b]);
        if (nx1 - nx0 > 140 || nz1 - nz0 > 140 || cnt[a] + cnt[b] > 60) continue;
        par[b] = a; cnt[a] += cnt[b];
        bx0[a] = nx0; bx1[a] = nx1; bz0[a] = nz0; bz1[a] = nz1;
    }
    QHash<int, QVector<int>> groups;                       // the root -> the cand indices
    for (int k = 0; k < n; ++k) groups[find(k)].append(k);
    QVector<char> drop(in.size(), 0);
    QHash<int, O> mergedAt;                                 // the position of the first member -> the glued object
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
    {
        const QVector<int>& g = it.value();
        if (g.size() < 2) continue;
        QVector<int> ord = g;
        std::sort(ord.begin(), ord.end());
        O m = in[cand[ord.first()]];
        for (int q = 1; q < ord.size(); ++q)
        {
            const O& p = in[cand[ord[q]]];
            const quint32 off = quint32(m.vertices.size());
            m.vertices += p.vertices;
            for (quint32 ix : p.indices) m.indices.append(ix + off);
            m.extrudeH = std::max(m.extrudeH, p.extrudeH);
            m.raised = std::min(m.raised, p.raised);                                 // the complex floats only if all its parts do
            if (m.street.isEmpty() && !p.street.isEmpty()) { m.streetType = p.streetType; m.street = p.street; m.number = p.number; }
            drop[cand[ord[q]]] = 1;
        }
        m.merged = true;
        m.thin = false; m.part = false;
        if (m.raised <= 0.0)
        {   // the columns under the raised parts only (not over a grounded member): the ground under them must not be raised to the body
            QSet<qint64> grounded;
            for (int k : ord)
                if (in[cand[k]].raised <= 0.0) for (qint64 ck : cols[k]) grounded.insert(ck);
            for (int k : ord)
                if (in[cand[k]].raised > 0.0) for (qint64 ck : cols[k]) if (!grounded.contains(ck)) m.floatCols.insert(ck);
            // the footprint columns do not include the edge columns of the walls: widen the set by 2 blocks (but not over the grounded members)
            const QList<qint64> core = m.floatCols.values();
            for (qint64 ck : core)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx)
                    {
                        const qint64 nk = pairKey(keyFirst(ck) + dx, keySecond(ck) + dz);
                        if (!grounded.contains(nk)) m.floatCols.insert(nk);
                    }
        }
        mergedAt.insert(cand[ord.first()], m);
    }
    if (mergedAt.isEmpty()) return in;
    QVector<O> out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i)
    {
        if (drop[i]) continue;
        const auto mit = mergedAt.constFind(i);
        out.append(mit != mergedAt.constEnd() ? mit.value() : in[i]);
    }
    return out;
}
