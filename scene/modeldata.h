#ifndef WDF3DMODELDATA_H
#define WDF3DMODELDATA_H

#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QPointF>
#include <QStringList>

class MapFeature;
class MapLayer;

/*!
 * \file   modeldata.h
 * \brief  3D model data (terrain + extruded objects) — plain buffers without Qt3D dependencies.
 *
 * Built in ModelBuilder::Build() (can run in a background thread), then passed
 * to the tqt 3D window, setModel() (the GUI thread only, because a Qt3D scene can only be built there).
 */

//! A semi-transparent RGBA geometry buffer — a format shared by two different effects that need
//! real alpha blending (unlike the ordinary opaque object mesh — walls/lid/ribbon,
//! see ModelMeshData), which is incompatible with the ordinary opaque object mesh: if depth-buffer writing is
//! turned off for the WHOLE mesh (needed for transparency), neighboring objects start to be drawn
//! over each other incorrectly. Therefore both are rendered by a separate Qt3D entity with its own unlit
//! material, see the tqt 3D window, BuildAlphaMeshEntity():
//!  - "Lamps" (see ModelEffect::RadialGlow/ModelBuilder::AppendRadialGlowMarkers()) — the alpha falls
//!    from the halo center to the edge;
//!  - shadow (see ModelEffect::Shadow/ModelBuilder::AppendShadowBlob()/AppendLineShadowSegment()) —
//!    the alpha is constant (a semi-transparent gray) so that the shadow does not fully cover what it falls on
//!    (roads, another shadow on top, etc.), see ModelMeshData::shadow.
struct ModelGlowMeshData
{
    QVector<QVector3D> vertices;
    QVector<QVector4D> colors;     //!< RGBA (0..1)
    QVector<quint32>   indices;
};

//! A support (a pillar) under a raised object (see ModelEffect::Supports/ModelBuilder::AppendSupportPillar())
//! — data for the "Explore" collision (one cannot walk through a pillar, see the tqt 3D window::
//! isClearOfSolidObjects()): in WORLD coordinates (not recentered, unlike the mesh vertices) and with
//! zExaggeration already applied, like terrainZAt()/findSolidObjectTopZ() — i.e. compared directly with
//! the height of the feet. A pillar is a square prism with the half-side radius around (x, y).
struct ModelPillar
{
    double x = 0, y = 0;
    double radius = 0;
    double zBottom = 0, zTop = 0;
};

//! One mesh (terrain, or one extruded object) as plain vertex buffers.
struct ModelMeshData
{
    QVector<QVector3D> vertices;   //!< Vertex positions in local (recentered) coordinates
    QVector<QVector3D> normals;    //!< Normals, the same size as vertices
    QVector<QVector3D> colors;     //!< Vertex color (0..1 RGB), the same size as vertices
    QVector<quint32>   indices;    //!< The triangle index list (triangle list)

    //! The "Lamps" markers of this object (if any, see ModelEffect::RadialGlow) — a separate
    //! semi-transparent buffer, see ModelGlowMeshData. Empty if the effect was not applied.
    ModelGlowMeshData glow;

    //! The shadow decal of this object (if any, see ModelEffect::Shadow) — the same format as glow,
    //! but a separate buffer: glow is built (and its entity created) only at night (see
    //! the tqt 3D window, mNightMode), while the shadow is needed regardless of the time of day. Empty if the effect was
    //! not applied/the object casts no shadow. Only the shadows of RIBBONS (see AppendLineShadowSegment()) — the shadows of
    //! POLYGONS are no longer written here, see shadowPieces below.
    ModelGlowMeshData shadow;

    //! The "raw" (possibly self-intersecting and intersecting each other) pieces of this object's shadow
    //! silhouette — see ModelBuilder::AppendShadowBlob(): the outline ring (one "lid"), the same outline
    //! shifted along the light (the second "lid"), and one "sail" quad per edge between them.
    //! World coordinates (XY, no Z — the shadow raster/mesh does not need height, see below; without subtracting
    //! origin — survives reOriginIfNeeded()). It replaces what AppendShadowBlob() used to write
    //! into shadow above: N objects with overlapping shadows gave a darker "seam" at the intersection when
    //! rendering N separate alpha decals. Instead the silhouettes of ALL scene objects are geometrically
    //! merged into one non-intersecting outline through a boolean union (see
    //! ModelBuilder::BuildMergedShadowMesh(), the tqt 3D window, tryShadowRebuild()) and drawn as one
    //! mesh — overlapping is physically impossible, because after the union the result has no self-intersections.
    QVector<QVector<QPointF>> shadowPieces;

    //! The supports of this object (see ModelPillar) — collision data only; the pillar geometry itself
    //! is already in the main vertices/indices of this mesh (ordinary opaque faces).
    QVector<ModelPillar> pillars;
};

//! The mesh of one extruded source object (obj is the original layer object, by whose pointer
//! the tqt 3D window finds/updates the corresponding Qt3D entity during live synchronization,
//! see MapLayer::signalObjAdded/signalObjDeleted).
struct ModelObjectMesh
{
    MapFeature* obj = nullptr;
    ModelMeshData mesh;
    //! The extrusion value (height, already with the multiplier, BEFORE zExaggeration) — 0 for "On the
    //! ground" objects (see ModelSettings::VectorSource::noExtrusion). Used only for
    //! the display when hovering the cursor (see the tqt 3D window, attachObjectPicker()) — it no longer affects
    //! the mesh geometry itself, which is in mesh.
    double rel = 0;
};

//! All extruded objects of one vector source layer.
struct ModelVectorSourceMeshes
{
    MapLayer* layer = nullptr;
    //! Right after Build() it is always empty — object meshes are no longer built synchronously inside it (see
    //! ModelBuilder::CollectLayerCandidates()/ModelData::pendingObjects below); the real
    //! mesh assembly of every object happens later, one by one, in the background — see
    //! the tqt 3D window, setModel()/loadObjectAsync().
    QVector<ModelObjectMesh> objects;
    //! The range of the height field values over the layer at the moment of assembly — needed so that when one new object is added
    //! live (without rebuilding the whole layer) the GradientByHeight coloring stays on
    //! the same scale as the already built objects.
    double relMin = 0, relMax = 0;
};

//! One vector layer object waiting for background mesh assembly (see ModelData::pendingObjects) —
//! ONLY a pointer + a cheap estimate of the "weight", without the mesh itself: the assembly itself (BuildSingleObjectMesh(),
//! self-sufficient — it fetches the needed VectorSource/rel/lift/color by obj/layer itself) happens later,
//! one object at a time, in the background — see the tqt 3D window, setModel()/loadObjectAsync().
struct ModelPendingObject
{
    MapFeature* obj = nullptr;
    MapLayer* layer = nullptr;
    int vertexCount = 0;   //!< the points of the outer outline + all holes (a polygon) or the points of a line
    bool hasHoles = false; //!< see the sorting by "waves" in ModelBuilder::Build()
};

//! The full set of 3D model data, ready to be passed to the tqt 3D window.
struct ModelData
{
    ModelMeshData terrain;                          //!< The terrain mesh (from the height-matrix raster)
    QVector<ModelVectorSourceMeshes> buildings;      //!< One group per vector source layer (relMin/relMax; objects is always empty, see its docstring)
    //! All vector layer objects not yet built into a mesh — already sorted (see
    //! ModelBuilder::Build()) from "light" to "heavy" (first without holes and with a small number of
    //! vertices, then with holes, then everything else), so that when gradually loading in an open window
    //! (see the tqt 3D window, setModel()/loadObjectAsync()) the fast objects appear first.
    QVector<ModelPendingObject> pendingObjects;
    QVector3D origin;                                //!< The world point (X,Y,minZ) subtracted during recentering
    bool ok = false;                                 //!< Whether the assembly succeeded
    QString error;                                   //!< The error message if !ok
    QStringList warnings;                            //!< Non-fatal warnings (for example a skipped layer)

    //! The point (in local, recentered coordinates) at which the camera should initially be aimed —
    //! it corresponds to the current view center on the tqt 2D map at the moment the 3D window was opened.
    //! If hasInitialCamera==false the window is centered on the whole terrain bounding box instead.
    QVector3D initialCameraTarget;
    double initialViewSpan = 0;            //!< The width of the area visible on the 2D map (world units) — the scale for the camera distance
    bool hasInitialCamera = false;
};

#endif // WDF3DMODELDATA_H
