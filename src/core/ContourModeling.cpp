#include "ContourModeling.h"
#include "Production.h"

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
    const bool openDart = operation == ContourOperation::MoveOpenDart;
    const bool explicitOutlet = openDart || operation == ContourOperation::TransferDartToOutlet;
    const bool dart = explicitOutlet || operation == ContourOperation::TransferDart;
    const bool pleat = operation == ContourOperation::Pleat;
    const bool gather = operation == ContourOperation::Gather;
    const bool spread = pleat || gather || operation == ContourOperation::Spread;
    const qsizetype guideCount = dart ? 3 : 1;
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    if (guides.size() != guideCount)
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
            return edge.piece == piece && (edge.seamAllowanceMm || !edge.seamNotches.isEmpty());
        }))
        return failure("Clear edge allowances and automatic notches before modeling this piece.");
    QSet<qsizetype> guideSet;
    for (qsizetype i = 0; i < guides.size(); ++i) {
        const auto index = guides[i];
        if (index < 0 || index >= entities.size() || guideSet.contains(index))
            return failure("Select distinct lines for the modeling operation.");
        const auto& guide = entities[index];
        const bool contourLeg = openDart && i < 2;
        const bool assigned = contourLeg ? guide.piece == piece : guide.piece == -1 || guide.piece == piece;
        const auto layer = contourLeg ? Layer::Main : Layer::Construction;
        if (guide.shape != Shape::Line || guide.layer != layer || !guide.draftName.isEmpty()
            || !assigned || !std::isfinite(length(guide.b - guide.a)) || near(guide.a, guide.b))
            return failure(openDart ? "Use two straight main-contour dart legs and one manual construction outlet."
                                    : "Use nonzero manual construction lines assigned to this piece or unassigned.");
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
    QVector<QLineF> markings;
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
        if (!sharedByAll(apex) || (!openDart && !ring.containsPoint(apex, Qt::OddEvenFill)))
            return failure("The selected lines must share a dart apex.");

        QVector<QPointF> mouths;
        for (const auto index : guides) {
            const auto& line = entities[index];
            const auto mouth = near(line.a, apex) ? line.b : line.a;
            const auto vertex = insertBoundaryPoint(ring, mouth);
            if (vertex < 0 || mouths.contains(ring[vertex]))
                return failure("The selected lines must end at distinct points on the contour.");
            mouths.append(ring[vertex]);
        }
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
            for (auto& point : second)
                point += shift;
            if (pleat || gather) {
                markings.append({a, b});
                markings.append({a + shift, b + shift});
            }
            if (pleat)
                markings.append({a + shift / 2, b + shift / 2});
            first += second;
            second.clear();
        }
    }

    ContourEdit edit{document, removed};
    for (auto i = removed.size(); i > 0; --i)
        edit.document.remove(removed[i - 1]);
    appendRing(edit.document, piece, first);
    for (const auto& mark : markings) {
        edit.document.insert(edit.document.entities().size(),
                             {Shape::Line, mark.p1(), mark.p2(), {}, piece, Layer::Construction});
    }
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

ContourEditResult shapeSilhouette(const PatternDocument& document, qsizetype piece,
                                 double axisX, double topScale, double bottomScale) {
    if (piece < 0 || piece >= document.pieces().size())
        return failure("Choose a piece.");
    const auto validScale = [](double scale) { return scale >= 0.2 && scale <= 5; };
    if (!std::isfinite(axisX) || !validScale(topScale) || !validScale(bottomScale))
        return failure("Use a finite axis and widths between 20% and 500%.");
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
        return QPointF(axisX + (point.x() - axisX) * scale, point.y());
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
