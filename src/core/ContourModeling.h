#pragma once

#include "PieceGeometry.h"

namespace opc {
inline bool dividesPiece(ContourOperation operation) {
    return operation == ContourOperation::Cut || operation == ContourOperation::Relief
        || operation == ContourOperation::Yoke;
}

inline bool redistributesDarts(ContourOperation operation) {
    return operation == ContourOperation::SplitDart || operation == ContourOperation::MergeDarts;
}

inline bool usesSpreadWidth(ContourOperation operation) {
    return operation == ContourOperation::Spread || operation == ContourOperation::Pleat
        || operation == ContourOperation::Gather || operation == ContourOperation::PleatWithTabs;
}

inline bool supportsCurvedBoundary(ContourOperation operation) {
    return redistributesDarts(operation) || dividesPiece(operation) || usesSpreadWidth(operation)
        || operation == ContourOperation::TransferDart || operation == ContourOperation::TransferDartToOutlet
        || operation == ContourOperation::MoveOpenDart;
}

struct SpreadMotion {
    QPolygonF movedPanel;
    QPolygonF cut;
    QPointF translation;

    // Empty when an object touches or crosses the cut.
    std::optional<QPointF> offsetFor(const Entity& guide) const;
};

struct ContourEdit {
    PatternDocument document;
    QVector<qsizetype> removedEntities;
    std::optional<SpreadMotion> spreadMotion;
};

struct ContourEditResult {
    std::optional<ContourEdit> edit;
    QString error;
};

std::optional<double> contourEdgeParameter(const Entity& edge, QPointF point);

// Dart guides list each source leg pair first, then the outlet lines.
// SplitDart takes two source legs and 2–10 outlets. spreadMm sets the first
// outlet's percentage of angular intake. The remaining outlets share the balance.
inline constexpr qsizetype maxDartOutlets = 10;
inline bool validDartOutletCount(qsizetype guideCount) {
    return guideCount >= 4 && guideCount <= 2 + maxDartOutlets;
}
ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm = 0, const QVector<double>& outletWeights = {});

inline constexpr qsizetype maxCrossingGuides = 8;
ContourEditResult cutCrossingGuides(const PatternDocument& document, qsizetype piece,
                                    const QVector<qsizetype>& guides);

// Scale width about the chosen axis and length about the top of the piece.
ContourEditResult shapeSilhouette(const PatternDocument& document, qsizetype piece,
                                 double axisX, double topScale, double bottomScale,
                                 double lengthScale = 1);
}
