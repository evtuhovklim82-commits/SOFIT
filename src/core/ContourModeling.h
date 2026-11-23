#pragma once

#include "PieceGeometry.h"

namespace opc {
enum class ContourOperation { Cut, Spread, TransferDart, TransferDartToOutlet, MoveOpenDart, Pleat, Gather };

struct ContourEdit {
    PatternDocument document;
    QVector<qsizetype> removedEntities;
};

struct ContourEditResult {
    std::optional<ContourEdit> edit;
    QString error;
};

// Explicit dart operations take both legs followed by the chosen outlet.
ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm = 0);

// Horizontal scaling about the chosen axis. Values are ratios, top first.
ContourEditResult shapeSilhouette(const PatternDocument& document, qsizetype piece,
                                 double axisX, double topScale, double bottomScale);
}
