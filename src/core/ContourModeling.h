#pragma once

#include "PieceGeometry.h"

namespace opc {
enum class ContourOperation { Cut, Spread, TransferDart };

struct ContourEdit {
    PatternDocument document;
    QVector<qsizetype> removedEntities;
};

struct ContourEditResult {
    std::optional<ContourEdit> edit;
    QString error;
};

ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm = 0);
}
