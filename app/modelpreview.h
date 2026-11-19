#pragma once
// An isometric preview of the point figures the exporter builds (the ModelPoint numbers 1..9, step
// 3.575 of export/exportrun.cpp): the rules dialog's spot-structure picker and code-resolution pane draw
// through it. The shapes are duplicated here on purpose - the exporter owns the world, the dialog only
// draws. The variants that the exporter picks by the column hash are shown side by side; materials that
// alternate by hash are drawn as their first option.
#include <QWidget>
#include <QPainter>
#include <QCoreApplication>
#include <QVector>
#include <QPair>
#include <algorithm>
#include <cstdlib>

class ModelPreview : public QWidget
{
public:
    explicit ModelPreview(QWidget* parent = nullptr, int minW = 360, int minH = 340) : QWidget(parent)
    {
        setMinimumSize(minW, minH);
    }

    void setBox(const QSize& s) { setMinimumSize(s); setMaximumSize(s); }

    void showModel(int id, const QString& title)
    {
        mId = id;
        mTitle = title;
        build();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        g.fillRect(rect(), palette().color(QPalette::Base));
        g.setPen(palette().color(QPalette::WindowText));
        g.drawText(rect().adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignTop, mTitle);
        if (mBoxes.isEmpty())
        {
            g.setPen(palette().color(QPalette::PlaceholderText));
            g.drawText(rect(), Qt::AlignCenter, mHint);
            return;
        }
        float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
        for (const Box& b : mBoxes)
        {
            u0 = qMin(u0, b.x - b.z - b.d);   u1 = qMax(u1, b.x + b.w - b.z);
            v0 = qMin(v0, (b.x + b.z) * 0.5f - b.y);
            v1 = qMax(v1, (b.x + b.w + b.z + b.d) * 0.5f - (b.y - b.h));
        }
        const qreal pad = 16.0, lh = fontMetrics().height();
        const qreal s = qBound(1.0, qMin((width() - 2 * pad) / (u1 - u0),
                                         (height() - 2 * pad - lh - 12) / (v1 - v0)), 22.0);
        const qreal cu = (u0 + u1) * 0.5, cv = (v0 + v1) * 0.5;
        const QPointF ctr(width() / 2.0, (height() - lh - 4) / 2.0 + 6);
        auto px = [&](float x, float y, float z)
            { return QPointF(ctr.x() + ((x - z) - cu) * 0.866 * s, ctr.y() + ((x + z) * 0.5f - y - cv) * s); };
        for (const Box& b : mBoxes) drawBox(g, b, px);
        QFont f = g.font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        g.setFont(f);
        g.setPen(palette().color(QPalette::PlaceholderText));
        for (const auto& lb : mLabels)                                   // the variant numbers under each model
            g.drawText(QRectF(ctr.x() + (lb.first - cu) * 0.866 * s - 30, ctr.y() + (v1 - cv) * s + 6, 60, lh),
                       Qt::AlignHCenter | Qt::AlignVCenter, lb.second);
    }

private:
    struct Box { float x, y, z, w, h, d; int mat; float key; };   // x/z/y are the TOP corner, the box goes down by h

    enum Mat { MatStone, MatCobb, MatMossy, MatSBrick, MatBricks, MatPDsl, MatCDsl, MatPBlk, MatBlk, MatGran,
               MatDior, MatAnd, MatSmth, MatGold, MatQuartz, MatPCalc, MatGlow, MatIron, MatWater, MatFall,
               MatWallC, MatWallS, MatOak, MatStair, MatComposter, MatLantern, MatCutter, MatPlate };

    static QColor matColor(int m)
    {
        switch (m)
        {
        case MatStone:     return QColor(0x83, 0x83, 0x83);
        case MatCobb:      return QColor(0x77, 0x77, 0x73);
        case MatMossy:     return QColor(0x6c, 0x7a, 0x55);
        case MatSBrick:    return QColor(0x7a, 0x7a, 0x7a);
        case MatBricks:    return QColor(0x9e, 0x5a, 0x4a);
        case MatPDsl:      return QColor(0x53, 0x55, 0x5f);
        case MatCDsl:      return QColor(0x4d, 0x4f, 0x55);
        case MatPBlk:      return QColor(0x44, 0x3f, 0x49);
        case MatBlk:       return QColor(0x33, 0x31, 0x38);
        case MatGran:      return QColor(0x9a, 0x69, 0x59);
        case MatDior:      return QColor(0xc8, 0xc8, 0xcb);
        case MatAnd:       return QColor(0x8c, 0x8c, 0x88);
        case MatSmth:      return QColor(0x88, 0x88, 0x8b);
        case MatGold:      return QColor(0xfb, 0xdf, 0x4f);
        case MatQuartz:    return QColor(0xec, 0xe6, 0xdc);
        case MatPCalc:     return QColor(0xd6, 0xd4, 0xd9);
        case MatGlow:      return QColor(0xf8, 0xc2, 0x51);
        case MatIron:      return QColor(0xd5, 0xd5, 0xd3);
        case MatWater:     return QColor(36, 92, 205, 150);
        case MatFall:      return QColor(110, 150, 230, 110);
        case MatWallC:     return QColor(0x77, 0x77, 0x73);
        case MatWallS:     return QColor(0x7a, 0x7a, 0x7a);
        case MatOak:       return QColor(0xb0, 0x8a, 0x5e);
        case MatStair:     return QColor(0x80, 0x80, 0x80);
        case MatComposter: return QColor(0x5d, 0x5f, 0x3a);
        case MatLantern:   return QColor(0xb0, 0x8c, 0x45);
        case MatCutter:    return QColor(0x74, 0x74, 0x74);
        default:           return QColor(0x9d, 0x9d, 0x9d);
        }
    }

    static QColor shade(const QColor& c, double f)
    {
        return QColor(qMin(255, int(c.red() * f)), qMin(255, int(c.green() * f)),
                      qMin(255, int(c.blue() * f)), c.alpha());
    }

    template <typename Px>
    void drawBox(QPainter& g, const Box& b, Px px) const
    {
        const QColor base = matColor(b.mat);
        const bool transp = (b.mat == MatWater || b.mat == MatFall);
        const bool emis = (b.mat == MatGlow || b.mat == MatLantern);        // light sources are barely shaded
        const QColor pen = transp ? QColor(0, 0, 0, 40) : QColor(30, 30, 30, 90);
        const float x1 = b.x + b.w, z1 = b.z + b.d, y0 = b.y - b.h;
        auto face = [&](const QPointF& a, const QPointF& p, const QPointF& q, const QPointF& r, const QColor& col)
        {
            g.setBrush(col);
            g.setPen(QPen(pen, 0.7));
            g.drawPolygon(QPolygonF() << a << p << q << r);
        };
        face(px(b.x, b.y, b.z), px(x1, b.y, b.z), px(x1, b.y, z1), px(b.x, b.y, z1),
             emis ? base : shade(base, 1.14));                                    // the top
        face(px(b.x, y0, z1), px(x1, y0, z1), px(x1, b.y, z1), px(b.x, b.y, z1),
             emis ? shade(base, 0.88) : shade(base, 0.70));                       // the +z face, the left
        face(px(x1, y0, b.z), px(x1, y0, z1), px(x1, b.y, z1), px(x1, b.y, b.z),
             emis ? shade(base, 0.94) : shade(base, 0.84));                       // the +x face, the right
    }

    void build()
    {
        mBoxes.clear();
        mLabels.clear();
        mHint = mId >= 1 && mId <= 9
            ? QCoreApplication::translate("RulesDialog", "No preview for model number %1").arg(mId)
            : QCoreApplication::translate("RulesDialog", "Select a row - the model that the exporter builds on such a point shows here");
        if (mId < 1 || mId > 9) return;

        float next = 0.0f;                                     // the u-coordinate the next variant starts at
        auto commit = [&](const QVector<Box>& v, const QString& label)
        {
            float u0 = 1e9f, u1 = -1e9f;
            for (const Box& b : v) { u0 = qMin(u0, b.x - b.z - b.d); u1 = qMax(u1, b.x + b.w - b.z); }
            const float sh = (next - u0) * 0.5f;               // +x and -z by the same amount: the row stays at one height
            for (Box b : v) { b.x += sh; b.z -= sh; mBoxes << b; }
            mLabels << qMakePair((u0 + u1) * 0.5f + 2 * sh, label);
            next = u1 + 2 * sh + 1.6f;
        };
        QVector<Box> v;
        auto add = [&v](float x, float yTop, float z, float w, float h, float d, int m)
            { Box b; b.x = x; b.y = yTop; b.z = z; b.w = w; b.h = h; b.d = d; b.mat = m; b.key = 0; v << b; };
        auto stair = [&add](float x, float k, float z, int m)   // the exporter's stairs: a slab plus a back half
            { add(x, k + 0.5f, z, 1, 0.5f, 1, m); add(x, k + 1.0f, z + 0.5f, 1, 0.5f, 0.5f, m); };
        auto wall = [&add](float x, float k, float z, int m, int segments)
        {
            for (int i = 0; i < segments; ++i) add(x + 0.25f, k + i + 0.81f, z + 0.25f, 0.5f, 0.81f, 0.5f, m);
        };
        auto lamp = [&add](float x, float k, float z, bool hang)
            { add(x + 0.3f, hang ? k + 0.95f : k + 0.5f, z + 0.3f, 0.4f, 0.5f, 0.4f, MatLantern); };

        switch (mId)
        {
        case 1:                                                 // memorial, the six variants of kStatue
        {
            v.clear();
            add(0, 1, 0, 1, 1, 1, MatMossy); add(1, 1, 0, 1, 1, 1, MatCobb);
            add(0, 1, 1, 1, 1, 1, MatCobb); add(1, 1, 1, 1, 1, 1, MatMossy);
            add(0, 4, 0, 1, 3, 1, MatPDsl); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a brick stele with a plaque
            add(0, 2, 0, 2, 2, 2, MatBricks); add(0, 4, 0, 1, 2, 1, MatSBrick);
            add(0.05f, 4.125f, 0.05f, 0.9f, 0.125f, 0.9f, MatPlate);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // an eternal flame with corner fence posts
            add(0, 2, 0, 1, 2, 1, MatPBlk); lamp(0, 2, 0, false);
            wall(-1, 0, -1, MatWallC, 1); wall(1, 0, -1, MatWallC, 1);
            wall(-1, 0, 1, MatWallC, 1); wall(1, 0, 1, MatWallC, 1);
            commit(v, QStringLiteral("3"));
            v.clear();                                         // a megalith of mossy boulders
            add(0, 1, 0, 1, 1, 1, MatMossy); add(1, 1, 0, 1, 1, 1, MatCobb);
            add(0, 1, 1, 1, 1, 1, MatAnd); add(1, 1, 1, 1, 1, 1, MatMossy);
            add(1, 2, 0, 1, 1, 1, MatCobb); add(0, 2, 1, 1, 1, 1, MatAnd);
            commit(v, QStringLiteral("4"));
            v.clear();                                         // a broken column on a platform
            add(-1, 1, -1, 3, 1, 3, MatCobb);
            add(-1, 2, -1, 1, 1, 1, MatSmth); add(1, 2, -1, 1, 1, 1, MatSmth);
            add(-1, 2, 1, 1, 1, 1, MatSmth); add(1, 2, 1, 1, 1, 1, MatSmth);
            add(0, 4, 0, 1, 3, 1, MatSBrick); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("5"));
            v.clear();                                         // a shattered deepslate stele
            add(0, 1, 0, 2, 1, 2, MatCDsl); add(0, 3, 0, 1, 2, 1, MatBlk);
            add(0, 4, 0, 1, 1, 1, MatCDsl); stair(0, 4, 0, MatStair);
            commit(v, QStringLiteral("6"));
            break;
        }
        case 2:                                                 // monument, the three variants of kStele
        {
            v.clear();
            add(-1, 1, -1, 3, 1, 3, MatStone); add(0, 2, 0, 2, 1, 2, MatPDsl);
            add(0, 7, 0, 1, 5, 1, MatPBlk); add(0, 8, 0, 1, 1, 1, MatGold);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a brick stele with an eternal flame
            add(-1, 1, -1, 3, 1, 3, MatGran); add(0, 5, 0, 2, 4, 2, MatBricks);
            add(0, 6, 0, 2, 1, 2, MatSBrick); lamp(0, 6, 0, false);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // a column with side walls on a diorite base
            add(0, 1, 0, 2, 1, 2, MatDior); add(0, 7, 0, 1, 6, 1, MatSBrick);
            wall(-1, 0, 0, MatWallS, 2); wall(2, 0, 0, MatWallS, 2);
            stair(0, 7, 0, MatStair);
            commit(v, QStringLiteral("3"));
            break;
        }
        case 3:                                                 // fountain
        {
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) == 2) add(float(dx), 2, float(dz), 1, 2, 1, MatSBrick);
                    else if (dx || dz) { add(float(dx), 1, float(dz), 1, 1, 1, MatStone); add(float(dx), 2, float(dz), 1, 1, 1, MatWater); }
                }
            add(0, 1, 0, 1, 1, 1, MatStone); add(0, 3, 0, 1, 2, 1, MatSBrick);
            add(0, 4, 0, 1, 1, 1, MatWater);                   // the water source on the post
            add(1, 3, 0, 1, 1, 1, MatFall); add(-1, 3, 0, 1, 1, 1, MatFall);
            add(0, 3, 1, 1, 1, 1, MatFall); add(0, 3, -1, 1, 1, 1, MatFall);
            add(1, 4, 0, 1, 1, 1, MatFall); add(-1, 4, 0, 1, 1, 1, MatFall);
            add(0, 4, 1, 1, 1, 1, MatFall); add(0, 4, -1, 1, 1, 1, MatFall);
            commit(v, QString());
            break;
        }
        case 4:                                                 // communication tower, a 32-block body
            add(0, 1, 0, 1, 1, 1, MatIron);
            for (int k = 1; k <= 28; ++k) wall(0, float(k), 0, MatWallC, 1);
            add(0, 32, 0, 1, 3, 1, MatQuartz);
            commit(v, QString());
            break;
        case 5:                                                 // composter
            add(0, 1, 0, 1, 1, 1, MatComposter);
            commit(v, QString());
            break;
        case 6:                                                 // lighting mast
            add(0, 3, 0, 1, 3, 1, MatPCalc); add(0, 4, 0, 1, 1, 1, MatGlow);
            add(0, 6, 0, 1, 2, 1, MatPCalc); add(0, 7, 0, 1, 1, 1, MatGlow);
            commit(v, QString());
            break;
        case 7:                                                 // street bench: two oak stairs along one axis
            stair(0, 0, 0, MatOak); stair(1, 0, 0, MatOak);
            commit(v, QString());
            break;
        case 8:                                                 // small sculpture, the three variants
        {
            v.clear();
            add(0, 1, 0, 1, 1, 1, MatDior); add(0, 2, 0, 1, 1, 1, MatPDsl);
            add(0, 3, 0, 1, 1, 1, MatBlk); add(0, 4, 0, 1, 1, 1, MatPBlk);
            commit(v, QStringLiteral("1"));
            v.clear();                                         // a bust on a stone-brick column
            add(0, 3, 0, 1, 3, 1, MatSBrick); add(0, 4, 0, 1, 1, 1, MatAnd);
            commit(v, QStringLiteral("2"));
            v.clear();                                         // a sculptor's stand with a stone cutter
            add(0, 1, 0, 2, 1, 2, MatCobb);
            add(0, 2, 0, 1, 1, 1, MatAnd); add(1, 2, 1, 1, 1, 1, MatAnd);
            add(0, 2, 1, 1, 1, 1, MatCutter);
            commit(v, QStringLiteral("3"));
            break;
        }
        case 9:                                                 // bus stop shelter, the road side toward the viewer
        {
            add(-2, 1, -1, 5, 1, 3, MatSmth);                  // the floor
            for (int dx = -2; dx <= 2; ++dx)                   // the walls, the dz==+1 side stays open
                for (int dz = -1; dz <= 0; ++dz)
                    if (std::abs(dx) == 2 || dz == -1) add(float(dx), 4, float(dz), 1, 3, 1, MatSBrick);
            add(-2, 5, -1, 5, 1, 3, MatSBrick);                // the flat roof
            lamp(-2, 3, -1, true);                             // the lamp under the roof
            commit(v, QString());
            break;
        }
        default:
            return;
        }

        for (Box& b : mBoxes) b.key = b.x + b.w * 0.5f + b.z + b.d * 0.5f;
        std::stable_sort(mBoxes.begin(), mBoxes.end(), [](const Box& a, const Box& b)   // back to front
                         { return a.key != b.key ? a.key < b.key : a.y < b.y; });
        mHint.clear();
    }

    int mId = 0;
    QString mTitle, mHint;
    QVector<Box> mBoxes;
    QVector<QPair<float, QString>> mLabels;
};
