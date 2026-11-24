#pragma once

#include "PieceGeometry.h"

namespace opc {
inline bool dividesPiece(ContourOperation operation) {
    return operation == ContourOperation::Cut || operation == ContourOperation::Relief
        || operation == ContourOperation::Yoke;
}

inline bool usesSpreadWidth(ContourOperation operation) {
    return operation == ContourOperation::Spread || operation == ContourOperation::Pleat
        || operation == ContourOperation::Gather;
}

struct ContourEdit {
    PatternDocument document;
    QVector<qsizetype> removedEntities;
};

struct ContourEditResult {
    std::optional<ContourEdit> edit;
    QString error;
};

std::optional<double> contourEdgeParameter(const Entity& edge, QPointF point);

// Explicit dart operations take both legs followed by the chosen outlet.
ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm = 0);

// Scale width about the chosen axis and length about the top of the piece.
ContourEditResult shapeSilhouette(const PatternDocument& document, qsizetype piece,
                                 double axisX, double topScale, double bottomScale,
                                 double lengthScale = 1);
}
