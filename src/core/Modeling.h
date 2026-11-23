#pragma once

#include "PatternDocument.h"

namespace opc {
using EdgePair = std::array<Entity, 2>;

// Split at the curve parameter t (proportional to length only for a line).
std::optional<EdgePair> splitEdge(const Entity& edge, double t);

// Join the nearest unambiguous pair of free endpoints in the same piece/layer.
std::optional<EdgePair> joinEdges(const PatternDocument& document, qsizetype first,
                                  qsizetype second, double maxGapMm);

std::optional<EdgePair> dartLegs(QPointF first, QPointF apex, QPointF second);
}
