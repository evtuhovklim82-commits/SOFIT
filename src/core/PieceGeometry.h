#pragma once

#include "PatternDocument.h"
#include <QPolygonF>
#include <QLineF>
#include <utility>
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
    QVector<QLineF> notchAnchors; // Seam point and outward direction.
};

// Main-layer edges must form one simple ring. The allowance is derived from
// the current geometry and the stored width, so edits cannot leave stale edges.
// Returns no value for unsupported geometry or if the length bound cannot be met.
std::optional<double> seamLength(const Entity& entity);
struct SeamEdge {
    qsizetype index;
    double lengthMm;
    bool reversed;
};

struct SeamSection {
    QVector<SeamEdge> edges;
    double lengthMm = 0;
    // Locate an interior distance on a source edge, measured from its start.
    std::optional<std::pair<qsizetype, double>> notchAt(double fraction) const;
};

std::optional<SeamSection> measureSeamSection(const PatternDocument& document,
                                              const QVector<qsizetype>& indices);
PieceGeometry analyzePiece(const PatternDocument& document, qsizetype piece);

}
