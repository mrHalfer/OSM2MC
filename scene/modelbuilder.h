#ifndef WDF3DMODELBUILDER_H
#define WDF3DMODELBUILDER_H

#include <QVector>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QColor>
#include <QFlags>
#include <QPair>
#include <QVariant>
#include <limits>
#include <functional>
#include "scene/modeldata.h"

//! A support placement filter (the "Supports" effect, see ModelBuilder::AppendSupports()): (x, y, radius,
//! baseZ) — the world coordinates of the future pillar, its half-side and the height of the object bottom above it;
//! true — the pillar must not be placed.
using ModelSupportSkipFn = std::function<bool(double, double, double, double)>;

class HeightRaster;
class MapLayer;
class MapFeature;

//! Terrain from one or several height matrices (a mosaic, e.g. the sheets of one area): the height at a point is
//! taken from the first matrix of the list that covers it and has a valid value there. heightAt() behaves the same
//! as HeightRaster::heightAt() (including -9999 outside all matrices), so all the 3D code works with it the same
//! way it used to with a single matrix. A light object (a pointer list + the common extent) — it can be copied into
//! background tasks; the matrices themselves belong to the map.
class ModelTerrain
{
public:
    ModelTerrain() = default;
    explicit ModelTerrain(const QVector<HeightRaster*>& layers);

    bool isEmpty() const { return mLayers.isEmpty(); }
    const QVector<HeightRaster*>& layers() const { return mLayers; }

    //! The common extent of all matrices (world coordinates).
    double xB() const { return mXB; }
    double xE() const { return mXE; }
    double yB() const { return mYB; }
    double yE() const { return mYE; }

    double heightAt(double x, double y) const;

private:
    QVector<HeightRaster*> mLayers;
    double mXB = 0, mXE = 0, mYB = 0, mYE = 0;
};

//! The coloring mode of the extruded objects of one vector layer.
enum class ModelColorMode
{
    SingleColor,      //!< One color for the whole layer, from a fixed palette (by the index of the source layer)
    GradientByHeight,  //!< A gradient between two colors by the height of the object itself (the REL field value)
    ByClassifier       //!< The object fill color from the layer classifier (by the object code), see wdfLayClassifier
};

//! The palette for coloring the terrain by height — the same variants as for the automatic coloring of height
//! matrices in 2D (see HeightRaster::grayscalePalette()/GetCutter()), so that the 3D terrain can be
//! configured in the familiar way, not only with a fixed color gradient.
enum class ModelTerrainPalette
{
    Grayscale,   //!< Shades of gray: from dark (the minimum height) to light (the maximum)
    Spectrum,    //!< "Spectrum" — a 5-color gradient red->yellow->green->light blue->blue (as in 2D, cutter=false)
    Special      //!< "Special" — a 9-color hypsometric gradient blue->green->yellow->orange->brick (as in 2D by default, cutter=true)
};

//! An "effect" of decorating the objects of one vector source — purely visual additions, all of them
//! implemented as cheaply as possible: fully baked right into the geometry vertices at build time
//! (see ModelBuilder::BuildObjectMesh()/BuildLineRibbonMesh()), without any materials/
//! shaders/periodic buffer animation — that variant (Ripple/Twinkle, CPU animation on a timer)
//! was tried and removed: the animation gave only a cosmetic effect at the price of a steady per-frame load
//! (the number of QBuffer::updateData() calls per tick grew with the number of markers instead of shrinking from merging them into
//! a common buffer, see the history) — a static version of the same visual effect costs nothing at all. None of the
//! effects is an "honest" implementation of its name (there is no real shadow mapping/bloom/Fresnel
//! shader/street lamps here) — it is a deliberate static imitation (a "fake").
//! Bit flags (not a variant index!) — the effects can be combined (for example Shadow|Glow
//! at once), see ModelEffects/CheckableComboBox.
enum class ModelEffect
{
    None       = 0,       //!< No effect (the default)
    Shadow     = 1 << 0,  //!< Shadow — an imitation: a darkened shifted copy of the footprint on the terrain under the object
    Glow       = 1 << 1,  //!< Glow — a static "fake" rim light: a brightened top edge of the walls
    RadialGlow = 1 << 2,  //!< Lamps — static radial light spots (brighter in the center, darker at the edge) at equal
                           //!< intervals along an outline/line — for example street lamps along a road
    OutlineLight = 1 << 4,  //!< A light outline (see ModelOutlineMode::Light) — moved to "Effects" from a separate column
    OutlineDark  = 1 << 5,  //!< A dark outline (see ModelOutlineMode::Dark); mutually exclusive with the light one
    Supports   = 1 << 6,  //!< Supports — pillars from the ground to a raised object (road ribbons/polygons with a lift) at
                           //!< every actual point, with a body for collision in "Explore", see
                           //!< ModelBuilder::AppendSupportPillar()
    Swamp      = 1 << 7,  //!< Swamp biome — an exporter mark (tMC): the whole layer's outline turns the ground under it
                           //!< into a swamp biome (the trees of any forest under the contour become swamp oaks with
                           //!< vines; open swamp ground gets rare oaks, mushrooms and dead branches). The 3D window
                           //!< does not draw the effect — the layer is shown by its ordinary color
    Rounded    = 1 << 3   //!< Smoothing — the top edge of polygon walls is rounded with a static chamfer
                           //!< (several quarter-circle sub-rings in section, see
                           //!< ModelBuilder::BuildObjectMesh()) instead of a sharp wall/roof corner —
                           //!< not applied to lines (BuildLineRibbonMesh()), they have no top polygon edge
};
Q_DECLARE_FLAGS(ModelEffects, ModelEffect)
Q_DECLARE_OPERATORS_FOR_FLAGS(ModelEffects)

//! An outline of the object faces (the top polygon edge / the side edges of a linear ribbon) — a thin strip
//! of the same color as the object itself, but darker/lighter (see ModelBuilder::AppendRingOutline()).
enum class ModelOutlineMode
{
    None,   //!< No outline (the default)
    Light,  //!< A light outline (the object color mixed with white)
    Dark    //!< A dark outline (the object color mixed with black)
};

//! The way of specifying the radio signal absorption by the buildings of one vector source (see
//! ModelSettings::VectorSource::absorptionMode) — extra signal attenuation in the shadow of this building when
//! computing the radio coverage (see RadioModel/RadioBuilding::absorption), dB, instead of a single value
//! for all buildings at once (RadioModel::kNlosLoss).
enum class ModelAbsorptionMode
{
    None,             //!< "None": the layer does not take part in the radio coverage calculation at all — it is not a building but, for example,
                       //!< grass/bushes/a fence; its objects are not read and do not affect the shadow of other buildings in any way
    MaterialColumn,   //!< "Material from a column": the column value (the object material/code) is looked up in the correspondence table
    ColumnValue,      //!< "Absolute value from a column": a ready numeric value (dB) is taken directly from the object column
    PerLayer          //!< "Absolute value for the layer": the same value (dB) for all buildings of this layer
};

//! The time of day of the whole scene (not of a single object) — controls the lighting/background (see
//! the tqt 3D window, setModel()), not the geometry: Glow and especially Lamps (RadialGlow) are almost
//! unreadable on a bright daytime background under full lighting, while at night, on the contrary, they are just right — this
//! switch is exactly about having somewhere to show them.
enum class ModelTimeOfDay
{
    Day,    //!< A light background, full brightness of the directional light (the default)
    Night   //!< A dark background, a subdued cold light — Glow/RadialGlow stand out noticeably against the general background
};

/*!
 * \file   modelbuilder.h
 * \brief  Building the 3D model (terrain from a raster + extruded polygons from vectors).
 *
 * All the math is free of QWidget/Qt3D dependencies, so Build() can safely run in a background
 * thread through QtConcurrent::run() (see MainWindow::Build3DModelAsync()). The result is plain
 * ModelData; the real Qt3D entities are built separately, on the GUI thread
 * (see the tqt 3D window, setModel()).
 */

//! The settings for building a 3D model, collected from the tqt 3D dialog.
struct ModelSettings
{
    //! The raster height-matrix layers used as terrain (one or several, see ModelTerrain).
    //! Optional — if the list is empty, a flat "ground" (z=0) is built under the extruded objects.
    QVector<HeightRaster*> terrainLayers;

    //! The vertex budget of the terrain grid (decimation of a large DTM) — see ModelBuilder::BuildTerrainMesh().
    int maxTerrainVertices = 500000;

    //! The height multiplier (for the terrain and for the extrusion) — a visual exaggeration of the relief.
    double zExaggeration = 1.0;

    //! The palette for coloring the terrain by height — see ModelTerrainPalette.
    ModelTerrainPalette terrainPalette = ModelTerrainPalette::Grayscale;

    //! The time of day of the whole scene — see ModelTimeOfDay. Affects only the lighting/background in
    //! the tqt 3D window, setModel(), and does not touch the mesh geometry/colors.
    ModelTimeOfDay timeOfDay = ModelTimeOfDay::Day;

    //! One source of extruded objects: a polygon vector layer + a relative height field.
    struct VectorSource
    {
        MapLayer* layer = nullptr;
        QString heightField;   //!< The name of the attribute field with the relative height (for example "REL"). Ignored if noExtrusion==true.
        //! The multiplier of the heightField value (1 by default) — needed if instead of a ready height
        //! a floors field is chosen: the user sets a multiplier of ~3.3, the final extrusion height
        //! is computed as heightField * heightMultiplier.
        double heightMultiplier = 1.0;
        //! A virtual pseudo-choice of the field "On the ground" in the dialog — the object is shown flat, at
        //! the terrain level, with no walls/height at all (heightField is not used and not needed then).
        bool noExtrusion = false;

        ModelColorMode colorMode = ModelColorMode::SingleColor;
        //! The color for colorMode==SingleColor — one color for the whole layer, chosen by the user in the dialog.
        QColor singleColor = QColor(200, 90, 80);
        //! The gradient colors for colorMode==GradientByHeight: gradientLowColor — the lowest objects
        //! of the layer (by the heightField value), gradientHighColor — the highest.
        QColor gradientLowColor = QColor(70, 150, 70);
        QColor gradientHighColor = QColor(190, 60, 50);

        //! The visual "effects" for all objects of this source (several can be combined
        //! at once, for example Shadow|Glow) — see ModelEffect.
        ModelEffects effect = ModelEffect::None;

        //! The line width (for linear layers — the full ribbon width, see BuildLineRibbonMesh()). For
        //! polygon layers it is not used.
        double lineWidth = 3.0;

        //! An additional extrusion height ON TOP of what heightField gives (or 0 if noExtrusion) —
        //! physically real (not an imitation): it is added directly to rel BEFORE the wall geometry is built, which
        //! means both the walls and the "Explore" collision (see the tqt 3D window, isClearOfSolidObjects()/
        //! registerGroundHugging()) take it into account as an ordinary object height. It also works with
        //! noExtrusion==true ("On the ground") — then extraExtrusion becomes the only
        //! source of rel instead of heightField. It can be negative — it reduces the final height
        //! of the object (see BuildObjectMesh(): a negative rel builds the walls downward from the base instead of skipping
        //! them), down to a burial below the terrain.
        double extraExtrusion = 0.0;

        //! The outline of the faces/side edges — see ModelOutlineMode.
        ModelOutlineMode outline = ModelOutlineMode::None;

        //! The "Lift" field is not the extrusion height but a shift of the WHOLE base of the object (or of the whole line)
        //! up/down BEFORE heightField/extraExtrusion is applied; a typical example is the level
        //! of an element of a multi-level road interchange in a separate field (for example "FLOOR", values from
        //! -3 to 5, 0 by default — on the ground). Empty ("None" in the dialog) — no lift is applied. If for
        //! a particular object the field value is empty/non-numeric, the lift for it is silently treated as 0 (unlike
        //! heightField the object is not skipped because of it — the lift is optional).
        QString liftField;
        //! The multiplier of the liftField value (real meters per field unit, 1 by default) — by analogy
        //! with heightMultiplier.
        double liftMultiplier = 1.0;
        //! An addition to the absolute height (the Z of the base) in real meters — the "Add." column of the "Abs.
        //! height" group: the final lift = the liftField value * liftMultiplier + liftAdd. It also works when
        //! liftField is not chosen (then the lift is simply the constant liftAdd for the whole layer).
        double liftAdd = 0.0;

        //! The "model field" of a point layer (an exporter mark, tMC): the name of the column with the object code
        //! (for example "CODE"). The code is looked up in the "code -> model" table (point_models.csv next to the
        //! program), and the exporter builds that model of structure on the point. Empty — the layer's objects are
        //! not point structures.
        QString modelField;

        //! The degree of radio signal absorption by the buildings of this layer when computing the radio coverage (see
        //! RadioModel) — used only if the layer is added as a building (a polygon with a height field)
        //! to the calculation area. By default — None ("None"): the layer does not go into the calculation at all (a safe
        //! default for a new, not yet configured layer — not every polygon layer
        //! of the table is a building). For already saved files without this key applySettingsJson()
        //! substitutes PerLayer/20 dB — the former behavior, common to all buildings (RadioModel::
        //! kNlosLoss), which applied BEFORE this setting appeared, when excluding a layer was impossible.
        ModelAbsorptionMode absorptionMode = ModelAbsorptionMode::None;
        //! Mode PerLayer — a fixed absorption of the whole layer, dB.
        double absorptionValue = 20.0;
        //! The column name: Mode ColumnValue — a ready numeric absorption value (dB) per object;
        //! Mode MaterialColumn — the object material/code used to search in absorptionMaterial.
        QString absorptionColumn;
        //! Mode MaterialColumn: the value of the absorptionColumn column (as a string, case-insensitive) ->
        //! absorption, dB. Entered by the user in a mini-dialog (see the tqt 3D dialog, pickAbsorptionMaterial()).
        QVariantMap absorptionMaterial;
        //! Mode MaterialColumn: what the absorption is replaced with if the object value is not in absorptionMaterial
        //! or is equal to 0.
        double absorptionDefault = 20.0;
    };
    QVector<VectorSource> vectorSources;

    //! The current view center and its width (world units) on the tqt 2D map at the moment the
    //! dialog was opened — the 3D window must open "in the same place". hasInitialCamera==false if the request
    //! for camera positioning was not passed (then the camera is centered on the whole bounding box of the model).
    QPointF initialViewCenter;
    double initialViewSpan = 0;
    bool hasInitialCamera = false;

    //! A dialog setting (on by default) — whether to limit the building of extruded
    //! objects to the visible area of the 2D map (with a margin) instead of loading the whole layer. It sharply reduces
    //! the number of Qt3D entities for large layers of which only a small visible part is really needed — and
    //! hence the cost of the subsequent scene deletion (see the class docstring of the tqt 3D window: it
    //! is synchronous and grows with the number of entities).
    bool restrictToVisibleExtent = true;

    //! The radius (world units) beyond which the tqt 3D window hides (does not delete, see
    //! the tqt 3D window, updateObjectVisibility()) the object entities relative to the current camera target —
    //! a cheap alternative to a full unload with a large number of objects. Configured in the dialog
    //! (3000 by default). If there are fewer objects in the scene than visibilitySkipThreshold, hiding by radius
    //! is not applied at all: the scene is small and needs no savings on drawing, while hiding only
    //! risks concealing something needed at such a small number of them.
    double visibilityRadius = 3000.0;

    //! The threshold of the scene object count below which hiding by visibilityRadius is not applied at all
    //! (all entities are always visible) — see its docstring. Configured in the dialog (1000 by default).
    int visibilitySkipThreshold = 1000;

    //! The area itself (world coordinates) — filled in by MainWindow (only it has access to the
    //! current 2D map viewport), not by the dialog itself. hasLoadExtent==false — no restriction at all
    //! (for example if restrictToVisibleExtent==false), then loadExtent is ignored.
    QRectF loadExtent;
    bool hasLoadExtent = false;

    //! The full extent of ALL map layers (not only those chosen for 3D, not limited by the visible area) —
    //! filled in by MainWindow from the already prepared tqt map: xMin/xMax/yMin/yMax (the map widget itself keeps
    //! them up to date as layers are added/edited, in tqt). Used for
    //! the size of the ground plane (see ModelBuilder::Build()) — it must cover EVERYTHING one can
    //! potentially drive to in 3D (including objects not yet loaded with restrictToVisibleExtent,
    //! see the tqt 3D window, extendLoadedExtentIfNeeded()), not only what is visible at the moment of opening —
    //! otherwise when zooming out/panning the camera the ground plane would end earlier than the loaded objects.
    QRectF fullExtent;

    //! A dialog setting (a checkbox + an explanatory caption with the current amount of accumulated memory,
    //! see the tqt 3D dialog, setOldScenesInfo()) — whether to destroy hidden old 3D scenes automatically
    //! (see MainWindow::mOld3DViews) when a new one is opened, without a separate question dialog. Off
    //! by default: tearing down a Qt3D scene is synchronous and blocks the interface for a noticeable time
    //! with a large number of objects (see the class docstring of the tqt 3D window) — it should be enabled deliberately.
    bool autoFreeOldScenes = false;

    //! The radio wave propagation calculation (the groupBox_radio block of the dialog) — the calculation itself is started
    //! automatically when the 3D window data is ready, see RadioModel. The fields are attribute names of the tower layer:
    //! the antenna height (m), the power (dBm), the frequency (MHz).
    struct Radio
    {
        bool enabled = false;
        QVector<MapLayer*> towerLayers;   //!< The tower layers (point layers) — there can be several
        QString heightField, powerField, freqField;
    };
    Radio radio;
};

class ModelBuilder
{
public:

    //! Builds the mesh of ONE object — used for live synchronization with the 2D editor without
    //! rebuilding the whole layer (see the tqt 3D window, onLayerObjAdded(), the subscription to
    //! MapLayer::signalObjAdded/signalObjDeleted). relMin/relMax — the range of the layer height field values
    //! at the moment of the last full build (see ModelVectorSourceMeshes) — used only
    //! for GradientByHeight, so that a new object falls on the same color scale as the already built ones;
    //! we do not try to recompute the range over the whole layer for one object — a deliberate simplification.
    //! Returns false if the object is not suitable for display (not a (multi)polygon/line, no
    //! non-negative value in heightField, outside hasLoadExtent/loadExtent, etc.) — then no entity
    //! is shown for it. hasLoadExtent/loadExtent — see ModelSettings, the same
    //! visible-area filter as in the full build (see CollectLayerCandidates()), so that
    //! live-added objects far outside the loaded area do not multiply entities
    //! uncontrollably. outRel, if not nullptr, receives the final extrusion value (for display
    //! when hovering the cursor — see the tqt 3D window, attachObjectPicker()).
    static bool BuildSingleObjectMesh(MapFeature* obj, const ModelSettings::VectorSource& vs,
                                       const ModelTerrain* terrain, double zExag, const QVector3D& origin,
                                       int colorIndex, double relMin, double relMax,
                                       bool hasLoadExtent, const QRectF& loadExtent,
                                       ModelMeshData& outMesh, double* outRel = nullptr);

    //! A point version (without rebuilding the whole layer) of what CollectLayerCandidates() computes for ALL
    //! the objects of a layer at once — for ONE object obj it looks among the other objects of the same layer
    //! (vs.layer->features) for those touching its start/end (see kJunctionSnapEpsilon), and computes the
    //! averaged lift at each such junction. It returns one pair (liftStart, liftEnd) per
    //! sub-line of obj, in the same order as the internal ExtractLineStrings() (see
    //! BuildSingleObjectMesh(), which passes the result directly to BuildObjectMesh()) — an empty vector
    //! if vs.liftField has no field or obj is not a line.
    //! outNeighbors (optional) — all neighboring objects FOUND at the junctions (except obj) — they also need
    //! to be rebuilt separately (see the tqt 3D window, onLayerObjAdded()), because the averaged
    //! lift at their own end could have changed from the edit of obj, and without a rebuild their old
    //! Qt3D entity would keep the lift value computed BEFORE this edit.
    static QVector<QPair<double, double>> ComputeObjectJunctionLifts(
        MapFeature* obj, const ModelSettings::VectorSource& vs,
        QVector<MapFeature*>* outNeighbors = nullptr);

    //! Estimates the Z of the ribbon of a linear object obj (see BuildLineRibbonMesh()) at the point (worldX,worldY),
    //! projected onto the NEAREST POINT OF THE LINE ITSELF (over all sub-lines if there are several) — needed
    //! for the "Explore" collision (see the tqt 3D window, findSolidObjectTopZ()) where, unlike
    //! the flat roof of a polygon, the height changes along the line (the terrain under it + the lift converging at junctions,
    //! see ComputeVertexLift()/ComputeObjectJunctionLifts()) — a single base (as for the
    //! centroid of a polygon) is physically wrong here. Returns false if obj has no linear geometry.
    //! outDistance — the distance from (worldX,worldY) to this nearest point ON THE LINE (without the width
    //! of the ribbon — the calling code compares it with vs.lineWidth*0.5 itself). outZ — the final ribbon height at the same place
    //! (already multiplied by zExag). outGroundZ — the height of the BARE terrain at the same point (without lift) —
    //! to tell a raised ribbon from one lying right on the ground. ignoreJunctions — do not count the
    //! averaged lift at junctions (see ComputeObjectJunctionLifts(), a full scan of the layer) — coarser,
    //! but orders of magnitude cheaper; needed where EvaluateLineObjectZ() is called many times (supports).
    static bool EvaluateLineObjectZ(MapFeature* obj, const ModelSettings::VectorSource& vs,
                                     const ModelTerrain* terrain, double zExag, double worldX, double worldY,
                                     double* outDistance, double* outZ, double* outGroundZ = nullptr,
                                     bool ignoreJunctions = false);

    //! The offset of the shadow decal (of a polygon or a ribbon — used in both BuildMergedShadowMesh() and
    //! AppendLineShadowSegment()) above the terrain at the given zExag — see their docstrings about the margin above
    //! low "pseudo-surfaces". A single source of truth for this formula: the tqt 3D window needs exactly the
    //! same number (not just a similar one) to correctly compensate the shadow height in the "Explore"
    //! mode (see updateGroundLift()/kExploreShadowLift) — duplicating the constants by hand in two
    //! translation units and keeping them in sync manually would be fragile.
    static double ShadowSurfaceLift(double zExag);

    //! A public wrapper over ExtractLineStrings() for the Minecraft export: the path axes (rails) in map coordinates.
    static bool ExtractLinePaths(MapFeature* obj, QVector<QVector<QPointF>>& outLines)
    { return ExtractLineStrings(obj, outLines); }

private:

    //! A fast (by the bounding box already computed in MapFeature, without walking the geometry) check
    //! of an object's intersection with an extent — see ModelSettings::loadExtent. hasExtent==false — the object
    //! always passes (no restriction).
    static bool IntersectsLoadExtent(const MapFeature* obj, bool hasExtent, const QRectF& extent);

    //! Extracts the outlines (footprints) of an object from its geometry — a polygon or a multipolygon
    //! expanded into sub-polygons. Returns false if there is no geometry/it is not a (multi)polygon.
    //! outZOffsets (optional) — the Z coordinate of the source geometry for each vertex of each outline,
    //! in the same order as outRings/the outline points themselves (the offset of the object base above the terrain,
    //! see BuildObjectMesh(); an outline may be inclined, hence per vertex, not one number per outline).
    //! 0 if the geometry is 2D.
    //! outHoles/outHolesZ (optional, parallel to outRings by index) — the inner rings (holes,
    //! OGRPolygon::getInteriorRing()) of each outline — cutouts in the walls/roof (windows, doors, inner
    //! rooms, etc., see BuildObjectMesh()) — and their Z in the same way as outZOffsets.
    static bool ExtractFootprints(MapFeature* obj, QVector<QVector<QPointF>>& outRings,
                                   QVector<QVector<double>>* outZOffsets = nullptr,
                                   QVector<QVector<QVector<QPointF>>>* outHoles = nullptr,
                                   QVector<QVector<QVector<double>>>* outHolesZ = nullptr);

    //! Stitches holes into the OUTER outline with a single simple (hole-free) outline using zero-width
    //! bridges — the classic technique for ear-clipping triangulators without native hole support (the same
    //! as in earcut.js/eliminateHole and the like). For each hole its rightmost vertex is taken,
    //! a ray is cast from it to the right up to the nearest intersection with the current (possibly already stitched with
    //! previous holes) outline, and a bridge is drawn to the outline vertex visible from there (with a check
    //! for other vertices inside the control triangle — otherwise the bridge could cross someone else's edge).
    //! outZ — the Z coordinates (already computed, e.g. topZ), stitched in the same order as outRing.
    static void MergeHolesIntoRing(const QVector<QPointF>& outer, const QVector<double>& outerZ,
                                    const QVector<QVector<QPointF>>& holes,
                                    const QVector<QVector<double>>& holesZ,
                                    QVector<QPointF>& outRing, QVector<double>& outZ);

    //! Extracts the polylines of an object — a line or a multiline expanded into sub-lines. Returns
    //! false if there is no geometry/it is not a (multi)line.
    //! outZ (optional, parallel to outLines by sub-line/point index) — the Z coordinate of the source
    //! geometry at each point (0 if the geometry is 2D) — see its use in BuildLineRibbonMesh():
    //! it is added to the base line height on a par with the terrain/lift/heightField, by analogy with
    //! outZOffsets of ExtractFootprints() for polygons.
    static bool ExtractLineStrings(MapFeature* obj, QVector<QVector<QPointF>>& outLines,
                                    QVector<QVector<double>>* outZ = nullptr);

    //! Builds the mesh of an object in the given color: for a polygon/multipolygon — walls+cap (walls+cap),
    //! for a line/multiline — either a ribbon of average width along the line (roads, etc.), or, if
    //! wallExtrusion!=0, a real wall (fences, etc.) — see BuildLineRibbonMesh(). rel — the extrusion
    //! height (already read from heightField by the calling code, includes wallExtrusion too — see below);
    //! rel<=0 — the flat mode ("On the ground", see ModelSettings::VectorSource::noExtrusion):
    //! only a cap/ribbon at the terrain level, without walls. Returns false if the object has not a single
    //! valid outline/line.
    //! effect — a visual effect fully baked right into the mesh geometry/colors (Shadow — an extra
    //! decal under the object, Glow — a static brightening of the top edge of the walls, RadialGlow — static
    //! "lamp" spots along the outline), see ModelEffect.
    //! lineHalfWidth — the half-width of the ribbon/the wall thickness for linear objects (see
    //! ModelSettings::VectorSource::lineWidth), not used for polygons.
    //! outline — see ModelOutlineMode/AppendRingOutline().
    //! lift — see ModelSettings::VectorSource::liftField — the already read and multiplied by
    //! liftMultiplier shift of the WHOLE base of the object (in real meters, BEFORE multiplying by zExag,
    //! which is done inside) up/down, applied to the terrain before rel.
    //! wallExtrusion — the value of VectorSource::extraExtrusion; for polygons it is already included in rel and is not
    //! used separately here, for lines it switches the construction from a flat ribbon (rel from heightField
    //! just raises it, see BuildLineRibbonMesh()) to a real vertical wall of such a height
    //! (for example fences) — physically real, not an imitation, the "Explore" collision sees it just
    //! like any other solid geometry.
    //! junctionLifts (optional) — one pair (liftStart, liftEnd) per sub-line of the object, in
    //! the SAME order in which the internal ExtractLineStrings() returns them (see CollectLayerCandidates()) —
    //! used only for lines, to "pull" the lift at the start/end of a line to the averaged
    //! lift at the junction with neighboring objects (see ComputeVertexLift()/BuildLineRibbonMesh()). An empty
    //! vector (the default) — the lift is constant along the whole line, as before (used, for example, in the
    //! point rebuild of a single object in BuildSingleObjectMesh(), where the neighbors at a junction are not counted).
    //! triangulationBudgetMs/outGaveUp — passed through to TriangulatePolygon() for the cap/bottom (see its
    //! docstring) — a protective time ceiling for an outline that is unexpectedly expensive to
    //! triangulate not because of the number of vertices (those are now not built synchronously anyway, see
    //! ModelBuilder::Build()/CollectLayerCandidates() — objects heavy by vertex count are simply
    //! later in the background assembly queue), but because of the shape of the outline itself (nearly degenerate/
    //! self-intersecting corners, etc.). By default (-1) no limit — currently no calling
    //! code sets it, a groundwork for the future.
    static bool BuildObjectMesh(MapFeature* obj, double rel, double lift, double wallExtrusion,
                                 const ModelTerrain* terrain, double zExag,
                                 const QVector3D& origin, const QVector3D& wallColor, ModelEffects effect,
                                 double lineHalfWidth, ModelOutlineMode outline,
                                 ModelMeshData& outMesh,
                                 const QVector<QPair<double, double>>& junctionLifts = {},
                                 qint64 triangulationBudgetMs = -1, bool* outGaveUp = nullptr,
                                 const ModelSupportSkipFn* supportSkip = nullptr);

    //! Builds the mesh of a linear object (see BuildObjectMesh()) of the given half-width along one polyline —
    //! one quad per segment, without roundings/chamfers at the joints (a deliberate simplification). wallExtrusion==0
    //! (the usual case, roads, etc.) — a flat ribbon: the height of each point is the terrain at that point (not
    //! only by the centroid, as for polygons — a line can change height noticeably along its length) + lift (see
    //! BuildObjectMesh()) + rel (for raised lines, for example overpasses). wallExtrusion!=0 (fences, etc.) —
    //! instead of a ribbon a real vertical wall: the base at the same level (terrain+lift+rel),
    //! the top — the base + wallExtrusion, with side faces, a cap on top and end walls
    //! at the ends of the polyline so that it does not look like a hollow box.
    //! effect — see BuildObjectMesh() (for a line Shadow is not applied — it makes no sense for either a ribbon
    //! or a fence wall).
    //! liftStart/liftEnd — the target lift at the first/last point of line (by default equal to lift, i.e.
    //! unchanged) — over a section of half the line length from the corresponding end the line smoothly
    //! (smoothstep) passes from lift to this value, see ComputeVertexLift()/BuildObjectMesh(). Needed
    //! for the lift to converge at junctions with neighboring lines of the same layer (interchange ramps).
    //! geomZ (optional, per point of line) — the Z of the source geometry at each point (see ExtractLineStrings()) —
    //! it is added to the base height on a par with the terrain/lift/rel; an empty vector (the default) — 0 everywhere
    //! (2D geometry, the usual case).
    static void BuildLineRibbonMesh(const QVector<QPointF>& line, double halfWidth, double rel, double lift,
                                     double wallExtrusion, const ModelTerrain* terrain, double zExag, const QVector3D& origin,
                                     const QVector3D& color, ModelEffects effect, ModelOutlineMode outline,
                                     ModelMeshData& outMesh, double liftStart = std::numeric_limits<double>::quiet_NaN(),
                                     double liftEnd = std::numeric_limits<double>::quiet_NaN(),
                                     const QVector<double>& geomZ = {},
                                     const ModelSupportSkipFn* supportSkip = nullptr);

    //! Computes the lift of EVERY vertex of line — by a smooth (smoothstep) transition to liftStart/liftEnd over the first/
    //! last HALF of the line length from the corresponding end (or with no transition at that end if
    //! liftStart/liftEnd is NaN, see BuildLineRibbonMesh()) — i.e. the transition is stretched over the whole run
    //! of the line, not over a fixed section at the very ends: the whole line is exactly the needed ramp length.
    static QVector<double> ComputeVertexLift(const QVector<QPointF>& line, double lift,
                                              double liftStart, double liftEnd);

    //! Computes the lateral offset (from the center point line[i] to the left edge of the ribbon/wall; the right edge is
    //! the same point minus this offset) for EVERY vertex of line — on straight sections it is the ordinary
    //! perpendicular of a segment of length halfWidth, and at INNER vertices (both the previous and
    //! the next segment exist) — the bisector of the angle between neighboring segments (a miter join), so that neighboring
    //! ribbon segments meet at a common edge without wedges/gaps at turns (see BuildLineRibbonMesh()
    //! before this fix: there every segment computed its own perpendicular independently, hence the visible "teeth"
    //! at bends). The bisector length is limited by kMiterLimitFactor*halfWidth — otherwise at a very sharp
    //! reversal (almost 180°) the offset would go to infinity as a "spike"; with a degenerate bisector
    //! (exactly 180°, the segments go straight back) the perpendicular of the incoming segment is used.
    static QVector<QPointF> ComputeRibbonOffsets(const QVector<QPointF>& line, double halfWidth);

    //! Adds to outMesh a thin outline strip along the closed outline ring (the top polygon edge,
    //! see BuildObjectMesh()) at the given Z of each vertex (topZ — may be inclined, like the cap
    //! itself) in the color color, slightly raised above the cap (kOutlineLift) to avoid z-fighting —
    //! see ModelOutlineMode.
    static void AppendRingOutline(const QVector<QPointF>& ring, const QVector<double>& z, bool closed,
                                   const QVector3D& origin, const QVector3D& color, ModelMeshData& outMesh);

    //! The outline color — the object color mixed with white (Light) or black (Dark); None — nullptr is not
    //! returned, the calling code checks outline!=None itself before calling AppendRingOutline().
    static QVector3D OutlineColor(const QVector3D& objColor, ModelOutlineMode outline);

    //! Determines the object color by the layer coloring mode (see ModelColorMode). classifierFallbackWarned —
    //! an optional flag "the absence of a classifier for this layer was already warned about" (nullptr —
    //! do not warn at all, used in BuildSingleObjectMesh during point live addition).
    static QVector3D ResolveObjectColor(const ModelSettings::VectorSource& vs, MapFeature* obj,
                                         double rel, double relMin, double relMax, int colorIndex,
                                         QStringList* warnings, bool* classifierFallbackWarned);

    //! Decomposes the silhouette of the projected shadow of an object (see ModelEffect::Shadow) into "raw" polygon pieces
    //! in outPieces (see ModelMeshData::shadowPieces): the outline ring in its original position, the same outline
    //! shifted along kShadowLightDir (the same direction as the real scene light) by a distance
    //! proportional to heightDrop, and one "sail" quad per edge between these two
    //! positions — together they give a geometrically honest shadow silhouette stretched in the direction
    //! opposite to the light, not an arbitrarily chosen blob. The outline is not triangulated and is not
    //! projected onto a particular Z here — that is done by BuildMergedShadowMesh() AFTER
    //! the silhouettes of ALL scene objects are merged through a boolean union into a single non-intersecting outline (see its
    //! docstring and ModelMeshData::shadowPieces) — an object does not know about that earlier, so as not to multiply
    //! N intersecting alpha decals over each other.
    static void AppendShadowBlob(const QVector<QPointF>& ring, double heightDrop,
                                  QVector<QVector<QPointF>>& outPieces);

    //! The shadow for ONE segment of a flat line ribbon (see BuildLineRibbonMesh(), ModelEffect::Shadow), into
    //! outMesh.shadow — simpler than AppendShadowBlob() for polygons (there are no "sweeping" side faces —
    //! the ribbon itself is thin, it physically has no side volume that would give a side shadow): one flat
    //! rectangular decal of the same profile as the segment ribbon itself, shifted along kShadowLightDir
    //! by a distance proportional to the height ABOVE THE TERRAIN at each of the two segment ends (h1/h2 —
    //! already the difference "ribbon height minus the terrain height under it", not the raw lift/rel) — sections almost on
    //! the ground (both h < kShadowMinLineHeight) cast no shadow at all, they do not interfere with "On the ground"/ordinary
    //! non-raised roads. groundZ1/groundZ2 — the terrain level (WITHOUT lift) under each end
    //! of the segment, the shadow itself lies on it. zExag — see AppendShadowBlob().
    static void AppendLineShadowSegment(const QPointF& p1, const QPointF& p2, double halfWidth,
                                         double h1, double h2, double groundZ1, double groundZ2, double zExag,
                                         const QVector3D& origin, ModelMeshData& outMesh);

    //! The "Supports" effect (ModelEffect::Supports): a square prism pillar with the half-side radius at the point
    //! (x, y) from zBottom (the terrain) to zTop (the bottom of the raised object minus a gap) — ordinary opaque
    //! side faces + the underside of the top cap (visible from below, from under an overpass) in outMesh, and a record
    //! ModelPillar in outMesh.pillars for the "Explore" collision. x/y/zBottom/zTop — WORLD (zExag is already
    //! applied, like baseZAt()/groundZAt()), origin is subtracted only for vertices. Does nothing if
    //! zTop - zBottom < kSupportMinPillarHeight.
    //! color — the pillar color: the same as the object itself (see AppendSupports()).
    static void AppendSupportPillar(double x, double y, double radius, double zBottom, double zTop,
                                     const QVector3D& color, const QVector3D& origin, ModelMeshData& outMesh);

    //! Places supports (see AppendSupportPillar()) under the vertices of one line/outline points — every
    //! vertex where the object is raised above the terrain (baseZ[i] - groundZ[i] >= kSupportMinHeight), except
    //! those too close to an already placed one (kSupportSpacingFactor*radius). topGap — an extra gap under
    //! the object on top of the slope margin (see the .cpp: for a ribbon between vertices the surface is inclined and
    //! would cut the top of the pillar, so the pillar is not brought right up to it). closed — the outline is closed
    //! (a polygon), otherwise a polyline. color — the pillar color (the object color). skip (optional) — called
    //! before placing a pillar at (x, y) with baseZ (the world height of the object bottom above it): true — do not
    //! place the pillar (for example, there is another ribbon of the same layer under it, see BuildSingleObjectMesh()).
    static void AppendSupports(const QVector<QPointF>& points, bool closed, const QVector<double>& baseZ,
                                const QVector<double>& groundZ, double radius, double topGap,
                                const QVector3D& color, const QVector3D& origin, ModelMeshData& outMesh,
                                const ModelSupportSkipFn* skip = nullptr);

    //! Adds to outMesh small static radial "light spots" (a fan of triangles from a bright
    //! center to a darkened edge — an imitation of a glow without alpha blending) every kGlowSpacing
    //! along the outline/line points (closed==true — a closed polygon outline, false — a polyline),
    //! see ModelEffect::RadialGlow. Fully static — no animation/periodic updates.
    static void AppendRadialGlowMarkers(const QVector<QPointF>& points, bool closed, const ModelTerrain* terrain,
                                         double zExag, double rel, double lift, const QVector3D& origin,
                                         const QVector3D& color, ModelMeshData& outMesh);

    //! Triangulation of a simple (possibly non-convex) closed outline by the ear clipping method.
    //! ring — the outline in local 2D coordinates (without a repeated last point = the first).
    //! outTris — a flat list of indices (3 per triangle, in the ring numbering).
    //! Returns false if the outline is degenerate (< 3 points).
    //! hasBridgeDuplicates — true only if ring could have come from stitching holes (see
    //! MergeHolesIntoRing()) and hence can contain duplicate vertices (the bridge points of a keyhole
    //! stitching) — enables an extra coordinate-equality check when looking for an "ear" (see the .cpp). An
    //! ordinary outline without holes (the overwhelming majority of objects) cannot in principle
    //! have duplicates — with false this check is not performed at all (a short circuit in the .cpp), i.e. for
    //! simple objects it adds NEITHER LOOPS NOR CALLS beyond the triangulation that has always been there.
    //! budgetMs — a protective time ceiling (ms) on the ear-clipping of THIS outline itself; < 0 (the
    //! default) — no limit. When exceeded the algorithm does not spend time trying to finish
    //! honestly — it immediately completes the remaining vertices with a cheap fan fallback (the same as for a
    //! self-intersecting outline) and sets *outGaveUp=true if outGaveUp was passed — the calling
    //! code (see CollectLayerCandidates()) uses this to understand "the object turned out to be unexpectedly
    //! expensive — although by vertex count it did not look heavy" and to move its
    //! recomputation to a background thread instead of taking obviously broken geometry on faith.
    static bool TriangulatePolygon(const QVector<QPointF>& ring, QVector<int>& outTris,
                                    bool hasBridgeDuplicates = false, qint64 budgetMs = -1,
                                    bool* outGaveUp = nullptr);
};

#endif // WDF3DMODELBUILDER_H
