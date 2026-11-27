#pragma once

#include "PieceGeometry.h"
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <functional>
#include <optional>

namespace opc {

bool validShrinkage(QPointF percent);

QVector<QPointF> gradingPoints(const PatternDocument& document, qsizetype piece);
PatternDocument gradedPattern(const PatternDocument& document, const QString& size,
                              QVector<qsizetype>* sourceIndices = nullptr,
                              GradeEdgeNames edgeNames = GradeEdgeNames::ForSeamLinks);
PatternDocument uncutGradedPattern(const PatternDocument& document, const QString& size,
                                  QVector<qsizetype>* sourceIndices, GradeEdgeNames edgeNames);
// Add an evaluated size as independent, cut-ready pieces without changing either project's grading.
bool appendMarkerModel(PatternDocument& destination, const PatternDocument& source,
                       const QString& size, const QString& modelName, int kits, QString& error);
QJsonObject saveProduction(const ProductionData& data);
bool loadProduction(const QJsonObject& json, qsizetype pieceCount, ProductionData& data);

inline const QString drillLayer = QStringLiteral("SOFIT_DRILL");

inline bool isDrillPoint(const Entity& entity) {
    return entity.shape == Shape::Point && entity.layer == Layer::Construction && entity.dxfLayer == drillLayer;
}

struct LayoutPiece {
    QPolygonF cut;
    QLineF grain;
    QLineF fold;
    std::optional<QPointF> repeatPoint;
    QPainterPath notches;
    QVector<QPointF> drillPoints;
    QVector<QPainterPath> symbols;
};

LayoutPiece fabricPiece(const PatternDocument& graded, const FabricPlacement& placement,
                        bool includeNotches = false, QPointF shrinkagePercent = {});

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

struct SizeLay {
    QString size;
    int layers;
};

QVector<SizeLay> planSizeLays(const QMap<QString, int>& demand, int maximumLayers);

struct KitLay {
    QMap<QString, int> kitsPerSize;
    int layers;
};

QVector<KitLay> planKitLays(const QMap<QString, int>& demand, int maximumLayers, int maximumKitsPerLayer);
// Exact allocation for up to four sizes and 24 kits. Minimize the number of lays,
// then total layers. This stage allocates quantities before geometric nesting.
QVector<KitLay> minimumKitLays(const QMap<QString, int>& demand, int maximumLayers, int maximumKitsPerLayer);
// Minimize total marker length times layer count across the whole order.
// The evaluator returns no length for an infeasible marker composition.
QVector<KitLay> minimumMaterialKitLays(const QMap<QString, int>& demand, int maximumLayers,
                                     int maximumKitsPerLayer,
                                     const std::function<std::optional<double>(const QMap<QString, int>&)>& markerLength);

// Index each measured candidate by the bit mask of its indivisible assignments.
// Missing or infeasible candidates have a non-finite length.
inline constexpr int maxExactLayAssignments = 7;
QVector<int> minimumMarkerPartition(const QVector<double>& lengths, double tableLength);

inline constexpr qsizetype maxAutomaticCopies = 200;
enum class FabricLayoutError { None, InvalidShrinkage, InvalidWidth, InvalidClearance, InvalidRepeat, InvalidFlaws, TooManyCopies, InvalidContour, TooWide, VerificationFailed };

struct AutomaticFabricLayout {
    QVector<FabricPlacement> placements;
    FabricLayoutError error = FabricLayoutError::None;
    qsizetype failedCopy = -1;
    std::optional<double> baselineLengthMm;
    std::optional<double> optimizedLengthMm;
};

AutomaticFabricLayout arrangeFabric(const PatternDocument& document, const ProductionData& data);

}
