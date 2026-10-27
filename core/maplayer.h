#pragma once
// Map layer: name, attribute list, geometry type and features.
#include <QString>
#include <QStringList>
#include <QVector>
#include "core/mapfeature.h"

enum class GeomKind { None, Point, Line, Polygon };

class MapLayer
{
public:
    QString name;                          //!< layer name (for shapefiles — the file base name with dots; the suffix after the last dot defines the layer kind)
    QStringList fieldNames;                //!< attribute names
    QVector<MapFeature*> features;         //!< layer features (owned)
    GeomKind geom = GeomKind::None;

    MapLayer() = default;
    MapLayer(const MapLayer&) = delete;
    MapLayer& operator=(const MapLayer&) = delete;
    ~MapLayer() { qDeleteAll(features); }

    //! Attribute index by name, case-insensitive; -1 if absent
    int fieldIndex(const QString& fieldName) const
    {
        for (int i = 0; i < fieldNames.size(); ++i)
            if (fieldNames[i].compare(fieldName, Qt::CaseInsensitive) == 0) return i;
        return -1;
    }
};
