#pragma once

#include "PieceGeometry.h"
#include <QJsonObject>
#include <QLineF>
#include <QPolygonF>
#include <optional>

namespace opc {

QVector<QPointF> gradingPoints(const PatternDocument& document, qsizetype piece);
PatternDocument gradedPattern(const PatternDocument& document, const QString& size,
                              QVector<qsizetype>* sourceIndices = nullptr,
                              GradeEdgeNames edgeNames = GradeEdgeNames::ForSeamLinks);
QJsonObject saveProduction(const ProductionData& data);
bool loadProduction(const QJsonObject& json, qsizetype pieceCount, ProductionData& data);

struct LayoutPiece {
    QPolygonF cut;
    QLineF grain;
    QLineF fold;
    std::optional<QPointF> repeatPoint;
};

LayoutPiece fabricPiece(const PatternDocument& graded, const FabricPlacement& placement);

bool matchesFabricRepeat(const ProductionData& data, const FabricPlacement& placement,
                         std::optional<QPointF> repeatPoint = std::nullopt);
bool canTurn(const ProductionData& data, const FabricPlacement& placement);
// Omit the fabric fold from the cutting stroke.
QPolygonF openFoldContour(QPolygonF cut, const QLineF& fold);
double foldPosition(const FabricPlacement& placement, double fabricWidth, double cutWidth);

struct FabricUsage {
    double length = 0;
    qsizetype invalid = 0;
    qsizetype outside = 0;
    qsizetype overlaps = 0;
    qsizetype flawConflicts = 0;
    double cutArea = 0;
    std::optional<double> utilization(double width) const;
};

FabricUsage fabricUsage(const QVector<QPolygonF>& polygons, double width, double clearanceMm = 0,
                        const QVector<QRectF>& flaws = {});
bool validFabricFlaws(const QVector<QRectF>& flaws);

struct CuttingReportRow {
    qsizetype piece;
    QString size;
    qsizetype single = 0;
    qsizetype left = 0;
    qsizetype right = 0;
    qsizetype onFold = 0;

    qsizetype copies() const { return single + left + right + onFold; }
};

struct CuttingReport {
    QVector<CuttingReportRow> rows;
    FabricUsage usage;
    qsizetype orientationViolations = 0;
    qsizetype foldViolations = 0;
    qsizetype repeatViolations = 0;

    bool canExport(double fabricWidth) const;
};

// Fill in missing pieces for every size represented in the marker (base for an empty marker).
QVector<CuttingReportRow> cuttingKitRows(const PatternDocument& document, const CuttingReport& report);

CuttingReport cuttingReport(const PatternDocument& document, const ProductionData& data);

inline constexpr qsizetype maxAutomaticCopies = 200;
enum class FabricLayoutError { None, InvalidWidth, InvalidClearance, InvalidRepeat, InvalidFlaws, TooManyCopies, InvalidContour, TooWide, VerificationFailed };

struct AutomaticFabricLayout {
    QVector<FabricPlacement> placements;
    FabricLayoutError error = FabricLayoutError::None;
    qsizetype failedCopy = -1;
};

AutomaticFabricLayout arrangeFabric(const PatternDocument& document, const ProductionData& data);

}
