#pragma once
// Map feature: geometry in the project coordinate system, string attributes and its bounds.
#include <QString>
#include <QVector>
#include "ogrsf_frmts.h"

class MapFeature
{
public:
    OGRGeometry* g = nullptr;              //!< geometry in the project coordinate system (owned)
    QVector<QString> fields;               //!< attribute values as strings, in layer order (MapLayer::fieldNames)
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;   //!< geometry bounds

    MapFeature() = default;
    MapFeature(const MapFeature&) = delete;
    MapFeature& operator=(const MapFeature&) = delete;
    ~MapFeature() { delete g; }

    //! Recompute the bounds from the geometry
    void updateBounds()
    {
        if (!g) return;
        OGREnvelope e;
        g->getEnvelope(&e);
        xmin = e.MinX; xmax = e.MaxX; ymin = e.MinY; ymax = e.MaxY;
    }
};
