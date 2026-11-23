#pragma once

#include "PatternDocument.h"
#include <QMap>

namespace opc {
struct DxfPath {
    QString layer;
    QVector<Entity> entities;
    bool closed = false;
    qsizetype contour = -1; // Shared by source paths that form the same editable piece.
};

struct DxfDrawing {
    QVector<DxfPath> paths;
    QMap<QString, qsizetype> skipped;
    int insertionUnits = 0;
    qsizetype approximated = 0;

    PatternDocument appendTo(PatternDocument document, const QSet<QString>& layers,
                             double millimetersPerUnit) const;
};

std::optional<DxfDrawing> readDxf(const QString& path, QString& error);
std::optional<double> dxfUnitMillimeters(int insertionUnits);
}
