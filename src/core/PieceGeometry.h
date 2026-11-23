#pragma once

#include "PatternDocument.h"
#include <QPolygonF>
#include <QRectF>

namespace opc {

enum class ContourError {
    None, Empty, UnsupportedShape, Degenerate, Open, Branched, Disconnected,
    SelfIntersecting, UncertainCurves, TooComplex
};

struct PieceGeometry {
    ContourError error = ContourError::Empty;
    double perimeterMm = 0;
    QRectF bounds;
    QPolygonF allowance;
    QPolygonF contour;
    bool allowanceFailed = false;
};

// Main-layer edges must form one simple ring. The allowance is derived from
// the current geometry and the stored width, so edits cannot leave stale edges.
PieceGeometry analyzePiece(const PatternDocument& document, qsizetype piece);

}
