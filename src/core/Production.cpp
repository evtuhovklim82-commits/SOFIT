#include "Production.h"
#include "EntityPath.h"
#include "PieceMarkings.h"

#include <QHash>
#include <QJsonArray>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <array>
#include <numeric>
#include <set>
#include <limits>
#include <tuple>

namespace opc {
bool validShrinkage(QPointF percent) {
    const auto validAxis = [](double value) { return value >= -99 && value <= 99; };
    return validAxis(percent.x()) && validAxis(percent.y());
}

namespace {
constexpr double layoutTolerance = 1e-7;

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

bool validRepeat(QPointF repeat) {
    const auto valid = [](double period) { return period == 0 || (period >= 0.001 && period <= 10000); };
    return valid(repeat.x()) && valid(repeat.y());
}

double nextRepeat(double coordinate, double period, double anchor) {
    if (period == 0)
        return coordinate;
    return std::max(0.0, std::ceil((std::max(0.0, coordinate) + anchor - layoutTolerance) / period)
        * period - anchor);
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

bool validFabricFlaws(const QVector<QRectF>& flaws) {
    return flaws.size() <= 100 && std::ranges::all_of(flaws, [](const auto& flaw) {
        return flaw.left() >= 0 && flaw.top() >= 0 && flaw.width() >= 0.001 && flaw.height() >= 0.001
            && flaw.right() <= 1000000 && flaw.bottom() <= 1000000;
    });
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
                              QVector<qsizetype>* sourceIndices, GradeEdgeNames edgeNames) {
    if (sourceIndices)
        sourceIndices->clear();
    if (size.isEmpty()) {
        if (sourceIndices) {
            for (qsizetype i = 0; i < document.entities().size(); ++i)
                sourceIndices->append(i);
        }
        return document;
    }

    QHash<QString, Entity> formulaObjects;
    const auto target = std::ranges::find(document.production().sizes, size, &PatternSize::name);
    if (target != document.production().sizes.cend()) {
        const auto evaluated = document.evaluateSizeDraft(*target);
        if (!evaluated)
            return {};
        for (const auto& entity : *evaluated)
            formulaObjects.insert(entity.draftName, entity);
    }

    PatternDocument graded;
    graded.pieces() = document.pieces();
    graded.seamLinks_ = document.seamLinks_;
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
        const auto& base = document.entities()[index];
        auto entity = base;
        const auto computed = formulaObjects.constFind(base.draftName);
        if (computed != formulaObjects.cend()) {
            entity.a = computed->a;
            entity.b = computed->b;
            entity.control = computed->control;
        }
        if (edgeNames == GradeEdgeNames::ForSeamLinks && graded.seamLinks().isEmpty())
            entity.draftName.clear();
        const auto append = [&](const Entity& gradedEntity) {
            graded.insert(graded.entities().size(), gradedEntity);
            if (sourceIndices)
                sourceIndices->append(index);
        };
        const auto shift = [&](QPointF basePoint, QPointF point) {
            if (entity.piece >= 0 && entity.piece < rules.size()) {
                for (const auto* rule : rules[entity.piece]) {
                    if (samePoint(rule->point, basePoint))
                        return point + rule->offset;
                }
            }
            return point;
        };
        if (entity.shape == Shape::Rectangle) {
            auto points = entityPoints(entity);
            auto moved = points;
            std::ranges::transform(points, moved.begin(), [&](QPointF point) { return shift(point, point); });
            if (!std::ranges::equal(moved, points, samePoint)) {
                for (int i = 0; i < 4; ++i)
                    append({Shape::Line, moved[i], moved[(i + 1) % 4], {}, entity.piece, entity.layer});
                continue;
            }
        }
        entity.a = shift(base.a, entity.a);
        entity.b = entity.shape == Shape::Point ? entity.a : shift(base.b, entity.b);
        if (entity.shape == Shape::Curve)
            entity.control = shift(base.control, entity.control);
        append(entity);
    }
    return graded;
}

bool appendMarkerModel(PatternDocument& destination, const PatternDocument& source,
                       const QString& size, const QString& modelName, int kits, QString& error) {
    const auto fail = [&](const QString& detail) {
        error = detail;
        return false;
    };
    if (kits <= 0 || modelName.trimmed().isEmpty())
        return fail(QStringLiteral("Укажите название модели и положительное число комплектов."));
    if (!size.isEmpty() && std::ranges::none_of(source.production().sizes, [&](const auto& entry) {
            return entry.name == size;
        }))
        return fail(QStringLiteral("Выбранный размер отсутствует в модели."));
    const auto graded = gradedPattern(source, size, nullptr, GradeEdgeNames::Preserve);
    if (graded.pieces().isEmpty())
        return fail(QStringLiteral("В выбранном размере нет деталей или его не удалось пересчитать."));

    auto candidate = destination;
    const auto offset = candidate.pieces().size();
    const auto sizeLabel = size.isEmpty() ? QStringLiteral("база") : size;
    auto count = candidate.production().placements.size();
    for (const auto& piece : graded.pieces()) {
        if (piece.cutCount <= 0 || kits > (maxAutomaticCopies - count) / piece.cutCount)
            return fail(QStringLiteral("Для этой раскладки доступно не более %1 копий.").arg(maxAutomaticCopies));
        count += qsizetype(kits) * piece.cutCount;
    }

    double nextY = 0;
    QMap<QString, PatternDocument> existingGrades;
    for (const auto& copy : candidate.production().placements) {
        if (!existingGrades.contains(copy.size))
            existingGrades.insert(copy.size, gradedPattern(destination, copy.size, nullptr, GradeEdgeNames::Preserve));
        const auto cut = fabricPiece(existingGrades.constFind(copy.size).value(), copy, false, candidate.production().fabricShrinkagePercent).cut;
        if (!cut.isEmpty())
            nextY = std::max(nextY, copy.position.y() + cut.boundingRect().height() + 10);
    }

    for (auto piece : graded.pieces()) {
        piece.name = QStringLiteral("%1 · %2 · %3").arg(modelName, sizeLabel, piece.name);
        piece.cutCount *= kits;
        candidate.pieces().append(std::move(piece));
    }
    const auto linked = graded.inspectSeamLinks();
    for (qsizetype index = 0; index < graded.entities().size(); ++index) {
        auto entity = graded.entities()[index];
        if (entity.piece < 0 || entity.piece >= graded.pieces().size())
            continue;
        entity.piece += offset;
        entity.draftName.clear();
        if (linked.allowances.contains(index))
            entity.seamAllowanceMm = linked.allowances.value(index);
        for (const auto notch : linked.notches.value(index)) {
            if (std::ranges::none_of(entity.seamNotches, [notch](double current) {
                    return std::abs(current - notch) < 1e-9;
                }))
                entity.seamNotches.append(notch);
        }
        if (entity.seamNotches.size() > 100)
            return fail(QStringLiteral("На одном срезе больше 100 надсечек."));
        candidate.insert(candidate.entities().size(), entity);
    }

    for (qsizetype index = offset; index < candidate.pieces().size(); ++index) {
        FabricPlacement placement{index, {}, {}};
        const auto cut = fabricPiece(candidate, placement, false, candidate.production().fabricShrinkagePercent).cut;
        if (cut.isEmpty())
            return fail(QStringLiteral("Не удалось подготовить контур детали «%1».").arg(candidate.pieces()[index].name));
        const auto step = cut.boundingRect().height() + 10;
        const auto copies = candidate.pieces()[index].cutCount;
        for (int copy = 0; copy < copies; ++copy) {
            placement.position.setY(nextY);
            candidate.production().placements.append(placement);
            nextY += step;
        }
    }
    destination = std::move(candidate);
    error.clear();
    return true;
}

QJsonObject saveProduction(const ProductionData& data) {
    QJsonArray sizes, rules, placements;
    for (const auto& size : data.sizes) {
        QJsonObject measurements;
        for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
            measurements.insert(it.key(), it.value());
        QJsonObject row{{"name", size.name}, {"measurements", measurements}};
        if (!size.source.isEmpty())
            row.insert("source", size.source);
        sizes.append(row);
    }
    for (const auto& rule : data.rules)
        rules.append(QJsonObject{{"size", rule.size}, {"piece", rule.piece},
                                 {"point", pair(rule.point)}, {"offset", pair(rule.offset)}});
    for (const auto& placement : data.placements) {
        QJsonObject row{{"piece", placement.piece}, {"size", placement.size},
                        {"position", pair(placement.position)}, {"reversed", placement.reversed},
                        {"hand", int(placement.hand)}, {"fold", int(placement.fold)},
                        {"fixedOrientation", placement.fixedOrientation}};
        if (!placement.repeatEdge.isEmpty()) {
            row.insert("repeatEdge", placement.repeatEdge);
            row.insert("repeatParameter", placement.repeatParameter);
        }
        placements.append(row);
    }
    QJsonObject result{{"sizes", sizes}, {"rules", rules}, {"fabricWidth", data.fabricWidth}, {"placements", placements},
            {"allowHalfTurns", data.allowHalfTurns}, {"directionalFabric", data.directionalFabric}};
    if (data.pieceClearanceMm != 0)
        result.insert("pieceClearanceMm", data.pieceClearanceMm);
    if (!data.fabricShrinkagePercent.isNull())
        result.insert("fabricShrinkagePercent", pair(data.fabricShrinkagePercent));
    if (!data.fabricRepeatMm.isNull())
        result.insert("fabricRepeatMm", pair(data.fabricRepeatMm));
    if (!data.fabricFlaws.isEmpty()) {
        QJsonArray flaws;
        for (const auto& flaw : data.fabricFlaws)
            flaws.append(QJsonArray{flaw.x(), flaw.y(), flaw.width(), flaw.height()});
        result.insert("fabricFlaws", flaws);
    }
    return result;
}

bool loadProduction(const QJsonObject& json, qsizetype pieceCount, ProductionData& data) {
    ProductionData parsed;
    parsed.fabricWidth = json.value("fabricWidth").toDouble();
    if (!(parsed.fabricWidth > 0 && std::isfinite(parsed.fabricWidth))
        || !json.value("sizes").isArray() || !json.value("rules").isArray()
        || !json.value("placements").isArray())
        return false;

    if (json.contains("pieceClearanceMm")) {
        parsed.pieceClearanceMm = json.value("pieceClearanceMm").toDouble(-1);
        if (!(parsed.pieceClearanceMm >= 0 && parsed.pieceClearanceMm <= 1000))
            return false;
    }

    if (json.contains("fabricShrinkagePercent")
        && (!readPoint(json.value("fabricShrinkagePercent"), parsed.fabricShrinkagePercent)
            || !validShrinkage(parsed.fabricShrinkagePercent)))
        return false;

    if (json.contains("fabricRepeatMm")
        && (!readPoint(json.value("fabricRepeatMm"), parsed.fabricRepeatMm) || !validRepeat(parsed.fabricRepeatMm)))
        return false;

    if (json.contains("fabricFlaws")) {
        if (!json.value("fabricFlaws").isArray())
            return false;
        const auto flaws = json.value("fabricFlaws").toArray();
        if (flaws.size() > 100)
            return false;
        for (const auto& value : flaws) {
            const auto row = value.toArray();
            if (row.size() != 4 || !row[0].isDouble() || !row[1].isDouble()
                || !row[2].isDouble() || !row[3].isDouble())
                return false;
            parsed.fabricFlaws.append(QRectF(row[0].toDouble(), row[1].toDouble(),
                                           row[2].toDouble(), row[3].toDouble()));
        }
        if (!validFabricFlaws(parsed.fabricFlaws))
            return false;
    }

    if (json.contains("allowHalfTurns")) {
        if (!json.value("allowHalfTurns").isBool())
            return false;
        parsed.allowHalfTurns = json.value("allowHalfTurns").toBool();
    }

    if (json.contains("directionalFabric")) {
        if (!json.value("directionalFabric").isBool())
            return false;
        parsed.directionalFabric = json.value("directionalFabric").toBool();
    }

    QSet<QString> names;
    for (const auto& value : json.value("sizes").toArray()) {
        const auto row = value.toObject();
        PatternSize size{row.value("name").toString()};
        if (size.name.trimmed().isEmpty() || names.contains(size.name) || !row.value("measurements").isObject())
            return false;
        if (row.contains("source") && !row.value("source").isString())
            return false;
        size.source = row.value("source").toString();
        if (size.source.size() > 512 || size.source.contains(u'\t')
            || size.source.contains(u'\r') || size.source.contains(u'\n'))
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
        const int hand = row.contains("hand") ? row.value("hand").toInt(-1) : 0;
        const int fold = row.contains("fold") ? row.value("fold").toInt(-1) : 0;
        if (hand < 0 || hand > int(CutHand::Right) || fold < 0 || fold > int(FoldEdge::Right)
            || (hand != 0 && fold != 0)
            || (row.contains("fixedOrientation") && !row.value("fixedOrientation").isBool()))
            return false;
        placement.hand = CutHand(hand);
        placement.fold = FoldEdge(fold);
        placement.fixedOrientation = row.value("fixedOrientation").toBool();
        if (row.contains("repeatEdge") || row.contains("repeatParameter")) {
            placement.repeatEdge = row.value("repeatEdge").toString();
            placement.repeatParameter = row.value("repeatParameter").toDouble(-1);
            if (placement.repeatEdge.isEmpty() || placement.repeatEdge.size() > 64
                || !(placement.repeatParameter >= 0 && placement.repeatParameter <= 1))
                return false;
        }
        parsed.placements.append(placement);
    }
    data = std::move(parsed);
    return true;
}

bool matchesFabricRepeat(const ProductionData& data, const FabricPlacement& placement,
                         std::optional<QPointF> repeatPoint) {
    if (!validRepeat(data.fabricRepeatMm) || (!placement.repeatEdge.isEmpty() && !repeatPoint))
        return false;
    const auto onRepeat = [](double position, double period) {
        return period == 0 || std::abs(std::remainder(position, period)) <= layoutTolerance;
    };
    const auto registered = placement.position + repeatPoint.value_or(QPointF{});
    return (placement.fold != FoldEdge::None || onRepeat(registered.x(), data.fabricRepeatMm.x()))
        && onRepeat(registered.y(), data.fabricRepeatMm.y());
}

bool canTurn(const ProductionData& data, const FabricPlacement& placement) {
    return !data.directionalFabric && !placement.fixedOrientation && placement.fold == FoldEdge::None;
}

double foldPosition(const FabricPlacement& placement, double fabricWidth, double cutWidth) {
    if (placement.fold == FoldEdge::Right)
        return fabricWidth - cutWidth;
    return 0;
}

// Start on the fold, traverse the cutting perimeter and stop on the fold.
QPolygonF openFoldContour(QPolygonF cut, const QLineF& fold) {
    if (cut.isEmpty())
        return {};
    if (cut.front() == cut.back())
        cut.removeLast();
    const auto onFold = [&](QPointF point) { return std::abs(point.x() - fold.x1()) <= 1e-7; };
    QPolygonF outline;
    for (qsizetype start = 0; start < cut.size(); ++start) {
        if (!onFold(cut[start]) || onFold(cut[(start + 1) % cut.size()]))
            continue;
        outline.append(cut[start]);
        for (qsizetype step = 1; step < cut.size(); ++step) {
            const auto point = cut[(start + step) % cut.size()];
            outline.append(point);
            if (onFold(point))
                return outline;
        }
    }
    return {};
}

LayoutPiece fabricPiece(const PatternDocument& graded, const FabricPlacement& placement, bool includeNotches,
                        QPointF shrinkagePercent) {
    if (!validShrinkage(shrinkagePercent) || placement.piece < 0 || placement.piece >= graded.pieces().size())
        return {};
    const auto geometry = analyzePiece(graded, placement.piece);
    if (geometry.error != ContourError::None || geometry.allowanceFailed)
        return {};
    const auto& piece = graded.pieces()[placement.piece];
    shrinkagePercent = piece.fabricShrinkageOverride.value_or(shrinkagePercent);
    if (!validShrinkage(shrinkagePercent))
        return {};
    QTransform rotation;
    rotation.rotate(90 - piece.grainAngle.value_or(90) + (placement.reversed ? 180 : 0));
    const auto compensation = QTransform::fromScale(100 / (100 - shrinkagePercent.x()),
                                                     100 / (100 - shrinkagePercent.y()));
    const auto toFabric = rotation * compensation;
    auto cut = toFabric.map(geometry.allowance.isEmpty() ? geometry.contour : geometry.allowance);
    auto center = toFabric.map(geometry.bounds.center());
    QPainterPath notches;
    QVector<QPointF> drillPoints;
    QVector<QPainterPath> symbols;
    if (includeNotches) {
        const auto marks = buildPieceMarkings(piece, geometry, !geometry.allowance.isEmpty());
        if (marks.invalidAnchoredSymbols)
            return {};
        notches = toFabric.map(marks.notches);
        for (const auto& mark : marks.anchoredSymbols)
            symbols.append(toFabric.map(mark));
        for (const auto& entity : graded.entities()) {
            if (entity.piece != placement.piece)
                continue;
            if (isDrillPoint(entity))
                drillPoints.append(toFabric.map(entity.a));
            if (entity.layer == Layer::Construction && entity.dxfLayer == pieceSymbolLayer)
                symbols.append(toFabric.map(entityPath(entity)));
        }
    }
    std::optional<QPointF> repeatPoint;
    if (!placement.repeatEdge.isEmpty()) {
        const auto edge = std::ranges::find(graded.entities(), placement.repeatEdge, &Entity::draftName);
        const auto t = placement.repeatParameter;
        if (edge != graded.entities().end() && edge->piece == placement.piece && edge->layer == Layer::Main
            && t >= 0 && t <= 1 && (edge->shape == Shape::Curve || edge->shape == Shape::Line)) {
            const auto u = 1 - t;
            const auto point = edge->shape == Shape::Curve
                ? u * u * edge->a + 2 * u * t * edge->control + t * t * edge->b
                : u * edge->a + t * edge->b;
            repeatPoint = toFabric.map(point);
        }
    }
    if (placement.hand == CutHand::Right) {
        const auto reflection = QTransform::fromScale(-1, 1);
        cut = reflection.map(cut);
        center = reflection.map(center);
        if (!notches.isEmpty())
            notches = reflection.map(notches);
        if (repeatPoint)
            repeatPoint = reflection.map(*repeatPoint);
        for (auto& point : drillPoints)
            point = reflection.map(point);
        for (auto& symbol : symbols)
            symbol = reflection.map(symbol);
    }
    QLineF fold;
    if (placement.fold != FoldEdge::None) {
        // A fold placement requires a pre-finished outline.
        if (!geometry.allowance.isEmpty() || placement.hand != CutHand::Single)
            return {};
        const auto bounds = cut.boundingRect();
        const bool right = (placement.fold == FoldEdge::Right) != placement.reversed;
        const double edge = right ? bounds.right() : bounds.left();
        double edgeLength = 0;
        double minY = bounds.bottom();
        double maxY = bounds.top();
        for (qsizetype index = 0; index < cut.size(); ++index) {
            const auto a = cut[index];
            const auto b = cut[(index + 1) % cut.size()];
            if (std::abs(a.x() - edge) <= layoutTolerance) {
                minY = std::min(minY, a.y());
                maxY = std::max(maxY, a.y());
                if (std::abs(b.x() - edge) <= layoutTolerance)
                    edgeLength += std::abs(b.y() - a.y());
            }
        }
        if (edgeLength <= layoutTolerance || std::abs(edgeLength - (maxY - minY)) > layoutTolerance)
            return {};
        fold = QLineF(edge, minY, edge, maxY);
    }
    const auto bounds = cut.boundingRect();
    cut.translate(-bounds.topLeft());
    center -= bounds.topLeft();
    if (!notches.isEmpty())
        notches.translate(-bounds.topLeft());
    if (repeatPoint)
        *repeatPoint -= bounds.topLeft();
    for (auto& point : drillPoints)
        point -= bounds.topLeft();
    for (auto& symbol : symbols)
        symbol.translate(-bounds.topLeft());
    const double halfLength = std::min(bounds.height(), bounds.width()) / 4;
    return {cut, QLineF(center - QPointF(0, placement.reversed ? -halfLength : halfLength),
                          center + QPointF(0, placement.reversed ? -halfLength : halfLength)),
            fold.isNull() ? fold : fold.translated(-bounds.topLeft()), repeatPoint, std::move(notches), std::move(drillPoints), std::move(symbols)};
}

namespace {
constexpr double placementGap = 0.01;

double polygonArea(const QPolygonF& cut) {
    if (cut.size() < 3)
        return 0;
    const auto origin = cut.front();
    long double sum = 0;
    for (qsizetype i = 1; i + 1 < cut.size(); ++i) {
        const auto a = cut[i] - origin;
        const auto b = cut[i + 1] - origin;
        sum += static_cast<long double>(a.x()) * b.y() - static_cast<long double>(a.y()) * b.x();
    }
    return std::abs(sum) / 2;
}

QPainterPath cutPath(const QPolygonF& cut) {
    QPainterPath path;
    path.addPolygon(cut);
    path.closeSubpath();
    return path;
}

QPainterPath clearancePath(const QPainterPath& path, double clearanceMm) {
    if (clearanceMm <= 0)
        return path;
    QPainterPathStroker stroker;
    stroker.setWidth(clearanceMm);
    stroker.setJoinStyle(Qt::RoundJoin);
    return path.united(stroker.createStroke(path));
}

bool outsideFabric(const QRectF& bounds, double width) {
    return bounds.left() < -layoutTolerance || bounds.right() > width + layoutTolerance
        || bounds.top() < -layoutTolerance;
}

bool overlap(const QPainterPath& a, const QPainterPath& b) {
    if (!a.boundingRect().intersects(b.boundingRect()))
        return false;
    const auto intersection = a.intersected(b).boundingRect();
    return intersection.width() > layoutTolerance && intersection.height() > layoutTolerance;
}

struct PositionedCut {
    QRectF bounds;
    QPainterPath path;
};

QVector<PositionedCut> flawPaths(const QVector<QRectF>& flaws) {
    QVector<PositionedCut> result;
    for (const auto& bounds : flaws) {
        QPainterPath path;
        path.addRect(bounds);
        result.append({bounds, std::move(path)});
    }
    return result;
}

struct CutOptions {
    std::array<LayoutPiece, 2> layouts;
    std::array<QPainterPath, 2> outlines;
    std::array<QPainterPath, 2> clearancePaths;
    double area;
};
}

std::optional<double> FabricUsage::utilization(double width) const {
    const double total = width * length;
    if (invalid || outside || overlaps || flawConflicts || !(total > 0) || !std::isfinite(total))
        return std::nullopt;
    return std::clamp(cutArea / total, 0.0, 1.0);
}

FabricUsage fabricUsage(const QVector<QPolygonF>& polygons, double width, double clearanceMm,
                        const QVector<QRectF>& flaws) {
    FabricUsage usage;
    if (!(clearanceMm >= 0 && clearanceMm <= 1000) || !validFabricFlaws(flaws)) {
        ++usage.invalid;
        return usage;
    }
    const auto obstacles = flawPaths(flaws);
    QVector<QPainterPath> previous;
    for (const auto& cut : polygons) {
        const auto bounds = cut.boundingRect();
        const double area = polygonArea(cut);
        if (!(area > 0) || !std::isfinite(area) || !std::isfinite(bounds.bottom())
            || !std::isfinite(bounds.right())) {
            ++usage.invalid;
            continue;
        }
        usage.cutArea += area;
        usage.length = std::max(usage.length, bounds.bottom());
        if (outsideFabric(bounds, width))
            ++usage.outside;
        if (!obstacles.isEmpty()) {
            const auto outline = cutPath(cut);
            if (std::ranges::any_of(obstacles, [&](const auto& flaw) { return overlap(outline, flaw.path); }))
                ++usage.flawConflicts;
        }
        const auto path = clearancePath(cutPath(cut), clearanceMm);
        for (const auto& other : previous) {
            if (overlap(path, other))
                ++usage.overlaps;
        }
        previous.append(path);
    }
    return usage;
}

bool CuttingReport::canExport(double fabricWidth) const {
    return std::isfinite(fabricWidth) && fabricWidth > 0 && !rows.isEmpty()
        && !usage.invalid && !usage.outside && !usage.overlaps && !usage.flawConflicts
        && !orientationViolations && !foldViolations && !repeatViolations;
}

CuttingReport cuttingReport(const PatternDocument& document, const ProductionData& data) {
    CuttingReport report;
    QMap<QString, PatternDocument> graded;
    QMap<std::pair<qsizetype, QString>, CuttingReportRow> counts;
    QVector<QPolygonF> polygons;
    for (const auto& placement : data.placements) {
        const auto key = std::pair(placement.piece, placement.size);
        auto found = counts.find(key);
        if (found == counts.end())
            found = counts.insert(key, CuttingReportRow{placement.piece, placement.size});
        auto& row = found.value();
        if (placement.fold != FoldEdge::None)
            ++row.onFold;
        else if (placement.hand == CutHand::Left)
            ++row.left;
        else if (placement.hand == CutHand::Right)
            ++row.right;
        else
            ++row.single;
        if (!graded.contains(placement.size))
            graded.insert(placement.size, gradedPattern(document, placement.size, nullptr, GradeEdgeNames::Preserve));
        const auto layout = fabricPiece(graded.constFind(placement.size).value(), placement, false, data.fabricShrinkagePercent);
        const auto& cut = layout.cut;
        polygons.append(cut.translated(placement.position));
        if (!matchesFabricRepeat(data, placement, layout.repeatPoint))
            ++report.repeatViolations;
        if (placement.reversed && !canTurn(data, placement))
            ++report.orientationViolations;
        if (placement.fold != FoldEdge::None && !cut.isEmpty()
            && std::abs(placement.position.x() - foldPosition(placement, data.fabricWidth,
                        cut.boundingRect().width())) > layoutTolerance)
            ++report.foldViolations;
    }
    report.rows = counts.values();
    report.usage = fabricUsage(polygons, data.fabricWidth, data.pieceClearanceMm, data.fabricFlaws);
    return report;
}

QVector<CuttingReportRow> cuttingKitRows(const PatternDocument& document, const CuttingReport& report) {
    QMap<QString, QMap<qsizetype, CuttingReportRow>> bySize;
    for (const auto& row : report.rows)
        bySize[row.size].insert(row.piece, row);
    if (bySize.isEmpty())
        bySize.insert(QString{}, {});

    QVector<CuttingReportRow> rows;
    for (auto size = bySize.cbegin(); size != bySize.cend(); ++size) {
        for (qsizetype i = 0; i < document.pieces().size(); ++i)
            rows.append(size->value(i, {i, size.key()}));
    }
    return rows;
}

QVector<SizeLay> planSizeLays(const QMap<QString, int>& demand, int maximumLayers) {
    if (maximumLayers < 1 || maximumLayers > 10000)
        return {};
    QVector<SizeLay> lays;
    for (auto size = demand.cbegin(); size != demand.cend(); ++size) {
        int outstanding = size.value();
        if (outstanding < 0 || outstanding > 1000000)
            return {};
        while (outstanding > 0) {
            if (lays.size() == 9999)
                return {};
            const int layers = std::min(outstanding, maximumLayers);
            lays.append({size.key(), layers});
            outstanding -= layers;
        }
    }
    return lays;
}

QVector<KitLay> planKitLays(const QMap<QString, int>& demand, int maximumLayers, int maximumKitsPerLayer) {
    if (maximumLayers < 1 || maximumLayers > 10000 || maximumKitsPerLayer < 1
        || maximumKitsPerLayer > maxAutomaticCopies
        || std::ranges::any_of(demand, [](int count) { return count < 0 || count > 1000000; }))
        return {};
    if (maximumKitsPerLayer == 1) {
        QVector<KitLay> result;
        for (const auto& lay : planSizeLays(demand, maximumLayers))
            result.append({{{lay.size, 1}}, lay.layers});
        return result;
    }

    auto remaining = demand;
    remaining.removeIf([](auto size) { return size.value() == 0; });
    QVector<KitLay> result;
    while (!remaining.isEmpty()) {
        if (result.size() == 9999)
            return {};
        KitLay lay{{}, maximumLayers};
        int available = maximumKitsPerLayer;
        for (auto size = remaining.cbegin(); size != remaining.cend() && available > 0; ++size) {
            lay.kitsPerSize.insert(size.key(), 1);
            --available;
        }
        // Keep small demand sizes from capping the whole run if the larger
        // sizes can use the remaining marker slots.
        while (available > 0) {
            auto best = lay.kitsPerSize.end();
            int layersWithExtraKit = 0;
            for (auto kits = lay.kitsPerSize.begin(); kits != lay.kitsPerSize.end(); ++kits) {
                const int layers = remaining.value(kits.key()) / (kits.value() + 1);
                if (layers > layersWithExtraKit) {
                    layersWithExtraKit = layers;
                    best = kits;
                }
            }
            if (best == lay.kitsPerSize.end())
                break;
            ++best.value();
            --available;
        }
        for (auto kits = lay.kitsPerSize.cbegin(); kits != lay.kitsPerSize.cend(); ++kits)
            lay.layers = std::min(lay.layers, remaining.value(kits.key()) / kits.value());
        for (auto kits = lay.kitsPerSize.cbegin(); kits != lay.kitsPerSize.cend(); ++kits) {
            auto count = remaining.find(kits.key());
            count.value() -= kits.value() * lay.layers;
            if (count.value() == 0)
                remaining.erase(count);
        }
        result.append(std::move(lay));
    }
    return result;
}

AutomaticFabricLayout arrangeFabric(const PatternDocument& document, const ProductionData& data) {
    if (!validShrinkage(data.fabricShrinkagePercent))
        return {{}, FabricLayoutError::InvalidShrinkage};
    if (!(data.fabricWidth > 0) || !std::isfinite(data.fabricWidth))
        return {{}, FabricLayoutError::InvalidWidth};
    if (!(data.pieceClearanceMm >= 0 && data.pieceClearanceMm <= 1000))
        return {{}, FabricLayoutError::InvalidClearance};
    if (!validRepeat(data.fabricRepeatMm))
        return {{}, FabricLayoutError::InvalidRepeat};
    if (!validFabricFlaws(data.fabricFlaws))
        return {{}, FabricLayoutError::InvalidFlaws};
    const auto obstacles = flawPaths(data.fabricFlaws);
    const double gap = placementGap + data.pieceClearanceMm;
    if (data.placements.size() > maxAutomaticCopies)
        return {{}, FabricLayoutError::TooManyCopies};
    if (data.placements.isEmpty())
        return {};

    QMap<QString, PatternDocument> graded;
    QMap<std::tuple<QString, qsizetype, CutHand, FoldEdge, bool, QString, double>, CutOptions> cached;
    QVector<const CutOptions*> options;
    for (qsizetype i = 0; i < data.placements.size(); ++i) {
        auto copy = data.placements[i];
        const bool turns = data.allowHalfTurns && canTurn(data, copy);
        if (!(copy.repeatParameter >= 0 && copy.repeatParameter <= 1))
            return {{}, FabricLayoutError::InvalidRepeat, i};
        const auto key = std::tuple(copy.size, copy.piece, copy.hand, copy.fold, turns,
                                    copy.repeatEdge, copy.repeatParameter);
        auto found = cached.constFind(key);
        if (found == cached.cend()) {
            if (!graded.contains(copy.size)) {
                if (!copy.size.isEmpty() && std::ranges::none_of(data.sizes, [&](const PatternSize& size) {
                        return size.name == copy.size;
                    }))
                    return {{}, FabricLayoutError::InvalidContour, i};
                graded.insert(copy.size, gradedPattern(document, copy.size, nullptr, GradeEdgeNames::Preserve));
            }
            const auto& pattern = graded.constFind(copy.size).value();
            copy.reversed = false;
            auto normal = fabricPiece(pattern, copy, false, data.fabricShrinkagePercent);
            if (!copy.repeatEdge.isEmpty() && !normal.repeatPoint)
                return {{}, FabricLayoutError::InvalidRepeat, i};
            const double area = polygonArea(normal.cut);
            const auto bounds = normal.cut.boundingRect();
            if (!(area > 0) || !std::isfinite(area) || !std::isfinite(bounds.height()))
                return {{}, FabricLayoutError::InvalidContour, i};
            if (outsideFabric(bounds, data.fabricWidth))
                return {{}, FabricLayoutError::TooWide, i};
            copy.reversed = true;
            auto reversed = turns ? fabricPiece(pattern, copy, false, data.fabricShrinkagePercent) : LayoutPiece{};
            if (reversed.cut == normal.cut && reversed.repeatPoint == normal.repeatPoint)
                reversed.cut.clear();
            CutOptions cut{{std::move(normal), std::move(reversed)}, {}, {}, area};
            for (std::size_t orientation = 0; orientation < cut.layouts.size(); ++orientation) {
                if (cut.layouts[orientation].cut.isEmpty())
                    continue;
                auto& outline = cut.outlines[orientation];
                outline = cutPath(cut.layouts[orientation].cut);
                cut.clearancePaths[orientation] = clearancePath(outline, data.pieceClearanceMm);
            }
            found = cached.insert(key, std::move(cut));
        }
        options.append(&found.value());
    }

    auto polygonsFor = [&](const QVector<FabricPlacement>& copies) {
        QVector<QPolygonF> polygons;
        for (qsizetype i = 0; i < copies.size(); ++i) {
            const auto& copy = copies[i];
            // An identical half-turn is elided from the search.
            const auto& layouts = options[i]->layouts;
            const auto& cut = copy.reversed && !layouts[1].cut.isEmpty() ? layouts[1].cut : layouts[0].cut;
            polygons.append(cut.translated(copy.position));
        }
        return polygons;
    };

    AutomaticFabricLayout best;
    double bestLength = std::numeric_limits<double>::infinity();
    const auto consider = [&](const QVector<FabricPlacement>& copies) -> std::optional<double> {
        const auto usage = fabricUsage(polygonsFor(copies), data.fabricWidth, data.pieceClearanceMm, data.fabricFlaws);
        if (usage.invalid || usage.outside || usage.overlaps || usage.flawConflicts)
            return std::nullopt;
        if (usage.length < bestLength) {
            bestLength = usage.length;
            best.placements = copies;
        }
        return usage.length;
    };

    qsizetype failedCopy = -1;
    QVector<qsizetype> order(data.placements.size());
    std::iota(order.begin(), order.end(), qsizetype{0});
    // Retain the original three orders as a measurable baseline. Additional
    // orders give priority to constrained pieces and try small pieces first.
    constexpr int baselinePasses = 3;
    QVector<QVector<qsizetype>> testedOrders;
    for (int pass = 0; pass < 6; ++pass) {
        if (pass == baselinePasses) {
            if (std::isfinite(bestLength))
                best.baselineLengthMm = bestLength;
            if (!obstacles.isEmpty())
                testedOrders.clear();
        }
        const auto priority = [&](qsizetype index) {
            const auto& cut = *options[index];
            const auto& copy = data.placements[index];
            const auto bounds = cut.layouts[0].cut.boundingRect();
            switch (pass) {
            case 0: return std::tuple(0, -cut.area, index);
            case 1: return std::tuple(0, -bounds.height(), index);
            case 2: return std::tuple(0, -bounds.width(), index);
            case 3: return std::tuple(int(copy.fold == FoldEdge::None), -bounds.height(), index);
            case 4: return std::tuple(int(!cut.layouts[1].cut.isEmpty()), -cut.area, index);
            default: return std::tuple(0, cut.area, index);
            }
        };
        std::ranges::sort(order, {}, priority);
        if (testedOrders.contains(order))
            continue;
        testedOrders.append(order);

        auto copies = data.placements;
        QVector<PositionedCut> placed;
        double length = 0;
        bool complete = true;
        for (const auto index : order) {
            auto& copy = copies[index];
            QPointF position(0, placed.isEmpty() ? 0 : length + gap);
            auto bestScore = std::tuple(std::numeric_limits<double>::infinity(), 0.0, 0.0, false);
            PositionedCut chosen;
            for (int orientation = 0; orientation < 2; ++orientation) {
                const auto& layout = options[index]->layouts[orientation];
                const auto& cut = layout.cut;
                const auto anchor = layout.repeatPoint.value_or(QPointF{});
                if (cut.isEmpty())
                    continue;
                const auto bounds = cut.boundingRect();
                const auto& path = options[index]->clearancePaths[orientation];
                const auto& outline = options[index]->outlines[orientation];
                const double rightEdge = std::max(0.0, data.fabricWidth - bounds.width());
                // Repeat snapping can bring many candidate coordinates to the same point.
                std::set<std::pair<double, double>> testedPositions;
                const auto tryPosition = [&](double x, double y) {
                    x = copy.fold == FoldEdge::None ? nextRepeat(x, data.fabricRepeatMm.x(), anchor.x())
                        : foldPosition(copy, data.fabricWidth, bounds.width());
                    y = nextRepeat(y, data.fabricRepeatMm.y(), anchor.y());
                    const QPointF point(x, y);
                    const auto candidate = bounds.translated(point);
                    const auto score = std::tuple(std::max(length, candidate.bottom()), y, x, orientation != 0);
                    if (!std::isfinite(candidate.bottom()) || score >= bestScore
                        || outsideFabric(candidate, data.fabricWidth) || !testedPositions.emplace(x, y).second)
                        return;
                    const auto translated = path.translated(point);
                    for (const auto& other : placed) {
                        if (overlap(translated, other.path))
                            return;
                    }
                    if (!obstacles.isEmpty()) {
                        const auto translatedOutline = outline.translated(point);
                        if (std::ranges::any_of(obstacles, [&](const auto& flaw) {
                                return overlap(translatedOutline, flaw.path);
                            }))
                            return;
                    }
                    position = point;
                    chosen = {candidate, translated};
                    bestScore = score;
                };

                // Stacking below all previous copies provides a fallback.
                tryPosition(0, placed.isEmpty() ? 0 : length + gap);
                const double repeatX = data.fabricRepeatMm.x();
                const double rightmost = repeatX == 0 ? rightEdge : std::floor((rightEdge + anchor.x()) / repeatX) * repeatX - anchor.x();
                tryPosition(rightmost, 0);
                tryPosition(0, 0);
                for (const auto& flaw : obstacles) {
                    const auto& defect = flaw.bounds;
                    tryPosition(0, std::max(length + gap, defect.bottom()));
                    tryPosition(0, defect.bottom());
                    tryPosition(defect.right(), defect.top());
                    tryPosition(defect.right(), 0);
                    tryPosition(rightmost, defect.bottom());
                    if (pass >= baselinePasses) {
                        // Try the free strip to the left of the flaw and the space directly above it.
                        const double leftLimit = defect.left() - bounds.width();
                        const double left = repeatX == 0 ? leftLimit
                            : std::floor((leftLimit + anchor.x()) / repeatX) * repeatX - anchor.x();
                        tryPosition(left, defect.top());
                        tryPosition(left, defect.bottom());
                        const double repeatY = data.fabricRepeatMm.y();
                        const double topLimit = defect.top() - bounds.height();
                        const double top = repeatY == 0 ? topLimit
                            : std::floor((topLimit + anchor.y()) / repeatY) * repeatY - anchor.y();
                        tryPosition(defect.left(), top);
                        tryPosition(rightmost, top);
                    }
                }
                for (const auto& other : placed) {
                    const auto b = other.bounds;
                    const std::array xs{0.0, rightEdge, b.left(), b.right() + gap,
                                        b.left() - bounds.width() - gap,
                                        b.center().x() - bounds.width() / 2};
                    const std::array ys{b.top(), b.bottom() + gap,
                                        b.center().y() - bounds.height() / 2};
                    for (const auto x : xs) {
                        for (const auto y : ys)
                            tryPosition(x, y);
                    }
                    tryPosition(b.right() + gap, 0);
                }
            }
            if (chosen.path.isEmpty()) {
                failedCopy = index;
                complete = false;
                break;
            }
            copy.position = position;
            copy.reversed = std::get<3>(bestScore);
            length = std::get<0>(bestScore);
            placed.append(std::move(chosen));
        }
        if (complete)
            consider(copies);
    }

    // Preserve a valid hand-tuned layout if the search cannot improve it.
    bool existingValid = true;
    for (qsizetype index = 0; index < data.placements.size(); ++index) {
        const auto& copy = data.placements[index];
        const auto& layouts = options[index]->layouts;
        const auto& current = copy.reversed && !layouts[1].cut.isEmpty() ? layouts[1] : layouts[0];
        if (!matchesFabricRepeat(data, copy, current.repeatPoint))
            existingValid = false;
        if (copy.reversed && (!data.allowHalfTurns || !canTurn(data, copy)))
            existingValid = false;
        if (copy.fold != FoldEdge::None
            && std::abs(copy.position.x() - foldPosition(copy, data.fabricWidth,
                        layouts[0].cut.boundingRect().width())) > layoutTolerance)
            existingValid = false;
    }
    if (existingValid) {
        const auto length = consider(data.placements);
        if (length && (!best.baselineLengthMm || *length < *best.baselineLengthMm))
            best.baselineLengthMm = length;
    }
    if (best.placements.isEmpty())
        return {{}, FabricLayoutError::VerificationFailed, failedCopy};
    best.optimizedLengthMm = bestLength;
    return best;
}

}
