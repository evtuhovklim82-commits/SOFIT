#include "Production.h"

#include <QHash>
#include <QJsonArray>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
#include <limits>
#include <tuple>

namespace opc {
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
        if (graded.seamLinks().isEmpty())
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
    auto center = rotation.map(geometry.bounds.center());
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
            repeatPoint = rotation.map(point);
        }
    }
    if (placement.hand == CutHand::Right) {
        const auto reflection = QTransform::fromScale(-1, 1);
        cut = reflection.map(cut);
        center = reflection.map(center);
        if (repeatPoint)
            repeatPoint = reflection.map(*repeatPoint);
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
    if (repeatPoint)
        *repeatPoint -= bounds.topLeft();
    const double halfLength = std::min(bounds.height(), bounds.width()) / 4;
    return {cut, QLineF(center - QPointF(0, placement.reversed ? -halfLength : halfLength),
                          center + QPointF(0, placement.reversed ? -halfLength : halfLength)),
            fold.isNull() ? fold : fold.translated(-bounds.topLeft()), repeatPoint};
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

QPainterPath clearancePath(const QPolygonF& cut, double clearanceMm) {
    const auto path = cutPath(cut);
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
        const auto path = clearancePath(cut, clearanceMm);
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
            graded.insert(placement.size, gradedPattern(document, placement.size));
        const auto layout = fabricPiece(graded.constFind(placement.size).value(), placement);
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

AutomaticFabricLayout arrangeFabric(const PatternDocument& document, const ProductionData& data) {
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
                graded.insert(copy.size, gradedPattern(document, copy.size));
            }
            const auto& pattern = graded.constFind(copy.size).value();
            copy.reversed = false;
            auto normal = fabricPiece(pattern, copy);
            if (!copy.repeatEdge.isEmpty() && !normal.repeatPoint)
                return {{}, FabricLayoutError::InvalidRepeat, i};
            const double area = polygonArea(normal.cut);
            const auto bounds = normal.cut.boundingRect();
            if (!(area > 0) || !std::isfinite(area) || !std::isfinite(bounds.height()))
                return {{}, FabricLayoutError::InvalidContour, i};
            if (outsideFabric(bounds, data.fabricWidth))
                return {{}, FabricLayoutError::TooWide, i};
            copy.reversed = true;
            auto reversed = turns ? fabricPiece(pattern, copy) : LayoutPiece{};
            if (reversed.cut == normal.cut && reversed.repeatPoint == normal.repeatPoint)
                reversed.cut.clear();
            found = cached.insert(key, {{std::move(normal), std::move(reversed)}, area});
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
    const auto consider = [&](const QVector<FabricPlacement>& copies) {
        const auto usage = fabricUsage(polygonsFor(copies), data.fabricWidth, data.pieceClearanceMm, data.fabricFlaws);
        if (!usage.invalid && !usage.outside && !usage.overlaps && !usage.flawConflicts
            && usage.length < bestLength) {
            bestLength = usage.length;
            best.placements = copies;
        }
    };

    QVector<qsizetype> order(data.placements.size());
    std::iota(order.begin(), order.end(), qsizetype{0});
    // Different largest-first orders give the greedy search different opportunities.
    for (int pass = 0; pass < 3; ++pass) {
        const auto priority = [&](qsizetype index) {
            const auto& cut = *options[index];
            const auto bounds = cut.layouts[0].cut.boundingRect();
            return pass == 0 ? cut.area : pass == 1 ? bounds.height() : bounds.width();
        };
        std::ranges::sort(order, [&](qsizetype a, qsizetype b) {
            return std::tuple(-priority(a), a) < std::tuple(-priority(b), b);
        });

        auto copies = data.placements;
        QVector<PositionedCut> placed;
        double length = 0;
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
                const auto path = clearancePath(cut, data.pieceClearanceMm);
                const auto outline = cutPath(cut);
                const double rightEdge = std::max(0.0, data.fabricWidth - bounds.width());
                const auto tryPosition = [&](double x, double y) {
                    x = copy.fold == FoldEdge::None ? nextRepeat(x, data.fabricRepeatMm.x(), anchor.x())
                        : foldPosition(copy, data.fabricWidth, bounds.width());
                    y = nextRepeat(y, data.fabricRepeatMm.y(), anchor.y());
                    const QPointF point(x, y);
                    const auto candidate = bounds.translated(point);
                    const auto score = std::tuple(std::max(length, candidate.bottom()), y, x, orientation != 0);
                    if (!std::isfinite(candidate.bottom()) || score >= bestScore
                        || outsideFabric(candidate, data.fabricWidth))
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
                    const auto bounds = flaw.bounds;
                    tryPosition(0, std::max(length + gap, bounds.bottom()));
                    tryPosition(0, bounds.bottom());
                    tryPosition(bounds.right(), bounds.top());
                    tryPosition(bounds.right(), 0);
                    tryPosition(rightmost, bounds.bottom());
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
            if (chosen.path.isEmpty())
                return {{}, FabricLayoutError::VerificationFailed, index};
            copy.position = position;
            copy.reversed = std::get<3>(bestScore);
            length = std::get<0>(bestScore);
            placed.append(std::move(chosen));
        }
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
    if (existingValid)
        consider(data.placements);
    if (best.placements.isEmpty())
        best.error = FabricLayoutError::VerificationFailed;
    return best;
}

}
