#include "Modeling.h"

#include <cmath>
#include <limits>

namespace opc {
namespace {
constexpr double endpointTolerance = 1e-6;

double distance(QPointF first, QPointF second) {
    return std::hypot(first.x() - second.x(), first.y() - second.y());
}

bool validPoint(QPointF point) {
    return std::abs(point.x()) <= 1e8 && std::abs(point.y()) <= 1e8;
}

bool validEdge(const Entity& edge) {
    return (edge.shape == Shape::Line || edge.shape == Shape::Curve)
        && validPoint(edge.a) && validPoint(edge.b)
        && (edge.shape == Shape::Line || validPoint(edge.control))
        && distance(edge.a, edge.b) > endpointTolerance;
}

QPointF interpolate(QPointF first, QPointF second, double t) {
    return {std::lerp(first.x(), second.x(), t), std::lerp(first.y(), second.y(), t)};
}

bool freeEndpoint(const PatternDocument& document, qsizetype index, QPointF point) {
    const auto& edge = document.entities()[index];
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        const auto& other = document.entities()[i];
        if (i == index || other.layer != edge.layer || other.piece != edge.piece)
            continue;
        if (other.shape == Shape::Line || other.shape == Shape::Curve || other.shape == Shape::Rectangle) {
            if (distance(point, other.a) <= endpointTolerance
                || distance(point, other.b) <= endpointTolerance)
                return false;
        }
        if (other.shape == Shape::Rectangle
            && (distance(point, {other.a.x(), other.b.y()}) <= endpointTolerance
                || distance(point, {other.b.x(), other.a.y()}) <= endpointTolerance))
            return false;
    }
    return true;
}
}

std::optional<EdgePair> splitEdge(const Entity& edge, double t) {
    if (!validEdge(edge) || !edge.seamNotches.isEmpty() || !(t > 0 && t < 1))
        return std::nullopt;

    EdgePair result{edge, edge};
    QPointF seam = interpolate(edge.a, edge.b, t);
    if (edge.shape == Shape::Curve) {
        result[0].control = interpolate(edge.a, edge.control, t);
        result[1].control = interpolate(edge.control, edge.b, t);
        seam = interpolate(result[0].control, result[1].control, t);
    }
    result[0].b = result[1].a = seam;
    if (!validEdge(result[0]) || !validEdge(result[1]))
        return std::nullopt;
    return result;
}

std::optional<EdgePair> joinEdges(const PatternDocument& document, qsizetype first,
                                  qsizetype second, double maxGapMm) {
    const auto& entities = document.entities();
    if (first < 0 || second < 0 || first >= entities.size() || second >= entities.size()
        || first == second || !std::isfinite(maxGapMm) || maxGapMm <= endpointTolerance)
        return std::nullopt;

    EdgePair joined{entities[first], entities[second]};
    if (!validEdge(joined[0]) || !validEdge(joined[1])
        || !joined[0].draftName.isEmpty() || !joined[1].draftName.isEmpty()
        || joined[0].piece != joined[1].piece || joined[0].layer != joined[1].layer)
        return std::nullopt;

    double bestGap = std::numeric_limits<double>::infinity();
    QPointF* firstEnd = nullptr;
    QPointF* secondEnd = nullptr;
    bool ambiguous = false;
    for (auto* a : {&joined[0].a, &joined[0].b}) {
        if (!freeEndpoint(document, first, *a))
            continue;
        for (auto* b : {&joined[1].a, &joined[1].b}) {
            const auto gap = distance(*a, *b);
            if (gap <= endpointTolerance || gap > maxGapMm || !freeEndpoint(document, second, *b))
                continue;
            if (gap < bestGap - endpointTolerance) {
                bestGap = gap;
                firstEnd = a;
                secondEnd = b;
                ambiguous = false;
            } else if (std::abs(gap - bestGap) <= endpointTolerance) {
                ambiguous = true;
            }
        }
    }
    if (!firstEnd || ambiguous)
        return std::nullopt;

    *firstEnd = *secondEnd = interpolate(*firstEnd, *secondEnd, 0.5);
    if (!validEdge(joined[0]) || !validEdge(joined[1]))
        return std::nullopt;
    return joined;
}

std::optional<EdgePair> dartLegs(QPointF first, QPointF apex, QPointF second) {
    if (!validPoint(first) || !validPoint(apex) || !validPoint(second)
        || distance(first, second) <= endpointTolerance)
        return std::nullopt;

    const auto base = second - first;
    const auto height = apex - first;
    const double cross = base.x() * height.y() - base.y() * height.x();
    if (std::abs(cross) <= distance(first, second) * endpointTolerance)
        return std::nullopt;

    return EdgePair{Entity{Shape::Line, first, apex, {}, -1, Layer::Construction},
                    Entity{Shape::Line, apex, second, {}, -1, Layer::Construction}};
}
}
