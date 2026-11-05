#pragma once
// Gluing building complexes: which codes merge, lifting low parts up and
// the union-find merge of touching/overlapping buildings of one code
// (moved out of mcworldexporter.cpp).
#include "export/geom.h"
#include <QVector>
#include <atomic>

bool mergeableCode(int code);
void raiseLowBuildingParts(QVector<McWorldExporter::McObject>& objs, const VxTransform& tf, double minBlocks);
QVector<McWorldExporter::McObject> mergeIndustrial(const QVector<McWorldExporter::McObject>& in, const VxTransform& tf,
                                                            const std::atomic_bool* cancel);
