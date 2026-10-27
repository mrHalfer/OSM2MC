// Smoke test of McWorldExporter: a closed building (hollow walls), a brick building from the "Material"
// column, a road strip (carpet), a railway strip (rail), a forest polygon with a REL height
// (trees), a grass polygon without height and two csrpol polygons: without height (wool instead of turf)
// and with height (a brick structure). The zip contents are checked by verify_mc_export.py.
#include "mcworldexporter.h"
#include <QCoreApplication>
#include <cmath>
#include <QDebug>

namespace {

// A closed parallelepiped (floor/walls/lid).
McWorldExporter::McObject makeBox(double x, double y, double w, double d, double h,
                                    const QColor& c, const QString& material = QString())
{
    McWorldExporter::McObject o;
    o.vertices = {
        { (float)x,        (float)y,        100.0f },
        { (float)(x + w),  (float)y,        100.0f },
        { (float)(x + w),  (float)(y + d),  100.0f },
        { (float)x,        (float)(y + d),  100.0f },
        { (float)x,        (float)y,        (float)(100.0 + h) },
        { (float)(x + w),  (float)y,        (float)(100.0 + h) },
        { (float)(x + w),  (float)(y + d),  (float)(100.0 + h) },
        { (float)x,        (float)(y + d),  (float)(100.0 + h) },
    };
    o.indices = { 0,1,2, 0,2,3, 4,6,5, 4,7,6, 0,4,5, 0,5,1, 1,5,6, 1,6,2, 2,6,7, 2,7,3, 3,7,4, 3,4,0 };
    o.color = c;
    o.closed = true;
    o.kind = McWorldExporter::McObject::Building;
    o.material = material;
    o.extrudeH = h;                                     // the honest height in meters (as in the export window)
    return o;
}

// A flat horizontal strip/lid at the height z (2 triangles).
McWorldExporter::McObject makeQuad(double x, double y, double w, double d, double z,
                                     const QColor& c)
{
    McWorldExporter::McObject o;
    o.vertices = {
        { (float)x,        (float)y,        (float)z },
        { (float)(x + w),  (float)y,        (float)z },
        { (float)(x + w),  (float)(y + d),  (float)z },
        { (float)x,        (float)(y + d),  (float)z },
    };
    o.indices = { 0,1,2, 0,2,3 };
    o.color = c;
    return o;
}

// An L-shaped (in plan) closed building: an L-polygon 12x11 with a cut-out, extrusion h. A regression for the
// "armor": the cornice rectangle of the roof by the BBOX of such a house covers the L cut-out, and stairs
// hanging over the void raise the earth fill to the cornice with their colMin (armor columns).
McWorldExporter::McObject makeGBox(double x, double y, double h, const QColor& c)
{
    const double px[6] = { 0, 12, 12, 7, 7, 0 };
    const double py[6] = { 0,  0,  6, 6, 11, 11 };
    McWorldExporter::McObject o;
    for (int i = 0; i < 6; ++i)
    {
        o.vertices << QVector3D(float(x + px[i]), float(y + py[i]), 100.0f);
        o.vertices << QVector3D(float(x + px[i]), float(y + py[i]), float(100.0 + h));
    }
    QVector<uint> idx;                                         // lids: ear-clipping (the L is concave!) — once,
    {                                                          // the bottom — as is, the top — the same triangulation reversed
        QVector<int> rem { 0, 1, 2, 3, 4, 5 };
        QVector<int> ears;
        while (rem.size() > 3)
        {
            bool cut = false;
            for (int i = 0; i < rem.size() && !cut; ++i)
            {
                const int a = rem[(i + rem.size() - 1) % rem.size()], b = rem[i], c = rem[(i + 1) % rem.size()];
                const double cr = (px[b] - px[a]) * (py[c] - py[a]) - (py[b] - py[a]) * (px[c] - px[a]);
                if (cr <= 1e-9) continue;                      // not convex (counter-clockwise traversal)
                bool inside = false;
                for (int r : rem)
                {
                    if (r == a || r == b || r == c) continue;
                    const double d1 = (px[b] - px[a]) * (py[r] - py[a]) - (py[b] - py[a]) * (px[r] - px[a]);
                    const double d2 = (px[c] - px[b]) * (py[r] - py[b]) - (py[c] - py[b]) * (px[r] - px[b]);
                    const double d3 = (px[a] - px[c]) * (py[r] - py[c]) - (py[a] - py[c]) * (px[r] - px[c]);
                    if (d1 >= -1e-9 && d2 >= -1e-9 && d3 >= -1e-9) { inside = true; break; }
                }
                if (inside) continue;
                ears << a << b << c;
                rem.removeAt(i);
                cut = true;
            }
            if (!cut) break;
        }
        if (rem.size() == 3) ears << rem[0] << rem[1] << rem[2];
        for (int i = 0; i + 2 < ears.size(); i += 3)
        {
            const uint a0 = uint(2 * ears[i]), b0 = uint(2 * ears[i + 1]), c0 = uint(2 * ears[i + 2]);
            idx << a0 << b0 << c0;                             // the bottom
            idx << a0 + 1 << c0 + 1 << b0 + 1;                 // the top — reverse winding
        }
    }
    for (int i = 0; i < 6; ++i)                                // walls (bottom 2i, top 2i+1)
    {
        const uint b0 = uint(2 * i), b1 = uint(2 * ((i + 1) % 6)), t0 = b0 + 1, t1 = b1 + 1;
        idx << b0 << t0 << t1 << b0 << t1 << b1;
    }
    o.indices = idx;
    o.color = c;
    o.closed = true;
    o.kind = McWorldExporter::McObject::Building;
    o.extrudeH = h;
    return o;
}

} // namespace

int main(int argc, char** argv)
{
    fprintf(stderr, "[dbg] start\n");
    QCoreApplication app(argc, argv);
    fprintf(stderr, "[dbg] app ok\n");

    const double x0 = 500000.0, y0 = 2000000.0;
    QVector<McWorldExporter::McObject> objs;

    // A building 10x5x10 (gray-red) — concrete by color, hollow inside. CODE 44400002 — a gable
    // roof of stairs (an even width of 10: the ridge — of two opposite stairs).
    McWorldExporter::McObject cube = makeBox(x0, y0, 10, 10, 5, QColor(180, 40, 40));
    cube.code = 44400002;
    cube.streetType = QStringLiteral("ул."); cube.street = QStringLiteral("Тестовая"); cube.number = QStringLiteral("12А");
    objs << cube;

    // A building with the "Material" column filled in = "brick" (brick is more important than the blue color).
    objs << makeBox(x0 + 20, y0, 8, 8, 4, QColor(40, 40, 200), QStringLiteral("Brick"));

    // A building 13x9 (CODE 44400100): the ridge along the LONG side (X), the slopes rise along Z,
    // an odd width of 9 — the middle ridge line of stairs along the crest.
    McWorldExporter::McObject gable = makeBox(x0 + 20, y0 - 25, 13, 9, 6, QColor(120, 60, 40));
    gable.code = 44400100;
    objs << gable;

    // A big private house 27x27x7 (CODE 44400100, orange concrete): a cavity of ~25x25 > 300 —
    // the partition cutting works. The verify gate "private sector partitions": a partition
    // stands ONLY on the 1st floor (up to the slab), the 2nd floor (the mezzanine) — without partitions (a request).
    // Fits into the scene bbox (world x ~240..266, z ~39..66) — tf does not shift.
    McWorldExporter::McObject bigPriv = makeBox(x0 + 160, y0 - 56, 27, 27, 7, QColor(255, 140, 0));
    bigPriv.code = 44400100;
    objs << bigPriv;

    // A private house of 5.01 m: by geometry the floor slab does NOT fit (5 rows instead of 6) -> walls of logs, the ceiling of planks.
    McWorldExporter::McObject logHouse = makeBox(x0 - 60, y0 - 150, 8, 8, 5.01, QColor(150, 110, 70));
    logHouse.code = 44400100;
    objs << logHouse;
    // Religious/school/kindergarten: a church 44319000 (a priest), a school 44312000 and a kindergarten 44313000 (a scientist, children).
    // All three are inside the previous scene bbox (north-west), floors >= 2 -> the interior pass works.
    {
        McWorldExporter::McObject church = makeBox(x0 - 80, y0 - 195, 14, 18, 12, QColor(200, 190, 180));
        church.code = 44319000;
        objs << church;
        // a bell tower 8x8x20 right next to the nave (from the south): the complex is glued together, the wall between them is removed
        McWorldExporter::McObject belfry = makeBox(x0 - 78, y0 - 177, 8, 8, 20, QColor(200, 190, 180));
        belfry.code = 44319000;
        objs << belfry;
        // a second, smaller church far from the first (>150 blocks): the first gets a shaft to the stronghold, this one — to the Ancient City
        McWorldExporter::McObject church2 = makeBox(x0 + 100, y0 - 120, 12, 16, 11, QColor(190, 185, 175));
        church2.code = 44319000;
        objs << church2;
        McWorldExporter::McObject school = makeBox(x0 - 55, y0 - 195, 24, 26, 10, QColor(230, 90, 90));
        school.code = 44312000;
        objs << school;
        McWorldExporter::McObject kinder = makeBox(x0 - 25, y0 - 195, 22, 16, 9, QColor(250, 240, 140));
        kinder.code = 44313000;
        objs << kinder;
        // an annex to the kindergarten (right next to it from the east): the complex is glued together, the wall between the parts is removed; above the kindergarten — a floating island
        McWorldExporter::McObject kinder2 = makeBox(x0 - 3, y0 - 195, 10, 12, 7, QColor(250, 240, 140));
        kinder2.code = 44313000;
        objs << kinder2;
        // hospital 44314000: beds, doctors (cleric) and patients on beds
        McWorldExporter::McObject hosp = makeBox(x0 + 135, y0 - 185, 22, 20, 10, QColor(240, 150, 185));
        hosp.code = 44314000;
        objs << hosp;
        // science building 44315000: like a school (without children) + an underground laboratory with redstone mechanisms; a spare entrance to the Ancient City
        McWorldExporter::McObject sci = makeBox(x0 + 190, y0 - 185, 24, 22, 10, QColor(150, 190, 230));
        sci.code = 44315000;
        objs << sci;
    }

    // A road: a gray strip 2 m wide along X.
    McWorldExporter::McObject road = makeQuad(x0 - 20, y0 + 30, 60, 2, 100.0, QColor(120, 120, 120));
    road.kind = McWorldExporter::McObject::Road;
    road.streetType = QStringLiteral("ул."); road.street = QStringLiteral("Первая");
    road.paths << (QVector<QPointF>() << QPointF(x0 - 20, y0 + 31) << QPointF(x0 + 40, y0 + 31));   // the axis
    objs << road;

    // A complex of non-residential parts (code 44311000, purple concrete): the main building 24x24x14 and a low annex 20x12x3 from the east — the annex room must be no lower than 3 blocks.
    {
        McWorldExporter::McObject big = makeBox(x0 - 250, y0 + 150, 24, 24, 14, QColor(255, 160, 200));
        big.code = 44311000;
        objs << big;
        McWorldExporter::McObject wing = makeBox(x0 - 226, y0 + 156, 20, 12, 3, QColor(255, 160, 200));
        wing.code = 44311000;
        objs << wing;
    }

    // A house right next to a road (dynamic width: the roadway widening on this section is cut, the strip under the house is not drawn).
    {
        McWorldExporter::McObject nearRoad = makeBox(x0 + 5, y0 + 33.2, 10, 6, 6.0, QColor(210, 180, 150));
        nearRoad.code = 44000000;   // a shed: without a basement and interiors, so as not to change the other gates
        objs << nearRoad;
    }

    // A railway: a strip parallel to the road.
    McWorldExporter::McObject rail = makeQuad(x0 - 20, y0 + 40, 60, 2, 100.0, QColor(90, 70, 60));
    rail.kind = McWorldExporter::McObject::Railway;
    rail.code = 61111000;                              // double-track — only this code
    objs << rail;

    // A single-track L-shaped railway (CODE 61122000, not 61111000): the track axis is a polyline to the east, then to the north.
    // The export lays ONE track of rails with shapes (straight + a north_west turn) and connected cells.
    McWorldExporter::McObject railL = makeQuad(x0 - 20, y0 + 140, 30, 2, 100.0, QColor(90, 70, 60));
    railL.kind = McWorldExporter::McObject::Railway;
    railL.code = 61122000;
    railL.paths << (QVector<QPointF>() << QPointF(x0 - 19.5, y0 + 140.5) << QPointF(x0 + 10.5, y0 + 140.5)
                                       << QPointF(x0 + 10.5, y0 + 160.5));
    objs << railL;

    // A forest: a 20x25 polygon with a REL height of 6 m -> trees.
    McWorldExporter::McObject forest = makeQuad(x0 + 15, y0 - 30, 20, 25, 100.0, QColor(30, 110, 40));
    forest.kind = McWorldExporter::McObject::Vegetation;
    forest.extrudeH = 6.0;
    objs << forest;

    // A lawn: a polygon without height -> grass.
    McWorldExporter::McObject lawn = makeQuad(x0 - 40, y0 - 20, 25, 20, 100.0, QColor(60, 150, 60));
    lawn.kind = McWorldExporter::McObject::Vegetation;
    lawn.extrudeH = 0.0;
    objs << lawn;

    // csrpol without height: a flat polygon -> wool of a random color instead of turf.
    McWorldExporter::McObject pad = makeQuad(x0 - 30, y0 - 70, 12, 10, 100.0, QColor(246, 255, 169));
    pad.kind = McWorldExporter::McObject::Plate;
    objs << pad;

    // csrpol with height: a closed body -> a brick structure (brick is more important than color), hollow, with a foundation.
    McWorldExporter::McObject padHouse = makeBox(x0 + 40, y0 - 30, 8, 8, 4, QColor(120, 80, 55));        // a color that maps to brown concrete: the export must give bricks instead
    padHouse.kind = McWorldExporter::McObject::Plate;
    objs << padHouse;

    // csrpol 51480000 — a lake: water (depth by area) + a fence around, like for sites; there is no golem on the lake.
    McWorldExporter::McObject csrLake = makeQuad(x0 - 200, y0 - 100, 14, 10, 100.0, QColor(74, 134, 232));
    csrLake.kind = McWorldExporter::McObject::Plate;
    csrLake.code = 51480000;
    objs << csrLake;

    // A field of csrpol sites "antenna nodes" — for a 10% chance of a Nether portal (of which 10%
    // active) one or two objects are not enough, hundreds are needed. 6×6, a step of 8: the fence rings
    // close together, do not merge. The field in the empty grass north of the wheat (x0-240..-70,
    // y0+110..+220): beyond the verify gates for the meadow/wheat/city, in the current bbox.
    for (int gy = 0; gy < 14; ++gy)
        for (int gx = 0; gx < 22; ++gx)
        {
            McWorldExporter::McObject p = makeQuad(x0 - 240 + 8 * gx, y0 + 110 + 8 * gy,
                                                     6, 6, 100.0, QColor(246, 255, 169));
            p.kind = McWorldExporter::McObject::Plate;
            objs << p;
        }

    // Forests for Nether portals (vegpol, CODE 71111110/71126000, an area >= 5000 real m², no
    // more than one portal within a radius of 1000 blocks). Forest A 100x80 (8000) and forest B 90x70 (6300) —
    // are suitable, but only the big one (A) gets a portal; forest C 40x40 (1600) and forest D with a foreign code —
    // are too small. All three are inside the previous scene bbox (x0-600..x0+440, y0-200..y0+220) — tf does not shift.
    {
        struct F { double x, y, w, d; int code; };
        const F forestsP[] = { { 300, -200, 100, 80, 71111110 },   // A: 8000 m² — gets a portal
                               { 330,  130,  90, 70, 71111110 },   // B: 6300 m² — suitable, but there is already a portal nearby; a forest of this code and this area — spruces (taiga)
                               { 300, -118,  60, 20, 71111110 } }; // C: 1200 m² — too small
        for (const F& f : forestsP)
        {
            McWorldExporter::McObject fo = makeQuad(x0 + f.x, y0 + f.y, f.w, f.d, 100.0, QColor(30, 110, 40));
            fo.kind = McWorldExporter::McObject::Vegetation;
            fo.extrudeH = 6.0;
            fo.code = f.code;
            objs << fo;
        }
    }

    // The biggest forest (CODE 71123000, 260×110 = 28600 m²) — becomes the Pale Garden (the donor template 128×128 is repeated mirrored, we keep one mansion).
    {
        McWorldExporter::McObject big = makeQuad(x0 - 590, y0, 260, 110, 100.0, QColor(30, 110, 40));
        big.kind = McWorldExporter::McObject::Vegetation;
        big.extrudeH = 6.0;
        big.code = 71123000;
        objs << big;
    }

    // A big taiga (71111110, 160×140 = 22400 m² >= 9 outposts of 48×48): bandit outposts and abandoned camps.
    {
        McWorldExporter::McObject tg = makeQuad(x0 - 320, y0 - 200, 160, 140, 100.0, QColor(30, 110, 40));
        tg.kind = McWorldExporter::McObject::Vegetation;
        tg.extrudeH = 6.0;
        tg.code = 71111110;
        if (!qEnvironmentVariableIsSet("MCX_NO_TG")) objs << tg;
    }

    // A park 71126000 (a lawn 80×80 = 6400 m²): above it — a floating island (a lake, slime, tropics, slimes); not above every park and not more often than once per 450 blocks.
    {
        McWorldExporter::McObject park = makeQuad(x0 - 540, y0 - 100, 80, 80, 100.0, QColor(90, 170, 60));
        park.kind = McWorldExporter::McObject::Vegetation;
        park.extrudeH = 0.0;
        park.code = 71126000;
        objs << park;
    }

    // A tall building 12x12x14 m (bldpol) — a "floors" check: slabs every 5-6 blocks and
    // strip windows of glass in the facades (every 4th column). LIFT=1 (a user request:
    // "WALLS 1 block thick instead of 2"): the wall lining is not written to the store, the floor/foundation
    // and the ceiling remain — the verify gate "thin walls (LIFT)" relies on it.
    McWorldExporter::McObject liftHouse = makeBox(x0 + 70, y0 - 10, 12, 12, 14, QColor(90, 140, 90));
    liftHouse.lift = true;
    objs << liftHouse;

    // A shaft on the host's roof: a building 10x10x6 (light blue) and an elevator shaft 4x4x14 (purple, LIFT=1) entirely within its spot.
    // The shaft must be built ONLY above the host's roof (below/inside — not a single purple block).
    objs << makeBox(x0 + 110, y0 - 30, 10, 10, 6, QColor(120, 190, 240));
    McWorldExporter::McObject roofLift = makeBox(x0 + 113, y0 - 27, 4, 4, 14, QColor(200, 40, 200));
    roofLift.lift = true;
    objs << roofLift;

    // A production building 44340000 (a candidate for a bandit lair: 30%; with MCX_FORCE_LAIR=1 — always).
    // A complex of three touching parts of DIFFERENT height (12, 8 and 16 m) — is glued into one building: the walls between the parts are removed,
    // the floors are common. The color is cyan (cyan_concrete) — verify finds the complex by it.
    {
        McWorldExporter::McObject fa = makeBox(x0 + 150, y0 + 130, 20, 16, 12, QColor(40, 150, 160));
        fa.code = 44340000;
        objs << fa;
        McWorldExporter::McObject fb = makeBox(x0 + 170, y0 + 130, 14, 16, 8, QColor(40, 150, 160));
        fb.code = 44340000;
        objs << fb;
        McWorldExporter::McObject fc = makeBox(x0 + 150, y0 + 146, 20, 10, 16, QColor(40, 150, 160));
        fc.code = 44340000;
        objs << fc;
    }

    // A parking lot (rdcpol) 24x14: dark gray concrete instead of turf + light gray carpets of place marking (perpendicular to the edges, a step of 7, a length up to 12).
    McWorldExporter::McObject parking = makeQuad(x0 + 90, y0 - 110, 24, 14, 100.0, QColor(94, 102, 111));
    parking.kind = McWorldExporter::McObject::ParkingLot;
    parking.paths << (QVector<QPointF>() << QPointF(x0 + 90, y0 - 110) << QPointF(x0 + 114, y0 - 110)
                                         << QPointF(x0 + 114, y0 - 96) << QPointF(x0 + 90, y0 - 96) << QPointF(x0 + 90, y0 - 110));
    objs << parking;

    // blkpol: asphalt without marking (62231000, 30×14) and a square (45300000, 26×22 = 572 columns) is laid with stone bricks; the other codes of the layer do not go into the export.
    {
        McWorldExporter::McObject asphalt = makeQuad(x0 - 120, y0 - 150, 30, 14, 100.0, QColor(94, 102, 111));
        asphalt.kind = McWorldExporter::McObject::BlockCover;
        asphalt.code = 62231000;
        objs << asphalt;
        McWorldExporter::McObject square = makeQuad(x0 - 120, y0 - 120, 26, 22, 100.0, QColor(94, 102, 111));
        square.kind = McWorldExporter::McObject::BlockCover;
        square.code = 45300000;
        objs << square;
    }

    // A two-storey 10x10x12 m (12 blocks — the minimum for 2 floors: shaftH = h-3 >= 9) — a cavity
    // of ~7x7: a stepped flight (storyP+1)x1 fits into it — here the staircase is of stairs.
    // (for the high-rise the spot is bigger, there the staircase is trivial).
    objs << makeBox(x0 + 40, y0 + 45, 10, 10, 12, QColor(150, 130, 90));

    // A "narrow" two-storey 6x6x12: a cavity of ~4x4, the stairs have no room — by the
    // user's rule ("did not fit — make a vertical one") a
    // VERTICAL ladder (minecraft:ladder) along the wall must grow here. world x ~134..140, z ~-95..-84.
    objs << makeBox(x0 + 55, y0 + 45, 6, 6, 12, QColor(120, 90, 150));

    // Hydrography: a pond 10x6 (area 60 -> depth round(60/6)=10) and a linear river strip 40x2
    // (80 -> three times shallower, round(80/18)=4).
    McWorldExporter::McObject pond = makeQuad(x0 + 40, y0 + 30, 10, 6, 100.0, QColor(74, 134, 232));
    pond.kind = McWorldExporter::McObject::Water;
    objs << pond;

    McWorldExporter::McObject river = makeQuad(x0 - 50, y0 + 60, 40, 2, 100.0, QColor(74, 134, 232));
    river.kind = McWorldExporter::McObject::Water;
    river.linear = true;
    objs << river;

    // A natural lake (a hexagon with uneven corners, a fan of triangles): a smooth bottom slope, sand and clay on the bottom, sandy shores.
    {
        const double px[6] = { 0, 21, 33, 27, 9, -3 }, py[6] = { 0, -3, 9, 24, 27, 15 };
        McWorldExporter::McObject lake;
        const double cx0 = x0 + 120, cy0 = y0 + 150;
        lake.vertices.append(QVector3D((float)(cx0 + 15), (float)(cy0 + 12), 100.0f));
        for (int i = 0; i < 6; ++i) lake.vertices.append(QVector3D((float)(cx0 + px[i]), (float)(cy0 + py[i]), 100.0f));
        for (int i = 0; i < 6; ++i) { lake.indices << 0 << (i + 1) << ((i + 1) % 6 + 1); }
        lake.color = QColor(74, 134, 232);
        lake.kind = McWorldExporter::McObject::Water;
        objs << lake;
    }

    // A meadow 240x220 (area 52800 > 50000) -> grown grass (tall_grass, 2 blocks).
    McWorldExporter::McObject meadow = makeQuad(x0 + 200, y0 - 100, 240, 220, 100.0, QColor(90, 170, 60));
    meadow.kind = McWorldExporter::McObject::Vegetation;
    meadow.extrudeH = 0.0;
    objs << meadow;

    // A wheat field 350x300 (105000 > 100000) -> ripe wheat (wheat age=7), dense planting.
    McWorldExporter::McObject wheatField = makeQuad(x0 - 600, y0 - 200, 350, 300, 100.0, QColor(200, 180, 60));
    wheatField.kind = McWorldExporter::McObject::Vegetation;
    wheatField.extrudeH = 0.0;
    objs << wheatField;

    // A shopping center 30x20x12 with CODE=44318000 — "panoramic windows": glass in bands of >=5 blocks
    // in a row (an impost every 6..9 columns). Checked by verify_mc_export.py (glass run statistics).
    McWorldExporter::McObject shopCenter = makeBox(x0 + 200, y0 + 200, 30, 20, 12.0, QColor(150, 155, 160));
    shopCenter.code = 44318000;
    objs << shopCenter;

    // A big shopping center 40x40 (plan 1600 m² >= 1200): a double-leaf door + stone pressure plates in front of the leaves.
    // Inside the previous scene bbox (x0+235..275, y0+140..180).
    McWorldExporter::McObject bigShop = makeBox(x0 + 235, y0 + 140, 40, 40, 10.0, QColor(150, 155, 160));
    bigShop.code = 44318000;
    objs << bigShop;

    // A shed/utility building CODE=44000000 — without windows at all (a user request), growth only +2.
    McWorldExporter::McObject barn = makeBox(x0 + 70, y0 + 20, 10, 8, 3.0, QColor(140, 130, 110));
    barn.code = 44000000;
    objs << barn;

    // A pedestrian road 45260000 CROSSING the highway and standing in the list EARLIER than it —
    // a priority regression test: in the old order (the first writer wins) it ate the highway cells
    // together with the marking. Now the dark gray roadway is drawn by the first pass, the pedestrian road —
    // by the last; in the intersection zone (rows dy24/dy27 — body strips without marking) there must be
    // gray_carpet, and the light gray rug — only outside the roadway (dy22..23 and dy29..30).
    McWorldExporter::McObject pedCross = makeQuad(x0 - 5, y0 + 22, 6, 9, 100.0, QColor(160, 160, 160));
    pedCross.kind = McWorldExporter::McObject::Road;
    pedCross.code = 45260000;
    pedCross.streetType = QStringLiteral("пер."); pedCross.street = QStringLiteral("Третий");
    pedCross.paths << (QVector<QPointF>() << QPointF(x0 - 2, y0 + 22) << QPointF(x0 - 2, y0 + 31));   // crosses both axes
    objs << pedCross;
    // An unnamed street crosses the "First" (a check: a signpost from ONE name, nameId -1 must not get into the list).
    McWorldExporter::McObject roadU = makeQuad(x0 + 29, y0 + 24, 2, 12, 100.0, QColor(120, 120, 120));
    roadU.kind = McWorldExporter::McObject::Road;
    roadU.paths << (QVector<QPointF>() << QPointF(x0 + 30, y0 + 24) << QPointF(x0 + 30, y0 + 36));
    objs << roadU;

    // The highway CODE=45210000: widens by 2 blocks on each side, dark gray, with a gray
    // marking line in the middle (a user request: do NOT mark the edges).
    McWorldExporter::McObject carRoad = makeQuad(x0 - 20, y0 + 25, 40, 2, 100.0, QColor(90, 90, 90));
    carRoad.kind = McWorldExporter::McObject::Road;
    carRoad.code = 45210000;
    carRoad.streetType = QStringLiteral("пр."); carRoad.street = QStringLiteral("Вторая");
    carRoad.paths << (QVector<QPointF>() << QPointF(x0 - 20, y0 + 26) << QPointF(x0 + 20, y0 + 26));   // the axis: parallel to the "First", there is NO intersection
    objs << carRoad;

    // A bike path CODE=45261000 — a dark red carpet, without widening.
    McWorldExporter::McObject bikeLane = makeQuad(x0 + 130, y0 + 50, 30, 2, 100.0, QColor(120, 40, 40));
    bikeLane.kind = McWorldExporter::McObject::Road;
    bikeLane.code = 45261000;
    objs << bikeLane;

    // Light gray codes 61310000/61320000/61330000 (a request: "like 45260000"): three strips,
    // the color of the source object is DELIBERATELY dark — the export must repaint it to light_gray
    // regardless of it. The widening ±2 and the priority — as for "the other" roads. A step of 12 —
    // the strips do not stick together after the widening (each has its own box in verify). Fit into the current
    // scene bbox, the tf auto-centering and the coordinates of the other verify checks do not shift.
    for (int i = 0; i < 3; ++i)
    {
        static const int kLgrayCodes[3] = { 61310000, 61320000, 61330000 };
        McWorldExporter::McObject lg = makeQuad(x0 + 130, y0 + 56 + 12 * i, 20, 2,
                                                  100.0, QColor(70, 70, 70));
        lg.kind = McWorldExporter::McObject::Road;
        lg.code = kLgrayCodes[i];
        objs << lg;
    }

    // A residential high-rise 14x14x200 m with CODE 44400000 (square windows 2×2) — the ONLY one above
    // 15 floors (shaftH=197 -> n4=(198)/5=39 floors of 5 blocks): the "monster spawners" test
    // (every 15th floor of the city: 11 floors were counted before it, its own go as the 12th..50th —
    // the counter reaches 15, 30 and 45 INSIDE it) and "aviation beacons" (tiers k=14,19,24,29,34).
    // Fit into the current scene bbox (world x 169..182, z -104..-91; the height 200 < 240 — zScale
    // is not turned on), so the tf auto-centering and the coordinates of all the other verify checks
    // do not shift. In the list — the LAST of the buildings: the floor counter of the other buildings does not change.
    McWorldExporter::McObject tower = makeBox(x0 + 90, y0 + 100, 14, 14, 200.0, QColor(170, 160, 150));
    tower.code = 44400000;
    objs << tower;

    // An L-shaped private sector house (CODE 44400100, a roof of stairs). Stands AFTER the high-rise:
    // the floor counter for spawners does not depend on it, and the scene bbox is not widened (x0-20,y0+80
    // inside the current borders) — the coordinates of all verify checks do not shift. The point: the L-house
    // has a cornice by bbox hanging OVER THE CUT-OUT — earlier these stairs raised the earth fill
    // ("armor", a user remark); now the roof catches only the columns with a real
    // wall under it, and verify catches both hanging stairs and earth right under the stairs.
    McWorldExporter::McObject gableG = makeGBox(x0 - 20, y0 + 80, 5.0, QColor(150, 110, 70));
    gableG.code = 44400100;
    objs << gableG;

    // The "big one" 46x24x24 (CODE 44310000 — non-residential, windows 2×3): a cavity of ~43x21 (~900 cells) —
    // a regression for two user requests:
    // 1) a SECOND inter-floor staircase in the most distant corner of the cavity;
    // 2) room PARTITIONS of 100..300 cells with passages (the building is cut into ~4 rooms).
    // Stands in a free corner (x0-140,y0-180), inside the current scene bbox — the auto-centering of tf
    // does not shift the coordinates of the other checks.
    McWorldExporter::McObject market = makeBox(x0 - 140, y0 - 180, 46, 24, 24.0, QColor(200, 195, 185));
    market.code = 44310000;
    objs << market;

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    fprintf(stderr, "[dbg] objs=%d\n", objs.size());
    // Crash diagnostics: we print the export stage change (0 objects / 1 ground / 2 chunks / 3 zip).
    static int lastStage = -99;
    McWorldExporter::ProgressFn stageLog = [](int done, int total, int stage)
    {
        if (stage != lastStage) { lastStage = stage; fprintf(stderr, "[dbg] stage=%d\n", stage); }
        if (stage == 3 && done == total) fprintf(stderr, "[dbg] stage=3 done\n");
    };
    // Caves (a user request): the underground of ALL four test worlds is filled with templates from
    // the donor world (cave_templates.bin next to the exe) — the verifier looks for cave air below
    // the turf; earlier the underground was solid rock (0 air under y=0).
    McWorldExporter::WorldOptions flat;
    flat.caves = true;
    flat.cavesFile = QStringLiteral("cave_templates.bin");
    const McWorldExporter::Result r = McWorldExporter::Export(
        objs, McWorldExporter::HeightSampler(),
        outDir + QStringLiteral("/mc_test.zip"), QStringLiteral("mc_test"),
        nullptr, McWorldExporter::Format::Native26x, stageLog, flat);

    qInfo() << "native ok" << r.ok << "cancelled" << r.cancelled << "err" << r.error
            << "objects" << r.objectCount << "skipped" << r.skippedCount
            << "blocks" << r.blockCount << "regions" << r.regionCount << "bytes" << r.zipBytes;

    fprintf(stderr, "[dbg] native done ok=%d err=%s\n", r.ok, r.error.toLocal8Bit().constData());

    const McWorldExporter::Result r2 = McWorldExporter::Export(
        objs, McWorldExporter::HeightSampler(),
        outDir + QStringLiteral("/mc_test_legacy.zip"), QStringLiteral("mc_legacy"),
        nullptr, McWorldExporter::Format::Legacy121x, stageLog, flat);

    qInfo() << "legacy ok" << r2.ok << "cancelled" << r2.cancelled << "err" << r2.error
            << "objects" << r2.objectCount << "skipped" << r2.skippedCount
            << "blocks" << r2.blockCount << "regions" << r2.regionCount << "bytes" << r2.zipBytes;

    // The "normal seed-based world": the same content, but the surroundings — a noise generator + a seed (level.dat /
    // world_gen_settings.dat must carry seed=777 and type=minecraft:noise — the verifier will check).
    const McWorldExporter::WorldOptions noise{ qEnvironmentVariableIsSet("MCX_TEST_SEED") ? qEnvironmentVariableIntValue("MCX_TEST_SEED") * 1ull : 777ull, true };   // MCX_TEST_SEED — another cave seed for checks
    McWorldExporter::WorldOptions noiseCaves = noise;   // the same noise worlds, but with caves
    noiseCaves.caves = true;
    noiseCaves.cavesFile = flat.cavesFile;
    noiseCaves.lairChurchChunks = qEnvironmentVariable("MCX_TEST_LAIRCHUNKS");   // "x y z; x y z" — churches by chunks (in the F3 format); empty — automatic
    const McWorldExporter::Result r3 = McWorldExporter::Export(
        objs, McWorldExporter::HeightSampler(),
        outDir + QStringLiteral("/mc_test_noise.zip"), QStringLiteral("mc_noise"),
        nullptr, McWorldExporter::Format::Native26x, stageLog, noiseCaves);

    const McWorldExporter::Result r4 = McWorldExporter::Export(
        objs, McWorldExporter::HeightSampler(),
        outDir + QStringLiteral("/mc_test_legacy_noise.zip"), QStringLiteral("mc_legacy_noise"),
        nullptr, McWorldExporter::Format::Legacy121x, stageLog, noiseCaves);

    // The fifth world: the same objects, but "map units -> meters" with a coefficient of 0.5 (like Mercator, only 2 times):
    // the horizontal sizes are twice smaller, noticeably fewer chunks. Checked by check_scaled.py.
    McWorldExporter::WorldOptions scaled = flat;
    scaled.unitsToMetersX = 0.5;
    scaled.unitsToMetersY = 0.5;
    scaled.hasSpawnPoint = true;                      // the spawn in the center of the "view": the middle of the first cube (x0+5, y0+5)
    scaled.spawnMapX = x0 + 5;
    scaled.spawnMapY = y0 + 5;
    const McWorldExporter::Result r5 = McWorldExporter::Export(
        objs, McWorldExporter::HeightSampler(),
        outDir + QStringLiteral("/mc_test_scaled.zip"), QStringLiteral("mc_scaled"),
        nullptr, McWorldExporter::Format::Native26x, stageLog, scaled);

    // The sixth world: noise + TERRAIN (a wavy surface with valleys and hills). Checked by verify_relief.py: valleys are not filled up to
    // the "plane" (earlier everything below the median was filled up, buildings in valleys ended up in the ground), the spawn — on the surface.
    const McWorldExporter::HeightSampler relief = [=](double wx, double wy) -> double
    {
        return 100.0 + 30.0 * std::sin((wx - x0) / 60.0) + 15.0 * std::cos((wy - y0) / 45.0);
    };
    const McWorldExporter::Result r6 = McWorldExporter::Export(
        objs, relief,
        outDir + QStringLiteral("/mc_test_relief.zip"), QStringLiteral("mc_relief"),
        nullptr, McWorldExporter::Format::Native26x, stageLog, noiseCaves);

    fprintf(stderr, "[lair] %s\n", r3.lairInfo.toUtf8().constData());
    fprintf(stderr, "[result] flat=%d legacy=%d noise=%d legacyNoise=%d scaled=%d relief=%d\n", r.ok, r2.ok, r3.ok, r4.ok, r5.ok, r6.ok);
    fprintf(stderr, "[errors] %s | %s | %s | %s\n", r3.error.toUtf8().constData(), r4.error.toUtf8().constData(), r5.error.toUtf8().constData(), r6.error.toUtf8().constData());
    return (r.ok && r2.ok && r3.ok && r4.ok && r5.ok && r6.ok) ? 0 : 1;
}
