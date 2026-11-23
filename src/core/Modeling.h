#pragma once

#include "PatternDocument.h"

namespace opc {
using EdgePair = std::array<Entity, 2>;

// Split at the curve parameter t (proportional to length only for a line).
std::optional<EdgePair> splitEdge(const Entity& edge, double t);

// Join the nearest unambiguous pair of free endpoints in the same piece/layer.
std::optional<EdgePair> joinEdges(const PatternDocument& document, qsizetype first,
                                  qsizetype second, double maxGapMm);

// Update a manual point or edge and optionally carry coincident endpoints in its piece and layer.
std::optional<PatternDocument> editGeometry(const PatternDocument& document, qsizetype index,
                                            QPointF a, QPointF b, QPointF control, bool moveConnected);

std::optional<EdgePair> dartLegs(QPointF first, QPointF apex, QPointF second);
}
