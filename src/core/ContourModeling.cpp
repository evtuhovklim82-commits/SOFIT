#include "ContourModeling.h"
#include "Production.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <ranges>
#include <span>
#include <utility>

namespace opc {
namespace {
constexpr double tolerance = 1e-6;

double length(QPointF vector) { return std::hypot(vector.x(), vector.y()); }
double cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
bool near(QPointF a, QPointF b) { return length(a - b) <= tolerance; }

ContourEditResult failure(const char* message) {
    return {{}, QCoreApplication::translate("ContourModeling", message)};
}

ContourEditResult invalidContour(ContourError error) {
    switch (error) {
    case ContourError::Empty: return failure("The piece has no main contour.");
    case ContourError::Open: return failure("The contour has open ends.");
    case ContourError::Branched: return failure("The contour has a shared endpoint with more than two edges.");
    case ContourError::Disconnected: return failure("The piece has multiple disconnected contours.");
    case ContourError::SelfIntersecting: return failure("The contour crosses, touches, or overlaps itself.");
    case ContourError::Degenerate: return failure("The contour has a zero-length or folded edge.");
    case ContourError::TooComplex: return failure("The contour exceeds the geometry limits.");
    default: return failure("The contour could not be validated.");
    }
}

qsizetype insertBoundaryPoint(QPolygonF& ring, QPointF point) {
    for (qsizetype i = 0; i < ring.size(); ++i) {
        if (near(point, ring[i]))
            return i;
    }
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const auto start = ring[i];
        const auto edge = ring[(i + 1) % ring.size()] - start;
        const auto unit = edge / length(edge);
        const double distance = QPointF::dotProduct(point - start, unit);
        if (distance > tolerance && distance < length(edge) - tolerance
            && std::abs(cross(point - start, unit)) <= tolerance) {
            ring.insert(i + 1, start + distance * unit);
            return i + 1;
        }
    }
    return -1;
}

QPolygonF arc(const QPolygonF& ring, qsizetype from, qsizetype to) {
    QPolygonF points;
    for (auto i = from; i != to; i = (i + 1) % ring.size())
        points.append(ring[i]);
    points.append(ring[to]);
    return points;
}

bool straight(const QPolygonF& points) {
    const auto direction = points.last() - points.first();
    if (length(direction) <= tolerance)
        return false;
    const auto unit = direction / length(direction);
    return std::ranges::all_of(points, [&](QPointF point) {
        return std::abs(cross(point - points.first(), unit)) <= tolerance;
    });
}

bool interiorSegment(const QPolygonF& ring, QPointF a, QPointF b) {
    if (near(a, b) || !ring.containsPoint((a + b) / 2, Qt::OddEvenFill))
        return false;
    const auto segmentLength = length(b - a);
    const auto direction = (b - a) / segmentLength;
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const auto c = ring[i];
        const auto d = ring[(i + 1) % ring.size()];
        const bool incident = near(a, c) || near(a, d) || near(b, c) || near(b, d);
        const auto edgeDirection = (d - c) / length(d - c);
        if (incident) {
            const auto endpoint = near(a, c) || near(b, c) ? c : d;
            const auto other = near(endpoint, c) ? d : c;
            const auto toward = near(endpoint, a) ? direction : -direction;
            const auto along = (other - endpoint) / length(other - endpoint);
            if (std::abs(cross(toward, along)) <= 1e-12
                && QPointF::dotProduct(toward, along) > 0)
                return false;
            continue;
        }
        const double cSide = cross(direction, c - a);
        const double dSide = cross(direction, d - a);
        if (std::abs(cSide) <= tolerance && std::abs(dSide) <= tolerance) {
            const double cProjection = QPointF::dotProduct(c - a, direction);
            const double dProjection = QPointF::dotProduct(d - a, direction);
            if (std::max(cProjection, dProjection) >= -tolerance
                && std::min(cProjection, dProjection) <= segmentLength + tolerance)
                return false;
        } else if ((cSide <= tolerance && dSide >= -tolerance)
                   || (dSide <= tolerance && cSide >= -tolerance)) {
            const double aSide = cross(edgeDirection, a - c);
            const double bSide = cross(edgeDirection, b - c);
            if ((aSide <= tolerance && bSide >= -tolerance)
                || (bSide <= tolerance && aSide >= -tolerance))
                return false;
        }
    }
    return true;
}

bool splitBoundary(QVector<Entity>& edges, QPointF point) {
    if (std::ranges::any_of(edges, [point](const Entity& edge) {
            return near(edge.a, point) || near(edge.b, point);
        }))
        return true;
    for (qsizetype i = 0; i < edges.size(); ++i) {
        auto first = edges[i];
        const auto parameter = contourEdgeParameter(first, point);
        if (!parameter)
            continue;
        auto second = first;
        const auto lerp = [t = *parameter](QPointF a, QPointF b) { return a + t * (b - a); };
        if (first.shape == Shape::Curve) {
            first.control = lerp(first.a, first.control);
            second.control = lerp(second.control, second.b);
            first.b = second.a = lerp(first.control, second.control);
        } else {
            first.b = second.a = lerp(first.a, first.b);
        }
        edges[i] = first;
        edges.insert(i + 1, second);
        return true;
    }
    return false;
}

QVector<Entity> traceBoundary(QVector<Entity>& edges, QPointF start, QPointF end,
                              std::optional<QPointF> nextPoint = std::nullopt) {
    QVector<Entity> result;
    auto current = start;
    while (!near(current, end)) {
        const auto next = std::ranges::find_if(edges, [&](const Entity& edge) {
            return (near(current, edge.a) || near(current, edge.b))
                && (!nextPoint || contourEdgeParameter(edge, *nextPoint));
        });
        if (next == edges.end())
            return {};
        auto edge = *next;
        edges.erase(next);
        if (!near(current, edge.a))
            std::swap(edge.a, edge.b);
        result.append(edge);
        current = edge.b;
        nextPoint.reset();
    }
    return result;
}

// Find simple roots by partitioning the interval at the derivative's roots.
QVector<double> crossingRoots(std::span<const double> polynomial) {
    const auto magnitude = std::ranges::max(polynomial | std::views::transform([](double value) {
        return std::abs(value);
    }));
    while (polynomial.size() > 1 && std::abs(polynomial.back()) <= 1e-14 * magnitude)
        polynomial = polynomial.first(polynomial.size() - 1);
    if (polynomial.size() == 1)
        return {};
    if (polynomial.size() == 2) {
        const double t = -polynomial[0] / polynomial[1];
        return t > 0 && t < 1 ? QVector<double>{t} : QVector<double>{};
    }
    std::array<double, 4> derivative{};
    for (size_t i = 1; i < polynomial.size(); ++i)
        derivative[i - 1] = i * polynomial[i];
    const auto evaluate = [&](double t) {
        double value = 0;
        for (const auto coefficient : polynomial | std::views::reverse)
            value = std::fma(value, t, coefficient);
        return value;
    };
    auto bounds = crossingRoots(std::span(derivative).first(polynomial.size() - 1));
    bounds.prepend(0);
    bounds.append(1);
    QVector<double> roots;
    for (qsizetype i = 1; i < bounds.size(); ++i) {
        double low = bounds[i - 1], high = bounds[i];
        const double lowValue = evaluate(low), highValue = evaluate(high);
        if (lowValue == 0 || highValue == 0 || std::signbit(lowValue) == std::signbit(highValue))
            continue;
        for (int iteration = 0; iteration < 52; ++iteration) {
            const double middle = std::midpoint(low, high);
            if (std::signbit(evaluate(middle)) == std::signbit(lowValue))
                low = middle;
            else
                high = middle;
        }
        roots.append(std::midpoint(low, high));
    }
    return roots;
}

void appendRing(PatternDocument& document, qsizetype piece, const QPolygonF& ring) {
    for (qsizetype i = 0; i < ring.size(); ++i) {
        document.insert(document.entities().size(),
                        {Shape::Line, ring[i], ring[(i + 1) % ring.size()], {}, piece});
    }
}
}

std::optional<double> contourEdgeParameter(const Entity& edge, QPointF point) {
    if (near(point, edge.a))
        return 0;
    if (near(point, edge.b))
        return 1;
    if (edge.shape == Shape::Line) {
        const auto direction = edge.b - edge.a;
        const auto distance = length(direction);
        if (distance <= tolerance)
            return {};
        const auto offset = point - edge.a;
        const auto unit = direction / distance;
        const auto t = QPointF::dotProduct(offset, unit) / distance;
        if (t > 0 && t < 1 && std::abs(cross(offset, unit)) <= tolerance)
            return t;
        return {};
    }
    if (edge.shape != Shape::Curve)
        return {};
    const auto linear = 2 * (edge.control - edge.a);
    const auto quadratic = (edge.b - edge.a) - linear;
    const auto offset = edge.a - point;
    const auto axis = [&](QPointF vector) {
        return std::max(std::abs(linear.x()), std::abs(quadratic.x()))
             >= std::max(std::abs(linear.y()), std::abs(quadratic.y())) ? vector.x() : vector.y();
    };
    const auto a = axis(quadratic), b = axis(linear), c = axis(offset);
    const auto valid = [&](double t) -> std::optional<double> {
        if (t > 0 && t < 1 && length(offset + t * (linear + t * quadratic)) <= tolerance)
            return t;
        return {};
    };
    if (std::abs(a) <= 1e-12)
        return b == 0 ? std::nullopt : valid(-c / b);
    const auto discriminant = b * b - 4 * a * c;
    if (discriminant < 0)
        return {};
    const auto q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
    if (const auto t = valid(q / a))
        return t;
    return q == 0 ? std::nullopt : valid(c / q);
}

std::optional<QPointF> SpreadMotion::offsetFor(const Entity& guide) const {
    if (guide.shape != Shape::Line && guide.shape != Shape::Curve)
        return {};
    const auto guidePoint = [&](double t) {
        const double u = 1 - t;
        return guide.shape == Shape::Curve
            ? u * u * guide.a + 2 * u * t * guide.control + t * t * guide.b
            : u * guide.a + t * guide.b;
    };
    auto start = guide.a;
    const int segments = guide.shape == Shape::Curve ? 128 : 1;
    for (int i = 1; i <= segments; ++i) {
        const auto end = guidePoint(double(i) / segments);
        const Entity chord{Shape::Line, start, end};
        for (qsizetype j = 1; j < cut.size(); ++j) {
            const Entity slash{Shape::Line, cut[j - 1], cut[j]};
            if (QLineF(start, end).intersects(QLineF(slash.a, slash.b)) == QLineF::BoundedIntersection
                || contourEdgeParameter(slash, start) || contourEdgeParameter(slash, end)
                || contourEdgeParameter(chord, slash.a) || contourEdgeParameter(chord, slash.b))
                return {};
        }
        start = end;
    }
    return movedPanel.containsPoint(guidePoint(0.5), Qt::OddEvenFill) ? translation : QPointF{};
}

ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm, const QVector<double>& outletWeights) {
    const bool redistribute = redistributesDarts(operation);
    const bool splitDart = operation == ContourOperation::SplitDart;
    const bool openDart = operation == ContourOperation::MoveOpenDart;
    const bool explicitOutlet = openDart || operation == ContourOperation::TransferDartToOutlet;
    const bool dart = explicitOutlet || operation == ContourOperation::TransferDart;
    const bool pleatTabs = operation == ContourOperation::PleatWithTabs;
    const bool pleat = operation == ContourOperation::Pleat || pleatTabs;
    const bool gather = operation == ContourOperation::Gather;
    const bool spread = usesSpreadWidth(operation);
    const qsizetype guideCount = redistribute ? 5 : dart ? 3 : 1;
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    std::optional<Entity> curvedSeam;
    if (splitDart ? !validDartOutletCount(guides.size()) : redistribute && guides.size() != guideCount)
        return failure("Select the source dart leg pairs and the required outlet lines.");
    double weightTotal = 0;
    if (!outletWeights.isEmpty()) {
        if (!splitDart || outletWeights.size() != guides.size() - 2)
            return failure("Enter one weight per dart outlet.");
        for (const double weight : outletWeights) {
            if (!std::isfinite(weight) || weight <= 0)
                return failure("Dart outlet weights must be finite and positive.");
            weightTotal += weight;
        }
        if (!std::isfinite(weightTotal))
            return failure("The sum of dart outlet weights is too large.");
    }
    if (splitDart && outletWeights.isEmpty() && !(spreadMm > 0 && spreadMm < 100))
        return failure("Enter a first-outlet share between 0 and 100 percent.");
    if (pleatTabs && (guides.size() != 1 || guides.front() < 0 || guides.front() >= document.entities().size()
                      || document.entities()[guides.front()].shape != Shape::Line))
        return failure("Fold extensions require a single straight guide.");
    if (spread && (guides.isEmpty() || guides.size() > 32))
        return failure("Use one to 32 connected spread guide segments.");
    if (!redistribute && !spread && guides.size() != guideCount)
        return failure(dart ? "Select two dart legs and an outlet line on the Construction layer."
                            : "Select one cut line on the Construction layer.");
    if (spread && (!std::isfinite(spreadMm) || spreadMm <= tolerance))
        return failure("Enter a positive spread width.");
    const auto& metadata = document.pieces()[piece];
    if (!metadata.notches.isEmpty()
        || std::ranges::any_of(document.production().rules, [piece](const auto& rule) { return rule.piece == piece; })
        || std::ranges::any_of(document.production().placements, [piece](const auto& placement) {
               return placement.piece == piece;
           }))
        return failure("This piece has notches, grading rules, or fabric placements. Remove those before modeling.");

    const auto& entities = document.entities();
    if (std::ranges::any_of(entities, [piece](const auto& edge) {
            return edge.piece == piece && (edge.seamAllowanceMm || !edge.seamNotches.isEmpty()
                || edge.bevelAtStart || edge.bevelAtEnd || !edge.seamSymbols.isEmpty());
        }))
        return failure("Clear edge allowances, corner overrides and automatic notches before modeling this piece.");
    QSet<qsizetype> guideSet;
    for (qsizetype i = 0; i < guides.size(); ++i) {
        const auto index = guides[i];
        if (index < 0 || index >= entities.size() || guideSet.contains(index))
            return failure("Select distinct lines for the modeling operation.");
        const auto& guide = entities[index];
        const bool contourLeg = openDart && i < 2;
        const bool assigned = contourLeg ? guide.piece == piece : guide.piece == -1 || guide.piece == piece;
        const auto layer = contourLeg ? Layer::Main : Layer::Construction;
        const bool curvedGuideAllowed = dividesPiece(operation) || spread;
        const bool cutCurve = curvedGuideAllowed && guide.shape == Shape::Curve;
        if ((!cutCurve && guide.shape != Shape::Line) || guide.layer != layer || !guide.draftName.isEmpty()
            || !assigned || !std::isfinite(length(guide.b - guide.a)) || near(guide.a, guide.b))
            return failure(openDart ? "Use two straight main-contour dart legs and one manual construction outlet."
                            : curvedGuideAllowed ? "Use a nonzero construction line or quadratic curve."
                            : "Use nonzero manual construction lines assigned to this piece or unassigned.");
        guideSet.insert(index);
    }

    QVector<qsizetype> removed;
    bool curvedBoundary = redistribute;
    for (qsizetype i = 0; i < entities.size(); ++i) {
        const auto& entity = entities[i];
        if (guideSet.contains(i)) {
            removed.append(i);
        } else if (entity.piece == piece) {
            if (!entity.draftName.isEmpty())
                return failure("This piece is generated by a parametric draft.");
            if (entity.layer != Layer::Main)
                return failure("Remove or unassign the other construction and allowance objects on this piece.");
            if (pleatTabs && entity.shape == Shape::Curve)
                return failure("Fold extensions currently require a straight-edged piece.");
            const bool allowedCurve = supportsCurvedBoundary(operation) && entity.shape == Shape::Curve;
            if (!allowedCurve && entity.shape != Shape::Line && entity.shape != Shape::Rectangle)
                return failure("Use straight contour edges for this operation.");
            curvedBoundary |= allowedCurve;
            removed.append(i);
        }
    }
    auto geometry = analyzePiece(document, piece);
    if (geometry.error != ContourError::None)
        return invalidContour(geometry.error);
    auto ring = std::move(geometry.contour);

    QVector<Entity> firstEdges, secondEdges;
    QPolygonF first;
    QPolygonF second;
    QVector<Entity> markings;
    std::optional<SpreadMotion> motion;
    QVector<Entity> boundary;
    if (curvedBoundary) {
        for (const auto index : removed) {
            const auto& edge = entities[index];
            if (guideSet.contains(index) && edge.layer != Layer::Main)
                continue;
            if (edge.shape != Shape::Rectangle) {
                boundary.append(edge);
                continue;
            }
            const QPointF c(edge.a.x(), edge.b.y()), d(edge.b.x(), edge.a.y());
            boundary.append({{Shape::Line, edge.a, c}, {Shape::Line, c, edge.b},
                             {Shape::Line, edge.b, d}, {Shape::Line, d, edge.a}});
        }
    }
    const auto splitAt = [&](const QVector<QPointF>& ends) {
        for (const auto& end : ends) {
            if (!splitBoundary(boundary, end))
                return false;
        }
        PatternDocument splitSource;
        splitSource.pieces().append({metadata.name});
        for (auto edge : boundary) {
            edge.piece = 0;
            splitSource.insert(splitSource.entities().size(), edge);
        }
        auto splitGeometry = analyzePiece(splitSource, 0);
        if (splitGeometry.error != ContourError::None)
            return false;
        ring = std::move(splitGeometry.contour);
        return true;
    };
    if (redistribute) {
        const auto& firstLeg = entities[guides.front()];
        const auto isPivot = [&](QPointF point) {
            return std::ranges::all_of(guides, [&](qsizetype index) {
                const auto& line = entities[index];
                return near(point, line.a) || near(point, line.b);
            });
        };
        const auto pivot = isPivot(firstLeg.a) ? firstLeg.a : firstLeg.b;
        if (!isPivot(pivot) || !ring.containsPoint(pivot, Qt::OddEvenFill))
            return failure("All dart guides must share one pivot inside the piece.");
        QVector<QPointF> mouths;
        for (const auto index : guides) {
            const auto& line = entities[index];
            const auto mouth = near(pivot, line.a) ? line.b : line.a;
            if (std::ranges::any_of(mouths, [&](QPointF point) { return near(point, mouth); }))
                return failure("Dart guides must end at distinct points on the contour.");
            mouths.append(mouth);
        }
        if (!splitAt(mouths))
            return failure("All dart guides must end on the exact contour.");
        QVector<qsizetype> vertices;
        for (const auto mouth : mouths) {
            const auto vertex = insertBoundaryPoint(ring, mouth);
            if (vertex < 0 || !interiorSegment(ring, pivot, ring[vertex]))
                return failure("Each guide must stay inside the piece up to the contour.");
            vertices.append(vertex);
        }

        struct BoundaryEvent {
            qsizetype start;
            qsizetype end;
            double turn;
            bool outlet;
        };
        QVector<BoundaryEvent> events;
        const qsizetype legCount = splitDart ? 2 : 4;
        double closedAngle = 0;
        for (qsizetype i = 0; i < legCount; i += 2) {
            auto a = vertices[i], b = vertices[i + 1];
            if (!straight(arc(ring, a, b)))
                std::swap(a, b);
            const auto mouth = arc(ring, a, b);
            const auto from = ring[b] - pivot, to = ring[a] - pivot;
            if (!straight(mouth) || std::abs(length(from) - length(to)) > tolerance)
                return failure("Each source dart must have equal legs and a straight mouth.");
            const double turn = std::atan2(cross(from, to), QPointF::dotProduct(from, to));
            if (std::abs(turn) <= tolerance || (closedAngle != 0 && closedAngle * turn <= 0))
                return failure("Source darts must close in the same direction.");
            events.append({a, b, turn, false});
            closedAngle += turn;
        }
        if (std::abs(closedAngle) >= std::numbers::pi)
            return failure("The combined dart angle must be smaller than 180 degrees.");
        if (splitDart) {
            const double firstShare = spreadMm / 100;
            const double remainingShare = (1 - firstShare) / (vertices.size() - legCount - 1);
            for (qsizetype i = legCount; i < vertices.size(); ++i) {
                const double share = outletWeights.isEmpty()
                    ? (i == legCount ? firstShare : remainingShare)
                    : outletWeights[i - legCount] / weightTotal;
                events.append({vertices[i], vertices[i], -closedAngle * share, true});
            }
        } else {
            const auto outlet = vertices[legCount];
            events.append({outlet, outlet, -closedAngle, true});
        }

        const auto origin = events.front().start;
        const auto distance = [&](qsizetype vertex) { return (vertex - origin + ring.size()) % ring.size(); };
        std::ranges::sort(events, {}, [&](const BoundaryEvent& event) { return distance(event.start); });
        double angle = 0;
        const auto rotate = [&](QPointF point) {
            const auto p = point - pivot;
            const double c = std::cos(angle), s = std::sin(angle);
            return pivot + QPointF(c * p.x() - s * p.y(), s * p.x() + c * p.y());
        };
        for (qsizetype i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            const auto next = events[(i + 1) % events.size()].start;
            const auto endDistance = distance(event.start)
                + (event.end - event.start + ring.size()) % ring.size();
            if (endDistance >= (next == origin ? ring.size() : distance(next)))
                return failure("Dart mouths must not overlap each other or contain an outlet.");
            const auto before = rotate(ring[event.start]);
            angle += event.turn;
            const auto after = rotate(ring[event.end]);
            if (event.outlet) {
                // Back off toward the mouth to give each split dart a distinct
                // tip while retaining equal legs and the angular intake at the pivot.
                const auto tip = splitDart ? 0.8 * pivot + 0.2 * (before + after) / 2 : pivot;
                firstEdges.append({{Shape::Line, before, tip}, {Shape::Line, tip, after}});
            }
            auto remaining = boundary;
            auto section = traceBoundary(remaining, ring[event.end], ring[next],
                                          ring[(event.end + 1) % ring.size()]);
            if (section.isEmpty())
                return failure("Could not follow the exact sewing edges between dart guides.");
            for (auto& edge : section) {
                edge.a = rotate(edge.a);
                edge.b = rotate(edge.b);
                if (edge.shape == Shape::Curve)
                    edge.control = rotate(edge.control);
            }
            firstEdges += section;
        }
    } else if (dart) {
        const auto& leg = entities[guides.first()];
        auto apex = leg.a;
        const auto sharedByAll = [&](QPointF point) {
            return std::ranges::all_of(guides, [&](qsizetype index) {
                const auto& line = entities[index];
                return near(line.a, point) || near(line.b, point);
            });
        };
        if (!sharedByAll(apex))
            apex = leg.b;
        if (!sharedByAll(apex) || (!openDart && !ring.containsPoint(apex, Qt::OddEvenFill)))
            return failure("The selected lines must share a dart apex.");

        QVector<QPointF> mouths;
        for (const auto index : guides) {
            const auto& line = entities[index];
            mouths.append(near(line.a, apex) ? line.b : line.a);
        }
        if (curvedBoundary && !splitAt(mouths))
            return failure("The selected lines must end on the exact contour.");
        QVector<QPointF> snapped;
        for (const auto mouth : mouths) {
            const auto vertex = insertBoundaryPoint(ring, mouth);
            if (vertex < 0 || snapped.contains(ring[vertex]))
                return failure("The selected lines must end at distinct points on the contour.");
            snapped.append(ring[vertex]);
        }
        mouths = std::move(snapped);
        QVector<qsizetype> vertices;
        for (qsizetype i = 0; i < mouths.size(); ++i) {
            const auto mouth = mouths[i];
            if ((!openDart || i == 2) && !interiorSegment(ring, apex, mouth))
                return failure("The dart and outlet lines must stay inside the piece.");
            vertices.append(ring.indexOf(mouth));
        }
        qsizetype a = -1, b = -1, outlet = -1;
        for (qsizetype i = 0; i < guideCount; ++i) {
            for (qsizetype j = 0; j < guideCount; ++j) {
                if (i == j || (explicitOutlet && (i == 2 || j == 2)))
                    continue;
                const auto candidate = arc(ring, vertices[i], vertices[j]);
                const bool isMouth = openDart
                    ? candidate.size() == 3 && near(candidate[1], apex)
                        && !ring.containsPoint((apex + candidate.first() + candidate.last()) / 3, Qt::OddEvenFill)
                    : straight(candidate);
                if (!isMouth || candidate.contains(mouths[3 - i - j]))
                    continue;
                if (a >= 0)
                    return failure("Use two dart legs on one straight edge and an outlet on a different edge.");
                a = vertices[i];
                b = vertices[j];
                outlet = vertices[3 - i - j];
            }
        }
        if (a < 0)
            return failure(openDart ? "The two dart legs must form an inward V in the contour."
                                    : "The old dart mouth must lie on a straight edge, with the outlet on a different edge.");
        const auto fixed = ring[a] - apex;
        const auto moving = ring[b] - apex;
        if (std::abs(length(fixed) - length(moving)) > tolerance)
            return failure("The old dart legs must have equal length to close by rotation.");
        const double angle = std::atan2(cross(moving, fixed), QPointF::dotProduct(moving, fixed));
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const auto rotate = [&](QPointF point) {
            const auto p = point - apex;
            return apex + QPointF(cosine * p.x() - sine * p.y(), sine * p.x() + cosine * p.y());
        };
        if (curvedBoundary) {
            // Follow the same winding as the validated polygon, regardless of how
            // the individual source edges were drawn.
            const auto trace = [&](qsizetype start, qsizetype end) {
                auto remaining = boundary;
                return traceBoundary(remaining, ring[start], ring[end],
                                     ring[(start + 1) % ring.size()]);
            };
            firstEdges = trace(b, outlet);
            auto stationary = trace(outlet, a);
            if (firstEdges.isEmpty() || stationary.isEmpty())
                return failure("Could not follow the exact sewing edges around the dart.");
            for (auto& edge : firstEdges) {
                edge.a = rotate(edge.a);
                edge.b = rotate(edge.b);
                if (edge.shape == Shape::Curve)
                    edge.control = rotate(edge.control);
            }
            firstEdges.front().a = ring[a];
            firstEdges.append({Shape::Line, firstEdges.last().b, apex});
            firstEdges.append({Shape::Line, apex, stationary.front().a});
            firstEdges += stationary;
        } else {
            first = arc(ring, b, outlet);
            for (auto& point : first)
                point = rotate(point);
            first.front() = ring[a];
            first.append(apex);
            auto stationary = arc(ring, outlet, a);
            stationary.removeLast();
            first += stationary;
        }
    } else {
        QVector<Entity> chain;
        for (const auto index : guides)
            chain.append(entities[index]);
        if (chain.size() > 1) {
            if (near(chain[0].a, chain[1].a) || near(chain[0].a, chain[1].b))
                std::swap(chain[0].a, chain[0].b);
            for (qsizetype i = 1; i < chain.size(); ++i) {
                if (near(chain[i - 1].b, chain[i].b))
                    std::swap(chain[i].a, chain[i].b);
                if (!near(chain[i - 1].b, chain[i].a))
                    return failure("Spread guide segments must join end to end in order.");
                chain[i].a = chain[i - 1].b;
            }
        }
        auto& startPoint = chain.first().a;
        auto& endPoint = chain.last().b;
        if (curvedBoundary) {
            if (!splitAt({startPoint, endPoint}))
                return failure("Both ends of the cut must be distinct points on the contour.");
            if (!spread) {
                firstEdges = traceBoundary(boundary, startPoint, endPoint);
                secondEdges = traceBoundary(boundary, endPoint, startPoint);
                if (firstEdges.isEmpty() || secondEdges.isEmpty() || !boundary.isEmpty())
                    return failure("The cut endpoints do not divide the contour into two boundaries.");
            }
        }
        const auto start = insertBoundaryPoint(ring, startPoint);
        if (start < 0)
            return failure("Both ends of the cut must be distinct points on the contour.");
        const auto a = ring[start];
        const auto end = insertBoundaryPoint(ring, endPoint);
        if (end < 0 || a == ring[end])
            return failure("Both ends of the cut must be distinct points on the contour.");
        const auto b = ring[end];
        startPoint = a;
        endPoint = b;
        QPointF previous = a;
        QPolygonF cutPath{a};
        for (const auto& segment : chain) {
            const int segments = segment.shape == Shape::Curve ? 128 : 1;
            for (int i = 1; i <= segments; ++i) {
                const double t = double(i) / segments;
                const double u = 1 - t;
                const auto point = segment.shape == Shape::Curve
                    ? u * u * segment.a + 2 * u * t * segment.control + t * t * segment.b : segment.b;
                if (!interiorSegment(ring, previous, point)) {
                    if (chain.size() > 1)
                        return failure("The cut must stay inside the contour between its two ends.");
                    return segment.shape == Shape::Curve
                        ? failure("The curved cut must stay inside the contour.")
                        : failure("The cut must run through the interior without touching or crossing another edge.");
                }
                cutPath.append(point);
                previous = point;
            }
        }
        if (chain.size() > 1) {
            auto boundaryCheck = ring;
            for (qsizetype i = 1; i < chain.size(); ++i) {
                if (insertBoundaryPoint(boundaryCheck, chain[i].a) >= 0)
                    return failure("Only the two ends of a compound cut may touch the contour.");
            }
            // A closed panel on either side must be simple. This also checks
            // that the sampled chain never doubles back onto an earlier segment.
            PatternDocument cutPanel;
            cutPanel.pieces().append({metadata.name});
            const auto edge = arc(ring, end, ring.indexOf(a));
            for (qsizetype i = 1; i < edge.size(); ++i)
                cutPanel.insert(cutPanel.entities().size(), {Shape::Line, edge[i - 1], edge[i], {}, 0});
            for (auto segment : chain) {
                segment.piece = 0;
                segment.layer = Layer::Main;
                cutPanel.insert(cutPanel.entities().size(), segment);
            }
            if (analyzePiece(cutPanel, 0).error != ContourError::None)
                return failure("The compound cut must form a simple path.");
        }
        if (!spread && chain.first().shape == Shape::Curve)
            curvedSeam = Entity{Shape::Curve, b, a, chain.first().control, piece};
        const auto startIndex = ring.indexOf(a);
        first = arc(ring, startIndex, end);
        second = arc(ring, end, startIndex);
        if (spread) {
            const auto cut = b - a;
            const QPointF left(-cut.y(), cut.x());
            const auto midpoint = (a + b) / 2;
            auto normal = left / length(left);
            // The winding determines which side of closing edge a->b holds the second panel.
            double twiceArea = 0;
            for (qsizetype i = 0; i + 1 < second.size(); ++i)
                twiceArea += cross(second[i] - midpoint, second[i + 1] - midpoint);
            if (twiceArea < 0)
                normal = -normal;
            const auto shift = spreadMm * normal;
            std::optional<QPointF> aTab, bTab;
            if (pleatTabs) {
                const auto alongFold = cut / length(cut);
                const auto tab = [&](QPointF end, QPointF stationary, QPointF moving) -> std::optional<QPointF> {
                    const auto before = stationary - end;
                    const auto after = moving - end;
                    const auto across = QPointF::dotProduct(before, normal);
                    if (across >= -tolerance || std::abs(cross(before / length(before), after / length(after))) > tolerance
                        || QPointF::dotProduct(before, after) >= 0)
                        return {};
                    const auto slope = QPointF::dotProduct(before, alongFold) / across;
                    return end + shift / 2 - alongFold * (slope * spreadMm / 2);
                };
                aTab = tab(a, first[1], second[second.size() - 2]);
                bTab = tab(b, first[first.size() - 2], second[1]);
                if (!aTab || !bTab)
                    return failure("The guide ends must cross straight, continuous seam edges.");
            }
            auto movingPanel = second;
            for (qsizetype i = 1; i < cutPath.size(); ++i)
                movingPanel.append(cutPath[i]);
            motion = SpreadMotion{std::move(movingPanel), std::move(cutPath), shift};
            for (auto& point : second)
                point += shift;
            const auto addMark = [&](QPointF offset) {
                for (const auto& segment : chain) {
                    const auto control = segment.shape == Shape::Curve ? segment.control + offset : QPointF{};
                    markings.append({segment.shape, segment.a + offset, segment.b + offset,
                                     control, piece, Layer::Construction});
                }
            };
            if (pleat || gather) {
                addMark({});
                addMark(shift);
            }
            if (pleatTabs)
                markings.append({Shape::Line, *aTab, *bTab, {}, piece, Layer::Construction});
            else if (pleat)
                addMark(shift / 2);
            if (curvedBoundary) {
                // Match the exact arcs to the polygon winding used to choose the spread direction.
                firstEdges = traceBoundary(boundary, a, b, first[1]);
                secondEdges = traceBoundary(boundary, b, a, ring[(end + 1) % ring.size()]);
                if (firstEdges.isEmpty() || secondEdges.isEmpty() || !boundary.isEmpty())
                    return failure("The cut endpoints do not divide the contour into two boundaries.");
                for (auto& edge : secondEdges) {
                    edge.a += shift;
                    edge.b += shift;
                    if (edge.shape == Shape::Curve)
                        edge.control += shift;
                }
                firstEdges.append({Shape::Line, firstEdges.last().b, secondEdges.front().a});
                firstEdges += secondEdges;
                secondEdges.clear();
            }
            // The inserted strip folds in two equally deep layers. Its seam
            // profile is reflected along the two parallel crease lines.
            if (pleatTabs)
                first.append(*bTab);
            first += second;
            if (pleatTabs)
                first.append(*aTab);
            second.clear();
        }
    }

    ContourEdit edit{document, removed, std::move(motion)};
    for (auto i = removed.size(); i > 0; --i)
        edit.document.remove(removed[i - 1]);
    const auto appendPanel = [&](qsizetype target, const QPolygonF& points, QVector<Entity>& edges) {
        if (edges.isEmpty()) {
            appendRing(edit.document, target, points);
            if (curvedSeam)
                edit.document.replace(edit.document.entities().size() - 1, *curvedSeam);
            return;
        }
        if (!near(edges.last().b, edges.first().a))
            edges.append(curvedSeam.value_or(Entity{Shape::Line, edges.last().b, edges.first().a}));
        for (auto edge : edges) {
            edge.piece = target;
            edit.document.insert(edit.document.entities().size(), edge);
        }
    };
    appendPanel(piece, first, firstEdges);
    for (const auto& mark : markings)
        edit.document.insert(edit.document.entities().size(), mark);
    if (!second.isEmpty()) {
        auto other = metadata;
        if (operation == ContourOperation::Cut) {
            edit.document.pieces()[piece].name += QStringLiteral(" A");
            other.name += QStringLiteral(" B");
        } else {
            other.name += operation == ContourOperation::Yoke
                ? QStringLiteral(" · Yoke") : QStringLiteral(" · Relief panel");
        }
        const auto otherIndex = edit.document.pieces().size();
        edit.document.pieces().append(other);
        if (curvedSeam) {
            std::swap(curvedSeam->a, curvedSeam->b);
            curvedSeam->piece = otherIndex;
        }
        appendPanel(otherIndex, second, secondEdges);
    }
    const QVector<qsizetype> changedPieces = second.isEmpty()
        ? QVector<qsizetype>{piece} : QVector<qsizetype>{piece, edit.document.pieces().size() - 1};
    for (const auto changedPiece : changedPieces) {
        const auto result = analyzePiece(edit.document, changedPiece);
        if (result.error != ContourError::None)
            return invalidContour(result.error);
        if (result.allowanceFailed)
            return failure("The existing seam allowance cannot be rebuilt on the new contour.");
    }
    return {std::move(edit), {}};
}

ContourEditResult cutCrossingGuides(const PatternDocument& document, qsizetype piece,
                                    const QVector<qsizetype>& guideIndices) {
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    if (guideIndices.size() < 2 || guideIndices.size() > maxCrossingGuides)
        return failure("Select two to eight manual construction guides.");

    const auto& entities = document.entities();
    QSet<qsizetype> selected;
    QVector<Entity> guides;
    for (const auto index : guideIndices) {
        if (index < 0 || index >= entities.size() || selected.contains(index))
            return failure("Select distinct construction guides.");
        const auto& guide = entities[index];
        if ((guide.shape != Shape::Line && guide.shape != Shape::Curve) || guide.layer != Layer::Construction
            || !guide.draftName.isEmpty()
            || (guide.piece != -1 && guide.piece != piece)
            || !std::isfinite(length(guide.b - guide.a)) || near(guide.a, guide.b))
            return failure("Select manual construction guides assigned to this piece or unassigned.");
        auto cutGuide = guide;
        if (guide.shape == Shape::Curve) {
            const auto offset = guide.control - guide.a;
            if (!std::isfinite(length(offset)))
                return failure("Use a quadratic guide with a finite control point.");
            const auto chord = guide.b - guide.a;
            const auto unit = chord / length(chord);
            if (std::abs(cross(offset, unit)) <= tolerance) {
                const double distance = QPointF::dotProduct(offset, unit);
                if (distance < -tolerance || distance > length(chord) + tolerance)
                    return failure("A straight quadratic guide must not double back on itself.");
                cutGuide.shape = Shape::Line;
            }
        }
        selected.insert(index);
        guides.append(cutGuide);
    }
    // Divide along straight guides first so the resulting curved seams can terminate on them.
    std::stable_partition(guides.begin(), guides.end(), [](const Entity& guide) { return guide.shape == Shape::Line; });

    const auto pointAt = [](const Entity& guide, double t) {
        if (guide.shape == Shape::Line)
            return guide.a + t * (guide.b - guide.a);
        const auto u = 1 - t;
        return u * u * guide.a + 2 * u * t * guide.control + t * t * guide.b;
    };
    QVector<QVector<double>> breakpoints(guides.size());
    QVector<bool> crossesAnother(guides.size(), false);
    for (qsizetype i = 0; i < guides.size(); ++i) {
        const auto& guide = guides[i];
        auto& breaks = breakpoints[i];
        breaks = {0, 1};
        for (qsizetype j = 0; j < i; ++j) {
            const auto& previous = guides[j];
            const QLineF baseline(previous.a, previous.b);
            const auto unit = (previous.b - previous.a) / baseline.length();
            const auto addCrossing = [&](double t, QPointF point) {
                if (near(point, guide.a) || near(point, guide.b)
                    || near(point, previous.a) || near(point, previous.b))
                    return;
                crossesAnother[i] = true;
                crossesAnother[j] = true;
                if (std::ranges::none_of(breaks, [&](double value) { return near(pointAt(guide, value), point); }))
                    breaks.append(t);
            };
            if (previous.shape == Shape::Curve) {
                const auto linear = 2 * (previous.control - previous.a);
                const auto quadratic = previous.b - previous.a - linear;
                const double scale = std::max(length(linear), length(quadratic));
                const auto b = linear / scale;
                const auto a = quadratic / scale;
                const double determinant = cross(b, a);
                // In the (b, a) basis the first curve is (s, s²).
                const auto guideLinear = 2 * (guide.control - guide.a);
                const auto guideQuadratic = guide.b - guide.a - guideLinear;
                const std::array<QPointF, 3> coefficients{
                    (guide.a - previous.a) / scale, guideLinear / scale, guideQuadratic / scale};
                std::array<double, 5> polynomial{};
                for (size_t k = 0; k < coefficients.size(); ++k) {
                    for (size_t l = 0; l < coefficients.size(); ++l)
                        polynomial[k + l] += cross(coefficients[k], a) * cross(coefficients[l], a);
                    polynomial[k] -= determinant * cross(b, coefficients[k]);
                }
                const double projectionScale = std::max({
                    length(coefficients[0]), length(coefficients[1]), length(coefficients[2]), 1.0});
                if (std::ranges::all_of(polynomial, [&](double coefficient) {
                    return std::abs(coefficient) <= 1e-14 * projectionScale * projectionScale;
                }))
                    return failure("The cut guides must not overlap on the same curve.");
                for (const auto t : crossingRoots(polynomial)) {
                    const auto point = pointAt(guide, t);
                    const auto previousT = contourEdgeParameter(previous, point);
                    if (!previousT)
                        continue;
                    const auto tangent = guideLinear + (2 * t) * guideQuadratic;
                    const auto previousTangent = linear + (2 * *previousT) * quadratic;
                    if (std::abs(cross(tangent / length(tangent), previousTangent / length(previousTangent))) > 1e-9)
                        addCrossing(t, point);
                }
                continue;
            }
            if (guide.shape == Shape::Curve) {
                const auto c = cross(guide.a - previous.a, unit);
                const auto b = 2 * cross(guide.control - guide.a, unit);
                const auto a = cross(guide.b - guide.a, unit) - b;
                const auto accept = [&](double t) {
                    if (t <= 0 || t >= 1)
                        return;
                    const auto point = pointAt(guide, t);
                    const auto distance = QPointF::dotProduct(point - previous.a, unit);
                    if (distance > tolerance && distance < baseline.length() - tolerance)
                        addCrossing(t, point);
                };
                if (std::abs(a) <= 1e-12) {
                    if (std::abs(b) > 1e-12)
                        accept(-c / b);
                    else if (std::abs(c) <= tolerance)
                        return failure("The cut guides must not overlap on the same line.");
                    continue;
                }
                const auto discriminant = b * b - 4 * a * c;
                if (discriminant <= 0)
                    continue;
                const auto q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
                accept(q / a);
                accept(c / q);
                continue;
            }
            const QLineF line(guide.a, guide.b);
            QPointF point;
            const auto intersection = line.intersects(baseline, &point);
            if (intersection == QLineF::NoIntersection
                && std::abs(cross(previous.a - guide.a, guide.b - guide.a)) <= tolerance * line.length())
                return failure("The cut guides must not overlap on the same line.");
            if (intersection == QLineF::BoundedIntersection)
                addCrossing(length(point - guide.a) / line.length(), point);
        }
        std::ranges::sort(breaks);
    }
    if (std::ranges::find(crossesAnother, false) != crossesAnother.end())
        return failure("Each guide must cross another guide between their endpoints.");

    auto panels = document;
    auto removeGuides = guideIndices;
    std::ranges::sort(removeGuides, std::greater<>());
    for (const auto index : removeGuides)
        panels.remove(index);
    const auto originalContour = analyzePiece(panels, piece);
    if (originalContour.error != ContourError::None)
        return invalidContour(originalContour.error);

    QMap<qsizetype, QPolygonF> contours{{piece, originalContour.contour}};
    for (qsizetype guideIndex = 0; guideIndex < guides.size(); ++guideIndex) {
        const auto& guide = guides[guideIndex];
        const auto& breaks = breakpoints[guideIndex];
        for (qsizetype i = 1; i < breaks.size(); ++i) {
            const auto t0 = breaks[i - 1], t1 = breaks[i];
            const auto start = pointAt(guide, t0);
            const auto end = pointAt(guide, t1);
            const auto midpoint = pointAt(guide, (t0 + t1) / 2);
            qsizetype target = -1;
            for (auto contour = contours.cbegin(); contour != contours.cend(); ++contour) {
                if (!contour.value().containsPoint(midpoint, Qt::OddEvenFill))
                    continue;
                if (target >= 0)
                    return failure("A cut segment must divide exactly one panel.");
                target = contour.key();
            }
            if (target < 0)
                return failure("Each cut segment must lie inside the piece.");
            const auto index = panels.entities().size();
            const auto control = guide.shape == Shape::Curve ? 2 * midpoint - (start + end) / 2 : QPointF{};
            panels.insert(index, {guide.shape, start, end, control, target, Layer::Construction});
            auto result = modelContour(panels, target, ContourOperation::Cut, {index});
            if (!result.edit)
                return result;
            panels = std::move(result.edit->document);
            const auto newPanel = panels.pieces().size() - 1;
            contours[target] = analyzePiece(panels, target).contour;
            contours.insert(newPanel, analyzePiece(panels, newPanel).contour);
        }
    }

    QVector<qsizetype> removed;
    for (qsizetype i = 0; i < entities.size(); ++i) {
        if (selected.contains(i) || entities[i].piece == piece)
            removed.append(i);
    }
    return {ContourEdit{std::move(panels), std::move(removed), {}}, {}};
}

ContourEditResult shapeSilhouette(const PatternDocument& document, qsizetype piece,
                                 double axisX, double topScale, double bottomScale, double lengthScale) {
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    const auto validScale = [](double scale) { return scale >= 0.2 && scale <= 5; };
    if (!std::isfinite(axisX) || !validScale(topScale) || !validScale(bottomScale) || !validScale(lengthScale))
        return failure("Use a finite axis and width and length factors between 20% and 500%.");
    const auto source = analyzePiece(document, piece);
    if (source.error != ContourError::None)
        return invalidContour(source.error);
    if (!document.pieces()[piece].notches.isEmpty())
        return failure("Remove perimeter-percentage notches or replace them with edge notches before shaping.");
    if (std::ranges::any_of(document.production().placements, [piece](const auto& placement) {
            return placement.piece == piece;
        }))
        return failure("Remove fabric placements for this piece before shaping.");
    const auto transform = [&](QPointF point) {
        const double level = std::clamp((point.y() - source.bounds.top()) / source.bounds.height(), 0., 1.);
        const double scale = std::lerp(topScale, bottomScale, level);
        const double y = lengthScale == 1 ? point.y()
            : source.bounds.top() + (point.y() - source.bounds.top()) * lengthScale;
        return QPointF(axisX + (point.x() - axisX) * scale, y);
    };

    auto updated = document;
    QVector<qsizetype> rectangles;
    QVector<QPolygonF> rectangularOutlines;
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        if (entity.piece != piece)
            continue;
        if (!entity.draftName.isEmpty())
            return failure("Adjust this piece in its parametric draft. This tool edits manually drawn pieces.");
        if (entity.layer == Layer::Allowance)
            return failure("Use calculated seam allowances for the piece before shaping.");
        if (entity.shape == Shape::Rectangle && entity.layer == Layer::Main) {
            if (entity.seamAllowanceMm || !entity.seamNotches.isEmpty())
                return failure("Set rectangle allowances on the piece before shaping.");
            const auto [a, b] = std::pair(entity.a, entity.b);
            rectangularOutlines.append(QPolygonF{transform(a), transform({a.x(), b.y()}),
                                                 transform(b), transform({b.x(), a.y()})});
            rectangles.append(i);
            continue;
        }
        if (entity.shape != Shape::Point && entity.shape != Shape::Line && entity.shape != Shape::Curve)
            return failure("Use lines or curves for construction guides before shaping.");
        entity.a = transform(entity.a);
        entity.b = entity.shape == Shape::Point ? entity.a : transform(entity.b);
        if (entity.shape == Shape::Curve)
            entity.control = transform(entity.control);
        updated.replace(i, entity);
    }
    for (auto i = rectangles.size(); i > 0; --i)
        updated.remove(rectangles[i - 1]);
    for (const auto& ring : rectangularOutlines)
        appendRing(updated, piece, ring);

    QSet<QString> changedSizes;
    for (auto& rule : updated.production().rules) {
        if (rule.piece != piece)
            continue;
        const auto moved = transform(rule.point + rule.offset);
        rule.point = transform(rule.point);
        rule.offset = moved - rule.point;
        changedSizes.insert(rule.size);
    }
    const auto result = analyzePiece(updated, piece);
    if (result.error != ContourError::None)
        return invalidContour(result.error);
    if (result.allowanceFailed)
        return failure("The existing seam allowance cannot be rebuilt on the new contour.");
    for (const auto& size : changedSizes) {
        const auto geometry = analyzePiece(gradedPattern(updated, size), piece);
        if (geometry.error != ContourError::None || geometry.allowanceFailed)
            return {{}, QCoreApplication::translate("ContourModeling",
                "Shaping would invalidate the contour or seam allowance for size %1.").arg(size)};
    }
    return {ContourEdit{std::move(updated), std::move(rectangles)}, {}};
}

}
