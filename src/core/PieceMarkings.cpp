#include "PieceMarkings.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace opc {
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
        QPointF cutAnchor = anchor;
        if (useAllowance && !geometry.allowance.isEmpty()) {
            const auto& allowance = geometry.allowance;
            double nearest = std::numeric_limits<double>::infinity();
            for (qsizetype i = 0; i < allowance.size(); ++i) {
                QPointF intersection;
                const QLineF edge(allowance[i], allowance[(i + 1) % allowance.size()]);
                if (ray.intersects(edge, &intersection) == QLineF::NoIntersection)
                    continue;
                const double distance = QPointF::dotProduct(intersection - anchor, outward);
                const double alongEdge = QPointF::dotProduct(intersection - edge.p1(), edge.p2() - edge.p1());
                if (distance >= -1e-6 && distance < nearest && alongEdge >= 0
                    && alongEdge <= QPointF::dotProduct(edge.p2() - edge.p1(), edge.p2() - edge.p1())) {
                    nearest = distance;
                    cutAnchor = intersection;
                }
            }
        }
        marks.notches.moveTo(cutAnchor - outward * 1.5);
        marks.notches.lineTo(cutAnchor + outward * 3);
    }
    return marks;
}
}
