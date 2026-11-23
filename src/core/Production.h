#pragma once

#include "PieceGeometry.h"
#include <QJsonObject>
#include <QLineF>
#include <optional>

namespace opc {

QVector<QPointF> gradingPoints(const PatternDocument& document, qsizetype piece);
PatternDocument gradedPattern(const PatternDocument& document, const QString& size,
                              QVector<qsizetype>* sourceIndices = nullptr);
QJsonObject saveProduction(const ProductionData& data);
bool loadProduction(const QJsonObject& json, qsizetype pieceCount, ProductionData& data);

struct LayoutPiece {
    QPolygonF cut;
    QLineF grain;
};

LayoutPiece fabricPiece(const PatternDocument& graded, const FabricPlacement& placement);

struct FabricUsage {
    double length = 0;
    qsizetype invalid = 0;
    qsizetype outside = 0;
    qsizetype overlaps = 0;
    double cutArea = 0;
    std::optional<double> utilization(double width) const;
};

FabricUsage fabricUsage(const QVector<QPolygonF>& polygons, double width);

inline constexpr qsizetype maxAutomaticCopies = 200;
enum class FabricLayoutError { None, InvalidWidth, TooManyCopies, InvalidContour, TooWide, VerificationFailed };

struct AutomaticFabricLayout {
    QVector<FabricPlacement> placements;
    FabricLayoutError error = FabricLayoutError::None;
    qsizetype failedCopy = -1;
};

AutomaticFabricLayout arrangeFabric(const PatternDocument& document, const ProductionData& data);

}
