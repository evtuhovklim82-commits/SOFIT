#include "Production.h"

#include <QJsonArray>
#include <QPainterPath>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace opc {
namespace {
bool samePoint(QPointF a, QPointF b) {
    return a.x() == b.x() && a.y() == b.y();
}

QVector<QPointF> entityPoints(const Entity& entity) {
    if (entity.shape == Shape::Rectangle) {
        const auto [a, b] = std::pair(entity.a, entity.b);
        return {a, {a.x(), b.y()}, b, {b.x(), a.y()}};
    }
    if (entity.shape == Shape::Point)
        return {entity.a};
    if (entity.shape == Shape::Curve)
        return {entity.a, entity.b, entity.control};
    return {entity.a, entity.b};
}

QJsonArray pair(QPointF point) { return {point.x(), point.y()}; }

bool readPoint(const QJsonValue& value, QPointF& point) {
    const auto values = value.toArray();
    if (values.size() != 2 || !values[0].isDouble() || !values[1].isDouble())
        return false;
    point = {values[0].toDouble(), values[1].toDouble()};
    return std::isfinite(point.x()) && std::isfinite(point.y());
}
}

void ProductionData::removePiece(qsizetype piece) {
    const auto remove = [piece](auto& records) {
        records.removeIf([piece](const auto& item) { return item.piece == piece; });
        for (auto& item : records) {
            if (item.piece > piece)
                --item.piece;
        }
    };
    remove(rules);
    remove(placements);
}

QVector<QPointF> gradingPoints(const PatternDocument& document, qsizetype piece) {
    QVector<QPointF> points;
    for (const auto& entity : document.entities()) {
        if (entity.piece != piece)
            continue;
        for (const auto point : entityPoints(entity)) {
            if (std::ranges::none_of(points, [point](QPointF other) { return samePoint(point, other); }))
                points.append(point);
        }
    }
    return points;
}

PatternDocument gradedPattern(const PatternDocument& document, const QString& size,
                              QVector<qsizetype>* sourceIndices) {
    if (sourceIndices)
        sourceIndices->clear();
    if (size.isEmpty()) {
        if (sourceIndices) {
            for (qsizetype i = 0; i < document.entities().size(); ++i)
                sourceIndices->append(i);
        }
        return document;
    }

    PatternDocument graded;
    graded.pieces() = document.pieces();
    for (auto& piece : graded.pieces())
        piece.size = size;
    for (auto layer : {Layer::Main, Layer::Construction, Layer::Allowance})
        graded.setLayerVisible(layer, document.layerVisible(layer));

    QVector<QVector<const GradeRule*>> rules(document.pieces().size());
    for (const auto& rule : document.production().rules) {
        if (rule.size == size && rule.piece >= 0 && rule.piece < rules.size())
            rules[rule.piece].append(&rule);
    }
    for (qsizetype index = 0; index < document.entities().size(); ++index) {
        auto entity = document.entities()[index];
        const auto append = [&](const Entity& gradedEntity) {
            graded.insert(graded.entities().size(), gradedEntity);
            if (sourceIndices)
                sourceIndices->append(index);
        };
        const auto shift = [&](QPointF point) {
            if (entity.piece >= 0) {
                for (const auto* rule : rules[entity.piece]) {
                    if (samePoint(rule->point, point))
                        return point + rule->offset;
                }
            }
            return point;
        };
        if (entity.shape == Shape::Rectangle) {
            auto points = entityPoints(entity);
            auto moved = points;
            std::ranges::transform(points, moved.begin(), shift);
            if (!std::ranges::equal(moved, points, samePoint)) {
                for (int i = 0; i < 4; ++i)
                    append({Shape::Line, moved[i], moved[(i + 1) % 4], {}, entity.piece, entity.layer});
                continue;
            }
        }
        entity.a = shift(entity.a);
        entity.b = entity.shape == Shape::Point ? entity.a : shift(entity.b);
        if (entity.shape == Shape::Curve)
            entity.control = shift(entity.control);
        append(entity);
    }
    return graded;
}

QJsonObject saveProduction(const ProductionData& data) {
    QJsonArray sizes, rules, placements;
    for (const auto& size : data.sizes) {
        QJsonObject measurements;
        for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
            measurements.insert(it.key(), it.value());
        sizes.append(QJsonObject{{"name", size.name}, {"measurements", measurements}});
    }
    for (const auto& rule : data.rules)
        rules.append(QJsonObject{{"size", rule.size}, {"piece", rule.piece},
                                 {"point", pair(rule.point)}, {"offset", pair(rule.offset)}});
    for (const auto& placement : data.placements)
        placements.append(QJsonObject{{"piece", placement.piece}, {"size", placement.size},
                                      {"position", pair(placement.position)}, {"reversed", placement.reversed}});
    return {{"sizes", sizes}, {"rules", rules}, {"fabricWidth", data.fabricWidth}, {"placements", placements}};
}

bool loadProduction(const QJsonObject& json, qsizetype pieceCount, ProductionData& data) {
    ProductionData parsed;
    parsed.fabricWidth = json.value("fabricWidth").toDouble();
    if (!(parsed.fabricWidth > 0 && std::isfinite(parsed.fabricWidth))
        || !json.value("sizes").isArray() || !json.value("rules").isArray()
        || !json.value("placements").isArray())
        return false;

    QSet<QString> names;
    for (const auto& value : json.value("sizes").toArray()) {
        const auto row = value.toObject();
        PatternSize size{row.value("name").toString()};
        if (size.name.trimmed().isEmpty() || names.contains(size.name) || !row.value("measurements").isObject())
            return false;
        const auto measurements = row.value("measurements").toObject();
        for (auto it = measurements.begin(); it != measurements.end(); ++it) {
            if (it.key().trimmed().isEmpty() || !it.value().isDouble()
                || !(it.value().toDouble() > 0 && std::isfinite(it.value().toDouble())))
                return false;
            size.measurements.insert(it.key(), it.value().toDouble());
        }
        names.insert(size.name);
        parsed.sizes.append(std::move(size));
    }

    const auto pieceIndex = [pieceCount](const QJsonObject& row) -> qsizetype {
        const double piece = row.value("piece").toDouble(-1);
        return piece >= 0 && piece < pieceCount && std::trunc(piece) == piece ? qsizetype(piece) : -1;
    };
    QSet<QString> ruleKeys;
    for (const auto& value : json.value("rules").toArray()) {
        const auto row = value.toObject();
        GradeRule rule{row.value("size").toString(), pieceIndex(row)};
        if (rule.piece < 0 || !names.contains(rule.size)
            || !readPoint(row.value("point"), rule.point) || !readPoint(row.value("offset"), rule.offset)
            || !std::isfinite((rule.point + rule.offset).x()) || !std::isfinite((rule.point + rule.offset).y()))
            return false;
        const auto key = QString::number(rule.piece) + QLatin1Char('/') + QString::number(rule.point.x(), 'g', 17)
            + QLatin1Char('/') + QString::number(rule.point.y(), 'g', 17) + QLatin1Char('/') + rule.size;
        if (ruleKeys.contains(key))
            return false;
        ruleKeys.insert(key);
        parsed.rules.append(rule);
    }
    for (const auto& value : json.value("placements").toArray()) {
        const auto row = value.toObject();
        FabricPlacement placement{pieceIndex(row), row.value("size").toString()};
        if (placement.piece < 0 || !row.value("size").isString()
            || (!placement.size.isEmpty() && !names.contains(placement.size))
            || !readPoint(row.value("position"), placement.position) || !row.value("reversed").isBool())
            return false;
        placement.reversed = row.value("reversed").toBool();
        parsed.placements.append(placement);
    }
    data = std::move(parsed);
    return true;
}

LayoutPiece fabricPiece(const PatternDocument& graded, const FabricPlacement& placement) {
    if (placement.piece < 0 || placement.piece >= graded.pieces().size())
        return {};
    const auto geometry = analyzePiece(graded, placement.piece);
    if (geometry.error != ContourError::None || geometry.allowanceFailed)
        return {};
    const auto& piece = graded.pieces()[placement.piece];
    QTransform rotation;
    rotation.rotate(90 - piece.grainAngle.value_or(90) + (placement.reversed ? 180 : 0));
    auto cut = rotation.map(geometry.allowance.isEmpty() ? geometry.contour : geometry.allowance);
    const auto bounds = cut.boundingRect();
    cut.translate(-bounds.topLeft());
    const auto center = rotation.map(geometry.bounds.center()) - bounds.topLeft();
    const double halfLength = std::min(bounds.height(), bounds.width()) / 4;
    return {cut, QLineF(center - QPointF(0, placement.reversed ? -halfLength : halfLength),
                          center + QPointF(0, placement.reversed ? -halfLength : halfLength))};
}

FabricUsage fabricUsage(const QVector<QPolygonF>& polygons, double width) {
    FabricUsage usage;
    QVector<QPainterPath> previous;
    for (const auto& cut : polygons) {
        if (cut.isEmpty()) {
            ++usage.invalid;
            continue;
        }
        const auto bounds = cut.boundingRect();
        usage.length = std::max(usage.length, bounds.bottom());
        constexpr double tolerance = 1e-7;
        if (bounds.left() < -tolerance || bounds.right() > width + tolerance || bounds.top() < -tolerance)
            ++usage.outside;
        QPainterPath path;
        path.addPolygon(cut);
        path.closeSubpath();
        for (const auto& other : previous) {
            if (!bounds.intersects(other.boundingRect()))
                continue;
            const auto intersection = path.intersected(other).boundingRect();
            if (intersection.width() > tolerance && intersection.height() > tolerance)
                ++usage.overlaps;
        }
        previous.append(path);
    }
    return usage;
}

}
