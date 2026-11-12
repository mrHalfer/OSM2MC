#pragma once
// STL models of the landmarks: a binary/ASCII parser and a voxelizer (no GUI, no Qt3D).
#include <QString>
#include <QVector>
#include <QtGlobal>

//! A triangle of a 3D model in model units (the landmark STL models are z-up: z is the height).
struct StlVec { float x, y, z; };
struct StlTri { StlVec v[3]; };
//! A block of the voxelized model: x/z are offsets from the model's centre (may be negative), y is 0 at the base.
struct StlCell { qint16 x, y, z; };

//! Binary or ASCII STL -> triangles. false + err — unreadable/empty/truncated.
bool stlLoad(const QString& path, QVector<StlTri>& tris, QString* err = nullptr);

//! Voxelize: the model's z is the height (the STL files of the landmarks are z-up), a uniform scale makes the
//! model heightBlocks tall, the footprint is centred at (0,0) and the base is at y=0; yaw rotates the model
//! around the up axis (degrees clockwise). The shell is sampled and the interior is filled by vertical rays,
//! so a closed mesh comes out solid, not a one-voxel shell. false + err — a degenerate/empty model.
bool stlVoxelize(const QVector<StlTri>& tris, double heightBlocks, double yawDeg,
                 QVector<StlCell>& cells, QString* err = nullptr);
