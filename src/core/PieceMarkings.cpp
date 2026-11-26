#include "PieceMarkings.h"
#include "EntityPath.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace opc {
bool validEdgeSymbol(const EdgeSymbol& symbol) {
    return int(symbol.type) >= int(PieceSymbol::Cross) && int(symbol.type) <= int(PieceSymbol::Scissors)
        && symbol.fraction > 0 && symbol.fraction < 1 && symbol.sizeMm >= 0.1 && symbol.sizeMm <= 1000
        && symbol.insetMm >= 0.1 && symbol.insetMm <= 1000;
}

QVector<Entity> buildPieceSymbol(PieceSymbol type, qsizetype piece, QPointF center,
                                 double lengthMm, double angleDegrees) {
    if (piece < 0 || !std::isfinite(center.x()) || !std::isfinite(center.y())
        || !std::isfinite(lengthMm) || lengthMm <= 0 || !std::isfinite(angleDegrees))
        return {};
    QTransform transform;
    transform.translate(center.x(), center.y());
    transform.rotate(angleDegrees);
    const double h = lengthMm / 2;
    QVector<Entity> result;
    const auto add = [&](Shape shape, QPointF a, QPointF b) {
        Entity entity{shape, transform.map(a), transform.map(b), {}, piece, Layer::Construction};
        entity.dxfLayer = pieceSymbolLayer;
        result.append(entity);
    };
    const auto arrowHead = [&](double direction, double tip) {
        const double base = tip - direction * h / 2;
        add(Shape::Line, {base, -h / 3}, {tip, 0});
        add(Shape::Line, {tip, 0}, {base, h / 3});
    };
    switch (type) {
    case PieceSymbol::Cross:
        add(Shape::Line, {-h, 0}, {h, 0});
        add(Shape::Line, {0, -h}, {0, h});
        break;
    case PieceSymbol::Circle:
        add(Shape::Circle, {}, {h, 0});
        break;
    case PieceSymbol::Buttonhole:
        add(Shape::Line, {-h, 0}, {h, 0});
        add(Shape::Line, {-h, -h / 4}, {-h, h / 4});
        add(Shape::Line, {h, -h / 4}, {h, h / 4});
        break;
    case PieceSymbol::WarpRepeat:
        add(Shape::Line, {0, -h}, {0, h});
        add(Shape::Line, {-h / 2, -h / 2}, {0, -h});
        add(Shape::Line, {0, -h}, {h / 2, -h / 2});
        break;
    case PieceSymbol::WeftRepeat:
        add(Shape::Line, {-h, 0}, {h, 0});
        add(Shape::Line, {h / 2, -h / 2}, {h, 0});
        add(Shape::Line, {h, 0}, {h / 2, h / 2});
        break;
    case PieceSymbol::NotchStroke:
        add(Shape::Line, {0, -h}, {0, h});
        break;
    case PieceSymbol::VNotch:
        add(Shape::Line, {-h / 2, -h}, {0, h});
        add(Shape::Line, {0, h}, {h / 2, -h});
        break;
    case PieceSymbol::UNotch:
        add(Shape::Line, {-h / 2, -h}, {-h / 2, h});
        add(Shape::Line, {-h / 2, h}, {h / 2, h});
        add(Shape::Line, {h / 2, h}, {h / 2, -h});
        break;
    case PieceSymbol::Arrow:
    case PieceSymbol::ReverseArrow:
    case PieceSymbol::GrainArrow:
        add(Shape::Line, {-h, 0}, {h, 0});
        if (type != PieceSymbol::ReverseArrow)
            arrowHead(1, h);
        if (type != PieceSymbol::Arrow)
            arrowHead(-1, -h);
        break;
    case PieceSymbol::Tip:
        arrowHead(1, h / 4);
        break;
    case PieceSymbol::CenterFront:
    case PieceSymbol::CenterBack:
        add(Shape::Line, {-h, 0}, {h, 0});
        add(Shape::Line, {-h, -h / 3}, {-h, h / 3});
        add(Shape::Line, {h, -h / 3}, {h, h / 3});
        if (type == PieceSymbol::CenterBack)
            add(Shape::Line, {0, -h / 3}, {0, h / 3});
        break;
    case PieceSymbol::Scissors:
        add(Shape::Circle, {-h * 0.65, -h * 0.4}, {-h * 0.4, -h * 0.4});
        add(Shape::Circle, {-h * 0.65, h * 0.4}, {-h * 0.4, h * 0.4});
        add(Shape::Line, {-h * 0.4, -h * 0.4}, {h, h * 0.4});
        add(Shape::Line, {-h * 0.4, h * 0.4}, {h, -h * 0.4});
        break;
    }
    return result;
}

bool symbolFitsContour(const QPainterPath& symbol, const QPolygonF& contour) {
    QPainterPath boundary;
    boundary.addPolygon(contour);
    boundary.closeSubpath();
    QPainterPathStroker stroke;
    stroke.setWidth(0.001);
    return boundary.contains(stroke.createStroke(symbol));
}

QPainterPath PieceMarkings::labelPath() const {
    QPainterPath path;
    if (label.isEmpty())
        return path;
    QFont font;
    font.setPixelSize(40);
    path.addText({}, font, label);
    QTransform transform;
    transform.translate(labelOrigin.x() - QFontMetricsF(font).horizontalAdvance(label) / 20,
                        labelOrigin.y());
    transform.scale(0.1, 0.1);
    return transform.map(path);
}

PieceMarkings buildPieceMarkings(const Piece& piece, const PieceGeometry& geometry,
                                 bool useAllowance) {
    PieceMarkings marks;
    const auto& ring = geometry.contour;
    if (ring.isEmpty())
        return marks;

    const auto center = geometry.bounds.center();
    if (piece.showLabel) {
        marks.label = QStringLiteral("%1  |  ×%2").arg(piece.name).arg(piece.cutCount);
        if (!piece.size.isEmpty())
            marks.label += QStringLiteral("  |  ") + piece.size;
        marks.label = marks.label.simplified();
        marks.labelOrigin = center + QPointF(0, 8);
    }

    if (piece.grainAngle) {
        const double radians = *piece.grainAngle * std::numbers::pi / 180;
        const QPointF direction(std::cos(radians), std::sin(radians));
        const QPointF normal(-direction.y(), direction.x());
        const double half = std::min(30.0, std::min(geometry.bounds.width(), geometry.bounds.height()) / 4);
        const auto start = center - direction * half;
        const auto tip = center + direction * half;
        const double head = std::min(half / 2, 4.0);
        marks.grainline.moveTo(start);
        marks.grainline.lineTo(tip);
        marks.grainline.lineTo(tip - direction * head + normal * head / 2);
        marks.grainline.moveTo(tip);
        marks.grainline.lineTo(tip - direction * head - normal * head / 2);
    }

    const auto cutAnchor = [&](const QLineF& ray) -> std::optional<QPointF> {
        if (geometry.allowance.isEmpty())
            return ray.p1();
        const auto direction = ray.p2() - ray.p1();
        const auto& allowance = geometry.allowance;
        double nearest = std::numeric_limits<double>::infinity();
        std::optional<QPointF> point;
        for (qsizetype i = 0; i < allowance.size(); ++i) {
            QPointF intersection;
            const QLineF edge(allowance[i], allowance[(i + 1) % allowance.size()]);
            if (ray.intersects(edge, &intersection) == QLineF::NoIntersection)
                continue;
            const double distance = QPointF::dotProduct(intersection - ray.p1(), direction);
            const double alongEdge = QPointF::dotProduct(intersection - edge.p1(), edge.p2() - edge.p1());
            if (distance >= -1e-6 && distance < nearest && alongEdge >= 0
                && alongEdge <= QPointF::dotProduct(edge.p2() - edge.p1(), edge.p2() - edge.p1())) {
                nearest = distance;
                point = intersection;
            }
        }
        return point;
    };
    for (const auto& [symbol, ray] : geometry.symbolAnchors) {
        const auto anchor = symbol.fromCutOutline ? cutAnchor(ray) : std::optional(ray.p1());
        if (!anchor || !validEdgeSymbol(symbol)) {
            ++marks.invalidAnchoredSymbols;
            continue;
        }
        const auto outward = ray.p2() - ray.p1();
        const auto angle = std::atan2(outward.x(), -outward.y()) * (180 / std::numbers::pi);
        QPainterPath glyph;
        for (const auto& item : buildPieceSymbol(symbol.type, 0, *anchor - outward * symbol.insetMm,
                                                  symbol.sizeMm, angle))
            glyph.addPath(entityPath(item));
        const auto& outline = symbol.fromCutOutline && !geometry.allowance.isEmpty() ? geometry.allowance : ring;
        if (glyph.isEmpty() || !symbolFitsContour(glyph, outline)) {
            ++marks.invalidAnchoredSymbols;
            continue;
        }
        marks.anchoredSymbols.append(std::move(glyph));
    }

    if (piece.notches.isEmpty() && geometry.notchAnchors.isEmpty())
        return marks;
    QVector<double> distances{0.0};
    double area = 0;
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const auto a = ring[i] - ring.front();
        const auto b = ring[(i + 1) % ring.size()] - ring.front();
        distances.append(distances.back() + QLineF(a, b).length());
        area += a.x() * b.y() - a.y() * b.x();
    }
    QVector<QLineF> anchors = geometry.notchAnchors;
    for (double percent : piece.notches) {
        const double distance = percent / 100 * distances.back();
        const auto edge = std::upper_bound(distances.begin(), distances.end(), distance) - distances.begin() - 1;
        if (edge < 0 || edge >= ring.size())
            continue;
        const QLineF segment(ring[edge], ring[(edge + 1) % ring.size()]);
        const auto tangent = (segment.p2() - segment.p1()) / segment.length();
        const auto anchor = segment.pointAt((distance - distances[edge]) / segment.length());
        const auto outward = QPointF(tangent.y(), -tangent.x()) * (area > 0 ? 1 : -1);
        anchors.append(QLineF(anchor, anchor + outward));
    }
    for (const auto& ray : anchors) {
        const auto anchor = ray.p1();
        const auto outward = ray.p2() - anchor;
        const auto point = useAllowance ? cutAnchor(ray) : std::optional(anchor);
        if (!point)
            ++marks.notchesMissingCutOutline;
        const auto position = point.value_or(anchor);
        marks.notches.moveTo(position - outward * 1.5);
        marks.notches.lineTo(position + outward * 3);
    }
    return marks;
}
}
