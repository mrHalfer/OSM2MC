// Running the Minecraft exporter on a scene from shp_to_scene.py (real data of a .tqt project). Usage: mc_scene_run.exe scene.bin output.zip [native|legacy] [apron]
#include "mcworldexporter.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QDataStream>
#include <cstdio>

static QString rdStr(QDataStream& in) { quint16 n; in >> n; QByteArray b(n, 0); in.readRawData(b.data(), n); return QString::fromUtf8(b); }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: mc_scene_run scene.bin out.zip [native|legacy] [apronChunks]\n"); return 2; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "no scene\n"); return 2; }
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 magic, n; double u2m;
    in >> magic >> n;
    in.setFloatingPointPrecision(QDataStream::DoublePrecision);
    in >> u2m;
    QVector<McWorldExporter::McObject> objs;
    for (quint32 i = 0; i < n; ++i)
    {
        McWorldExporter::McObject o;
        quint8 kind, closed, linear, lift, intra; qint32 code; float ext; double rs;
        in.setFloatingPointPrecision(QDataStream::SinglePrecision);
        in >> kind >> code >> closed >> linear >> lift >> intra >> ext;
        in.setFloatingPointPrecision(QDataStream::DoublePrecision);
        in >> rs;
        quint8 cr, cg, cb; in >> cr >> cg >> cb;
        o.kind = McWorldExporter::McObject::Kind(kind); o.code = code; o.closed = closed; o.linear = linear; o.lift = lift; o.intraBlock = intra;
        o.extrudeH = ext; o.color = QColor(cr, cg, cb);
        o.material = rdStr(in); o.street = rdStr(in); o.streetType = rdStr(in); o.number = rdStr(in);
        quint32 nv; in >> nv; o.vertices.resize(int(nv));
        in.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (quint32 k = 0; k < nv; ++k) { float x, y, z; in >> x >> y >> z; o.vertices[int(k)] = QVector3D(x, y, z); }
        quint32 ni; in >> ni; o.indices.resize(int(ni));
        for (quint32 k = 0; k < ni; ++k) in >> o.indices[int(k)];
        quint32 np; in >> np;
        in.setFloatingPointPrecision(QDataStream::DoublePrecision);
        for (quint32 k = 0; k < np; ++k)
        {
            quint32 m; in >> m; QVector<QPointF> pa; pa.reserve(int(m));
            for (quint32 q = 0; q < m; ++q) { double x, y; in >> x >> y; pa.append(QPointF(x, y)); }
            o.paths.append(pa);
        }
        objs.append(o);
    }
    fprintf(stderr, "objects: %d\n", int(objs.size()));
    McWorldExporter::WorldOptions w;
    w.seed = 777; w.normalSurroundings = true; w.caves = true; w.cavesFile = QStringLiteral("cave_templates.bin");
    w.apronChunks = argc > 4 ? atoi(argv[4]) : 0;
    w.unitsToMetersX = u2m; w.unitsToMetersY = u2m;
    const bool legacy = argc > 3 && QByteArray(argv[3]) == "legacy";
    QElapsedTimer t; t.start();
    int lastStage = -1;
    const auto r = McWorldExporter::Export(objs, McWorldExporter::HeightSampler(), QString::fromLocal8Bit(argv[2]), QStringLiteral("stal_kult_scene"), nullptr,
        legacy ? McWorldExporter::Format::Legacy121x : McWorldExporter::Format::Native26x,
        [&](int done, int total, int stage) { static int lastDone = -100000; if (stage == lastStage && done - lastDone >= 10000) { lastDone = done; fprintf(stderr, "[%6.1fs]   %d/%d\n", t.elapsed() / 1000.0, done, total); } if (stage != lastStage) { lastDone = done; lastStage = stage; fprintf(stderr, "[%6.1fs] stage %d (%d/%d)\n", t.elapsed() / 1000.0, stage, done, total); } }, w);
    fprintf(stderr, "ok=%d err=%s objects=%d skipped=%d blocks=%d regions=%d bytes=%lld time %.1f s\n", r.ok, qPrintable(r.error), r.objectCount, r.skippedCount, r.blockCount, r.regionCount, (long long)r.zipBytes, t.elapsed() / 1000.0);
    fprintf(stderr, "[info] %s\n", qPrintable(r.lairInfo));
    { QFile fi(QString::fromLocal8Bit(argv[2]) + QStringLiteral(".info.txt")); if (fi.open(QIODevice::WriteOnly)) { fi.write(r.lairInfo.toUtf8()); fi.write(QByteArray(1, 10)); fi.write(r.error.toUtf8()); } }
    return r.ok ? 0 : 1;
}
