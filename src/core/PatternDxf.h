#pragma once

#include "PieceGeometry.h"

namespace opc {
bool exportPatternDxf(const QString& path, const PatternDocument& document,
                      const QVector<bool>& visibility, const QVector<PieceGeometry>& geometry,
                      QString& error);
bool exportFabricDxf(const QString& path, const PatternDocument& document,
                     const ProductionData& data, QString& error);
}
