#include "PieceGeometry.h"

#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace opc {
namespace {
constexpr double joinTolerance = 1e-6;
constexpr double curveTolerance = 0.01;
constexpr qsizetype maxEdges = 2048;
constexpr double maxCoordinate = 1e8;
constexpr double miterLimit = 8;

struct Edge {
    QPointF start, end;
    QPointF control;
    bool curved = false;
    double chordError = 0;
    qsizetype entityIndex = -1;
    double width = 0;
    std::optional<bool> bevelAtStart;
};

double length(const QPointF& vector) {
    return std::hypot(vector.x(), vector.y());
}

double cross(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

bool near(const QPointF& a, const QPointF& b) {
    return length(a - b) <= joinTolerance;
}

bool validPoint(const QPointF& point) {
    return std::abs(point.x()) <= maxCoordinate && std::abs(point.y()) <= maxCoordinate;
}

double pointSegmentDistance(const QPointF& point, const QPointF& start, const QPointF& end) {
    const auto direction = end - start;
    const auto square = QPointF::dotProduct(direction, direction);
    if (square == 0)
        return length(point - start);
    const auto t = std::clamp(QPointF::dotProduct(point - start, direction) / square, 0.0, 1.0);
    return length(point - (start + t * direction));
}

bool segmentsCross(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d) {
    const auto oppositeSides = [](double first, double second) {
        return (first < 0 && second > 0) || (first > 0 && second < 0);
    };
    return oppositeSides(cross(b - a, c - a), cross(b - a, d - a))
        && oppositeSides(cross(d - c, a - c), cross(d - c, b - c));
}

double endpointClearance(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d) {
    return std::min({pointSegmentDistance(a, c, d), pointSegmentDistance(b, c, d),
                     pointSegmentDistance(c, a, b), pointSegmentDistance(d, a, b)});
}

double signedAreaTwice(const QPolygonF& ring) {
    double area = 0;
    for (qsizetype i = 0; i < ring.size(); ++i)
        area += cross(ring[i] - ring.front(), ring[(i + 1) % ring.size()] - ring.front());
    return area;
}

bool simpleRing(const QPolygonF& ring) {
    const auto count = ring.size();
    if (count < 3 || std::abs(signedAreaTwice(ring)) <= joinTolerance * joinTolerance)
        return false;

    for (qsizetype i = 0; i < count; ++i) {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % count];
        const auto& c = ring[(i + 2) % count];
        if (near(a, b) || pointSegmentDistance(c, a, b) <= joinTolerance
            || pointSegmentDistance(a, b, c) <= joinTolerance)
            return false;
        for (qsizetype j = i + 2; j < count; ++j) {
            if (i == 0 && j == count - 1)
                continue;
            const auto& start = ring[j];
            const auto& end = ring[(j + 1) % count];
            if (segmentsCross(a, b, start, end) || endpointClearance(a, b, start, end) <= joinTolerance)
                return false;
        }
    }
    return true;
}

// The quadratic lies within half the control-to-chord-midpoint distance
// of its parameter-matched chord. Control polygon and chord also bound length.
bool flatten(const Edge& edge, QVector<Edge>& chords, double& perimeter, double lengthBudget, int depth = 0) {
    if (chords.size() >= maxEdges || depth == 32)
        return false;

    const auto midpoint = (edge.start + edge.end) / 2;
    const auto chord = length(edge.end - edge.start);
    const auto upper = length(edge.control - edge.start) + length(edge.end - edge.control);
    const auto error = length(edge.control - midpoint) / 2;
    if (error <= curveTolerance && upper - chord <= 2 * lengthBudget) {
        auto chordEdge = edge;
        chordEdge.chordError = error;
        chords.append(chordEdge);
        perimeter += (upper + chord) / 2;
        return true;
    }

    const auto first = (edge.start + edge.control) / 2;
    const auto second = (edge.control + edge.end) / 2;
    const auto split = (first + second) / 2;
    auto left = edge;
    left.end = split;
    left.control = first;
    auto right = edge;
    right.start = split;
    right.bevelAtStart.reset();
    right.control = second;
    return flatten(left, chords, perimeter, lengthBudget / 2, depth + 1)
        && flatten(right, chords, perimeter, lengthBudget / 2, depth + 1);
}

// Adjacent control hulls must lie on opposite sides of a line through
// their shared endpoint. Non-adjacent chords must be farther apart than
// the sum of their error bounds. Reject ambiguous close approaches.
bool separatedCurves(const QVector<Edge>& chords) {
    for (qsizetype i = 0; i < chords.size(); ++i) {
        const auto& a = chords[i];
        const auto& b = chords[(i + 1) % chords.size()];
        if (a.curved || b.curved) {
            const auto before = a.end - a.start;
            const auto after = b.end - b.start;
            const auto axis = before / length(before) + after / length(after);
            if ((a.curved && QPointF::dotProduct(a.control - a.end, axis) > 0)
                || (b.curved && QPointF::dotProduct(b.control - b.start, axis) < 0))
                return false;
        }
        for (qsizetype j = i + 2; j < chords.size(); ++j) {
            if (i == 0 && j == chords.size() - 1)
                continue;
            const auto& c = chords[j];
            if ((a.curved || c.curved) && endpointClearance(a.start, a.end, c.start, c.end)
                    <= a.chordError + c.chordError + joinTolerance)
                return false;
        }
    }
    return true;
}

QPolygonF offsetRing(const QPolygonF& ring, double width) {
    if (width <= joinTolerance)
        return {};

    QPolygonF offset;
    const double orientation = signedAreaTwice(ring) > 0 ? 1 : -1;
    const auto normal = [orientation](const QPointF& direction) {
        return orientation * QPointF(direction.y(), -direction.x()) / length(direction);
    };
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const auto& vertex = ring[i];
        const auto before = normal(vertex - ring[(i + ring.size() - 1) % ring.size()]);
        const auto after = normal(ring[(i + 1) % ring.size()] - vertex);
        const auto denominator = 1 + QPointF::dotProduct(before, after);
        if (denominator <= 0)
            return {};
        const auto shift = width * (before + after) / denominator;
        if (length(shift) > width * miterLimit || !validPoint(vertex + shift))
            return {};
        offset.append(vertex + shift);
    }
    if (!simpleRing(offset) || signedAreaTwice(offset) * orientation <= std::abs(signedAreaTwice(ring))
        || !offset.containsPoint(ring.front(), Qt::OddEvenFill))
        return {};

    // Refuse offsets that cross the original ring or approach another part
    // of it too closely (for example when an offset overruns a narrow notch).
    const double minimum = width - joinTolerance;
    for (qsizetype i = 0; i < offset.size(); ++i) {
        const auto& a = offset[i];
        const auto& b = offset[(i + 1) % offset.size()];
        const auto source = ring[(i + 1) % ring.size()] - ring[i];
        if (QPointF::dotProduct(b - a, source) <= 0)
            return {};
        for (qsizetype j = 0; j < ring.size(); ++j) {
            const auto& c = ring[j];
            const auto& d = ring[(j + 1) % ring.size()];
            if (segmentsCross(a, b, c, d) || endpointClearance(a, b, c, d) < minimum)
                return {};
        }
    }
    return offset;
}

QPolygonF offsetVariableRing(const QPolygonF& ring, const QVector<Edge>& chords, bool bevel) {
    const double orientation = signedAreaTwice(ring) > 0 ? 1 : -1;
    QPolygonF offset;
    QVector<qsizetype> edgeStarts;
    QVector<qsizetype> edgeEnds;
    for (qsizetype i = 0; i < ring.size(); ++i) {
        edgeEnds.append(offset.size());
        const auto previous = (i + ring.size() - 1) % ring.size();
        const auto& vertex = ring[i];
        const auto before = (vertex - ring[previous]) / length(vertex - ring[previous]);
        const auto afterEdge = ring[(i + 1) % ring.size()] - vertex;
        const auto after = afterEdge / length(afterEdge);
        const auto previousWidth = chords[previous].width;
        const auto nextWidth = chords[i].width;
        const auto a = vertex + previousWidth * orientation * QPointF(before.y(), -before.x());
        const auto b = vertex + nextWidth * orientation * QPointF(after.y(), -after.x());
        if (!validPoint(a) || !validPoint(b))
            return {};
        if (near(a, b)) {
            edgeStarts.append(offset.size());
            offset.append(b);
            continue;
        }
        const auto denominator = cross(before, after);
        const bool nearlyStraight = QPointF::dotProduct(before, after) > 0
            && std::abs(denominator) < 0.05;
        const bool convexCorner = denominator * orientation > 1e-8 && !nearlyStraight
            && (chords[previous].entityIndex != chords[i].entityIndex
                || (!chords[previous].curved && !chords[i].curved));
        if ((chords[i].bevelAtStart.value_or(bevel) && convexCorner) || (nearlyStraight && previousWidth != nextWidth)) {
            offset.append(a);
            edgeStarts.append(offset.size());
            offset.append(b);
            continue;
        }
        if (std::abs(denominator) <= 1e-8)
            return {};
        const auto corner = a + before * (cross(b - a, after) / denominator);
        if (!validPoint(corner) || length(corner - vertex) > miterLimit * std::max(previousWidth, nextWidth))
            return {};
        edgeStarts.append(offset.size());
        offset.append(corner);
    }
    for (qsizetype i = 0; i < chords.size(); ++i) {
        const auto next = (i + 1) % chords.size();
        if (QPointF::dotProduct(offset[edgeEnds[next]] - offset[edgeStarts[i]],
                                chords[i].end - chords[i].start) <= 0)
            return {};
    }
    if (!simpleRing(offset) || signedAreaTwice(offset) * orientation <= std::abs(signedAreaTwice(ring)))
        return {};
    for (qsizetype i = 0; i < offset.size(); ++i) {
        const auto& a = offset[i];
        const auto& b = offset[(i + 1) % offset.size()];
        if (ring.containsPoint((a + b) / 2, Qt::OddEvenFill)) {
            bool onBoundary = false;
            for (const auto& edge : chords)
                onBoundary |= pointSegmentDistance((a + b) / 2, edge.start, edge.end) <= joinTolerance;
            if (!onBoundary)
                return {};
        }
        for (const auto& edge : chords) {
            if (segmentsCross(a, b, edge.start, edge.end)
                && endpointClearance(a, b, edge.start, edge.end) > joinTolerance)
                return {};
        }
    }
    return offset;
}

double quadraticLength(const QPointF& a, const QPointF& control, const QPointF& b,
                       double budget, int depth = 0) {
    const auto chord = length(b - a);
    const auto upper = length(control - a) + length(b - control);
    if (upper - chord <= 2 * budget)
        return (chord + upper) / 2;
    if (depth == 22)
        return std::numeric_limits<double>::quiet_NaN();
    const auto left = (a + control) / 2;
    const auto right = (control + b) / 2;
    const auto middle = (left + right) / 2;
    return quadraticLength(a, left, middle, budget / 2, depth + 1)
        + quadraticLength(middle, right, b, budget / 2, depth + 1);
}
}

std::optional<double> seamLength(const Entity& entity) {
    if (!validPoint(entity.a) || !validPoint(entity.b))
        return {};
    if (entity.shape == Shape::Line)
        return length(entity.b - entity.a);
    if (entity.shape != Shape::Curve || !validPoint(entity.control))
        return {};
    const auto measured = quadraticLength(entity.a, entity.control, entity.b, 0.001);
    return std::isfinite(measured) ? std::optional<double>(measured) : std::nullopt;
}

std::optional<std::pair<qsizetype, double>> SeamSection::notchAt(double fraction) const {
    if (!(fraction > 0 && fraction < 1))
        return {};
    auto distance = fraction * lengthMm;
    for (const auto& edge : edges) {
        if (distance < edge.lengthMm - joinTolerance) {
            if (distance <= joinTolerance)
                return {}; // A junction is already a visible reference.
            const auto position = distance / edge.lengthMm;
            return std::pair{edge.index, edge.reversed ? 1 - position : position};
        }
        distance -= edge.lengthMm;
    }
    return {};
}

std::optional<SeamSection> measureSeamSection(const PatternDocument& document,
                                              const QVector<qsizetype>& indices) {
    if (indices.isEmpty() || indices.size() > maxEdges)
        return {};
    const auto& entities = document.entities();
    SeamSection section;
    QVector<SeamEdge> unordered;
    qsizetype piece = -1;
    for (const auto index : indices) {
        if (index < 0 || index >= entities.size())
            return {};
        const auto& edge = entities[index];
        if (unordered.isEmpty())
            piece = edge.piece;
        const auto measured = seamLength(edge);
        if (edge.piece != piece || edge.layer != Layer::Main || !measured || *measured <= joinTolerance)
            return {};
        unordered.append({index, *measured, false});
        section.lengthMm += *measured;
    }
    const auto endpoint = [&](qsizetype i) {
        const auto& edge = entities[unordered[i / 2].index];
        return i % 2 ? edge.b : edge.a;
    };
    QVector<qsizetype> links(2 * unordered.size(), -1);
    for (qsizetype i = 0; i < links.size(); ++i) {
        for (auto j = i + 1; j < links.size(); ++j) {
            if (!near(endpoint(i), endpoint(j)))
                continue;
            if (i / 2 == j / 2 || links[i] != -1 || links[j] != -1)
                return {};
            links[i] = j;
            links[j] = i;
        }
    }
    qsizetype endCount = 0;
    qsizetype current = -1;
    for (qsizetype i = 0; i < links.size(); ++i) {
        if (links[i] == -1) {
            if (current == -1)
                current = i;
            ++endCount;
        }
    }
    if (endCount != 2)
        return {};
    while (current != -1 && section.edges.size() < indices.size()) {
        auto edge = unordered[current / 2];
        edge.reversed = current % 2;
        section.edges.append(edge);
        current = links[current ^ 1];
    }
    if (section.edges.size() != indices.size())
        return {};
    return section;
}

PieceGeometry analyzePiece(const PatternDocument& document, qsizetype piece) {
    if (piece < 0 || piece >= document.pieces().size())
        return {};
    const auto linked = document.inspectSeamLinks();

    QVector<Edge> edges;
    for (qsizetype index = 0; index < document.entities().size(); ++index) {
        const auto& entity = document.entities()[index];
        if (entity.piece != piece || entity.layer != Layer::Main)
            continue;
        if (!validPoint(entity.a) || !validPoint(entity.b))
            return {ContourError::TooComplex};
        const auto firstEdge = edges.size();
        if (entity.shape == Shape::Rectangle) {
            const QPointF c(entity.a.x(), entity.b.y());
            const QPointF d(entity.b.x(), entity.a.y());
            edges.append({{entity.a, c, {}}, {c, entity.b, {}}, {entity.b, d, {}}, {d, entity.a, {}}});
        } else if (entity.shape == Shape::Line || entity.shape == Shape::Curve) {
            if (entity.shape == Shape::Curve && !validPoint(entity.control))
                return {ContourError::TooComplex};
            if (entity.shape == Shape::Curve && pointSegmentDistance(
                    (2 * entity.control + entity.a + entity.b) / 4, entity.a, entity.b) <= joinTolerance
                && (QPointF::dotProduct(entity.control - entity.a, entity.b - entity.a) < 0
                    || QPointF::dotProduct(entity.control - entity.b, entity.a - entity.b) < 0))
                return {ContourError::Degenerate};
            edges.append({entity.a, entity.b, entity.control, entity.shape == Shape::Curve});
        } else {
            return {ContourError::UnsupportedShape};
        }
        for (auto i = firstEdge; i < edges.size(); ++i) {
            edges[i].entityIndex = index;
            edges[i].width = linked.allowances.value(index,
                entity.seamAllowanceMm.value_or(document.pieces()[piece].allowanceMm));
        }
        if (edges.size() > maxEdges)
            return {ContourError::TooComplex};
    }
    if (edges.isEmpty())
        return {};

    // Encode each endpoint as 2 * edge index plus its end flag.
    const auto endpoint = [&edges](qsizetype index) {
        const auto& edge = edges[index / 2];
        return index % 2 ? edge.end : edge.start;
    };
    QVector<qsizetype> links(2 * edges.size(), -1);
    bool open = false;
    for (qsizetype i = 0; i < edges.size(); ++i) {
        if (near(edges[i].start, edges[i].end))
            return {ContourError::Degenerate};
    }
    for (qsizetype i = 0; i < links.size(); ++i) {
        for (qsizetype j = i + 1; j < links.size(); ++j) {
            if (!near(endpoint(i), endpoint(j)))
                continue;
            if (links[i] >= 0 || links[j] >= 0)
                return {ContourError::Branched};
            links[i] = j;
            links[j] = i;
        }
        open |= links[i] < 0;
    }
    if (open)
        return {ContourError::Open};

    PieceGeometry geometry;
    QVector<Edge> chords;
    const auto start = edges.front().start;
    QPainterPath path(start);
    struct NotchSpan {
        qsizetype first, end, entity;
        bool reversed;
    };
    QVector<NotchSpan> spans;
    bool conflictingCorners = false;
    qsizetype current = 0;
    for (qsizetype i = 0; i < edges.size(); ++i) {
        auto edge = edges[current / 2];
        const bool reversed = current % 2;
        const auto firstChord = chords.size();
        if (reversed)
            std::swap(edge.start, edge.end);
        const auto& source = document.entities()[edge.entityIndex];
        const auto& adjoining = document.entities()[edges[links[current] / 2].entityIndex];
        const auto nextCorner = reversed ? source.bevelAtEnd : source.bevelAtStart;
        const auto previousCorner = links[current] % 2 ? adjoining.bevelAtEnd : adjoining.bevelAtStart;
        conflictingCorners |= nextCorner && previousCorner && nextCorner != previousCorner;
        edge.bevelAtStart = nextCorner ? nextCorner : previousCorner;
        current = links[current ^ 1];
        if (current == 0 && i + 1 < edges.size())
            return {ContourError::Disconnected};

        // Join each pair at the same position, with at most the join tolerance
        // of adjustment across the entire pair.
        edge.start = chords.isEmpty() ? start : chords.back().end;
        edge.end = current == 0 ? start : (edge.end + endpoint(current)) / 2;
        if (edge.curved) {
            path.quadTo(edge.control, edge.end);
            if (!flatten(edge, chords, geometry.perimeterMm, curveTolerance / edges.size()))
                return {ContourError::TooComplex};
        } else {
            path.lineTo(edge.end);
            geometry.perimeterMm += length(edge.end - edge.start);
            if (chords.size() >= maxEdges)
                return {ContourError::TooComplex};
            chords.append(edge);
        }
        if (linked.notches.contains(edge.entityIndex) || !source.seamNotches.isEmpty() || !source.seamSymbols.isEmpty())
            spans.append({firstChord, chords.size(), edge.entityIndex, reversed});
    }
    QPolygonF ring;
    ring.reserve(chords.size());
    for (const auto& chord : chords)
        ring.append(chord.start);
    if (!simpleRing(ring))
        return {ContourError::SelfIntersecting};
    if (!separatedCurves(chords))
        return {ContourError::UncertainCurves};

    geometry.error = ContourError::None;
    geometry.bounds = path.boundingRect();
    const auto width = chords.front().width;
    geometry.allowanceFailed = conflictingCorners;
    if (!conflictingCorners && std::ranges::any_of(chords, [](const Edge& edge) { return edge.width > 0; })) {
        const bool uniform = std::ranges::all_of(chords, [width](const Edge& edge) { return edge.width == width; });
        const auto bevel = document.pieces()[piece].bevelAllowanceCorners;
        const bool hasOverride = std::ranges::any_of(chords, [](const Edge& edge) {
            return edge.bevelAtStart.has_value();
        });
        geometry.allowance = uniform && !bevel && !hasOverride ? offsetRing(ring, width)
                                                              : offsetVariableRing(ring, chords, bevel);
        geometry.allowanceFailed = geometry.allowance.isEmpty();
    }
    const double orientation = signedAreaTwice(ring) > 0 ? 1 : -1;
    for (const auto& span : spans) {
        QVector<double> distances{0};
        for (auto i = span.first; i < span.end; ++i)
            distances.append(distances.back() + length(chords[i].end - chords[i].start));
        auto fractions = document.entities()[span.entity].seamNotches;
        for (double fraction : linked.notches.value(span.entity)) {
            if (std::ranges::none_of(fractions, [fraction](double existing) {
                    return std::abs(existing - fraction) < 1e-9;
                }))
                fractions.append(fraction);
        }
        const auto anchorAt = [&](double fraction) -> std::optional<QLineF> {
            const auto distance = (span.reversed ? 1 - fraction : fraction) * distances.back();
            const auto index = std::upper_bound(distances.begin(), distances.end(), distance)
                - distances.begin() - 1;
            if (index < 0 || index + 1 >= distances.size())
                return {};
            const auto& chord = chords[span.first + index];
            const auto tangent = (chord.end - chord.start) / (distances[index + 1] - distances[index]);
            const auto anchor = chord.start + tangent * (distance - distances[index]);
            return QLineF(anchor, anchor + orientation * QPointF(tangent.y(), -tangent.x()));
        };
        for (const auto fraction : fractions) {
            if (const auto anchor = anchorAt(fraction))
                geometry.notchAnchors.append(*anchor);
        }
        for (const auto& symbol : document.entities()[span.entity].seamSymbols) {
            if (const auto anchor = anchorAt(symbol.fraction))
                geometry.symbolAnchors.append({symbol, *anchor});
        }
    }
    geometry.contour = std::move(ring);
    return geometry;
}
}
