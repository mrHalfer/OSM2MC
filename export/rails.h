#pragma once
// Rail line helpers of the exporter: the block-cell chain of a track axis,
// the rail shape by its neighbours and the powered-rail pass
// (moved out of mcworldexporter.cpp).
#include "export/voxelstore.h"
#include <QPoint>
#include <QPointF>
#include <QVector>

QVector<QPoint> railCellChain(const QVector<QPointF>& pts);
int railShapeFor(const QPoint& cur, const QPoint* prev, const QPoint* next);
int electrifyRails(VoxelStore& store, const QVector<QPoint>& chain, const QVector<int>& ys);
