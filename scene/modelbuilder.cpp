#include "scene/modelbuilder.h"
#include "core/heightraster.h"
#include "core/maplayer.h"
#include "core/mapfeature.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <QHash>
#include <QSet>
#include <QPair>
#include <QThread>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <QDateTime>

/*!
 * \file   modelbuilder.cpp
 * \brief  Implementation of the 3D model build: terrain (a heightmap mesh) + extruded polygons.
 *
 * Extruded objects are built as one mesh PER SOURCE OBJECT of a layer (not as one
 * merged mesh per layer) — this is a deliberate compromise in favor of live synchronization with the 2D editor
 * (see the tqt 3D window, onLayerObjAdded()/onLayerObjDeleted(), the subscription to
 * MapLayer::signalObjAdded/signalObjDeleted): adding/deleting one object in 2D must
 * update one Qt3D entity without requiring a rebuild of the whole merged layer buffer.
 */

// A small fixed palette for coloring extruded objects by the index of the source layer.
static const QVector3D kBuildingPalette[] = {
    QVector3D(0.80f, 0.35f, 0.30f),   // brick red
    QVector3D(0.55f, 0.60f, 0.65f),   // gray-blue
    QVector3D(0.45f, 0.65f, 0.35f),   // green (forest)
    QVector3D(0.85f, 0.70f, 0.30f),   // ochre
    QVector3D(0.55f, 0.40f, 0.70f),   // violet
};
static const int kBuildingPaletteSize = sizeof(kBuildingPalette) / sizeof(kBuildingPalette[0]);

//! The minimum visible extrusion for objects with an honest zero in the height field (see
//! CollectLayerCandidates()/BuildSingleObjectMesh()) — such an object is still shown as a thin "slab"
//! rather than vanishing completely, unlike "On the ground" (ModelSettings::VectorSource::noExtrusion),
//! where the absence of walls is a deliberate user choice, not a consequence of the data.
static const double kMinExtrusionHeight = 0.5;

//! The thickness of the outline of faces/side edges (see ModelOutlineMode/AppendRingOutline()) — always small,
//! not configurable by the user (by request). kOutlineLift — the lift above the cap/ribbon on top of
//! kFlatLiftEpsilon so that the strip does not flicker (z-fighting) with the surface it outlines.
static const double kOutlineWidth = 0.4;
static const double kOutlineLift = 0.08;
//! How far the outline color departs from the object color toward white (Light) or toward black (Dark), see
//! ModelBuilder::OutlineColor().
static const float kOutlineLightMix = 0.35f;
static const float kOutlineDarkMix = 0.55f;

//! A small lift above the terrain for geometry that would otherwise lie EXACTLY on its surface —
//! the flat "On the ground" cap (rel<=0, see BuildObjectMesh()) and the ribbon of linear objects (always,
//! see BuildLineRibbonMesh()). Without it two triangles coinciding in height (the terrain mesh and the object
//! mesh) flicker (z-fighting) — which of them is closer to the camera is decided not by the scene depth but by the noise of
//! coordinate rounding. An absolute constant, not multiplied by zExaggeration — the goal is just to lift
//! slightly off the surface, not to "raise" in the artistic sense.
static const double kFlatLiftEpsilon = 0.15;

//! The convergence of the lift at junctions of lines of one vector source (see CollectLayerCandidates()/
//! BuildLineRibbonMesh()) — a typical case: a ramp of a multi-level interchange whose lift
//! (VectorSource::liftField) at the junction with another segment must match it instead of breaking off
//! with a jump. The tolerance (in map units) when matching the end points of different objects as "one and
//! the same" junction — network nodes are usually topologically snapped to each other (they coincide almost exactly),
//! a small tolerance is needed only to survive the rounding error of the coordinates. The length of the smooth
//! transition section itself is not a separate constant but half of the line length from each junction end
//! (see ComputeVertexLift()) — the transition is stretched over the whole run of the connecting object.
static const double kJunctionSnapEpsilon = 0.5;

//! The "Smoothing" of the top edge (see ModelEffect::Rounded/BuildObjectMesh()) — a static chamfer instead of
//! a sharp wall/cap corner: several sub-rings (kRoundedRings) along a quarter-circle profile of radius
//! kRoundedRadius (in section), the same trick of "several rings — not 1-2" as for the "Lamps" (see
//! kGlowRings) — with Gouraud interpolation of normals inside the sub-rings this reads as a smooth rounding
//! rather than a faceted chamfer. At every outline vertex the actual radius (both in XY and in height — common for
//! the whole ring) is clamped by the object height (not more than 40% of rel*zExag — otherwise on low bushes the chamfer
//! would "eat" the whole wall) and in XY by the lengths of the neighboring outline edges (not more than 40% of the smaller of them —
//! otherwise at sharp/short corners the chamfer would "run over" to the opposite side of the outline and
//! turn the geometry inside out). The rounding direction at a vertex is an approximate bisector of the normals of the two
//! adjacent edges (not an exact miter offset) — also a cheap approximation, like the rest of the set of
//! effects in this file.
static const double kRoundedRadius = 1.2;
static const int kRoundedRings = 3;

// --- support for the "effects" decorating objects (see ModelEffect) — all of them deliberately cheap imitations,
// without extra materials/shaders/entities, at the direct request of the user.

//! The color and alpha of polygon shadows (see AppendShadowBlob()) — not Qt3D shadow mapping (there is no depth pass/
//! shadow map here), but also not an arbitrary blob: a geometrically honest flat projection
//! shadow. True alpha translucency (see ModelMeshData::shadow/
//! the tqt 3D window, BuildAlphaMeshEntity()) — by request: an opaque shadow of a building on a road
//! covered it completely. These values are for BUILDINGS/polygons (do not touch them because of complaints about
//! LINE shadows — see kLineShadowColor/kLineShadowAlpha, those have their own setting).
static const QVector3D kShadowColor(0.15f, 0.15f, 0.17f);
static const float kShadowAlpha = 0.45f;
//! The color and alpha of LINE shadows (see AppendLineShadowSegment()) — separately from polygons (kShadowColor/
//! kShadowAlpha): by request the shadows of road ribbons specifically were too light (in particular not visible
//! at night), the shadows of buildings were not affected by such a complaint — there is no need to darken them.
static const QVector3D kLineShadowColor(0.04f, 0.04f, 0.05f);
static const float kLineShadowAlpha = 0.6f;
//! The light direction — THE SAME as that of the scene's real QDirectionalLight (see
//! the tqt 3D window, setModel(), light->setWorldDirection()) — the shadow is computed as the projection of the top
//! face of the object along this direction onto the terrain level under it (see AppendShadowBlob()), so
//! its direction and length correspond to the real object height and the real scene lighting, and are not
//! picked by eye. There is only one simplification: the terrain under the shadow is considered locally flat (at the level of
//! baseZ under the object) — the shadow does not "hug" the real slope of the ground.
static const QVector3D kShadowLightDir = QVector3D(-0.5f, -0.7f, -1.0f).normalized();

//! The minimum height of a line ribbon ABOVE the terrain (see AppendLineShadowSegment()) below which the segment
//! casts no shadow at all — without a threshold ordinary roads "on the ground" (kFlatLiftEpsilon~0.15 is the gap against
//! z-fighting, not a real lift) would be covered with a solid strip of a slightly darker tone under themselves,
//! indistinguishable from noise and only cluttering the scene. A noticeable real lift (an overpass,
//! a raised interchange element) passes this threshold easily.
static const double kShadowMinLineHeight = 0.5;

//! The "Supports" effect (ModelEffect::Supports, see AppendSupports()/AppendSupportPillar()): a pillar is placed
//! only where the object is raised above the terrain by at least kSupportMinHeight (a pillar right at the ground
//! is pointless), and only if after subtracting the gap under the object a height of kSupportMinPillarHeight remains.
//! The radius is a fraction of the half-width of the ribbon, but not below/above the limits (for a narrow path the pillar must not
//! be thicker than it, for a wide one — gigantic). Neighboring pillars closer than kSupportSpacingFactor*radius are not
//! placed (on a densely digitized curve the vertices go every meter — a solid palisade). The gap under
//! the object: kSupportBaseGap + a slope correction (see AppendSupports()) — for an inclined ribbon between
//! vertices the surface next to a pillar is lower than exactly above its center and would cut off
//! the top of the pillar (by request: better a gap under the ribbon than a pillar sticking out through it).
static const double kSupportMinHeight = 1.0;
static const double kSupportMinPillarHeight = 0.5;
static const double kSupportRadiusFactor = 0.35;
static const double kSupportMinRadius = 0.25;
static const double kSupportMaxRadius = 1.5;
static const double kSupportSpacingFactor = 10.0;   // by request: neighboring supports — not closer than 10 radii
static const double kSupportBaseGap = 0.15;
//! A polygon is considered raised above the ground (and casts no shadow, see BuildObjectMesh()) if its
//! base is above the terrain at at least one outline vertex by this much.
static const double kRaisedPolygonMinHeight = 0.5;
//! Another ribbon is considered lying "under" a support if its surface at the pillar point is lower than the bottom of the object
//! by at least this much (otherwise a neighboring ribbon at the same level — a junction — would erroneously cancel the pillar).
static const double kSupportClearBelow = 0.5;

//! The offset of the shadow above the terrain (see AppendShadowBlob()/AppendLineShadowSegment()) — it used to be
//! kFlatLiftEpsilon (~0.15, only to avoid flickering with the terrain itself). By request: shadows must be
//! visible not only on bare terrain but also on top of "objects without a real surface of their own" —
//! road ribbons (they sit on terrain+kFlatLiftEpsilon) and "honest" flat slabs with a zero field height
//! (see kMinExtrusionHeight — their conventional "roof" at terrain+kMinExtrusionHeight). This does not give a full
//! occlusion-aware shading (the shadow climbing the walls of REAL tall buildings) — for
//! that an honest shadow mapping is needed (a separate depth pass from the light point), which is in principle
//! absent here (see the docstring of kShadowColor/AppendShadowBlob) — but it is enough to lift the shadow decal itself
//! above these low "pseudo-surfaces" so that it does not sink under them/flicker with
//! them but lies on top, as a shadow should. Taken with a margin above both cases.
static const double kShadowSurfaceLift = 0.6;

//! The radial light spots of the "lamps" (see AppendRadialGlowMarkers()/ModelEffect::RadialGlow) — static,
//! without animation: SEVERAL concentric rings (not 1-2, see kGlowRings) from a bright center outward, the color
//! and alpha of each ring follow a quadratic falloff curve (see GlowFalloff()), not a linear one — so
//! neighboring rings/triangles (already smoothed by Gouraud interpolation inside themselves) visually merge
//! into a continuous gradient and are not read as separate steps. The goal is the look of a light spot from above (as in
//! an aerial photo of a night road with lamps), not a sharp small cone. True alpha blending (not an imitation
//! through mixing with the surface color, as it used to be) — the markers are drawn by a separate translucent
//! entity on top of anything (terrain, neighboring objects), see ModelGlowMeshData/
//! the tqt 3D window, BuildAlphaMeshEntity() — at the outer edge the alpha reaches kGlowRimAlpha (almost 0),
//! the halo dissolves into what is under it instead of fading to a black disc.
static const double kGlowSpacing = 50.0;           //!< the distance between "lamps" along an outline/line
static const double kGlowRadius = 13.5;            //!< the halo radius (the outer edge) — a noticeable area, not a point
static const int kGlowRings = 6;                   //!< the number of rings from the center to the edge — more rings, a smoother look
static const int kGlowSegments = 18;               //!< segments per ring (the more, the smoother the circle)
static const QVector3D kGlowCenterColor(1.0f, 0.92f, 0.55f);   //!< the warm "flame" of the lamp in the center
static const float kGlowCenterAlpha = 0.85f;       //!< the alpha at the very center — also not fully opaque
static const float kGlowRimAlpha = 0.0f;           //!< the alpha at the outer edge of the halo — fully transparent

//! The color+alpha of a ring at the relative radius t (0=center, 1=outer edge) — a quadratic falloff
//! instead of a linear one: for a real light source the brightness falls faster at the edge than in the middle of the spot,
//! because of which linear interpolation looks too uniform/flat "by eye", while a quadratic one looks
//! far more like a real halo. The color stays warm (kGlowCenterColor) on all rings —
//! it is the alpha, not the mixing of the color with the background, that is now responsible for the disappearance toward the edge.
static QVector4D GlowFalloff(float t)
{
    float k = 1.0f - t;
    k = k * k;
    float alpha = kGlowRimAlpha + (kGlowCenterAlpha - kGlowRimAlpha) * k;
    return QVector4D(kGlowCenterColor, alpha);
}

//! How much we brighten the color of the top edge of walls for the "fake" rim light (see ModelEffect::Glow) —
//! a static addition baked into the vertices (NOT a real Fresnel shader depending on the camera viewing
//! angle — at the direct request of the user), linear interpolation toward white.
static const float kGlowRimBrighten = 0.55f;

static QVector3D BrightenTowardWhite(const QVector3D& c, float t)
{
    return c + (QVector3D(1, 1, 1) - c) * t;
}

// A flat rectangle at z=0 (relative to origin) added to an already existing mesh (used
// to "extend" the ground plane beyond the coverage of the terrain raster — see Build()).
static void AppendGroundQuad(double x0, double y0, double x1, double y1, const QVector3D& origin,
                              const QVector3D& color, ModelMeshData& out)
{
    if (x1 <= x0 || y1 <= y0) return;
    quint32 base = quint32(out.vertices.size());
    QVector3D n(0, 0, 1);
    float z = float(-origin.z());
    out.vertices << QVector3D(float(x0 - origin.x()), float(y0 - origin.y()), z)
                 << QVector3D(float(x1 - origin.x()), float(y0 - origin.y()), z)
                 << QVector3D(float(x1 - origin.x()), float(y1 - origin.y()), z)
                 << QVector3D(float(x0 - origin.x()), float(y1 - origin.y()), z);
    out.normals << n << n << n << n;
    out.colors << color << color << color << color;
    out.indices << base << base + 1 << base + 2 << base << base + 2 << base + 3;
}

// TEMPORARY diagnostics (see the same in MainWindow::logBuildDiag()) — it writes to the same file to find out
// in which exact phase of Build() the time is spent: the terrain or which exact vector layer. The same
// file is written to by both the GUI thread (a heartbeat) and this background one — the QFile is opened/
// closed here on every call, concurrent writing from two threads to a diagnostic text
// log is acceptable (at worst — a rare interleaved line, not critical). Remove it together with
// MainWindow::logBuildDiag() when the cause of the hang is found and fixed.
extern bool g_debugLog;   // main.cpp — only when started with -debug/--debug

static void LogBuildPhase(QElapsedTimer& t, const QString& msg)
{
    if (!g_debugLog) return;
    QFile f(QCoreApplication::applicationDirPath() + "/3d_build_diag.log");
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "  +" << t.elapsed() << "ms  [Build] " << msg << "\n";
}

ModelTerrain::ModelTerrain(const QVector<HeightRaster*>& layers)
{
    bool first = true;
    for (HeightRaster* r : layers)
    {
        if (!r) continue;
        mLayers << r;
        double x0 = std::min(r->xB, r->xE), x1 = std::max(r->xB, r->xE);
        double y0 = std::min(r->yB, r->yE), y1 = std::max(r->yB, r->yE);
        if (first) { mXB = x0; mXE = x1; mYB = y0; mYE = y1; first = false; }
        else { mXB = std::min(mXB, x0); mXE = std::max(mXE, x1); mYB = std::min(mYB, y0); mYE = std::max(mYE, y1); }
    }
}

double ModelTerrain::heightAt(double x, double y) const
{
    for (HeightRaster* r : mLayers)
    {
        // a quick rejection by the matrix extent (heightAt() of the matrix itself is expensive — it reads a raster block)
        if (x < std::min(r->xB, r->xE) || x > std::max(r->xB, r->xE)
            || y < std::min(r->yB, r->yE) || y > std::max(r->yB, r->yE)) continue;
        double z = r->heightAt(x, y);
        if (z > -9998.0) return z;
    }
    return -9999.0;
}

bool ModelBuilder::ExtractFootprints(MapFeature* obj, QVector<QVector<QPointF>>& outRings,
                                           QVector<QVector<double>>* outZOffsets,
                                           QVector<QVector<QVector<QPointF>>>* outHoles,
                                           QVector<QVector<QVector<double>>>* outHolesZ)
{
    outRings.clear();
    if (outZOffsets) outZOffsets->clear();
    if (outHoles) outHoles->clear();
    if (outHolesZ) outHolesZ->clear();
    if (!obj || !obj->g) return false;

    OGRwkbGeometryType gType = wkbFlatten(obj->g->getGeometryType());

    // an OGR ring is usually closed (the first point == the last) — do not duplicate the last one.
    auto ringToPoints = [](OGRLinearRing* ring, QVector<QPointF>& pts, QVector<double>& zs) -> bool
    {
        if (!ring || ring->getNumPoints() < 3) return false;
        int n = ring->getNumPoints();
        for (int i = 0; i < n - 1; ++i)
        {
            pts << QPointF(ring->getX(i), ring->getY(i));
            zs << ring->getZ(i);   // 0 if the geometry is 2D — then the base comes from the terrain only
        }
        return pts.size() >= 3;
    };

    auto addPolygon = [&](OGRPolygon* poly)
    {
        if (!poly) return;
        QVector<QPointF> pts;
        QVector<double> zs;
        if (!ringToPoints(poly->getExteriorRing(), pts, zs)) return;
        outRings << pts;
        // Z is taken for each vertex separately (not averaged) — an outline may be an inclined
        // surface (for example a slope cut), not only a horizontal plot.
        if (outZOffsets) *outZOffsets << zs;

        // The inner rings (holes) — cutouts in the walls/roof (windows, doors, inner rooms,
        // etc.). Empty/degenerate rings (<3 points) are silently skipped — as for the outer outline.
        QVector<QVector<QPointF>> holes;
        QVector<QVector<double>> holesZ;
        int nInterior = poly->getNumInteriorRings();
        for (int hi = 0; hi < nInterior; ++hi)
        {
            QVector<QPointF> hpts;
            QVector<double> hzs;
            if (ringToPoints(poly->getInteriorRing(hi), hpts, hzs))
            {
                holes << hpts;
                holesZ << hzs;
            }
        }
        if (outHoles) *outHoles << holes;
        if (outHolesZ) *outHolesZ << holesZ;
    };

    if (gType == wkbPolygon)
    {
        addPolygon(obj->g->toPolygon());
    }
    else if (gType == wkbMultiPolygon)
    {
        OGRMultiPolygon* mp = obj->g->toMultiPolygon();
        for (int g = 0; g < mp->getNumGeometries(); ++g)
            addPolygon(mp->getGeometryRef(g)->toPolygon());
    }

    return !outRings.isEmpty();
}

bool ModelBuilder::ExtractLineStrings(MapFeature* obj, QVector<QVector<QPointF>>& outLines,
                                            QVector<QVector<double>>* outZ)
{
    outLines.clear();
    if (outZ) outZ->clear();
    if (!obj || !obj->g) return false;

    OGRwkbGeometryType gType = wkbFlatten(obj->g->getGeometryType());

    auto addLine = [&](OGRLineString* ls)
    {
        if (!ls || ls->getNumPoints() < 2) return;
        QVector<QPointF> pts;
        QVector<double> z;
        int n = ls->getNumPoints();
        for (int i = 0; i < n; ++i)
        {
            pts << QPointF(ls->getX(i), ls->getY(i));
            if (outZ) z << ls->getZ(i);   // 0.0 for 2D geometry (OGR returns it that way without a Z of its own)
        }
        outLines << pts;
        if (outZ) *outZ << z;
    };

    if (gType == wkbLineString)
    {
        addLine(obj->g->toLineString());
    }
    else if (gType == wkbMultiLineString)
    {
        OGRMultiLineString* ml = obj->g->toMultiLineString();
        for (int g = 0; g < ml->getNumGeometries(); ++g)
            addLine(ml->getGeometryRef(g)->toLineString());
    }

    return !outLines.isEmpty();
}

bool ModelBuilder::BuildObjectMesh(MapFeature* obj, double rel, double lift, double wallExtrusion,
                                         const ModelTerrain* terrain, double zExag,
                                         const QVector3D& origin, const QVector3D& wallColor, ModelEffects effect,
                                         double lineHalfWidth, ModelOutlineMode outline,
                                         ModelMeshData& outMesh,
                                         const QVector<QPair<double, double>>& junctionLifts,
                                         qint64 triangulationBudgetMs, bool* outGaveUp,
                                         const ModelSupportSkipFn* supportSkip)
{
    if (outGaveUp) *outGaveUp = false;
    // Lines (roads/fences, etc.) — a separate geometry (a ribbon or a wall), not a polygon. rel arrives here
    // ALREADY including wallExtrusion (see CollectLayerCandidates()/BuildSingleObjectMesh()) — a ribbon
    // needs only the lift from heightField, so we subtract wallExtrusion back.
    QVector<QVector<QPointF>> lines;
    QVector<QVector<double>> linesZ;
    if (ExtractLineStrings(obj, lines, &linesZ))
    {
        double flatLift = rel - wallExtrusion;
        static const double kNaN = std::numeric_limits<double>::quiet_NaN();
        for (int li = 0; li < lines.size(); ++li)
        {
            double liftStart = kNaN, liftEnd = kNaN;
            if (li < junctionLifts.size())
            {
                liftStart = junctionLifts[li].first;
                liftEnd = junctionLifts[li].second;
            }
            BuildLineRibbonMesh(lines[li], lineHalfWidth, flatLift, lift, wallExtrusion, terrain, zExag, origin,
                                 wallColor, effect, outline, outMesh, liftStart, liftEnd, linesZ[li], supportSkip);
        }
        return !outMesh.vertices.isEmpty();
    }

    QVector<QVector<QPointF>> footprints;
    QVector<QVector<double>> zOffsets;
    QVector<QVector<QVector<QPointF>>> holesByRing;
    QVector<QVector<QVector<double>>> holesZByRing;
    if (!ExtractFootprints(obj, footprints, &zOffsets, &holesByRing, &holesZByRing)) return false;

    // rel==0 — the flat mode ("On the ground"): only a cap at the terrain level, without walls.
    // A negative rel is not flat: the walls are built downward from the base (see topZ below), not skipped.
    bool flat = std::abs(rel) <= 1e-9;
    QVector3D capColor = flat ? wallColor : wallColor * 0.85f;

    // Computes base/top Z for each vertex of the outline r (outer or inner/a hole — the formula is one and
    // the same, see baseZArr/topZArr below): the Z of the source geometry + the terrain at the same point (without terrain
    // the geometry itself stands on the z=0 plane with an offset by the same Z).
    auto computeZArrays = [&](const QVector<QPointF>& r, const QVector<double>& z,
                               QVector<double>& baseOut, QVector<double>& topOut)
    {
        int rn = r.size();
        baseOut.resize(rn);
        topOut.resize(rn);
        for (int i = 0; i < rn; ++i)
        {
            double bz = i < z.size() ? z[i] : 0.0;
            if (terrain)
            {
                double terrainZ = terrain->heightAt(r[i].x(), r[i].y());
                if (terrainZ <= -9998.0) terrainZ = 0.0; // a point outside the raster — put on a conventional zero
                bz += terrainZ;
            }
            bz += lift;   // the lift of the base (see VectorSource::liftField) — BEFORE zExag, like the terrain/rel
            bz *= zExag;
            baseOut[i] = bz;
            topOut[i] = flat ? bz + kFlatLiftEpsilon : bz + rel * zExag;
        }
    };

    // Walls (2 triangles per edge) for one outline — shared code for the outer ring and for
    // every hole (see the calls below): a hole simply has its own pair of base/top arrays and its own winding sign
    // (ringCcw), the outward normal is computed by the same formula — and "outward from the outline" for a hole means
    // "into the cutout", exactly what is needed (see the docstring of ExtractFootprints() about the ring orientation).
    auto buildWalls = [&](const QVector<QPointF>& r, const QVector<double>& baseArr,
                           const QVector<double>& topArr, bool ringCcw)
    {
        int rn = r.size();
        for (int i = 0; i < rn; ++i)
        {
            int iN = (i + 1) % rn;
            QPointF p1 = r[i];
            QPointF p2 = r[iN];
            QVector3D v1b(float(p1.x() - origin.x()), float(p1.y() - origin.y()), float(baseArr[i] - origin.z()));
            QVector3D v2b(float(p2.x() - origin.x()), float(p2.y() - origin.y()), float(baseArr[iN] - origin.z()));
            QVector3D v1t(v1b.x(), v1b.y(), float(topArr[i] - origin.z()));
            QVector3D v2t(v2b.x(), v2b.y(), float(topArr[iN] - origin.z()));

            // the wall normal — a perpendicular to the edge in the XY plane, outward from the outline
            QVector3D edge = v2b - v1b;
            QVector3D wallN = ringCcw ? QVector3D(edge.y(), -edge.x(), 0) : QVector3D(-edge.y(), edge.x(), 0);
            if (!wallN.isNull()) wallN.normalize();

            // The "fake" rim light (see ModelEffect::Glow) — statically brighten the color of only
            // the top wall vertices (v1t/v2t), the bottom (v1b/v2b) stays the original wallColor: from afar
            // it reads as a glowing outline along the top edge of the building, without a real Fresnel term.
            QVector3D topColor = effect.testFlag(ModelEffect::Glow) ? BrightenTowardWhite(wallColor, kGlowRimBrighten) : wallColor;

            int i0 = outMesh.vertices.size();
            outMesh.vertices << v1b << v2b << v2t << v1t;
            outMesh.normals << wallN << wallN << wallN << wallN;
            outMesh.colors << wallColor << wallColor << topColor << topColor;

            // The order of the triangle vertices (winding) — not only for the lit side by the normal
            // (wallN above), but also for the back-face culling by the renderer, which looks at the
            // ACTUAL geometric normal of the triangle (by the right-hand rule from the vertex
            // order), not at the stored per-vertex wallN. wallN is computed in XY only and does not
            // depend on the sign of (topArr-baseArr) — but the geometric normal itself does: with a
            // positive extrusion (top above base) the traversal v1b→v2b→v2t gives a normal that coincides with
            // wallN; with a negative one (top below base, see topZArr in the calling code) the same
            // vertex sequence gives a normal in the OPPOSITE direction — from outside the
            // culled back face is visible, the wall looks "inside out" and transparent. The sign
            // (topArr[i]-baseArr[i]) equals the sign of rel for any vertex (see computeZArrays — top and
            // base differ by exactly rel*zExag), so it is enough to reverse the whole order by the
            // sign of rel, without a check on every edge.
            if (rel >= 0)
            {
                outMesh.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
                outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
            }
            else
            {
                outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 1);
                outMesh.indices << quint32(i0) << quint32(i0 + 3) << quint32(i0 + 2);
            }
        }
    };

    for (int ringIdx = 0; ringIdx < footprints.size(); ++ringIdx)
    {
        const QVector<QPointF>& ring = footprints[ringIdx];
        int n = ring.size();
        const QVector<QVector<QPointF>>& holes = holesByRing.value(ringIdx);
        const QVector<QVector<double>>& holesZ = holesZByRing.value(ringIdx);
        bool hasHoles = !holes.isEmpty();

        const QVector<double>& z = zOffsets.value(ringIdx);
        QVector<double> baseZArr, topZArr;
        computeZArrays(ring, z, baseZArr, topZArr);
        // A flat base (all vertices at one level) — most real objects; then the
        // rounding and the shadow (see below) are computed as before, by a single number. An inclined base is
        // a rare case (a slope cut, etc.); for it we do not build the edge rounding (it would complicate
        // the chamfer geometry without an obvious benefit), and the shadow/the scalar topZ is taken from the first outline vertex.
        bool slanted = false;
        for (int i = 1; i < n; ++i)
            if (std::abs(baseZArr[i] - baseZArr[0]) > 1e-6) { slanted = true; break; }
        double baseZ = baseZArr.isEmpty() ? 0.0 : baseZArr[0];
        double topZ = topZArr.isEmpty() ? 0.0 : topZArr[0];

        // The sign of the outline area (shoelace) — for a consistent outward wall normal.
        double area2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const QPointF& p1 = ring[i];
            const QPointF& p2 = ring[(i + 1) % n];
            area2 += p1.x() * p2.y() - p2.x() * p1.y();
        }
        bool ccw = area2 > 0;

        // --- "Smoothing" (see ModelEffect::Rounded) — we prepare for every outline vertex
        // the chamfer direction inward (an approximate bisector of the normals of the two adjacent edges) and its
        // actual radius in XY (clamped by the lengths of the neighboring edges, see the docstring of kRoundedRadius);
        // the chamfer itself (sub-rings) is built BELOW, between the top of the wall and the cap, since its vertical
        // clamp (rel*zExag) is already known here, but the geometry is more convenient after computing wallTopZ. With
        // holes we do not build it at all — the chamfer is designed for one simple outline, not for an outline with cutouts.
        bool rounded = !flat && !slanted && !hasHoles && effect.testFlag(ModelEffect::Rounded);
        double bevelHeight = 0.0;
        QVector<QPointF> inwardDir;
        QVector<double> insetDist;
        if (rounded)
        {
            bevelHeight = std::min(kRoundedRadius, std::max(0.0, rel * zExag * 0.4));
            if (bevelHeight > 1e-6)
            {
                inwardDir.resize(n);
                insetDist.resize(n);
                for (int i = 0; i < n; ++i)
                {
                    QPointF pPrev = ring[(i - 1 + n) % n], pCur = ring[i], pNext = ring[(i + 1) % n];
                    QPointF e1 = pCur - pPrev, e2 = pNext - pCur;
                    double l1 = std::hypot(e1.x(), e1.y()), l2 = std::hypot(e2.x(), e2.y());
                    QPointF edgeN1 = l1 > 1e-9 ? QPointF(e1.y() / l1, -e1.x() / l1) : QPointF(0, 0);
                    QPointF edgeN2 = l2 > 1e-9 ? QPointF(e2.y() / l2, -e2.x() / l2) : QPointF(0, 0);
                    if (!ccw) { edgeN1 = -edgeN1; edgeN2 = -edgeN2; }
                    QPointF bis = edgeN1 + edgeN2;
                    double bisLen = std::hypot(bis.x(), bis.y());
                    QPointF outwardDir = bisLen > 1e-9 ? QPointF(bis.x() / bisLen, bis.y() / bisLen) : edgeN1;
                    inwardDir[i] = QPointF(-outwardDir.x(), -outwardDir.y());
                    double edgeClamp = 0.4 * std::min(l1 > 1e-9 ? l1 : 1e9, l2 > 1e-9 ? l2 : 1e9);
                    insetDist[i] = std::min(bevelHeight, edgeClamp);
                }
            }
            else rounded = false;   // the object is too low — there is practically nothing to round
        }
        double wallTopZ = rounded ? (topZ - bevelHeight) : topZ;   // a scalar — used only when !slanted (see rounded above)
        QVector<double> wallTopZArr(n);
        for (int i = 0; i < n; ++i)
            wallTopZArr[i] = rounded ? (topZArr[i] - bevelHeight) : topZArr[i];

        // --- walls of the outer outline (skipped in the flat mode — see flat above) ---
        // The top of the wall is wallTopZArr, not topZArr: with rounded it is slightly lower, the remainder up to topZ
        // is made up by the chamfer (see below) — without rounded wallTopZArr==topZArr, the behavior does not differ from
        // the former one. The bottom and the top are taken for each vertex separately — for an inclined base the wall
        // comes out as a trapezoid, not a rectangle, which gives the inclined "trimming" of the object along the terrain.
        if (!flat) buildWalls(ring, baseZArr, wallTopZArr, ccw);

        // --- walls of the holes (windows, doors, inner rooms, etc., see ExtractFootprints()) — the same
        // extrusion (baseZ/topZ by the same formulas) as for the outer outline, but with its own winding sign:
        // a hole in a valid OGC geometry always goes with the orientation opposite to the outer ring, and
        // buildWalls() already takes this into account through ringCcw — the wall of a hole automatically "looks" inside
        // the cutout, without any special case.
        QVector<QVector<double>> holeBaseZ(holes.size()), holeTopZ(holes.size());
        for (int hi = 0; hi < holes.size(); ++hi)
        {
            const QVector<QPointF>& hole = holes[hi];
            if (hole.size() < 3) continue;
            computeZArrays(hole, holesZ.value(hi), holeBaseZ[hi], holeTopZ[hi]);
            if (flat) continue;   // the cap with holes is built below regardless of flat, but the hole walls are not
            double holeArea2 = 0;
            int hn = hole.size();
            for (int i = 0; i < hn; ++i)
                holeArea2 += hole[i].x() * hole[(i + 1) % hn].y() - hole[(i + 1) % hn].x() * hole[i].y();
            buildWalls(hole, holeBaseZ[hi], holeTopZ[hi], holeArea2 > 0);
        }

        // --- the "Smoothing" chamfer — kRoundedRings sub-rings between the top of the wall (wallTopZ, offset 0)
        // and the cap (topZ, offset insetDist[i] inward) along a quarter-circle profile of radius
        // bevelHeight in section: the XY offset grows like sin(θ), the height also like sin(θ) from wallTopZ
        // to topZ, and the normal is (outward*cosθ, sinθ) — at θ=0 it coincides with the wall normal, at θ=π/2 with
        // the cap normal (0,0,1), so the seam is smooth on both sides (no break in the lighting).
        // The resulting ring of the last sub-ring (θ=π/2) — capRing — is used below for the cap itself.
        // capZArr — the Z of the cap for every vertex of capRing (for !rounded && !hasHoles it coincides with topZArr,
        // the same size and order; with rounded — too, the inset changes neither the number nor the order of vertices;
        // with holes — its own array stitched by MergeHolesIntoRing(), see below).
        QVector<QPointF> capRing = ring;
        QVector<double> capZArr = topZArr;
        if (rounded)
        {
            capRing.resize(n);
            for (int i = 0; i < n; ++i)
                capRing[i] = ring[i] + inwardDir[i] * insetDist[i];

            for (int j = 0; j < kRoundedRings; ++j)
            {
                double a0 = (double(j) / kRoundedRings) * (M_PI * 0.5);
                double a1 = (double(j + 1) / kRoundedRings) * (M_PI * 0.5);
                double s0 = std::sin(a0), c0 = std::cos(a0);
                double s1 = std::sin(a1), c1 = std::cos(a1);
                double z0 = wallTopZ + bevelHeight * s0, z1 = wallTopZ + bevelHeight * s1;
                // the chamfer color — a linear transition from wallColor (at the wall, θ=0) to capColor (at the cap, θ=π/2)
                QVector3D col0 = wallColor + (capColor - wallColor) * float(s0);
                QVector3D col1 = wallColor + (capColor - wallColor) * float(s1);

                auto fillNormal = [](const QPointF& inward, double c, double s) -> QVector3D
                {
                    QVector3D nrm(float(-inward.x() * c), float(-inward.y() * c), float(s));
                    if (!nrm.isNull()) nrm.normalize();
                    return nrm;
                };

                for (int i = 0; i < n; ++i)
                {
                    int iN = (i + 1) % n;
                    QPointF p0a = ring[i]  + inwardDir[i]  * (insetDist[i]  * s0);
                    QPointF p0b = ring[iN] + inwardDir[iN] * (insetDist[iN] * s0);
                    QPointF p1a = ring[i]  + inwardDir[i]  * (insetDist[i]  * s1);
                    QPointF p1b = ring[iN] + inwardDir[iN] * (insetDist[iN] * s1);

                    QVector3D v00(float(p0a.x() - origin.x()), float(p0a.y() - origin.y()), float(z0 - origin.z()));
                    QVector3D v01(float(p0b.x() - origin.x()), float(p0b.y() - origin.y()), float(z0 - origin.z()));
                    QVector3D v10(float(p1a.x() - origin.x()), float(p1a.y() - origin.y()), float(z1 - origin.z()));
                    QVector3D v11(float(p1b.x() - origin.x()), float(p1b.y() - origin.y()), float(z1 - origin.z()));

                    int i0 = outMesh.vertices.size();
                    outMesh.vertices << v00 << v01 << v11 << v10;
                    outMesh.normals << fillNormal(inwardDir[i], c0, s0) << fillNormal(inwardDir[iN], c0, s0)
                                     << fillNormal(inwardDir[iN], c1, s1) << fillNormal(inwardDir[i], c1, s1);
                    outMesh.colors << col0 << col0 << col1 << col1;
                    outMesh.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
                    outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
                }
            }
        }
        else if (hasHoles)
        {
            // We stitch the holes with bridges right into the outer outline (see MergeHolesIntoRing()) — we get
            // one simple (hole-free) outline which TriangulatePolygon() below can triangulate
            // as usual; capZArr is stitched in the same order as capRing.
            MergeHolesIntoRing(ring, topZArr, holes, holeTopZ, capRing, capZArr);
        }

        // --- the cap: ear-clipping triangulation of the outline (capRing — the same ring if there are neither
        // "Smoothing" nor holes) at the level capZArr[i] (with !slanted and without holes all values are equal to
        // topZ, as before) ---
        // The cap normal is not always (0,0,1): with an inclined base the roof goes with the same slope as
        // the object (see topZArr above), so we take the normal from the actual plane of the first three points
        // of the outline (a simplification — we consider the outline flat) and turn it upward.
        QVector3D capNormal(0, 0, 1);
        if (n >= 3)
        {
            QVector3D p0(float(ring[0].x()), float(ring[0].y()), float(topZArr[0]));
            QVector3D p1v(float(ring[1].x()), float(ring[1].y()), float(topZArr[1]));
            QVector3D p2v(float(ring[2].x()), float(ring[2].y()), float(topZArr[2]));
            QVector3D n3 = QVector3D::crossProduct(p1v - p0, p2v - p0);
            if (!ccw) n3 = -n3;
            if (n3.z() < 0) n3 = -n3;
            if (!n3.isNull()) { n3.normalize(); capNormal = n3; }
        }
        QVector<int> tris;
        bool capGaveUp = false;
        if (TriangulatePolygon(capRing, tris, hasHoles, triangulationBudgetMs, &capGaveUp))
        {
            if (capGaveUp && outGaveUp) *outGaveUp = true;
            int capBase = outMesh.vertices.size();
            for (int i = 0; i < capRing.size(); ++i)
            {
                const QPointF& p = capRing[i];
                double zc = i < capZArr.size() ? capZArr[i] : topZ;
                outMesh.vertices << QVector3D(float(p.x() - origin.x()), float(p.y() - origin.y()), float(zc - origin.z()));
                outMesh.normals << capNormal;
                outMesh.colors << capColor;
            }
            for (int i = 0; i + 2 < tris.size(); i += 3)
            {
                // ear-clipping returns the triangles in the traversal order of ring (a CCW outline => CCW from above);
                // with a CW outline we reverse the winding so that the cap is visible from above.
                if (ccw)
                    outMesh.indices << quint32(capBase + tris[i]) << quint32(capBase + tris[i + 1]) << quint32(capBase + tris[i + 2]);
                else
                    outMesh.indices << quint32(capBase + tris[i]) << quint32(capBase + tris[i + 2]) << quint32(capBase + tris[i + 1]);
            }
        }

        if (outline != ModelOutlineMode::None)
        {
            QVector3D outlineColor = OutlineColor(wallColor, outline);
            if (hasHoles)
            {
                // NOT capRing/capZArr: with holes this is an outline stitched by zero-width bridges (see
                // MergeHolesIntoRing()) — an outline along it would go along those bridges too, visibly giving away
                // the stitching trick. We outline the real boundaries separately: the outer outline and each hole
                // by its own closed outline (rounded does not combine with holes, see above).
                AppendRingOutline(ring, topZArr, /*closed=*/true, origin, outlineColor, outMesh);
                for (int hi = 0; hi < holes.size(); ++hi)
                    if (holes[hi].size() >= 3)
                        AppendRingOutline(holes[hi], holeTopZ[hi], /*closed=*/true, origin, outlineColor, outMesh);
            }
            else
            {
                // Without holes capRing is either the same ring (without rounded) or the inset outline of the chamfer (rounded) —
                // in both cases the real visible boundary of the cap, not stitched by anything.
                AppendRingOutline(capRing, capZArr, /*closed=*/true, origin, outlineColor, outMesh);
            }
        }

        // --- The "bottom" — the same cap, only at the level baseZArr and facing down, not up. With a
        // positive extrusion it is usually not visible (the object stands on the terrain), but with a
        // negative one the "bottom" geometrically ends up ON TOP (see topZArr above — the base is fixed,
        // and the top goes down), and without this cap the side that is now on top would be open
        // (an "emptiness" is visible from inside the walls). We build it always (not only for rel<0) — by request: this way
        // the object stays a closed body when viewed from any side regardless of the extrusion sign.
        // In flat mode there are no walls at all (see flat above) — there is nowhere for a bottom to come from, the cap is not built.
        if (!flat)
        {
            QVector<QPointF> bottomRing = ring;
            QVector<double> bottomZArr = baseZArr;
            if (hasHoles) MergeHolesIntoRing(ring, baseZArr, holes, holeBaseZ, bottomRing, bottomZArr);

            QVector<int> bottomTris;
            bool bottomGaveUp = false;
            if (TriangulatePolygon(bottomRing, bottomTris, hasHoles, triangulationBudgetMs, &bottomGaveUp))
            {
                if (bottomGaveUp && outGaveUp) *outGaveUp = true;
                // The same plane as that of the top cap (base and top differ by the constant
                // rel*zExag — see computeZArrays, the slope is the same), so the normal is simply
                // capNormal turned in the opposite direction.
                QVector3D bottomNormal = -capNormal;
                int capBase = outMesh.vertices.size();
                for (int i = 0; i < bottomRing.size(); ++i)
                {
                    const QPointF& p = bottomRing[i];
                    double zc = i < bottomZArr.size() ? bottomZArr[i] : baseZ;
                    outMesh.vertices << QVector3D(float(p.x() - origin.x()), float(p.y() - origin.y()), float(zc - origin.z()));
                    outMesh.normals << bottomNormal;
                    outMesh.colors << capColor;
                }
                for (int i = 0; i + 2 < bottomTris.size(); i += 3)
                {
                    // The winding — mirrored to the top cap (see the ccw fork there): this one must
                    // face down, not up.
                    if (ccw)
                        outMesh.indices << quint32(capBase + bottomTris[i]) << quint32(capBase + bottomTris[i + 2]) << quint32(capBase + bottomTris[i + 1]);
                    else
                        outMesh.indices << quint32(capBase + bottomTris[i]) << quint32(capBase + bottomTris[i + 1]) << quint32(capBase + bottomTris[i + 2]);
                }
            }
        }

        // The bare terrain under the outline vertices and the "object is raised above the ground" flag (the base is above the
        // terrain at at least one vertex, because of lift or the geometry Z) — needed both by supports and by the shadow.
        QVector<double> groundArr(n, 0.0);
        if (terrain)
            for (int i = 0; i < n; ++i)
            {
                double tz = terrain->heightAt(ring[i].x(), ring[i].y());
                groundArr[i] = (tz <= -9998.0 ? 0.0 : tz) * zExag;
            }
        bool raisedPolygon = false;
        for (int i = 0; i < n && i < baseZArr.size(); ++i)
            if (baseZArr[i] - groundArr[i] >= kRaisedPolygonMinHeight) { raisedPolygon = true; break; }

        if (effect.testFlag(ModelEffect::Supports))
        {
            // Supports under the vertices of the outer outline of a raised polygon (lift > 0 — the base above the
            // terrain): from the terrain to the base (baseZArr — the bottom of the walls/bottom) with a small gap.
            AppendSupports(ring, /*closed=*/true, baseZArr, groundArr, 0.6, kSupportBaseGap, capColor, origin, outMesh);
        }

        // Polygons raised above the ground ("houses on legs") cast no shadow: AppendShadowBlob()
        // puts the shadow at the level of the object's own base, i.e. for such a house — in the air, not
        // on the ground. These are isolated cases — by request it is simpler to turn it off than to compute a shadow on the terrain.
        if (effect.testFlag(ModelEffect::Shadow) && !flat && !raisedPolygon)
        {
            // The accuracy of the outline for the shadow does not matter (see the docstring of AppendShadowBlob()/ModelMeshData::
            // shadowPieces) — we ALWAYS take the outer outline ring without holes, even if the object has them
            // (hasHoles): the final silhouette is merged anyway (a boolean union) with the silhouettes of the other
            // scene objects in BuildMergedShadowMesh(), and the Z of each vertex of the result is taken there
            // too, by the terrain at that point — neither shadowBaseZ (the minimum over the outline) nor capRing/tris
            // (the cap triangulation with holes) are needed for the shadow any more.
            AppendShadowBlob(ring, rel * zExag, outMesh.shadowPieces);
        }

        if (effect.testFlag(ModelEffect::RadialGlow))
            AppendRadialGlowMarkers(ring, /*closed=*/true, terrain, zExag, flat ? 0.0 : rel, lift, origin, capColor, outMesh);
    }

    return !outMesh.vertices.isEmpty();
}

QVector<double> ModelBuilder::ComputeVertexLift(const QVector<QPointF>& line, double lift,
                                                       double liftStart, double liftEnd)
{
    int n = line.size();
    QVector<double> out(n, lift);
    if (n < 2) return out;

    bool blendStart = std::isfinite(liftStart) && std::abs(liftStart - lift) > 1e-9;
    bool blendEnd = std::isfinite(liftEnd) && std::abs(liftEnd - lift) > 1e-9;
    if (!blendStart && !blendEnd) return out;   // no junction at either end — the lift is constant, as before

    QVector<double> arc(n, 0.0);
    for (int i = 1; i < n; ++i)
        arc[i] = arc[i - 1] + std::hypot(line[i].x() - line[i - 1].x(), line[i].y() - line[i - 1].y());
    double totalLen = arc.last();
    if (totalLen < 1e-6) return out;

    // We blend over HALF of the line length from each junction end, without an upper limit: for an object
    // deliberately cut as a short connecting ramp between two levels (a typical case
    // here) this means that its whole run turns into a smooth ramp from one junction to
    // another — instead of what used to be a flat section with a fixed transition "tail"
    // (kJunctionBlendDistance) of just a few length units at the very ends, because of which with a large difference
    // of lift (units-tens of meters) on a short tail an almost vertical, visually
    // unnoticeable "cliff" came out, not a visible slope. For a very long line with a junction at only one end
    // this still means a slope over half of its length — a deliberate compromise: for dedicated
    // ramps/connectors (for which this convergence is designed) the line length is exactly the length of the needed
    // transition, not an arbitrary value.
    double blendLen = totalLen * 0.5;
    auto smoothstep = [](double t) { return t * t * (3.0 - 2.0 * t); };

    for (int i = 0; i < n; ++i)
    {
        double vStart = lift, vEnd = lift;
        bool inStart = blendStart && arc[i] < blendLen;
        bool inEnd = blendEnd && (totalLen - arc[i]) < blendLen;
        if (inStart)
        {
            double s = smoothstep(1.0 - arc[i] / blendLen);
            vStart = lift + (liftStart - lift) * s;
        }
        if (inEnd)
        {
            double s = smoothstep(1.0 - (totalLen - arc[i]) / blendLen);
            vEnd = lift + (liftEnd - lift) * s;
        }
        // A short line where both transition zones overlap at both ends at once — we average the contribution
        // of both junctions instead of letting one silently override the other.
        if (inStart && inEnd) out[i] = (vStart + vEnd) * 0.5;
        else if (inStart) out[i] = vStart;
        else if (inEnd) out[i] = vEnd;
    }
    return out;
}

//! The limiter of the bisector length in ComputeRibbonOffsets() — see its docstring.
static const double kMiterLimitFactor = 4.0;

QVector<QPointF> ModelBuilder::ComputeRibbonOffsets(const QVector<QPointF>& line, double halfWidth)
{
    int n = line.size();
    QVector<QPointF> offset(n, QPointF(0, 0));
    if (n < 2) return offset;

    for (int i = 0; i < n; ++i)
    {
        bool hasPrev = i > 0, hasNext = i + 1 < n;
        QPointF dirPrev, dirNext;
        if (hasPrev)
        {
            QPointF d = line[i] - line[i - 1];
            double l = std::hypot(d.x(), d.y());
            if (l > 1e-9) dirPrev = QPointF(d.x() / l, d.y() / l); else hasPrev = false;
        }
        if (hasNext)
        {
            QPointF d = line[i + 1] - line[i];
            double l = std::hypot(d.x(), d.y());
            if (l > 1e-9) dirNext = QPointF(d.x() / l, d.y() / l); else hasNext = false;
        }

        if (hasPrev && hasNext)
        {
            QPointF perpPrev(-dirPrev.y(), dirPrev.x());
            QPointF perpNext(-dirNext.y(), dirNext.x());
            QPointF sum(perpPrev.x() + perpNext.x(), perpPrev.y() + perpNext.y());
            double sumLen = std::hypot(sum.x(), sum.y());
            if (sumLen > 1e-6)
            {
                QPointF bis(sum.x() / sumLen, sum.y() / sumLen);
                double cosHalf = bis.x() * perpPrev.x() + bis.y() * perpPrev.y();   // >0 at reasonable angles
                double miterLen = cosHalf > 0.2 ? std::min(halfWidth / cosHalf, halfWidth * kMiterLimitFactor)
                                                 : halfWidth * kMiterLimitFactor;
                offset[i] = QPointF(bis.x() * miterLen, bis.y() * miterLen);
            }
            else
            {
                // A reversal of exactly 180° (the line goes back onto itself) — the bisector is undefined,
                // we use the perpendicular of the incoming segment (as it was before, without a miter).
                offset[i] = QPointF(perpPrev.x() * halfWidth, perpPrev.y() * halfWidth);
            }
        }
        else if (hasPrev)
        {
            QPointF perpPrev(-dirPrev.y(), dirPrev.x());
            offset[i] = QPointF(perpPrev.x() * halfWidth, perpPrev.y() * halfWidth);
        }
        else if (hasNext)
        {
            QPointF perpNext(-dirNext.y(), dirNext.x());
            offset[i] = QPointF(perpNext.x() * halfWidth, perpNext.y() * halfWidth);
        }
    }
    return offset;
}

void ModelBuilder::BuildLineRibbonMesh(const QVector<QPointF>& line, double halfWidth, double rel, double lift,
                                             double wallExtrusion, const ModelTerrain* terrain, double zExag, const QVector3D& origin,
                                             const QVector3D& color, ModelEffects effect, ModelOutlineMode outline,
                                             ModelMeshData& outMesh, double liftStart, double liftEnd,
                                             const QVector<double>& geomZ, const ModelSupportSkipFn* supportSkip)
{
    int n = line.size();
    if (n < 2) return;

    QVector3D ribbonColor = effect.testFlag(ModelEffect::Glow) ? BrightenTowardWhite(color, kGlowRimBrighten * 0.6f) : color;

    // The lateral offset per vertex (see its docstring) — with a miter at inner vertices so that neighboring
    // ribbon/wall segments at turns meet at a common edge and do not diverge with wedges ("teeth").
    QVector<QPointF> ribbonOffsets = ComputeRibbonOffsets(line, halfWidth);

    // The lift per vertex — usually constant (lift), except the zones at the ends of the line where there is a junction with another
    // object (liftStart/liftEnd are not NaN, see CollectLayerCandidates()) — there a smooth transition to the averaged
    // lift at the junction (see ComputeVertexLift()) so that the interchange ramps converge with each other and do not
    // break off with a jump.
    QVector<double> vertexLift = ComputeVertexLift(line, lift, liftStart, liftEnd);

    // The base height at a point — the terrain AT THIS point (not only by the centroid, as for polygons: a line
    // can stretch across a noticeable height difference) + the Z of the source geometry at this point (geomZ — 0 for
    // ordinary 2D geometry, see ExtractLineStrings()) + the lift at this vertex (see vertexLift above) +
    // rel (the lift from heightField, for raised lines like overpasses).
    auto baseZAt = [&](int idx) -> double
    {
        const QPointF& p = line[idx];
        double z = 0.0;
        if (terrain)
        {
            z = terrain->heightAt(p.x(), p.y());
            if (z <= -9998.0) z = 0.0;
        }
        double gz = idx < geomZ.size() ? geomZ[idx] : 0.0;
        return (z + gz + vertexLift[idx] + rel) * zExag;
    };

    // The bare terrain level (WITHOUT geomZ/lift/rel) at a point — needed only for the shadow (see
    // AppendLineShadowSegment()/ModelEffect::Shadow), to know how much the ribbon is raised ABOVE
    // the ground at each vertex, not its absolute height.
    auto groundZAt = [&](int idx) -> double
    {
        if (!terrain) return 0.0;
        const QPointF& p = line[idx];
        double z = terrain->heightAt(p.x(), p.y());
        if (z <= -9998.0) z = 0.0;
        return z * zExag;
    };

    bool asWall = std::abs(wallExtrusion) > 1e-6;

    auto quad = [&](const QVector3D& a, const QVector3D& b, const QVector3D& c, const QVector3D& d,
                     const QVector3D& nrm, const QVector3D& col)
    {
        int i0 = outMesh.vertices.size();
        outMesh.vertices << a << b << c << d;
        for (int k = 0; k < 4; ++k) { outMesh.normals << nrm; outMesh.colors << col; }
        outMesh.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
        outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
    };

    // Supports (ModelEffect::Supports) — under every vertex where the ribbon is raised above the terrain; the pillar
    // rests on the BOTTOM of the object (baseZAt() — for a wall it is its base, for a flat ribbon — the ribbon itself) with
    // a gap for the slope, see AppendSupports(). Called in both branches below.
    auto addSupports = [&]()
    {
        if (!effect.testFlag(ModelEffect::Supports)) return;
        QVector<double> baseZ(n), groundZ(n);
        for (int i = 0; i < n; ++i) { baseZ[i] = baseZAt(i); groundZ[i] = groundZAt(i); }
        double radius = std::max(kSupportMinRadius, std::min(kSupportMaxRadius, halfWidth * kSupportRadiusFactor));
        AppendSupports(line, /*closed=*/false, baseZ, groundZ, radius, kSupportBaseGap, ribbonColor, origin, outMesh,
                        supportSkip);
    };

    if (asWall)
    {
        // The "extrusion" for lines (wallExtrusion!=0) is not a lift of the ribbon (that is done by rel/heightField above,
        // see baseZAt()), but a real vertical wall: for example fences along a line. One quad per
        // segment — two side faces of thickness halfWidth*2 (see VectorSource::lineWidth) and a cap
        // on top, plus end walls at the ends of the polyline (otherwise it would look like a hollow box). A physically
        // real volume, not an imitation — the "Explore" collision sees this wall just like any
        // other solid geometry (see VectorSource::extraExtrusion), it can only be bypassed from the side/above.
        QVector3D outlineColor = outline != ModelOutlineMode::None ? OutlineColor(color, outline) : QVector3D();

        for (int i = 0; i + 1 < n; ++i)
        {
            QPointF p1 = line[i], p2 = line[i + 1];
            QPointF dir = p2 - p1;
            double len = std::hypot(dir.x(), dir.y());
            if (len < 1e-6) continue;
            QPointF off1 = ribbonOffsets[i], off2 = ribbonOffsets[i + 1];

            double base1 = baseZAt(i) - origin.z(), base2 = baseZAt(i + 1) - origin.z();
            double top1 = base1 + wallExtrusion * zExag, top2 = base2 + wallExtrusion * zExag;

            // The shadow on the terrain (see ModelEffect::Shadow/AppendLineShadowSegment()) — from the TOP of the wall
            // (top1/top2), not from the base: the real height that an object casts includes
            // wallExtrusion too (for example fences/curbs with a positive "Extrusion" — see
            // VectorSource::extraExtrusion — for roads are now also built by this branch, not by a flat
            // ribbon, since extraExtrusion!=0, see asWall above).
            if (effect.testFlag(ModelEffect::Shadow))
            {
                double groundZ1 = groundZAt(i), groundZ2 = groundZAt(i + 1);
                double h1 = baseZAt(i) + wallExtrusion * zExag - groundZ1;
                double h2 = baseZAt(i + 1) + wallExtrusion * zExag - groundZ2;
                AppendLineShadowSegment(p1, p2, halfWidth, h1, h2, groundZ1, groundZ2, zExag, origin, outMesh);
            }

            QVector3D bL1(float(p1.x() + off1.x() - origin.x()), float(p1.y() + off1.y() - origin.y()), float(base1));
            QVector3D bR1(float(p1.x() - off1.x() - origin.x()), float(p1.y() - off1.y() - origin.y()), float(base1));
            QVector3D bL2(float(p2.x() + off2.x() - origin.x()), float(p2.y() + off2.y() - origin.y()), float(base2));
            QVector3D bR2(float(p2.x() - off2.x() - origin.x()), float(p2.y() - off2.y() - origin.y()), float(base2));
            QVector3D tL1(bL1.x(), bL1.y(), float(top1));
            QVector3D tR1(bR1.x(), bR1.y(), float(top1));
            QVector3D tL2(bL2.x(), bL2.y(), float(top2));
            QVector3D tR2(bR2.x(), bR2.y(), float(top2));

            QVector3D outNL(float(dir.y()), float(-dir.x()), 0);   // the normal along the segment direction (not the offset — a miter would distort it)
            if (!outNL.isNull()) outNL.normalize();
            QVector3D outNR = -outNL;

            quad(bL1, bL2, tL2, tL1, outNL, ribbonColor);           // the left face
            quad(bR2, bR1, tR1, tR2, outNR, ribbonColor);           // the right face
            quad(tL1, tL2, tR2, tR1, QVector3D(0, 0, 1), ribbonColor); // the cap

            if (outline != ModelOutlineMode::None)
            {
                // A thin strip along the top edges of the wall (by analogy with the roof outline of a polygon) —
                // raised above the cap (kOutlineLift) to avoid z-fighting. The outward direction —
                // by the normalized offset of EACH vertex separately (not common per segment), otherwise at
                // mitered joints the strip would "sink" into the ribbon at one of the ends.
                auto outwardAt = [&](const QPointF& off) -> QPointF
                {
                    double l = std::hypot(off.x(), off.y());
                    return l > 1e-9 ? QPointF(off.x() / l * kOutlineWidth, off.y() / l * kOutlineWidth) : QPointF();
                };
                QPointF out1 = outwardAt(off1), out2 = outwardAt(off2);
                auto appendTopStrip = [&](const QVector3D& a1, const QVector3D& a2, const QPointF& o1, const QPointF& o2)
                {
                    QVector3D a1r(a1.x(), a1.y(), a1.z() + float(kOutlineLift));
                    QVector3D a2r(a2.x(), a2.y(), a2.z() + float(kOutlineLift));
                    QVector3D b1(a1r.x() + float(o1.x()), a1r.y() + float(o1.y()), a1r.z());
                    QVector3D b2(a2r.x() + float(o2.x()), a2r.y() + float(o2.y()), a2r.z());
                    quad(a1r, b1, b2, a2r, QVector3D(0, 0, 1), outlineColor);
                };
                appendTopStrip(tL1, tL2, out1, out2);
                appendTopStrip(tR1, tR2, QPointF(-out1.x(), -out1.y()), QPointF(-out2.x(), -out2.y()));
            }

            QVector3D dirUnit(float(dir.x() / len), float(dir.y() / len), 0);
            if (i == 0) quad(bR1, bL1, tL1, tR1, -dirUnit, ribbonColor);           // the end face at the start
            if (i == n - 2) quad(bL2, bR2, tR2, tL2, dirUnit, ribbonColor);        // the end face at the end
        }

        addSupports();
        if (effect.testFlag(ModelEffect::RadialGlow))
            AppendRadialGlowMarkers(line, /*closed=*/false, terrain, zExag, rel + wallExtrusion, lift, origin, ribbonColor, outMesh);
        return;
    }

    // An ordinary flat ribbon (roads, etc.) — kFlatLiftEpsilon: the ribbon ALWAYS (not only for "On the
    // ground") runs right against the terrain by construction, without it it would flicker with the terrain mesh under it
    // even with a nonzero rel.
    auto zAt = [&](int idx) -> double { return baseZAt(idx) + kFlatLiftEpsilon; };

    // One quad per segment, the side points — from ribbonOffsets (a miter at inner vertices, see its
    // docstring) — neighboring segments share a common edge at the joint, without wedges/gaps at turns.
    for (int i = 0; i + 1 < n; ++i)
    {
        QPointF p1 = line[i], p2 = line[i + 1];
        QPointF dir = p2 - p1;
        double len = std::hypot(dir.x(), dir.y());
        if (len < 1e-6) continue;
        QPointF off1 = ribbonOffsets[i], off2 = ribbonOffsets[i + 1];

        double z1 = zAt(i) - origin.z(), z2 = zAt(i + 1) - origin.z();

        QVector3D vL1(float(p1.x() + off1.x() - origin.x()), float(p1.y() + off1.y() - origin.y()), float(z1));
        QVector3D vR1(float(p1.x() - off1.x() - origin.x()), float(p1.y() - off1.y() - origin.y()), float(z1));
        QVector3D vL2(float(p2.x() + off2.x() - origin.x()), float(p2.y() + off2.y() - origin.y()), float(z2));
        QVector3D vR2(float(p2.x() - off2.x() - origin.x()), float(p2.y() - off2.y() - origin.y()), float(z2));

        QVector3D n3(0, 0, 1);
        quad(vL1, vR1, vR2, vL2, n3, ribbonColor);

        // The shadow on the terrain (see ModelEffect::Shadow/AppendLineShadowSegment()) — only where the ribbon
        // is really raised above the ground (an overpass, a raised interchange element), not for an ordinary
        // road "on the ground" (see kShadowMinLineHeight) — h1/h2 are computed from baseZAt(), not from zAt()
        // (that one includes kFlatLiftEpsilon — an insignificant technical gap against z-fighting, not a real
        // lift, there is no point in counting it twice in the threshold).
        if (effect.testFlag(ModelEffect::Shadow))
        {
            double groundZ1 = groundZAt(i), groundZ2 = groundZAt(i + 1);
            double h1 = baseZAt(i) - groundZ1, h2 = baseZAt(i + 1) - groundZ2;
            AppendLineShadowSegment(p1, p2, halfWidth, h1, h2, groundZ1, groundZ2, zExag, origin, outMesh);
        }

        // The outline at the sides of the ribbon (see ModelOutlineMode) — a thin strip along each of the two edges of
        // the segment, slightly raised above the carriageway (kOutlineLift). The outward direction — by the
        // normalized offset of EACH vertex separately (not common per segment), otherwise at
        // mitered joints the strip would "sink" into the ribbon at one of the ends.
        if (outline != ModelOutlineMode::None)
        {
            QVector3D outlineColor = OutlineColor(color, outline);
            auto outwardAt = [&](const QPointF& off) -> QPointF
            {
                double l = std::hypot(off.x(), off.y());
                return l > 1e-9 ? QPointF(off.x() / l * kOutlineWidth, off.y() / l * kOutlineWidth) : QPointF();
            };
            QPointF out1 = outwardAt(off1), out2 = outwardAt(off2);
            float zo1 = z1 + float(kOutlineLift), zo2 = z2 + float(kOutlineLift);

            auto appendStrip = [&](const QVector3D& a1, const QVector3D& a2, const QPointF& o1, const QPointF& o2)
            {
                QVector3D b1(a1.x() + float(o1.x()), a1.y() + float(o1.y()), zo1);
                QVector3D b2(a2.x() + float(o2.x()), a2.y() + float(o2.y()), zo2);
                QVector3D a1r(a1.x(), a1.y(), zo1), a2r(a2.x(), a2.y(), zo2);
                quad(a1r, b1, b2, a2r, n3, outlineColor);
            };
            appendStrip(vL1, vL2, out1, out2);
            appendStrip(vR1, vR2, QPointF(-out1.x(), -out1.y()), QPointF(-out2.x(), -out2.y()));
        }
    }

    addSupports();
    if (effect.testFlag(ModelEffect::RadialGlow))
        AppendRadialGlowMarkers(line, /*closed=*/false, terrain, zExag, rel, lift, origin, ribbonColor, outMesh);
}

QVector3D ModelBuilder::OutlineColor(const QVector3D& objColor, ModelOutlineMode outline)
{
    if (outline == ModelOutlineMode::Light) return BrightenTowardWhite(objColor, kOutlineLightMix);
    if (outline == ModelOutlineMode::Dark) return objColor * (1.0f - kOutlineDarkMix);
    return objColor;
}

void ModelBuilder::AppendRingOutline(const QVector<QPointF>& ring, const QVector<double>& z, bool closed,
                                           const QVector3D& origin, const QVector3D& color, ModelMeshData& outMesh)
{
    int n = ring.size();
    if (n < 2) return;
    int lastEdge = closed ? n : n - 1;
    QVector3D n3(0, 0, 1);

    for (int i = 0; i < lastEdge; ++i)
    {
        int iN = (i + 1) % n;
        QPointF p1 = ring[i], p2 = ring[iN];
        QPointF dir = p2 - p1;
        double len = std::hypot(dir.x(), dir.y());
        if (len < 1e-6) continue;
        QPointF perp(-dir.y() / len * kOutlineWidth, dir.x() / len * kOutlineWidth);

        double z1 = (i < z.size() ? z[i] : 0.0) - origin.z() + kOutlineLift;
        double z2 = (iN < z.size() ? z[iN] : 0.0) - origin.z() + kOutlineLift;

        QVector3D v1(float(p1.x() - origin.x()), float(p1.y() - origin.y()), float(z1));
        QVector3D v2(float(p2.x() - origin.x()), float(p2.y() - origin.y()), float(z2));
        QVector3D v1o(v1.x() + float(perp.x()), v1.y() + float(perp.y()), v1.z());
        QVector3D v2o(v2.x() + float(perp.x()), v2.y() + float(perp.y()), v2.z());

        int i0 = outMesh.vertices.size();
        outMesh.vertices << v1 << v1o << v2o << v2;
        for (int k = 0; k < 4; ++k) { outMesh.normals << n3; outMesh.colors << color; }
        outMesh.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
        outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
    }
}

void ModelBuilder::AppendShadowBlob(const QVector<QPointF>& ring, double heightDrop,
                                          QVector<QVector<QPointF>>& outPieces)
{
    int n = ring.size();
    if (n < 3 || heightDrop <= 1e-9) return;

    // A flat projection shadow: we shift the outline of the top face of the object along kShadowLightDir until
    // it meets the terrain level under the object: from topZ + t*Lz = baseZ, heightDrop = topZ-baseZ,
    // we get t = heightDrop / (-Lz) (Lz<0, hence t>0), the XY shift — (t*Lx, t*Ly). The shadow length is
    // proportional to the real height of the object.
    double t = heightDrop / (-double(kShadowLightDir.z()));
    double ox = t * kShadowLightDir.x();
    double oy = t * kShadowLightDir.y();

    // The shadow under the object itself (the outline in its original position) and its projection shifted by (ox,oy) —
    // together with the "sweeping" side faces below they give the full shadow silhouette (not just a spot under
    // the object, but one stretched in the direction opposite to the light, as a shadow should look). Each
    // piece is a simple (possibly non-convex, but not self-intersecting) polygon; the traversal order does not
    // matter — BuildMergedShadowMesh() below deals with it through a boolean union (GEOS), to which the
    // outline orientation is irrelevant.
    outPieces << ring;
    QVector<QPointF> shifted;
    shifted.reserve(n);
    for (const QPointF& p : ring) shifted << QPointF(p.x() + ox, p.y() + oy);
    outPieces << shifted;

    for (int i = 0; i < n; ++i)
    {
        const QPointF& p1 = ring[i];
        const QPointF& p2 = ring[(i + 1) % n];
        outPieces << (QVector<QPointF>() << p1 << p2 << QPointF(p2.x() + ox, p2.y() + oy) << QPointF(p1.x() + ox, p1.y() + oy));
    }
}

double ModelBuilder::ShadowSurfaceLift(double zExag)
{
    return std::max(kShadowSurfaceLift, kMinExtrusionHeight * zExag + 0.1);
}

void ModelBuilder::AppendLineShadowSegment(const QPointF& p1, const QPointF& p2, double halfWidth,
                                                 double h1, double h2, double groundZ1, double groundZ2, double zExag,
                                                 const QVector3D& origin, ModelMeshData& outMesh)
{
    if (h1 < kShadowMinLineHeight && h2 < kShadowMinLineHeight) return;
    h1 = std::max(h1, 0.0);
    h2 = std::max(h2, 0.0);
    ModelGlowMeshData& shadow = outMesh.shadow;

    QPointF dir = p2 - p1;
    double len = std::hypot(dir.x(), dir.y());
    if (len < 1e-6) return;
    QPointF perp(-dir.y() / len * halfWidth, dir.x() / len * halfWidth);

    // The same shift along kShadowLightDir as in AppendShadowBlob() (see its docstring) — separately at
    // each end of the segment by its own height, so that the shadow narrows to zero where the ribbon
    // approaches the ground (the end with h=0 is not shifted at all — the shadow "grows" onto the foot).
    auto shiftFor = [](double h) -> QPointF
    {
        double t = h / (-double(kShadowLightDir.z()));
        return QPointF(t * kShadowLightDir.x(), t * kShadowLightDir.y());
    };
    QPointF s1 = shiftFor(h1), s2 = shiftFor(h2);

    // See AppendShadowBlob() — the same dynamic margin above the "roof" of honest flat slabs of zero
    // height at a high zExag, not only the fixed kShadowSurfaceLift.
    double surfaceLift = ShadowSurfaceLift(zExag);
    float z1 = float(groundZ1 + surfaceLift - origin.z());
    float z2 = float(groundZ2 + surfaceLift - origin.z());

    QVector3D vL1(float(p1.x() + perp.x() + s1.x() - origin.x()), float(p1.y() + perp.y() + s1.y() - origin.y()), z1);
    QVector3D vR1(float(p1.x() - perp.x() + s1.x() - origin.x()), float(p1.y() - perp.y() + s1.y() - origin.y()), z1);
    QVector3D vL2(float(p2.x() + perp.x() + s2.x() - origin.x()), float(p2.y() + perp.y() + s2.y() - origin.y()), z2);
    QVector3D vR2(float(p2.x() - perp.x() + s2.x() - origin.x()), float(p2.y() - perp.y() + s2.y() - origin.y()), z2);

    int i0 = shadow.vertices.size();
    shadow.vertices << vL1 << vR1 << vR2 << vL2;
    for (int k = 0; k < 4; ++k) shadow.colors << QVector4D(kLineShadowColor, kLineShadowAlpha);
    shadow.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
    shadow.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
}

void ModelBuilder::AppendSupportPillar(double x, double y, double radius, double zBottom, double zTop,
                                             const QVector3D& color, const QVector3D& origin, ModelMeshData& outMesh)
{
    if (zTop - zBottom < kSupportMinPillarHeight || radius <= 1e-6) return;

    float x0 = float(x - radius - origin.x()), x1 = float(x + radius - origin.x());
    float y0 = float(y - radius - origin.y()), y1 = float(y + radius - origin.y());
    float zb = float(zBottom - origin.z()), zt = float(zTop - origin.z());

    auto quad = [&](const QVector3D& a, const QVector3D& b, const QVector3D& c, const QVector3D& d, const QVector3D& n)
    {
        int i0 = outMesh.vertices.size();
        outMesh.vertices << a << b << c << d;
        for (int k = 0; k < 4; ++k) { outMesh.normals << n; outMesh.colors << color; }
        outMesh.indices << quint32(i0) << quint32(i0 + 1) << quint32(i0 + 2);
        outMesh.indices << quint32(i0) << quint32(i0 + 2) << quint32(i0 + 3);
    };
    // Four side faces (the normal outward) + the underside of the top cap (visible from below, from under an
    // overpass — from above it is covered by the object itself). We do not build the bottom — it stands on the terrain.
    quad(QVector3D(x0, y0, zb), QVector3D(x1, y0, zb), QVector3D(x1, y0, zt), QVector3D(x0, y0, zt), QVector3D(0, -1, 0));
    quad(QVector3D(x1, y0, zb), QVector3D(x1, y1, zb), QVector3D(x1, y1, zt), QVector3D(x1, y0, zt), QVector3D(1, 0, 0));
    quad(QVector3D(x1, y1, zb), QVector3D(x0, y1, zb), QVector3D(x0, y1, zt), QVector3D(x1, y1, zt), QVector3D(0, 1, 0));
    quad(QVector3D(x0, y1, zb), QVector3D(x0, y0, zb), QVector3D(x0, y0, zt), QVector3D(x0, y1, zt), QVector3D(-1, 0, 0));
    quad(QVector3D(x0, y0, zt), QVector3D(x1, y0, zt), QVector3D(x1, y1, zt), QVector3D(x0, y1, zt), QVector3D(0, 0, -1));

    ModelPillar p;
    p.x = x; p.y = y; p.radius = radius; p.zBottom = zBottom; p.zTop = zTop;
    outMesh.pillars << p;
}

void ModelBuilder::AppendSupports(const QVector<QPointF>& points, bool closed, const QVector<double>& baseZ,
                                        const QVector<double>& groundZ, double radius, double topGap,
                                        const QVector3D& color, const QVector3D& origin, ModelMeshData& outMesh,
                                        const ModelSupportSkipFn* skip)
{
    int n = points.size();
    if (n < 1 || baseZ.size() != n || groundZ.size() != n) return;

    // The steepness of the object surface around vertex i: over both neighboring segments we take the maximum
    // |dz|/length. The surface at a horizontal distance d from the pillar center differs in height
    // by up to slope*d, and the corner of a square pillar is at radius*sqrt(2) ~ 1.5*radius from the center — by
    // that much (plus topGap) we lower the top of the pillar under the object so that an inclined ribbon does not cut it.
    auto slopeAt = [&](int i) -> double
    {
        double best = 0.0;
        for (int d = -1; d <= 1; d += 2)
        {
            int j = i + d;
            if (closed) j = (j + n) % n;
            else if (j < 0 || j >= n) continue;
            double len = std::hypot(points[j].x() - points[i].x(), points[j].y() - points[i].y());
            if (len < 1e-6) continue;
            best = std::max(best, std::abs(baseZ[j] - baseZ[i]) / len);
        }
        return best;
    };

    double minDist = kSupportSpacingFactor * radius;
    QPointF last;
    bool haveLast = false;
    for (int i = 0; i < n; ++i)
    {
        if (baseZ[i] - groundZ[i] < kSupportMinHeight) continue;
        if (haveLast && std::hypot(points[i].x() - last.x(), points[i].y() - last.y()) < minDist) continue;
        // We do not place a pillar if there is another ribbon under it (see ModelSupportSkipFn) — otherwise it would stand on/
        // through it. last is not updated on a skip: the next suitable vertex is counted from
        // the last REALLY placed pillar.
        if (skip && (*skip)(points[i].x(), points[i].y(), radius, baseZ[i])) continue;
        double gap = topGap + slopeAt(i) * radius * 1.5;
        AppendSupportPillar(points[i].x(), points[i].y(), radius, groundZ[i], baseZ[i] - gap, color, origin, outMesh);
        last = points[i];
        haveLast = true;
    }
}

void ModelBuilder::AppendRadialGlowMarkers(const QVector<QPointF>& points, bool closed, const ModelTerrain* terrain,
                                                 double zExag, double rel, double lift, const QVector3D& origin,
                                                 const QVector3D& color, ModelMeshData& outMesh)
{
    Q_UNUSED(color);   // it used to imitate transparency by mixing with the background — now
                        // real alpha blending (see GlowFalloff()/ModelGlowMeshData), the color is not needed
    ModelGlowMeshData& glow = outMesh.glow;
    int n = points.size();
    if (n < 2) return;

    auto zAt = [&](const QPointF& p) -> double
    {
        double z = 0.0;
        if (terrain)
        {
            z = terrain->heightAt(p.x(), p.y());
            if (z <= -9998.0) z = 0.0;
            z *= zExag;
        }
        return z + (lift + rel) * zExag + kFlatLiftEpsilon + 0.1;   // slightly above the ribbon/cap itself so as not to flicker with it
    };
    // the total length of the outline/line — to place the "lamps" roughly evenly.
    int segCount = closed ? n : n - 1;
    QVector<double> segLen(segCount);
    double total = 0;
    for (int i = 0; i < segCount; ++i)
    {
        const QPointF& p1 = points[i];
        const QPointF& p2 = points[(i + 1) % n];
        segLen[i] = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        total += segLen[i];
    }
    if (total < 1e-6) return;

    double next = 0.0;
    double travelled = 0.0;
    for (int i = 0; i < segCount; ++i)
    {
        const QPointF& p1 = points[i];
        const QPointF& p2 = points[(i + 1) % n];
        double len = segLen[i];
        if (len < 1e-9) continue;
        while (next <= travelled + len)
        {
            double t = (next - travelled) / len;
            QPointF p(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t);
            float z = float(zAt(p) - origin.z());

            // A fan of kGlowRings concentric rings from a bright semi-transparent center to a fully
            // transparent outer edge — real alpha blending (see ModelGlowMeshData), not an imitation:
            // the vertex color is interpolated together with the alpha (GlowFalloff() — a quadratic falloff,
            // not linear, see its docstring). A separate buffer (glow), not outMesh.vertices/colors —
            // drawn by a separate translucent entity, see the tqt 3D window, BuildAlphaMeshEntity().
            int centerIdx = glow.vertices.size();
            glow.vertices << QVector3D(float(p.x() - origin.x()), float(p.y() - origin.y()), z);
            glow.colors << QVector4D(kGlowCenterColor, kGlowCenterAlpha);

            int prevRingBase = -1;   // -1 — the previous "ring" is the center itself (a single point)
            for (int ring = 1; ring <= kGlowRings; ++ring)
            {
                float t = float(ring) / float(kGlowRings);
                double radius = kGlowRadius * t;
                QVector4D ringColor = GlowFalloff(t);

                int ringBase = glow.vertices.size();
                for (int s = 0; s < kGlowSegments; ++s)
                {
                    double a = (2.0 * M_PI * s) / kGlowSegments;
                    double rx = p.x() + radius * std::cos(a);
                    double ry = p.y() + radius * std::sin(a);
                    glow.vertices << QVector3D(float(rx - origin.x()), float(ry - origin.y()), z);
                    glow.colors << ringColor;
                }

                if (prevRingBase < 0)
                {
                    for (int s = 0; s < kGlowSegments; ++s)
                        glow.indices << quint32(centerIdx) << quint32(ringBase + s) << quint32(ringBase + (s + 1) % kGlowSegments);
                }
                else
                {
                    for (int s = 0; s < kGlowSegments; ++s)
                    {
                        int p0 = prevRingBase + s, p1 = prevRingBase + (s + 1) % kGlowSegments;
                        int r0 = ringBase + s, r1 = ringBase + (s + 1) % kGlowSegments;
                        glow.indices << quint32(p0) << quint32(r0) << quint32(r1);
                        glow.indices << quint32(p0) << quint32(r1) << quint32(p1);
                    }
                }
                prevRingBase = ringBase;
            }

            next += kGlowSpacing;
        }
        travelled += len;
    }
}

QVector3D ModelBuilder::ResolveObjectColor(const ModelSettings::VectorSource& vs, MapFeature* obj,
                                                 double rel, double relMin, double relMax, int colorIndex,
                                                 QStringList* warnings, bool* classifierFallbackWarned)
{
    QVector3D defaultColor = kBuildingPalette[colorIndex % kBuildingPaletteSize];

    switch (vs.colorMode)
    {
        case ModelColorMode::SingleColor:
            return QVector3D(vs.singleColor.redF(), vs.singleColor.greenF(), vs.singleColor.blueF());

        case ModelColorMode::GradientByHeight:
        {
            QVector3D gradLow(vs.gradientLowColor.redF(), vs.gradientLowColor.greenF(), vs.gradientLowColor.blueF());
            QVector3D gradHigh(vs.gradientHighColor.redF(), vs.gradientHighColor.greenF(), vs.gradientHighColor.blueF());
            double range = std::max(1e-6, relMax - relMin);
            float t = float((rel - relMin) / range);
            t = std::max(0.0f, std::min(1.0f, t));
            return gradLow + (gradHigh - gradLow) * t;
        }

        case ModelColorMode::ByClassifier:
        {   // coloring by a classifier is not supported in tMC (there is no classifier): the default color + one warning per layer
            if (warnings && classifierFallbackWarned && !*classifierFallbackWarned)
            {
                *warnings << QObject::tr("Layer '%1': coloring by a classifier is not supported in tMC — the default color is used.")
                                 .arg(vs.layer ? vs.layer->name : QString());
                *classifierFallbackWarned = true;
            }
            return defaultColor;
        }
    }
    return defaultColor;
}

bool ModelBuilder::IntersectsLoadExtent(const MapFeature* obj, bool hasExtent, const QRectF& extent)
{
    if (!hasExtent) return true;
    // obj->xmin/xmax/ymin/ymax — the bounding box already computed when the object was loaded (see tqt),
    // an ordinary rectangle-in-rectangle intersection without touching the geometry itself.
    return obj->xmax >= extent.left() && obj->xmin <= extent.right() &&
           obj->ymax >= extent.top()  && obj->ymin <= extent.bottom();
}

bool ModelBuilder::BuildSingleObjectMesh(MapFeature* obj, const ModelSettings::VectorSource& vs,
                                               const ModelTerrain* terrain, double zExag, const QVector3D& origin,
                                               int colorIndex, double relMin, double relMax,
                                               bool hasLoadExtent, const QRectF& loadExtent,
                                               ModelMeshData& outMesh, double* outRel)
{
    if (!obj || !obj->g || !vs.layer) return false;
    if (!IntersectsLoadExtent(obj, hasLoadExtent, loadExtent)) return false;

    double rel;
    bool honestZero = false;
    if (vs.noExtrusion)
    {
        rel = 0.0;
    }
    else
    {
        int fieldNo = vs.layer->fieldIndex(vs.heightField);
        if (fieldNo < 0) return false;

        bool ok = false;
        rel = obj->fields.value(fieldNo).toDouble(&ok) * vs.heightMultiplier;
        if (!ok) return false;
    }
    rel += vs.extraExtrusion;   // see CollectLayerCandidates()
    if (!vs.noExtrusion && rel == 0.0) { rel = kMinExtrusionHeight; honestZero = true; }

    // See CollectLayerCandidates(): an honest zero in the data — no shadow, even with an artificially raised rel.
    ModelEffects effect = vs.effect;
    if (honestZero) effect &= ~ModelEffects(ModelEffect::Shadow);

    // The absolute height: the field value * the multiplier + the addition (VectorSource::liftAdd, works without a field too).
    double lift = vs.liftAdd;
    if (!vs.liftField.isEmpty())
    {
        int liftFieldNo = vs.layer->fieldIndex(vs.liftField);
        if (liftFieldNo >= 0) lift += obj->fields.value(liftFieldNo).toDouble() * vs.liftMultiplier;
    }

    // The convergence of the lift at junctions (see ComputeObjectJunctionLifts()) — without it a live update of a single
    // object (see the tqt 3D window, rebuildObjectEntity()) would roll it back to its flat "own" lift,
    // losing the blending computed at the last full build (CollectLayerCandidates()).
    QVector<QPair<double, double>> junctionLifts = ComputeObjectJunctionLifts(obj, vs);

    // Supports (ModelEffect::Supports) are not placed for a ribbon if under the pillar point there is already another ribbon
    // OF THE SAME layer lying noticeably lower (the pillar would stand on it/through it): we go over the other objects
    // of the layer (a rejection by bbox with a margin for the ribbon width and the radius), take the Z of their ribbon at this point
    // (EvaluateLineObjectZ without recomputing junctions — precision is not needed here, and a full scan of the layer on every
    // call would be too expensive).
    ModelSupportSkipFn supportSkip;
    if (effect.testFlag(ModelEffect::Supports))
    {
        double halfWidth = vs.lineWidth * 0.5;
        supportSkip = [&, obj, halfWidth](double x, double y, double radius, double baseZ) -> bool
        {
            double reach = halfWidth + radius;
            for (MapFeature* other : vs.layer->features)
            {
                if (!other || other == obj || !other->g) continue;
                if (x + reach < other->xmin || x - reach > other->xmax ||
                    y + reach < other->ymin || y - reach > other->ymax) continue;
                double dist = 0.0, z = 0.0;
                if (!EvaluateLineObjectZ(other, vs, terrain, zExag, x, y, &dist, &z, nullptr, /*ignoreJunctions=*/true))
                    continue;   // not a line
                if (dist <= reach && z < baseZ - kSupportClearBelow) return true;
            }
            return false;
        };
    }

    QVector3D color = ResolveObjectColor(vs, obj, rel, relMin, relMax, colorIndex, nullptr, nullptr);
    bool built = BuildObjectMesh(obj, rel, lift, vs.extraExtrusion, terrain, zExag, origin, color, effect,
                                  vs.lineWidth * 0.5, vs.outline, outMesh, junctionLifts, -1, nullptr,
                                  supportSkip ? &supportSkip : nullptr);
    if (built && outRel) *outRel = rel;
    return built;
}

QVector<QPair<double, double>> ModelBuilder::ComputeObjectJunctionLifts(
    MapFeature* obj, const ModelSettings::VectorSource& vs, QVector<MapFeature*>* outNeighbors)
{
    QVector<QPair<double, double>> result;
    if (outNeighbors) outNeighbors->clear();
    if (!obj || !vs.layer || vs.liftField.isEmpty()) return result;

    int liftFieldNo = vs.layer->fieldIndex(vs.liftField);
    if (liftFieldNo < 0) return result;

    auto liftOf = [&](MapFeature* o) -> double
    {
        return o->fields.value(liftFieldNo).toDouble() * vs.liftMultiplier + vs.liftAdd;
    };

    QVector<QVector<QPointF>> lines;
    if (!ExtractLineStrings(obj, lines)) return result;   // a polygon — lift junctions are not applicable

    auto snapKey = [](const QPointF& p) -> QPair<qint64, qint64>
    {
        return qMakePair(qint64(std::llround(p.x() / kJunctionSnapEpsilon)),
                          qint64(std::llround(p.y() / kJunctionSnapEpsilon)));
    };

    QSet<QPair<qint64, qint64>> ownEndpointKeys;
    for (const QVector<QPointF>& line : lines)
    {
        if (line.size() < 2) continue;
        ownEndpointKeys.insert(snapKey(line.first()));
        ownEndpointKeys.insert(snapKey(line.last()));
    }
    if (ownEndpointKeys.isEmpty()) return result;

    // We walk the WHOLE layer (like CollectLayerCandidates() in the full build, but here only for the junction
    // points of this one object) — in terms of network size a live edit is a one-off and cheap, it is called
    // only on the fact of adding/replacing obj, not on every frame.
    struct Acc { double sumLift = 0.0; QSet<MapFeature*> objs; };
    QHash<QPair<qint64, qint64>, Acc> acc;
    for (MapFeature* other : vs.layer->features)
    {
        if (!other || !other->g) continue;
        QVector<QVector<QPointF>> otherLines;
        if (!ExtractLineStrings(other, otherLines)) continue;
        for (const QVector<QPointF>& ol : otherLines)
        {
            if (ol.size() < 2) continue;
            for (const QPointF& ep : { ol.first(), ol.last() })
            {
                auto key = snapKey(ep);
                if (!ownEndpointKeys.contains(key)) continue;   // not a junction point of obj — not interesting
                Acc& a = acc[key];
                if (!a.objs.contains(other))
                {
                    a.objs.insert(other);
                    a.sumLift += liftOf(other);
                    if (outNeighbors && other != obj) *outNeighbors << other;
                }
            }
        }
    }

    double ownLift = liftOf(obj);
    auto avgAt = [&](const QPointF& p) -> double
    {
        auto it = acc.constFind(snapKey(p));
        if (it == acc.constEnd() || it->objs.size() < 2) return ownLift;
        return it->sumLift / it->objs.size();
    };
    result.reserve(lines.size());
    for (const QVector<QPointF>& line : lines)
    {
        if (line.size() < 2) { result << qMakePair(ownLift, ownLift); continue; }
        result << qMakePair(avgAt(line.first()), avgAt(line.last()));
    }
    return result;
}

bool ModelBuilder::EvaluateLineObjectZ(MapFeature* obj, const ModelSettings::VectorSource& vs,
                                             const ModelTerrain* terrain, double zExag, double worldX, double worldY,
                                             double* outDistance, double* outZ, double* outGroundZ,
                                             bool ignoreJunctions)
{
    if (!obj || !vs.layer) return false;

    QVector<QVector<QPointF>> lines;
    QVector<QVector<double>> linesZ;
    if (!ExtractLineStrings(obj, lines, &linesZ)) return false;   // not a line — not our case

    int liftFieldNo = vs.liftField.isEmpty() ? -1 : vs.layer->fieldIndex(vs.liftField);
    double ownLift = vs.liftAdd + (liftFieldNo >= 0 ? obj->fields.value(liftFieldNo).toDouble() * vs.liftMultiplier : 0.0);

    double rel;
    if (vs.noExtrusion) rel = 0.0;
    else
    {
        int fieldNo = vs.layer->fieldIndex(vs.heightField);
        bool ok = false;
        rel = fieldNo >= 0 ? obj->fields.value(fieldNo).toDouble(&ok) : 0.0;
        if (!ok) rel = 0.0;
        rel *= vs.heightMultiplier;
    }
    rel += vs.extraExtrusion;

    // ignoreJunctions — we skip the full scan of the layer for junctions (see the parameter in the .h): for a rough
    // estimate of "a ribbon somewhere noticeably lower" (supports) the object's own lift is enough.
    QVector<QPair<double, double>> junctionLifts;
    if (!ignoreJunctions) junctionLifts = ComputeObjectJunctionLifts(obj, vs);
    static const double kNaN = std::numeric_limits<double>::quiet_NaN();

    bool any = false;
    double bestDist = 0.0, bestZ = 0.0, bestGroundZ = 0.0;

    for (int li = 0; li < lines.size(); ++li)
    {
        const QVector<QPointF>& line = lines[li];
        int n = line.size();
        if (n < 2) continue;

        double liftStart = kNaN, liftEnd = kNaN;
        if (li < junctionLifts.size()) { liftStart = junctionLifts[li].first; liftEnd = junctionLifts[li].second; }
        QVector<double> vertexLift = ComputeVertexLift(line, ownLift, liftStart, liftEnd);
        const QVector<double>& geomZ = li < linesZ.size() ? linesZ[li] : QVector<double>();

        for (int i = 0; i + 1 < n; ++i)
        {
            QPointF p1 = line[i], p2 = line[i + 1];
            QPointF dir = p2 - p1;
            double len2 = dir.x() * dir.x() + dir.y() * dir.y();
            double t = 0.0;
            if (len2 > 1e-12)
            {
                t = ((worldX - p1.x()) * dir.x() + (worldY - p1.y()) * dir.y()) / len2;
                t = std::max(0.0, std::min(1.0, t));
            }
            QPointF proj(p1.x() + dir.x() * t, p1.y() + dir.y() * t);
            double dist = std::hypot(worldX - proj.x(), worldY - proj.y());
            if (any && dist >= bestDist) continue;

            double terrainZ = 0.0;
            if (terrain)
            {
                terrainZ = terrain->heightAt(proj.x(), proj.y());
                if (terrainZ <= -9998.0) terrainZ = 0.0;
            }
            double gz1 = i < geomZ.size() ? geomZ[i] : 0.0;
            double gz2 = (i + 1) < geomZ.size() ? geomZ[i + 1] : 0.0;
            double gz = gz1 + (gz2 - gz1) * t;
            double vl = vertexLift[i] + (vertexLift[i + 1] - vertexLift[i]) * t;

            bestDist = dist;
            bestZ = (terrainZ + gz + vl + rel) * zExag;
            bestGroundZ = terrainZ * zExag;
            any = true;
        }
    }

    if (!any) return false;
    if (outDistance) *outDistance = bestDist;
    if (outZ) *outZ = bestZ;
    if (outGroundZ) *outGroundZ = bestGroundZ;
    return true;
}

void ModelBuilder::MergeHolesIntoRing(const QVector<QPointF>& outer, const QVector<double>& outerZ,
                                            const QVector<QVector<QPointF>>& holes,
                                            const QVector<QVector<double>>& holesZ,
                                            QVector<QPointF>& outRing, QVector<double>& outZ)
{
    outRing = outer;
    outZ = outerZ;
    if (holes.isEmpty()) return;

    auto cross = [](const QPointF& o, const QPointF& a, const QPointF& b) -> double
    {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    auto pointInTriangle = [&](const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) -> bool
    {
        double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    };
    auto shoelaceCcw = [](const QVector<QPointF>& r) -> bool
    {
        double area2 = 0;
        int rn = r.size();
        for (int i = 0; i < rn; ++i)
        {
            const QPointF& p1 = r[i];
            const QPointF& p2 = r[(i + 1) % rn];
            area2 += p1.x() * p2.y() - p2.x() * p1.y();
        }
        return area2 > 0;
    };
    // The keyhole stitching (see the loop below) inserts the hole points "as is", in the outline order — this gives a
    // simple (without self-intersections) polygon ONLY if the hole is traversed in the direction
    // opposite to the outer ring (the standard OGC/right-hand rule convention: the outer outline and
    // holes have opposite orientation). GeoJSON formally requires exactly that, but real data
    // does not always comply (the same orientation of both rings is a common case) —
    // if a hole is traversed in THE SAME direction as the outer outline, the stitched outline becomes
    // self-intersecting, and ear-clipping produces garbage (junk triangles over the whole hole,
    // not a cutout). So we normalize the hole orientation to the outer outline here and do not rely on
    // the data.
    bool outerCcw = shoelaceCcw(outer);

    for (int hIdx = 0; hIdx < holes.size(); ++hIdx)
    {
        QVector<QPointF> hole = holes[hIdx];
        QVector<double> hz = holesZ.value(hIdx);
        if (hole.size() < 3) continue;
        if (shoelaceCcw(hole) == outerCcw)
        {
            std::reverse(hole.begin(), hole.end());
            std::reverse(hz.begin(), hz.end());
        }

        // The rightmost point of the hole — the entry point of the bridge on the hole side (the classic choice for
        // the "keyhole" stitching technique, see the docstring in the .h).
        int hStart = 0;
        for (int i = 1; i < hole.size(); ++i)
            if (hole[i].x() > hole[hStart].x()) hStart = i;
        QPointF H = hole[hStart];

        // The nearest-in-X intersection of the horizontal ray from H to the right with the edges of the CURRENT (already,
        // possibly stitched with previous holes) outline.
        int n = outRing.size();
        int bestEdge = -1;
        double bestX = std::numeric_limits<double>::max();
        QPointF bestPoint;
        for (int i = 0; i < n; ++i)
        {
            QPointF a = outRing[i], b = outRing[(i + 1) % n];
            if ((a.y() > H.y()) == (b.y() > H.y())) continue;   // the edge does not cross the horizontal y=H.y
            double t = (H.y() - a.y()) / (b.y() - a.y());
            double x = a.x() + t * (b.x() - a.x());
            if (x >= H.x() - 1e-9 && x < bestX) { bestX = x; bestEdge = i; bestPoint = QPointF(x, H.y()); }
        }
        if (bestEdge < 0) continue;   // the hole is outside the outline/degenerate — we do not build a bridge, we simply do not cut the hole

        QPointF a = outRing[bestEdge], b = outRing[(bestEdge + 1) % n];
        int mIdx = (a.x() > b.x()) ? bestEdge : (bestEdge + 1) % n;
        QPointF M = outRing[mIdx];

        // If inside the control triangle (H, the intersection point, M) there are other vertices
        // of the outline — a bridge to the nearest of them by angle from H will certainly not cross other edges (the same
        // technique as in earcut.js/eliminateHole).
        auto samePointM = [](const QPointF& p1, const QPointF& p2) -> bool
        {
            return std::abs(p1.x() - p2.x()) < 1e-6 && std::abs(p1.y() - p2.y()) < 1e-6;
        };
        double bestAngle = std::numeric_limits<double>::max();
        for (int i = 0; i < n; ++i)
        {
            if (i == mIdx) continue;
            const QPointF& P = outRing[i];
            // Points coinciding in coordinates with the current M are duplicates of the bridge of a hole already stitched earlier
            // (see the closing repeat outRing[mIdx] at the end of the function): pointInTriangle() because of
            // them lands exactly on the vertex M of the control triangle and formally passes the
            // "inside" check. Redirecting the bridge TO such a point makes no sense (a bridge into itself) and in
            // practice it joins the bridges of two different holes at one point — the outline stops being simple, and
            // ear-clipping at that place either errs or falls back to the fan fallback,
            // visually "sealing" the area of the holes back. We skip such points — they have nothing to offer
            // in exchange for the already found M.
            if (samePointM(P, M)) continue;
            if (!pointInTriangle(P, H, bestPoint, M)) continue;
            double angle = std::abs(std::atan2(double(P.y() - H.y()), double(P.x() - H.x())));
            if (angle < bestAngle) { bestAngle = angle; M = P; mIdx = i; }
        }

        double mZ = outZ.value(mIdx, 0.0);
        double hZAt = hStart < hz.size() ? hz[hStart] : 0.0;

        QVector<QPointF> merged;
        QVector<double> mergedZ;
        merged.reserve(n + hole.size() + 2);
        mergedZ.reserve(n + hole.size() + 2);
        for (int i = 0; i <= mIdx; ++i) { merged << outRing[i]; mergedZ << outZ.value(i, 0.0); }
        for (int k = 0; k <= hole.size(); ++k)
        {
            int hi = (hStart + k) % hole.size();
            merged << hole[hi];
            mergedZ << (hi < hz.size() ? hz[hi] : hZAt);
        }
        merged << outRing[mIdx];
        mergedZ << mZ;
        for (int i = mIdx + 1; i < n; ++i) { merged << outRing[i]; mergedZ << outZ.value(i, 0.0); }

        outRing = merged;
        outZ = mergedZ;
    }
}

bool ModelBuilder::TriangulatePolygon(const QVector<QPointF>& ring, QVector<int>& outTris,
                                            bool hasBridgeDuplicates, qint64 budgetMs, bool* outGaveUp)
{
    if (outGaveUp) *outGaveUp = false;
    outTris.clear();
    int n = ring.size();
    if (n < 3) return false;
    if (n == 3) { outTris << 0 << 1 << 2; return true; }

    // we determine the orientation of the outline (the sign of the area), ear-clipping needs to know the "convex" sign.
    double area2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF& p1 = ring[i];
        const QPointF& p2 = ring[(i + 1) % n];
        area2 += p1.x() * p2.y() - p2.x() * p1.y();
    }
    bool ccw = area2 > 0;

    auto cross = [](const QPointF& o, const QPointF& a, const QPointF& b) -> double
    {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    auto pointInTriangle = [&](const QPointF& p, const QPointF& a, const QPointF& b, const QPointF& c) -> bool
    {
        double d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    };

    // The outline may contain duplicate vertices with exactly coinciding coordinates — for example the bridge
    // vertex of the keyhole stitching of holes (see MergeHolesIntoRing()) deliberately appears in ring twice.
    // A duplicate of an ear's own vertex always lies exactly ON its boundary — pointInTriangle() classifies
    // such a point as "inside" (the boundary is included), which without this check would forever block a valid
    // ear around any such vertex and lead to a fallback to the crude fallback (a fan triangulation),
    // visually cutting the polygon corners. BUT duplicates are in principle possible only for an outline stitched from
    // holes (hasBridgeDuplicates==true, see the calling code) — for an ordinary outline without holes (the overwhelming
    // majority of objects) there cannot be any, while the check itself (samePoint) adds a noticeable share of
    // cost in the inner ear-clipping loop. So we check only when it makes any
    // sense at all — for simple objects samePoint() below is not even called (a short circuit
    // hasBridgeDuplicates && ...).
    auto samePoint = [](const QPointF& p1, const QPointF& p2) -> bool
    {
        return std::abs(p1.x() - p2.x()) < 1e-6 && std::abs(p1.y() - p2.y()) < 1e-6;
    };

    // Ear-clipping as a whole is O(n) "ears" × O(n) ear candidates × O(n) checks "are there other vertices inside the
    // ear" = cubic complexity in the number of outline vertices. For ordinary buildings (tens of
    // vertices) this is microseconds, but for structures with a large number of holes (many windows, etc.) the contour after
    // stitching the holes with bridges (see MergeHolesIntoRing()) can grow to many hundreds of vertices, and
    // the cubic cost on such outlines is noticeable. There used to be an emergency ceiling of 300 vertices here
    // (above it — immediately a cheap but visually corner-cutting fan fallback), while building the geometry
    // when loading new objects was synchronous on the GUI thread (see the class docstring there); now
    // that this build has moved entirely to a background thread (see the tqt 3D window, extendLoadedExtentIfNeeded()),
    // the GUI thread no longer feels this cost — the ceiling is removed, an honest triangulation is always built,
    // regardless of the number of outline vertices.

    QVector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i) idx << i;

    // A protective time budget (see the docstring in the .h) — for an outline that is expensive not so much by
    // the number of vertices (those are now not built synchronously anyway, see kLightVertexThreshold in
    // ModelBuilder::Build()) as by its very shape (nearly degenerate faces, many nearly
    // collinear points, etc.),
    // because of which EVERY attempt to find an "ear" is unexpectedly expensive. We start the timer and check it not
    // on every iteration (the call to QElapsedTimer::elapsed() itself is not free), but once per kBudgetCheckStride
    // iterations — with budgetMs < 0 the check is fully off, no overhead on ordinary
    // (non-budgeted) calls.
    QElapsedTimer budgetTimer;
    if (budgetMs >= 0) budgetTimer.start();
    const int kBudgetCheckStride = 64;

    int maxIter = n * n + 8;
    int iter = 0;
    while (idx.size() > 3 && iter++ < maxIter)
    {
        if (budgetMs >= 0 && (iter % kBudgetCheckStride) == 0 && budgetTimer.elapsed() > budgetMs)
        {
            // We do not fit into the budget — immediately a fan fallback for everything that remains, and a signal
            // to the calling code that this outline turned out to be unexpectedly expensive (see the docstring in the .h).
            for (int k = 1; k + 1 < idx.size(); ++k)
                outTris << idx[0] << idx[k] << idx[k + 1];
            idx.clear();
            if (outGaveUp) *outGaveUp = true;
            break;
        }

        bool clipped = false;
        int m = idx.size();
        for (int k = 0; k < m; ++k)
        {
            int ip = idx[(k - 1 + m) % m], ic = idx[k], in = idx[(k + 1) % m];
            const QPointF& a = ring[ip]; const QPointF& b = ring[ic]; const QPointF& c = ring[in];

            double cr = cross(a, b, c);
            bool convex = ccw ? (cr > 0) : (cr < 0);
            if (!convex) continue;

            bool anyInside = false;
            for (int j = 0; j < m; ++j)
            {
                int oi = idx[j];
                if (oi == ip || oi == ic || oi == in) continue;
                const QPointF& p = ring[oi];
                if (hasBridgeDuplicates && (samePoint(p, a) || samePoint(p, b) || samePoint(p, c))) continue;
                if (pointInTriangle(p, a, b, c)) { anyInside = true; break; }
            }
            if (anyInside) continue;

            outTris << ip << ic << in;
            idx.remove(k);
            clipped = true;
            break;
        }
        if (!clipped)
        {
            // a self-intersecting/degenerate outline — we could not find a valid "ear": a fallback to
            // a fan triangulation from the first remaining vertex (best-effort, we do not block the build).
            for (int k = 1; k + 1 < idx.size(); ++k)
                outTris << idx[0] << idx[k] << idx[k + 1];
            idx.clear();
            break;
        }
    }
    if (idx.size() == 3) outTris << idx[0] << idx[1] << idx[2];
    return !outTris.isEmpty();
}
