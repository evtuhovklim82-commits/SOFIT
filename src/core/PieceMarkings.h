#pragma once

#include "PieceGeometry.h"
#include <QPainterPath>

namespace opc {
struct PieceMarkings {
    QPainterPath notches;
    QPainterPath grainline;
    QString label;
    QPointF labelOrigin;
    qsizetype notchesMissingCutOutline = 0;
    QPainterPath labelPath() const;
};

PieceMarkings buildPieceMarkings(const Piece& piece, const PieceGeometry& geometry,
                                 bool useAllowance = false);
}
