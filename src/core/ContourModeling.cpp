#include "ContourModeling.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
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

void appendRing(PatternDocument& document, qsizetype piece, const QPolygonF& ring) {
    for (qsizetype i = 0; i < ring.size(); ++i) {
        document.insert(document.entities().size(),
                        {Shape::Line, ring[i], ring[(i + 1) % ring.size()], {}, piece});
    }
}
}

ContourEditResult modelContour(const PatternDocument& document, qsizetype piece,
                               ContourOperation operation, const QVector<qsizetype>& guides,
                               double spreadMm) {
    const bool dart = operation == ContourOperation::TransferDart;
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    if (guides.size() != (dart ? 3 : 1))
        return failure(dart ? "Select two dart legs and an outlet line on the Construction layer."
                            : "Select one cut line on the Construction layer.");
    if (operation == ContourOperation::Spread && (!std::isfinite(spreadMm) || spreadMm <= tolerance))
        return failure("Enter a positive spread width.");
    const auto& metadata = document.pieces()[piece];
    if (!metadata.notches.isEmpty()
        || std::ranges::any_of(document.production().rules, [piece](const auto& rule) { return rule.piece == piece; })
        || std::ranges::any_of(document.production().placements, [piece](const auto& placement) {
               return placement.piece == piece;
           }))
        return failure("This piece has notches, grading rules, or fabric placements. Remove those before modeling.");

    const auto& entities = document.entities();
    QSet<qsizetype> guideSet;
    for (const auto index : guides) {
        if (index < 0 || index >= entities.size() || guideSet.contains(index))
            return failure("Select distinct construction lines.");
        const auto& guide = entities[index];
        if (guide.shape != Shape::Line || guide.layer != Layer::Construction || !guide.draftName.isEmpty()
            || (guide.piece != -1 && guide.piece != piece)
            || !std::isfinite(length(guide.b - guide.a)) || near(guide.a, guide.b))
            return failure("Use nonzero manual construction lines assigned to this piece or unassigned.");
        guideSet.insert(index);
    }

    QVector<qsizetype> removed;
    for (qsizetype i = 0; i < entities.size(); ++i) {
        const auto& entity = entities[i];
        if (guideSet.contains(i)) {
            removed.append(i);
        } else if (entity.piece == piece) {
            if (!entity.draftName.isEmpty())
                return failure("This piece is generated by a parametric draft.");
            if (entity.layer != Layer::Main)
                return failure("Remove or unassign the other construction and allowance objects on this piece.");
            if (entity.shape != Shape::Line && entity.shape != Shape::Rectangle)
                return failure("Contour modeling supports straight edges and rectangles.");
            removed.append(i);
        }
    }
    auto geometry = analyzePiece(document, piece);
    if (geometry.error != ContourError::None)
        return invalidContour(geometry.error);
    auto ring = std::move(geometry.contour);

    QPolygonF first;
    QPolygonF second;
    if (dart) {
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
        if (!sharedByAll(apex) || !ring.containsPoint(apex, Qt::OddEvenFill))
            return failure("The three lines must share an apex inside the piece.");

        QVector<QPointF> mouths;
        for (const auto index : guides) {
            const auto& line = entities[index];
            const auto mouth = near(line.a, apex) ? line.b : line.a;
            const auto vertex = insertBoundaryPoint(ring, mouth);
            if (vertex < 0 || mouths.contains(ring[vertex]))
                return failure("The three lines must end at distinct points on the contour.");
            mouths.append(ring[vertex]);
        }
        QVector<qsizetype> vertices;
        for (const auto mouth : mouths) {
            if (!interiorSegment(ring, apex, mouth))
                return failure("The dart and outlet lines must stay inside the piece.");
            vertices.append(ring.indexOf(mouth));
        }
        qsizetype a = -1, b = -1, outlet = -1;
        for (qsizetype i = 0; i < 3; ++i) {
            for (qsizetype j = 0; j < 3; ++j) {
                if (i == j)
                    continue;
                const auto candidate = arc(ring, vertices[i], vertices[j]);
                if (!straight(candidate) || candidate.contains(mouths[3 - i - j]))
                    continue;
                if (a >= 0)
                    return failure("Use two dart legs on one straight edge and an outlet on a different edge.");
                a = vertices[i];
                b = vertices[j];
                outlet = vertices[3 - i - j];
            }
        }
        if (a < 0)
            return failure("The old dart mouth must lie on a straight edge, with the outlet on a different edge.");
        // The straight arc a->b is the old mouth. Rotate b->outlet toward a
        // and connect both positions of the outlet to the shared apex.
        const auto fixed = ring[a] - apex;
        const auto moving = ring[b] - apex;
        if (std::abs(length(fixed) - length(moving)) > tolerance)
            return failure("The old dart legs must have equal length to close by rotation.");
        const double angle = std::atan2(cross(moving, fixed), QPointF::dotProduct(moving, fixed));
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        first = arc(ring, b, outlet);
        for (auto& point : first) {
            const auto p = point - apex;
            point = apex + QPointF(cosine * p.x() - sine * p.y(), sine * p.x() + cosine * p.y());
        }
        first.front() = ring[a];
        first.append(apex);
        auto stationary = arc(ring, outlet, a);
        stationary.removeLast();
        first += stationary;
    } else {
        const auto& guide = entities[guides.front()];
        const auto start = insertBoundaryPoint(ring, guide.a);
        if (start < 0)
            return failure("Both ends of the cut must be distinct points on the contour.");
        const auto a = ring[start];
        const auto end = insertBoundaryPoint(ring, guide.b);
        if (end < 0 || a == ring[end])
            return failure("Both ends of the cut must be distinct points on the contour.");
        const auto b = ring[end];
        if (!interiorSegment(ring, a, b))
            return failure("The cut must run through the interior without touching or crossing another edge.");
        const auto startIndex = ring.indexOf(a);
        first = arc(ring, startIndex, end);
        second = arc(ring, end, startIndex);
        if (operation == ContourOperation::Spread) {
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
            for (auto& point : second)
                point += spreadMm * normal;
            first += second;
            second.clear();
        }
    }

    ContourEdit edit{document, removed};
    for (auto i = removed.size(); i > 0; --i)
        edit.document.remove(removed[i - 1]);
    appendRing(edit.document, piece, first);
    if (!second.isEmpty()) {
        auto other = metadata;
        edit.document.pieces()[piece].name += QStringLiteral(" A");
        other.name += QStringLiteral(" B");
        const auto otherIndex = edit.document.pieces().size();
        edit.document.pieces().append(other);
        appendRing(edit.document, otherIndex, second);
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
}
