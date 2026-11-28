#include "PatternDocument.h"
#include "PieceGeometry.h"
#include "Production.h"

#include <algorithm>
#include <array>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineF>
#include <QRegularExpression>
#include <QStringView>
#include <QSet>
#include <cmath>
#include <functional>
#include <iterator>
#include <numbers>
#include <utility>

namespace opc {
namespace {
struct EvaluationError {
    QString message;
};

bool validName(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z_][A-Za-z_0-9]*\\z"));
    return name.size() <= 64 && pattern.match(name).hasMatch();
}

bool isTangentFunction(QStringView name) {
    return name == u"tangentx" || name == u"tangenty";
}

bool isNumericFunction(QStringView name) {
    return name == u"sqrt" || name == u"abs" || name == u"min" || name == u"max"
        || name == u"hypot" || name == u"atan2deg";
}

class Formula {
public:
    Formula(QStringView source, const std::function<double(const QString&, int)>& resolve)
        : source_(source), resolve_(resolve) {}

    double evaluate(int depth) {
        if (source_.size() > 1024)
            fail(QStringLiteral("Formula is too long"));
        const auto result = expression(depth);
        skipSpace();
        if (pos_ != source_.size())
            fail(QStringLiteral("Unexpected character"));
        return result;
    }

private:
    double expression(int depth) {
        auto value = term(depth);
        for (;;) {
            if (take(u'+'))
                value = finite(value + term(depth));
            else if (take(u'-'))
                value = finite(value - term(depth));
            else
                return value;
        }
    }

    double term(int depth) {
        auto value = operand(depth);
        for (;;) {
            if (take(u'*')) {
                value = finite(value * operand(depth));
            } else if (take(u'/')) {
                const auto divisor = operand(depth);
                if (evaluating_ && divisor == 0)
                    fail(QStringLiteral("Division by zero"));
                value = evaluating_ ? finite(value / divisor) : 0;
            } else {
                return value;
            }
        }
    }

    double operand(int depth) {
        if (depth >= 128)
            fail(QStringLiteral("Formula nesting is too deep"));
        if (take(u'+'))
            return operand(depth + 1);
        if (take(u'-'))
            return -operand(depth + 1);
        if (take(u'(')) {
            const auto value = expression(depth + 1);
            if (!take(u')'))
                fail(QStringLiteral("Expected ')'"));
            return value;
        }

        const auto start = pos_;
        if (letter(peek())) {
            do {
                ++pos_;
            } while (letter(peek()) || digit(peek()));
            const auto name = source_.mid(start, pos_ - start);
            const auto end = pos_;
            if (name == u"iflt" && take(u'('))
                return conditional(depth + 1);
            if (isNumericFunction(name) && take(u'('))
                return numericFunction(name, depth + 1);
            if (isTangentFunction(name) && take(u'('))
                return tangentControl(name, depth + 1);
            if (name == u"quadlen" && take(u'('))
                return quadraticLength(depth + 1);
            if (name == u"archheight" && take(u'('))
                return archHeight(depth + 1);
            if (name == u"capcontrol" && take(u'('))
                return capControl(depth + 1);
            if (name == u"capwidth" && take(u'('))
                return capWidth(depth + 1);
            if (name == u"standarct" && take(u'('))
                return standArcParameter(depth + 1);
            if (name == u"positive" && take(u'(')) {
                const auto value = expression(depth + 1);
                if (!take(u')'))
                    fail(QStringLiteral("Expected ')'"));
                if (evaluating_ && value <= 0)
                    fail(QStringLiteral("Expected a value greater than zero"));
                return value;
            }
            pos_ = end;
            if (peek() == u'.') {
                ++pos_;
                if (source_.mid(pos_, 3) == u"len") {
                    pos_ += 3;
                } else {
                    if (peek() != u'x' && peek() != u'y')
                        fail(QStringLiteral("Expected point coordinate x or y, or edge length len"));
                    ++pos_;
                }
            }
            return evaluating_ ? resolve_(source_.mid(start, pos_ - start).toString(), depth + 1) : 0;
        }

        while (digit(peek()))
            ++pos_;
        if (peek() == u'.') {
            ++pos_;
            while (digit(peek()))
                ++pos_;
        }
        if (pos_ == start)
            fail(QStringLiteral("Expected a number, name or '('"));
        if (peek() == u'e' || peek() == u'E') {
            ++pos_;
            if (peek() == u'+' || peek() == u'-')
                ++pos_;
            while (digit(peek()))
                ++pos_;
        }
        bool ok = false;
        const auto number = source_.mid(start, pos_ - start).toDouble(&ok);
        if (!ok)
            fail(QStringLiteral("Invalid number"));
        return evaluating_ ? finite(number) : 0;
    }

    double conditional(int depth) {
        const auto left = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after the left comparison operand"));
        const auto right = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after the right comparison operand"));

        // Parse both branches, but resolve references and evaluate only the selected one.
        const bool wasEvaluating = evaluating_;
        evaluating_ = wasEvaluating && left < right;
        const auto whenLess = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' between conditional branches"));
        evaluating_ = wasEvaluating && left >= right;
        const auto otherwise = expression(depth);
        evaluating_ = wasEvaluating;
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        return left < right ? whenLess : otherwise;
    }

    double numericFunction(QStringView name, int depth) {
        const auto a = expression(depth);
        if (name == u"abs" || name == u"sqrt") {
            if (!take(u')'))
                fail(QStringLiteral("Expected ')'"));
            if (name == u"abs")
                return std::abs(a);
            if (evaluating_ && a < 0)
                fail(QStringLiteral("Square root requires a non-negative value"));
            return std::sqrt(a);
        }

        if (!take(u','))
            fail(QStringLiteral("Expected ',' between function arguments"));
        const auto b = expression(depth);
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (name == u"min")
            return std::min(a, b);
        if (name == u"max")
            return std::max(a, b);
        if (name == u"atan2deg") {
            if (evaluating_ && std::hypot(a, b) <= 1e-9)
                fail(QStringLiteral("A zero vector has no direction"));
            return std::atan2(a, b) * (180 / std::numbers::pi);
        }
        return finite(std::hypot(a, b));
    }

    double tangentControl(QStringView name, int depth) {
        std::array<double, 6> arguments;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0 && !take(u','))
                fail(QStringLiteral("Expected ',' between tangent arguments"));
            arguments[i] = expression(depth);
        }
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;

        const auto direction = [](double degrees) {
            const double angle = std::remainder(degrees, 360.) * (std::numbers::pi / 180.);
            return QPointF(std::cos(angle), std::sin(angle));
        };
        const auto cross = [](const QPointF& a, const QPointF& b) {
            return a.x() * b.y() - a.y() * b.x();
        };
        const QPointF start(arguments[0], arguments[1]);
        const QPointF end(arguments[3], arguments[4]);
        const auto startRay = direction(arguments[2]);
        const auto endRay = direction(arguments[5]);
        const double determinant = cross(startRay, endRay);
        if (std::abs(determinant) < 1e-9)
            fail(QStringLiteral("Endpoint tangents are parallel or nearly parallel"));
        const double fromStart = cross(end - start, endRay) / determinant;
        const double fromEnd = cross(end - start, startRay) / determinant;
        if (fromStart <= 1e-6 || fromEnd <= 1e-6)
            fail(QStringLiteral("Endpoint tangent rays must intersect away from both endpoints"));
        const auto control = start + startRay * fromStart;
        return finite(name == u"tangentx" ? control.x() : control.y());
    }

    double quadraticLength(int depth) {
        std::array<double, 6> coordinates;
        for (std::size_t i = 0; i < coordinates.size(); ++i) {
            if (i > 0 && !take(u','))
                fail(QStringLiteral("Expected ',' between quadratic curve coordinates"));
            coordinates[i] = expression(depth);
        }
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;
        const auto length = seamLength({Shape::Curve, {coordinates[0], coordinates[1]},
                                        {coordinates[4], coordinates[5]}, {coordinates[2], coordinates[3]}});
        if (!length)
            fail(QStringLiteral("Cannot measure the quadratic curve"));
        return *length;
    }

    double archHeight(int depth) {
        const auto chord = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after arch chord length"));
        const auto target = expression(depth);
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;
        if (chord <= 0 || target < chord - 1e-8)
            fail(QStringLiteral("A sewing curve cannot be shorter than the distance between its endpoints"));
        double low = 0;
        double high = target * 2;
        for (int i = 0; i < 36; ++i) {
            const double height = (low + high) / 2;
            const auto length = seamLength({Shape::Curve, {}, {chord, 0}, {chord / 2, height}});
            if (!length)
                fail(QStringLiteral("Cannot measure the fitted curve"));
            if (*length < target)
                low = height;
            else
                high = height;
        }
        return (low + high) / 2;
    }

    double capControl(int depth) {
        const double width = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after sleeve cap width"));
        const double rise = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after sleeve cap rise"));
        const double target = expression(depth);
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;
        if (width <= 0 || rise <= 0 || target <= 0)
            fail(QStringLiteral("Sleeve cap width, rise and target length must be positive"));

        // Keep the crown tangent horizontal and the cap monotone from crown to underarm.
        // For unreachable lengths, preserve a valid curve and let fitting diagnostics report the difference.
        double low = width * 0.20;
        double high = width * 0.95;
        for (int i = 0; i < 24; ++i) {
            const double control = (low + high) / 2;
            const auto length = seamLength({Shape::Curve, {0, 0}, {width, rise}, {control, 0}});
            if (!length)
                fail(QStringLiteral("Cannot measure the sleeve cap"));
            if (*length < target)
                low = control;
            else
                high = control;
        }
        return (low + high) / 2;
    }

    double capWidth(int depth) {
        const auto rise = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after arc rise"));
        const auto target = expression(depth);
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;
        if (rise <= 0 || target <= rise)
            fail(QStringLiteral("The half arc length must exceed the rise"));

        // The control is halfway across from the crown and at crown height.
        // Increasing horizontal scale strictly increases the arc length.
        double low = 0;
        double high = target;
        for (int i = 0; i < 36; ++i) {
            const auto width = (low + high) / 2;
            const auto length = seamLength({Shape::Curve, {}, {width, rise}, {width / 2, 0}});
            if (!length)
                fail(QStringLiteral("Cannot measure the arc"));
            if (*length < target)
                low = width;
            else
                high = width;
        }
        return (low + high) / 2;
    }

    double standArcParameter(int depth) {
        const auto width = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after stand width"));
        const auto rise = expression(depth);
        if (!take(u','))
            fail(QStringLiteral("Expected ',' after stand rise"));
        const auto target = expression(depth);
        if (!take(u')'))
            fail(QStringLiteral("Expected ')'"));
        if (!evaluating_)
            return 0;
        if (width <= 0 || rise <= 0 || target <= 0)
            fail(QStringLiteral("Stand width, rise and section length must be positive"));

        double low = 0;
        double high = 1;
        for (int i = 0; i < 36; ++i) {
            const auto t = (low + high) / 2;
            // De Casteljau subdivision of (0, 0), (width / 2, 2 * rise), (width, 0).
            const auto length = seamLength({Shape::Curve, {}, {width * t, 4 * rise * t * (1 - t)},
                                            {width * t / 2, 2 * rise * t}});
            if (!length || *length < target)
                low = t;
            else
                high = t;
        }
        return (low + high) / 2;
    }

    static bool letter(QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_';
    }
    static bool digit(QChar c) { return c >= u'0' && c <= u'9'; }
    QChar peek() const { return pos_ < source_.size() ? source_[pos_] : QChar{}; }
    void skipSpace() {
        while (peek().isSpace())
            ++pos_;
    }
    bool take(QChar wanted) {
        skipSpace();
        if (peek() != wanted)
            return false;
        ++pos_;
        return true;
    }
    double finite(double value) const {
        if (!std::isfinite(value))
            fail(QStringLiteral("Result is outside the numeric range"));
        return value;
    }
    [[noreturn]] void fail(const QString& reason) const {
        throw EvaluationError{QStringLiteral("%1 (column %2)").arg(reason).arg(pos_ + 1)};
    }

    QStringView source_;
    const std::function<double(const QString&, int)>& resolve_;
    qsizetype pos_ = 0;
    bool evaluating_ = true;
};

class DraftEvaluation {
public:
    explicit DraftEvaluation(const ParametricDraft& draft) {
        for (const auto& measurement : draft.measurements) {
            registerName(scalars_, measurement.name);
            if (!std::isfinite(measurement.mm) || measurement.mm < 0)
                throw EvaluationError{QStringLiteral("Measurement '%1' must be a finite, non-negative value in mm")
                                          .arg(measurement.name)};
            values_.insert(measurement.name, measurement.mm);
        }
        for (const auto& parameter : draft.parameters) {
            registerName(scalars_, parameter.name);
            formulas_.insert(parameter.name, parameter.formula);
        }
        for (const auto& point : draft.points) {
            registerName(objects_, point.name);
            formulas_.insert(point.name + QStringLiteral(".x"), point.x);
            formulas_.insert(point.name + QStringLiteral(".y"), point.y);
        }
        for (const auto& edge : draft.edges) {
            registerName(objects_, edge.name);
            formulas_.insert(edge.name + QStringLiteral(".len"), edge.control.isEmpty()
                ? QStringLiteral("hypot(%2.x - %1.x, %2.y - %1.y)").arg(edge.start, edge.end)
                : QStringLiteral("quadlen(%1.x, %1.y, %2.x, %2.y, %3.x, %3.y)")
                    .arg(edge.start, edge.control, edge.end));
        }
    }

    QVector<Entity> geometry(const ParametricDraft& draft) {
        for (const auto& parameter : draft.parameters)
            resolve(parameter.name);

        QVector<Entity> entities;
        QHash<QString, QPointF> points;
        for (const auto& point : draft.points) {
            const auto x = resolve(point.name + QStringLiteral(".x"));
            const auto y = resolve(point.name + QStringLiteral(".y"));
            const QPointF position(x, y);
            points.insert(point.name, position);
            entities.append({Shape::Point, position, position, {}, -1, Layer::Construction, point.name});
        }
        for (const auto& edge : draft.edges) {
            const auto position = [&](const QString& name) {
                const auto point = points.constFind(name);
                if (point == points.cend())
                    throw EvaluationError{QStringLiteral("Edge '%1' refers to unknown point '%2'").arg(edge.name, name)};
                return point.value();
            };
            const bool curve = !edge.control.isEmpty();
            entities.append({curve ? Shape::Curve : Shape::Line, position(edge.start), position(edge.end),
                             curve ? position(edge.control) : QPointF{}, -1, Layer::Main, edge.name});
        }
        return entities;
    }

    double expression(const QString& source) {
        const std::function<double(const QString&, int)> reference = [this](const QString& name, int depth) {
            return resolve(name, depth);
        };
        return Formula(source, reference).evaluate(0);
    }

private:
    static void registerName(QSet<QString>& names, const QString& name) {
        if (!validName(name))
            throw EvaluationError{QStringLiteral("Invalid name '%1'. Use letters A–Z, digits and underscore, and start with a letter or underscore.")
                                      .arg(name)};
        if (names.contains(name))
            throw EvaluationError{QStringLiteral("Duplicate name '%1'").arg(name)};
        names.insert(name);
    }

    double resolve(const QString& name, int depth = 0) {
        const auto value = values_.constFind(name);
        if (value != values_.cend())
            return value.value();
        if (active_.contains(name))
            throw EvaluationError{QStringLiteral("Cyclic dependency at '%1'").arg(name)};
        const auto formula = formulas_.constFind(name);
        if (formula == formulas_.cend())
            throw EvaluationError{QStringLiteral("Unknown variable '%1'").arg(name)};
        if (depth >= 128)
            throw EvaluationError{QStringLiteral("Dependency chain is too deep at '%1'").arg(name)};

        active_.insert(name);
        const std::function<double(const QString&, int)> resolveReference = [this](const QString& reference, int nextDepth) {
            return resolve(reference, nextDepth);
        };
        double result;
        try {
            result = Formula(formula.value(), resolveReference).evaluate(depth);
        } catch (const EvaluationError& error) {
            throw EvaluationError{QStringLiteral("%1: %2").arg(name, error.message)};
        }
        active_.remove(name);
        values_.insert(name, result);
        return result;
    }

    QSet<QString> scalars_, objects_, active_;
    QHash<QString, QString> formulas_;
    QHash<QString, double> values_;
};

bool applySizeMeasurements(ParametricDraft& draft, const PatternSize& size) {
    bool changed = false;
    for (auto& measurement : draft.measurements) {
        const auto value = size.measurements.constFind(measurement.name);
        if (value != size.measurements.cend() && measurement.mm != value.value()) {
            measurement.mm = value.value();
            changed = true;
        }
    }
    return changed;
}

std::optional<QVector<Entity>> evaluateSize(const ParametricDraft& base, const PatternSize& size,
                                            QString* error, ParametricDraft& draft) {
    draft = base;
    if (!applySizeMeasurements(draft, size))
        return QVector<Entity>{};

    try {
        return DraftEvaluation(draft).geometry(draft);
    } catch (const EvaluationError& failure) {
        if (error)
            *error = QStringLiteral("Size '%1': %2").arg(size.name, failure.message);
        return std::nullopt;
    }
}

}

std::optional<double> PatternDocument::evaluateModelingWidth(const ParametricDraft& draft,
                                                             const QString& formula, QString* error) {
    try {
        return DraftEvaluation(draft).expression(formula);
    } catch (const EvaluationError& failure) {
        if (error)
            *error = failure.message;
        return std::nullopt;
    }
}

bool PatternDocument::validateDraftSizes(const ParametricDraft& draft, const QVector<Entity>& base,
                                         const ProductionData& production, QString* error) const {
    if (production.useDraftMeasurements) {
        for (const auto& size : production.sizes) {
            ParametricDraft sizedDraft;
            auto generated = evaluateSize(draft, size, error, sizedDraft);
            if (!generated)
                return false;
            if (history_.isEmpty() || generated->isEmpty())
                continue;
            auto candidate = *this;
            candidate.entities_ = base;
            auto sized = candidate.bindDraftGeometry(std::move(*generated));
            QString detail;
            if (!replayModeling(sized, production, sizedDraft, &detail)) {
                if (error)
                    *error = QStringLiteral("Size '%1': %2").arg(size.name, detail);
                return false;
            }
        }
    }
    return true;
}

bool PatternDocument::validateParametricSizes(const ProductionData& production, QString* error) const {
    if (error)
        error->clear();
    if (history_.isEmpty() && seamLinks_.isEmpty() && sizeCuts_.isEmpty())
        return validateDraftSizes(draft_, entities_, production, error);
    if (draft_ == ParametricDraft{} && history_.isEmpty() && sizeCuts_.isEmpty()) {
        auto candidate = *this;
        candidate.production_ = production;
        return candidate.validateSeamLinks(error);
    }
    auto candidate = *this;
    candidate.production_ = production;
    return candidate.setParametricDraft(draft_, error);
}

std::optional<double> PatternDocument::evaluateSizeFormula(const QString& size, const QString& formula,
                                                            QString* error) const {
    if (size.isEmpty())
        return evaluateModelingWidth(draft_, formula, error);
    const auto target = std::ranges::find(production_.sizes, size, &PatternSize::name);
    if (target == production_.sizes.end()) {
        if (error)
            *error = QStringLiteral("Unknown size '%1'.").arg(size);
        return std::nullopt;
    }
    if (!production_.useDraftMeasurements)
        return evaluateModelingWidth(draft_, formula, error);
    auto draft = draft_;
    applySizeMeasurements(draft, *target);
    return evaluateModelingWidth(draft, formula, error);
}

std::optional<QVector<Entity>> PatternDocument::evaluateSizeDraft(const PatternSize& size) const {
    if (!production_.useDraftMeasurements)
        return QVector<Entity>{};
    ParametricDraft sizedDraft;
    auto generated = evaluateSize(draft_, size, nullptr, sizedDraft);
    if (history_.isEmpty() || !generated || generated->isEmpty())
        return generated;
    auto sized = bindDraftGeometry(std::move(*generated));
    if (!replayModeling(sized, production_, sizedDraft, nullptr))
        return std::nullopt;
    sized.removeIf([](const Entity& entity) { return entity.draftName.isEmpty(); });
    return sized;
}

QVector<Entity> PatternDocument::bindDraftGeometry(QVector<Entity> generated) const {
    QHash<QString, qsizetype> indices;
    for (qsizetype i = 0; i < generated.size(); ++i)
        indices.insert(generated[i].draftName, i);
    QVector<Entity> entities;
    for (const auto& original : std::as_const(entities_)) {
        if (original.draftName.isEmpty()) {
            entities.append(original);
            continue;
        }
        const auto index = indices.constFind(original.draftName);
        if (index == indices.cend())
            continue;
        auto& replacement = generated[index.value()];
        if (replacement.shape == original.shape
            || (replacement.shape != Shape::Point && original.shape != Shape::Point)) {
            replacement.piece = original.piece;
            replacement.layer = original.layer;
            replacement.seamAllowanceMm = original.seamAllowanceMm;
            replacement.seamNotches = original.seamNotches;
            replacement.seamSymbols = original.seamSymbols;
            replacement.gradedMarks = original.gradedMarks;
            replacement.bevelAtStart = original.bevelAtStart;
            replacement.bevelAtEnd = original.bevelAtEnd;
            replacement.dxfLayer = original.dxfLayer;
        }
        entities.append(replacement);
        indices.remove(original.draftName);
    }
    for (const auto& source : hiddenSources_) {
        const auto index = indices.constFind(source.name);
        if (index == indices.cend())
            continue;
        auto entity = generated[index.value()];
        entity.piece = source.piece;
        entity.layer = source.layer;
        entities.append(entity);
        indices.remove(source.name);
    }
    for (const auto& entity : std::as_const(generated)) {
        if (indices.contains(entity.draftName))
            entities.append(entity);
    }
    return entities;
}

bool PatternDocument::captureContourTopology(ModelingStep& step, QString* error) const {
    auto source = bindDraftGeometry(DraftEvaluation(draft_).geometry(draft_));
    return replayModeling(source, production_, draft_, error, &step);
}

bool PatternDocument::moveDraftPoint(const QString& name, QPointF offset, QString* error) {
    auto draft = draft_;
    const auto point = std::ranges::find(draft.points, name, &DraftPoint::name);
    if (point == draft.points.end() || !std::isfinite(offset.x()) || !std::isfinite(offset.y())) {
        if (error)
            *error = QStringLiteral("Invalid parametric point or offset");
        return false;
    }

    // Combine successive handle moves into one additive term in each formula.
    static const QRegularExpression moved(QStringLiteral(
        R"(\A\((.*)\) \+ \(([+-]?[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\)\z)"),
        QRegularExpression::DotMatchesEverythingOption);
    const auto translate = [&](QString& formula, double delta) {
        if (delta == 0)
            return true;
        const auto previous = moved.match(formula);
        if (previous.hasMatch()) {
            delta += previous.captured(2).toDouble();
            if (!std::isfinite(delta))
                return false;
            formula = previous.captured(1);
        }
        formula = QStringLiteral("(%1) + (%2)").arg(formula, QString::number(delta, 'g', 17));
        return true;
    };
    if (!translate(point->x, offset.x()) || !translate(point->y, offset.y())) {
        if (error)
            *error = QStringLiteral("The draft point offset is too large");
        return false;
    }

    auto candidate = *this;
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    QSet<qsizetype> changedPieces;
    for (qsizetype i = 0; i < std::min(entities_.size(), candidate.entities_.size()); ++i) {
        const auto& before = entities_[i];
        const auto& after = candidate.entities_[i];
        if (before.piece >= 0 && (before.a != after.a || before.b != after.b || before.control != after.control))
            changedPieces.insert(before.piece);
    }
    for (const auto piece : changedPieces) {
        const auto before = gradingPoints(*this, piece);
        const auto after = gradingPoints(candidate, piece);
        if (std::ranges::any_of(production_.rules, [&](const GradeRule& rule) {
                return rule.piece == piece && before.contains(rule.point) && !after.contains(rule.point);
            })) {
            if (error)
                *error = QStringLiteral("Moving this point would detach a grade rule on '%1'.").arg(pieces_[piece].name);
            return false;
        }
    }
    // Preserve valid cutting contours, while allowing repairs to unfinished pieces.
    const auto preservesContours = [&](const PatternDocument& before, const PatternDocument& after,
                                       const QString& size) {
        for (const auto piece : changedPieces) {
            const auto original = analyzePiece(before, piece);
            if (original.error != ContourError::None || original.allowanceFailed)
                continue;
            const auto updated = analyzePiece(after, piece);
            if (updated.error != ContourError::None || updated.allowanceFailed) {
                if (error)
                    *error = QStringLiteral("Moving this point would invalidate the cutting contour of '%1' (%2).")
                        .arg(pieces_[piece].name, size);
                return false;
            }
        }
        return true;
    };
    if (!changedPieces.isEmpty()) {
        if (!preservesContours(*this, candidate, QStringLiteral("Base")))
            return false;
        for (const auto& size : production_.sizes) {
            if (!preservesContours(gradedPattern(*this, size.name), gradedPattern(candidate, size.name), size.name))
                return false;
        }
    }
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::deriveParametricPiece(qsizetype source, const QString& name, QString* error,
                                           bool includeConstruction) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (source < 0 || source >= pieces_.size() || name.trimmed().isEmpty())
        return fail(QStringLiteral("Choose a source piece and enter a name."));
    if (std::ranges::any_of(production_.rules, [source](const auto& rule) { return rule.piece == source; }))
        return fail(QStringLiteral("This operation requires measurement-based grading."));

    auto candidate = *this;
    auto draft = draft_;
    auto metadata = pieces_[source];
    metadata.name = name.trimmed();
    const auto derived = candidate.pieces_.size();
    candidate.pieces_.append(std::move(metadata));
    QSet<QString> names;
    for (const auto& point : draft_.points)
        names.insert(point.name);
    QHash<QString, DraftEdge> edges;
    for (const auto& edge : draft_.edges) {
        names.insert(edge.name);
        edges.insert(edge.name, edge);
    }
    qsizetype number = 0;
    for (auto entity : entities_) {
        if (entity.piece != source)
            continue;
        const bool outline = entity.layer == Layer::Main;
        if (!outline && (!includeConstruction || entity.layer != Layer::Construction
                         || entity.draftName.isEmpty()))
            continue;
        const auto edge = edges.constFind(entity.draftName);
        const bool point = !outline && entity.shape == Shape::Point && names.contains(entity.draftName);
        if (edge == edges.cend() && !point)
            return fail(outline ? QStringLiteral("The source contour must consist of original formula edges.")
                                : QStringLiteral("A construction object has no original formula definition."));
        QString alias;
        do {
            alias = QStringLiteral("derived_%1_%2").arg(point ? QStringLiteral("point") : QStringLiteral("edge")).arg(++number);
        } while (names.contains(alias));
        names.insert(alias);
        if (point) {
            draft.points.append({alias, entity.draftName + QStringLiteral(".x"),
                                       entity.draftName + QStringLiteral(".y")});
        } else {
            auto shared = edge.value();
            shared.name = alias;
            draft.edges.append(shared);
        }
        entity.draftName = alias;
        entity.piece = derived;
        candidate.insert(candidate.entities_.size(), entity);
    }
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    const auto checkContour = [derived](const PatternDocument& project) {
        const auto geometry = analyzePiece(project, derived);
        return geometry.error == ContourError::None && !geometry.allowanceFailed;
    };
    if (!checkContour(candidate))
        return fail(QStringLiteral("Close the source contour and check its allowances first."));
    for (const auto& size : production_.sizes) {
        if (!checkContour(gradedPattern(candidate, size.name)))
            return fail(QStringLiteral("The derived contour is invalid in size '%1'.").arg(size.name));
    }
    *this = std::move(candidate);
    return true;
}

namespace {
template<typename NameMap>
QString rewriteFormula(const QString& formula, const NameMap& scalarNames, const NameMap& objectNames) {
    static const QRegularExpression tokens(QStringLiteral(
        R"regex((?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?|([A-Za-z_][A-Za-z_0-9]*)(\.(?:len|[xy]))?)regex"));
    QString result;
    qsizetype end = 0;
    auto matches = tokens.globalMatch(formula);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += formula.mid(end, match.capturedStart() - end);
        end = match.capturedEnd();
        const auto name = match.captured(1);
        const auto coordinate = match.captured(2);
        const bool function = coordinate.isEmpty()
            && (isNumericFunction(name) || isTangentFunction(name) || name == u"iflt" || name == u"quadlen"
                || name == u"positive" || name == u"archheight" || name == u"capcontrol" || name == u"capwidth" || name == u"standarct")
            && QStringView(formula).sliced(end).trimmed().startsWith(u'(');
        if (name.isEmpty() || function)
            result += match.captured();
        else if (coordinate.isEmpty())
            result += scalarNames.value(name, name);
        else
            result += objectNames.value(name, name) + coordinate;
    }
    return result + formula.mid(end);
}

QString digest(const QJsonArray& records) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(records).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

QString formulaDigest(const ParametricDraft& draft, const LibraryInstance& instance) {
    QJsonArray records;
    const auto collect = [&](const auto& map, const auto& definitions, auto fields) {
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const auto node = std::ranges::find(definitions, it.value(), [](const auto& item) { return item.name; });
            if (node == definitions.end())
                return false;
            auto record = fields(*node);
            record.prepend(it.value());
            record.prepend(it.key());
            records.append(record);
        }
        return true;
    };
    if (!collect(instance.parameters, draft.parameters, [](const auto& item) { return QJsonArray{item.formula}; })
        || !collect(instance.points, draft.points, [](const auto& item) { return QJsonArray{item.x, item.y}; })
        || !collect(instance.edges, draft.edges, [](const auto& item) { return QJsonArray{item.start, item.end, item.control}; }))
        return {};
    return digest(records);
}

QString sourceFormulaRevision(const ParametricDraft& draft, LibraryInstance instance) {
    for (auto* table : {&instance.parameters, &instance.points, &instance.edges}) {
        for (auto it = table->begin(); it != table->end(); ++it)
            it.value() = it.key();
    }
    return formulaDigest(draft, instance);
}

bool canTrackLibrarySource(const PatternDocument& source) {
    return source.sizeCutHistory().isEmpty()
        && std::ranges::all_of(source.entities(), [](const Entity& item) { return !item.draftName.isEmpty(); });
}

QString reserveLibraryName(const QString& name, QSet<QString>& used) {
    auto unique = name;
    for (qsizetype index = 2; used.contains(unique); ++index) {
        const auto suffix = QStringLiteral("_%1").arg(index);
        unique = name.left(64 - suffix.size()) + suffix;
    }
    used.insert(unique);
    return unique;
}

struct LibrarySplit {
    Entity original;
    QStringList sourceEdges;
    QStringList targetEdges;
};

struct LibraryMerge {
    QString sourceEdge;
    QStringList oldNames;
    QStringList targetEdges;
    QVector<double> ends;
};

std::optional<QVector<double>> chainEnds(const PatternDocument& document, const QStringList& names) {
    QVector<const Entity*> edges;
    for (const auto& name : names) {
        const auto edge = std::ranges::find(document.entities(), name, &Entity::draftName);
        if (edge == document.entities().end() || (edge->shape != Shape::Line && edge->shape != Shape::Curve)
            || edge->layer != Layer::Main || edge->piece < 0)
            return std::nullopt;
        edges.append(&*edge);
    }
    QVector<double> ends{0};
    for (qsizetype i = 0; i < edges.size(); ++i) {
        const auto& edge = *edges[i];
        const auto length = seamLength(edge);
        if (edge.piece != edges.front()->piece || edge.shape != edges.front()->shape || !length || *length <= 1e-6)
            return std::nullopt;
        if (i > 0) {
            const auto& previous = *edges[i - 1];
            const auto incoming = previous.b - (edge.shape == Shape::Curve ? previous.control : previous.a);
            const auto outgoing = (edge.shape == Shape::Curve ? edge.control : edge.b) - edge.a;
            const auto cross = incoming.x() * outgoing.y() - incoming.y() * outgoing.x();
            if (QLineF(previous.b, edge.a).length() > 1e-6 || QPointF::dotProduct(incoming, outgoing) <= 0
                || std::abs(cross) > 1e-8 * std::hypot(incoming.x(), incoming.y()) * std::hypot(outgoing.x(), outgoing.y()))
                return std::nullopt;
        }
        ends.append(ends.back() + *length);
    }
    const double total = ends.back();
    for (auto& position : ends)
        position /= total;
    return ends;
}

std::pair<qsizetype, double> splitPosition(const QVector<double>& ends, double fraction) {
    const auto segment = std::clamp<qsizetype>(
        std::upper_bound(ends.begin(), ends.end(), fraction) - ends.begin() - 1, 0, ends.size() - 2);
    return {segment, std::clamp((fraction - ends[segment]) / (ends[segment + 1] - ends[segment]), 0., 1.)};
}

std::optional<QVector<EdgeMarks>> divideEdgeMarks(const Entity& original, const QVector<double>& ends) {
    QVector<EdgeMarks> result(ends.size() - 1);
    for (const auto mark : original.seamNotches) {
        const auto [segment, fraction] = splitPosition(ends, mark);
        if (!(fraction > 1e-9 && fraction < 1 - 1e-9))
            return std::nullopt;
        result[segment].notches.append(fraction);
    }
    for (auto symbol : original.seamSymbols) {
        const auto [segment, fraction] = splitPosition(ends, symbol.fraction);
        symbol.fraction = fraction;
        if (!(fraction > 1e-9 && fraction < 1 - 1e-9))
            return std::nullopt;
        result[segment].symbols.append(symbol);
    }
    return result;
}

void appendEdgeMarks(EdgeMarks& result, const Entity& original, double start, double end) {
    for (const auto mark : original.seamNotches)
        result.notches.append(std::lerp(start, end, mark));
    for (auto symbol : original.seamSymbols) {
        symbol.fraction = std::lerp(start, end, symbol.fraction);
        result.symbols.append(symbol);
    }
}

bool collapseSplit(QVector<QString>& section, const QStringList& chain, bool ordered) {
    qsizetype count = 0;
    for (const auto& edge : chain)
        count += std::ranges::count(section, edge);
    if (count == 0)
        return true;
    if (count != chain.size())
        return false;
    if (ordered) {
        for (int direction : {1, -1}) {
            const auto start = section.indexOf(direction == 1 ? chain.front() : chain.back());
            if (start < 0 || start + chain.size() > section.size())
                continue;
            bool matches = true;
            for (qsizetype i = 0; i < chain.size(); ++i)
                matches &= section[start + i] == chain[direction == 1 ? i : chain.size() - 1 - i];
            if (matches) {
                section.remove(start, chain.size());
                section.insert(start, chain.front());
                return true;
            }
        }
        return false;
    }
    section.removeIf([&](const auto& name) { return name != chain.front() && chain.contains(name); });
    return true;
}

void expandMergedName(QVector<QString>& ids, const QString& name, const QStringList& restored) {
    const auto index = ids.indexOf(name);
    if (index < 0)
        return;
    ids.removeAt(index);
    for (qsizetype i = 0; i < restored.size(); ++i)
        ids.insert(index + i, restored[i]);
}

std::optional<bool> reversedSectionEdge(const PatternDocument& document, const QVector<QString>& section,
                                        bool reversed, const QString& name) {
    QPointF endpoint;
    for (qsizetype i = 0; i < section.size(); ++i) {
        const auto edge = std::ranges::find(document.entities(), section[i], &Entity::draftName);
        if (edge == document.entities().end())
            return std::nullopt;
        if (i > 0)
            reversed = QLineF(endpoint, edge->b).length() <= 1e-6;
        if (edge->draftName == name)
            return reversed;
        endpoint = reversed ? edge->a : edge->b;
    }
    return std::nullopt;
}
}

QString PatternDocument::libraryStructureDigest() const {
    QJsonArray records;
    for (const auto& piece : pieces_)
        records.append(QJsonArray{piece.name, piece.cutCount, piece.allowanceMm});
    QMap<QString, QJsonArray> objects;
    for (const auto& entity : entities_)
        objects.insert(entity.draftName, QJsonArray{entity.draftName, int(entity.shape), entity.piece, int(entity.layer)});
    for (const auto& object : objects)
        records.append(object);
    if (!history_.isEmpty()) {
        records.append(saveModelingHistory(history_));
        QJsonArray bindings;
        for (const auto& binding : hiddenSources_)
            bindings.append(QJsonArray{binding.name, binding.piece, int(binding.layer)});
        records.append(bindings);
    }
    if (!seamLinks_.isEmpty())
        records.append(QJsonArray{QStringLiteral("seamLinks"), saveSeamLinks(seamLinks_)});
    if (!production_.rules.isEmpty())
        records.append(QJsonArray{QStringLiteral("gradeRules"), saveProduction(production_).value("rules")});
    return digest(records);
}

bool PatternDocument::insertConstruction(const PatternDocument& source, QString* error,
                                         const QMap<QString, QString>& measurementBindings,
                                         const QString& libraryName, const QString& sourcePath) {
    if (!source.sizeCutHistory().isEmpty()) {
        if (error)
            *error = QStringLiteral("Import evaluated size panels through the marker model command.");
        return false;
    }
    if (source.draft_.points.isEmpty()) {
        if (error)
            *error = QStringLiteral("The selected project has no parametric draft.");
        return false;
    }

    if (!libraryName.isEmpty() && (libraryName.trimmed().isEmpty() || libraryName.size() > 255
                                   || !canTrackLibrarySource(source))) {
        if (error)
            *error = QStringLiteral("A tracked library source requires named formula objects.");
        return false;
    }

    if (!sourcePath.isEmpty() && (libraryName.isEmpty() || !QFileInfo(sourcePath).isAbsolute()
        || QFileInfo(sourcePath).fileName() != libraryName)) {
        if (error)
            *error = QStringLiteral("A tracked library path must identify the selected SOFIT file.");
        return false;
    }

    auto candidate = *this;
    auto draft = draft_;
    QSet<QString> scalars, objects, sharedMeasurements;
    QHash<QString, QString> scalarNames, objectNames;
    for (const auto& measurement : draft.measurements) {
        scalars.insert(measurement.name);
        sharedMeasurements.insert(measurement.name);
    }
    for (const auto& parameter : draft.parameters)
        scalars.insert(parameter.name);
    // Size tables can retain overrides for measurements that are not currently in the draft.
    for (const auto& size : production_.sizes) {
        for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
            scalars.insert(it.key());
    }
    for (const auto& point : draft.points)
        objects.insert(point.name);
    for (const auto& edge : draft.edges)
        objects.insert(edge.name);
    const auto invalidBinding = [&] {
        if (error)
            *error = QStringLiteral("Bind each source measurement to a distinct destination measurement.");
        return false;
    };
    for (auto it = measurementBindings.cbegin(); it != measurementBindings.cend(); ++it) {
        if (!sharedMeasurements.contains(it.value())
            || std::ranges::find(source.draft_.measurements, it.key(), &Measurement::name) == source.draft_.measurements.end())
            return invalidBinding();
    }

    QSet<QString> usedBindings;
    for (const auto& measurement : source.draft_.measurements) {
        const auto binding = measurementBindings.value(measurement.name, measurement.name);
        const bool shared = sharedMeasurements.contains(binding);
        const auto name = shared ? binding : reserveLibraryName(measurement.name, scalars);
        if (shared && usedBindings.contains(name))
            return invalidBinding();
        if (shared)
            usedBindings.insert(name);
        scalarNames.insert(measurement.name, name);
        if (!shared)
            draft.measurements.append({name, measurement.mm});
    }
    for (const auto& parameter : source.draft_.parameters)
        scalarNames.insert(parameter.name, reserveLibraryName(parameter.name, scalars));
    for (const auto& point : source.draft_.points)
        objectNames.insert(point.name, reserveLibraryName(point.name, objects));
    for (const auto& edge : source.draft_.edges)
        objectNames.insert(edge.name, reserveLibraryName(edge.name, objects));

    for (const auto& parameter : source.draft_.parameters)
        draft.parameters.append({scalarNames.value(parameter.name), rewriteFormula(parameter.formula, scalarNames, objectNames)});
    for (const auto& point : source.draft_.points)
        draft.points.append({objectNames.value(point.name), rewriteFormula(point.x, scalarNames, objectNames), rewriteFormula(point.y, scalarNames, objectNames)});
    for (const auto& edge : source.draft_.edges)
        draft.edges.append({objectNames.value(edge.name), objectNames.value(edge.start),
                            objectNames.value(edge.end), objectNames.value(edge.control)});

    const auto pieceOffset = pieces_.size();
    const auto stepOffset = history_.size();
    const auto mapPiece = [pieceOffset](qsizetype piece) { return piece < 0 ? piece : piece + pieceOffset; };
    const auto mapObject = [&](const QString& name) {
        if (!name.startsWith(u'@'))
            return objectNames.value(name, name);
        const auto colon = name.indexOf(u':');
        return QStringLiteral("@%1").arg(name.mid(1, colon - 1).toLongLong() + stepOffset)
            + name.mid(colon);
    };
    const auto mapObjects = [&](QVector<QString>& names) {
        for (auto& name : names)
            name = mapObject(name);
    };
    candidate.pieces_ += source.pieces_;
    for (auto entity : source.entities_) {
        entity.piece = mapPiece(entity.piece);
        entity.draftName = mapObject(entity.draftName);
        candidate.entities_.append(std::move(entity));
    }
    for (auto binding : source.hiddenSources_) {
        binding.name = mapObject(binding.name);
        binding.piece = mapPiece(binding.piece);
        candidate.hiddenSources_.append(std::move(binding));
    }
    for (auto step : source.history_) {
        mapObjects(step.objects);
        mapObjects(step.guides);
        if (step.followingGuides)
            mapObjects(*step.followingGuides);
        step.spreadFormula = rewriteFormula(step.spreadFormula, scalarNames, objectNames);
        step.rotationPivot = mapObject(step.rotationPivot);
        step.secondPiece = mapPiece(step.secondPiece);
        candidate.history_.append(std::move(step));
    }
    for (auto seam : source.seamLinks_) {
        mapObjects(seam.first);
        mapObjects(seam.second);
        seam.gradedLengthMeasurement = scalarNames.value(seam.gradedLengthMeasurement, seam.gradedLengthMeasurement);
        candidate.seamLinks_.append(std::move(seam));
    }

    for (const auto& size : source.production_.sizes) {
        auto& sizes = candidate.production_.sizes;
        auto target = std::ranges::find(sizes, size.name, &PatternSize::name);
        const bool existing = target != sizes.end();
        if (!existing) {
            sizes.append({size.name, {}});
            target = std::prev(sizes.end());
        }
        if (!source.production_.useDraftMeasurements)
            continue;
        for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it) {
            if (scalarNames.contains(it.key()) && (!existing || !sharedMeasurements.contains(scalarNames.value(it.key()))))
                target->measurements.insert(scalarNames.value(it.key()), it.value());
        }
    }
    for (auto rule : source.production_.rules) {
        rule.piece = mapPiece(rule.piece);
        candidate.production_.rules.append(std::move(rule));
    }

    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    for (const auto& rule : std::as_const(candidate.production_.rules)) {
        if (rule.piece >= pieceOffset && !gradingPoints(candidate, rule.piece).contains(rule.point)) {
            if (error)
                *error = QStringLiteral("A grade rule for '%1' no longer matches an imported point. "
                                        "Check the shared base measurements before inserting.")
                    .arg(candidate.pieces_[rule.piece].name);
            return false;
        }
    }
    if (!libraryName.isEmpty()) {
        LibraryInstance instance;
        instance.sourceName = libraryName;
        instance.sourcePath = sourcePath;
        instance.sourceStructure = source.libraryStructureDigest();
        for (const auto& measurement : source.draft_.measurements)
            instance.measurements.insert(measurement.name, scalarNames.value(measurement.name));
        for (const auto& parameter : source.draft_.parameters)
            instance.parameters.insert(parameter.name, scalarNames.value(parameter.name));
        for (const auto& point : source.draft_.points)
            instance.points.insert(point.name, objectNames.value(point.name));
        for (const auto& edge : source.draft_.edges)
            instance.edges.insert(edge.name, objectNames.value(edge.name));
        instance.installedFormulas = formulaDigest(candidate.draft_, instance);
        instance.revision = sourceFormulaRevision(source.draft_, instance);
        candidate.libraryInstances_.append(std::move(instance));
    }
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::smoothCurveChain(const QVector<qsizetype>& indices, double startDegrees,
                                       double endDegrees, QString* error) {
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (indices.size() < 2 || indices.size() > 32 || !std::isfinite(startDegrees) || !std::isfinite(endDegrees))
        return fail(QStringLiteral("Choose two to 32 edges and valid end directions."));
    const auto chain = measureSeamSection(*this, indices, true);
    if (!chain)
        return fail(QStringLiteral("The edges must form a connected chain on one piece."));
    const auto piece = entities_[chain->edges.front().index].piece;
    if (piece < 0 || std::ranges::any_of(production_.rules, [piece](const auto& rule) { return rule.piece == piece; }))
        return fail(QStringLiteral("Use a piece with measurement-based grading."));
    auto draft = draft_;
    QSet<QString> used;
    for (const auto& point : draft.points)
        used.insert(point.name);
    for (const auto& parameter : draft.parameters)
        used.insert(parameter.name);
    for (const auto& measurement : draft.measurements)
        used.insert(measurement.name);
    for (const auto& edge : draft.edges)
        used.insert(edge.name);
    QString prefix;
    int unit = 0;
    do {
        prefix = QStringLiteral("smooth_chain_%1_").arg(++unit);
    } while (std::ranges::any_of(used, [&](const auto& name) { return name.startsWith(prefix); }));

    QVector<qsizetype> definitions;
    QStringList vertices;
    for (const auto& entry : chain->edges) {
        const auto& entity = entities_[entry.index];
        const auto definition = std::ranges::find(draft.edges, entity.draftName, &DraftEdge::name);
        if ((entity.shape != Shape::Curve && entity.shape != Shape::Line) || definition == draft.edges.end())
            return fail(QStringLiteral("Each selected edge must be an original formula line or quadratic curve."));
        const auto& start = entry.reversed ? definition->end : definition->start;
        const auto& end = entry.reversed ? definition->start : definition->end;
        if (!vertices.isEmpty() && start != vertices.back())
            return fail(QStringLiteral("Adjacent curves must share the same formula endpoint."));
        if (vertices.isEmpty())
            vertices.append(start);
        vertices.append(end);
        definitions.append(definition - draft.edges.begin());
    }
    if (chain->closed && vertices.front() != vertices.back())
        return fail(QStringLiteral("The first and last curves must share the same formula endpoint."));
    const auto isLine = [&](qsizetype i) { return draft.edges[definitions[i]].control.isEmpty(); };
    if (std::ranges::all_of(definitions, [&](qsizetype i) { return draft.edges[i].control.isEmpty(); }))
        return fail(QStringLiteral("Select at least one quadratic curve to reshape."));

    QStringList directions;
    const auto jointCount = vertices.size() - (chain->closed ? 1 : 0);
    for (qsizetype joint = 0; joint < jointCount; ++joint) {
        const auto direction = prefix + QStringLiteral("direction_%1").arg(joint + 1);
        directions.append(direction);
        if (!chain->closed && (joint == 0 || joint == vertices.size() - 1)) {
            draft.parameters.append({direction, QString::number(joint == 0 ? startDegrees : endDegrees, 'g', 17)});
            continue;
        }
        const auto& previous = vertices[(joint + jointCount - 1) % jointCount];
        const auto& current = vertices[joint];
        const auto& next = vertices[joint + 1];
        const auto preceding = (joint + definitions.size() - 1) % definitions.size();
        if (isLine(preceding) || isLine(joint)) {
            const auto chord = isLine(preceding)
                ? QStringLiteral("atan2deg(%2.y - %1.y, %2.x - %1.x)").arg(previous, current)
                : QStringLiteral("atan2deg(%2.y - %1.y, %2.x - %1.x)").arg(current, next);
            draft.parameters.append({direction, chord});
            continue;
        }
        const auto bisector = QStringLiteral(
            "atan2deg((%2.y - %1.y) / hypot(%2.x - %1.x, %2.y - %1.y) + (%3.y - %2.y) / hypot(%3.x - %2.x, %3.y - %2.y),"
            "(%2.x - %1.x) / hypot(%2.x - %1.x, %2.y - %1.y) + (%3.x - %2.x) / hypot(%3.x - %2.x, %3.y - %2.y))")
            .arg(previous, current, next);
        draft.parameters.append({direction, bisector});
    }
    if (chain->closed)
        directions.append(directions.front());
    for (qsizetype i = 0; i < definitions.size(); ++i) {
        if (isLine(i))
            continue;
        const auto name = prefix + QStringLiteral("control_%1").arg(i + 1);
        const auto arguments = QStringLiteral("(%1.x, %1.y, %2, %3.x, %3.y, %4 + 180)")
            .arg(vertices[i], directions[i], vertices[i + 1], directions[i + 1]);
        draft.points.append({name, "tangentx" + arguments, "tangenty" + arguments});
        draft.edges[definitions[i]].control = name;
    }

    auto candidate = *this;
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    QStringList sizes{QString{}};
    for (const auto& size : production_.sizes)
        sizes.append(size.name);
    for (const auto& size : sizes) {
        const auto graded = gradedPattern(candidate, size, nullptr, GradeEdgeNames::Preserve);
        const auto geometry = analyzePiece(graded, piece);
        if (geometry.error != ContourError::None || geometry.allowanceFailed)
            return fail(QStringLiteral("The smoothed cutting contour is invalid in '%1'.")
                .arg(size.isEmpty() ? QStringLiteral("Base pattern") : size));
        QVector<Entity> ordered;
        for (const auto& entry : chain->edges) {
            const auto name = entities_[entry.index].draftName;
            const auto found = std::ranges::find(graded.entities(), name, &Entity::draftName);
            if (found == graded.entities().end())
                return fail(QStringLiteral("The smoothed seam edge '%1' is missing.").arg(name));
            auto edge = *found;
            if (entry.reversed)
                std::swap(edge.a, edge.b);
            ordered.append(edge);
        }
        for (qsizetype i = chain->closed ? 0 : 1; i < ordered.size(); ++i) {
            const auto& before = ordered[(i + ordered.size() - 1) % ordered.size()];
            const auto& after = ordered[i];
            const auto a = before.b - (before.shape == Shape::Curve ? before.control : before.a);
            const auto b = (after.shape == Shape::Curve ? after.control : after.b) - after.a;
            const auto determinant = a.x() * b.y() - a.y() * b.x();
            if (QPointF::dotProduct(a, b) <= 0
                || std::abs(determinant) > 1e-8 * std::hypot(a.x(), a.y()) * std::hypot(b.x(), b.y()))
                return fail(QStringLiteral("The seam directions disagree in '%1'.")
                    .arg(size.isEmpty() ? QStringLiteral("Base pattern") : size));
        }
    }
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::fitFormulaPieceToSeam(SeamLink link, QString* error, bool reshapeCurve) {
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto entity = [&](const QString& name) {
        return std::ranges::find(entities_, name, &Entity::draftName);
    };
    if (link.first.isEmpty() || link.second.isEmpty())
        return fail(QStringLiteral("Select a source and a receiving sewing chain."));
    const auto first = entity(link.first.front());
    const auto second = entity(link.second.front());
    if (first == entities_.end() || second == entities_.end()
        || first->piece < 0 || second->piece < 0 || first->piece == second->piece)
        return fail(QStringLiteral("Select sewing chains on two different pieces."));
    const auto target = second->piece;
    const auto source = first->piece;
    if (isSizeCutPiece(target) || isSizeCutPiece(source)
        || std::ranges::any_of(production_.rules, [&](const auto& rule) {
            return rule.piece == target || rule.piece == source;
        }))
        return fail(QStringLiteral("The sewing chains must use formula-based grading."));

    const auto lengthExpression = [&](const QVector<QString>& names, qsizetype piece) -> QString {
        QStringList terms;
        for (const auto& name : names) {
            const auto visible = entity(name);
            const auto edge = std::ranges::find(draft_.edges, name, &DraftEdge::name);
            if (visible == entities_.end() || visible->piece != piece || visible->layer != Layer::Main
                || edge == draft_.edges.end())
                return {};
            terms.append(name + QStringLiteral(".len"));
        }
        return terms.isEmpty() ? QString{} : "(" + terms.join(QStringLiteral(" + ")) + ")";
    };
    const auto sourceLength = lengthExpression(link.first, source);
    if (sourceLength.isEmpty() || lengthExpression(link.second, target).isEmpty() || !std::isfinite(link.targetEaseMm))
        return fail(QStringLiteral("Each sewing chain must contain original lines or quadratic formula edges."));

    auto draft = draft_;
    QSet<QString> occupied, targetNames;
    for (const auto& point : draft.points)
        occupied.insert(point.name);
    for (const auto& edge : draft.edges)
        occupied.insert(edge.name);
    for (const auto& measurement : draft.measurements)
        occupied.insert(measurement.name);
    for (const auto& parameter : draft.parameters)
        occupied.insert(parameter.name);
    for (const auto& size : production_.sizes)
        for (const auto& name : size.measurements.keys())
            occupied.insert(name);
    for (const auto& item : entities_) {
        if (item.piece != target)
            continue;
        if (item.draftName.isEmpty() || (item.shape != Shape::Line && item.shape != Shape::Curve
                                       && item.shape != Shape::Point))
            return fail(QStringLiteral("Every receiving object must be a formula line, quadratic curve or point."));
        if (item.draftName.startsWith(u'@'))
            return fail(QStringLiteral("Use an original formula piece as the receiver."));
        targetNames.insert(item.draftName);
    }
    for (const auto& instance : libraryInstances_) {
        if (std::ranges::any_of(instance.edges, [&](const auto& name) { return targetNames.contains(name); })
            || std::ranges::any_of(instance.points, [&](const auto& name) { return targetNames.contains(name); }))
            return fail(QStringLiteral("Use an editable formula piece outside a tracked library instance as the receiver."));
    }
    if (std::ranges::any_of(history_, [&](const auto& step) {
            return std::ranges::any_of(step.objects, [&](const auto& name) { return targetNames.contains(name); });
        }))
        return fail(QStringLiteral("The receiving contour already has a modeling procedure."));

    QString prefix;
    int serial = 0;
    do {
        prefix = QStringLiteral("fitted_seam_%1_").arg(++serial);
    } while (std::ranges::any_of(occupied, [&](const auto& name) { return name.startsWith(prefix); }));

    auto candidate = *this;
    draft.parameters.append({prefix + "source", sourceLength});
    if (reshapeCurve) {
        QStringList chords;
        for (const auto& name : link.second) {
            auto edge = std::ranges::find(draft.edges, name, &DraftEdge::name);
            if (edge->control.isEmpty())
                return fail(QStringLiteral("Reshaping needs quadratic receiving edges."));
            const auto stem = prefix + QString::number(chords.size() + 1) + "_";
            chords.append(stem + "chord");
            const auto start = edge->start;
            const auto end = edge->end;
            const auto rawControl = edge->control;
            const auto chord = stem + "chord";
            const auto rise = stem + "rise";
            const auto side = stem + "side";
            const auto dx = stem + "dx";
            const auto dy = stem + "dy";
            draft.parameters += {{dx, end + ".x - " + start + ".x"},
                                 {dy, end + ".y - " + start + ".y"},
                                 {chord, QStringLiteral("positive(hypot(%1, %2))").arg(dx, dy)},
                                 {rise, QStringLiteral("archheight(%1, (%2source + (%3)) * %1 / %2chords)")
                                     .arg(chord, prefix, QString::number(link.targetEaseMm, 'g', 17))},
                                 {side, QStringLiteral("iflt(%1 * (%2.y - %3.y) - %4 * (%2.x - %3.x), 0, -1, 1)")
                                     .arg(dx, rawControl, start, dy)}};
            edge->control = stem + "control";
            draft.points.append({edge->control,
                QStringLiteral("(%1.x + %2.x) / 2 - %3 * %4 * %5 / %6").arg(start, end, dy, rise, side, chord),
                QStringLiteral("(%1.y + %2.y) / 2 + %3 * %4 * %5 / %6").arg(start, end, dx, rise, side, chord)});
        }
        draft.parameters.append({prefix + "chords", chords.join(QStringLiteral(" + "))});
    } else {
        const auto anchorEdge = std::ranges::find(draft_.edges, link.second.front(), &DraftEdge::name);
        const auto anchor = link.reverseSecond ? anchorEdge->end : anchorEdge->start;
        const auto scale = prefix + "scale";
        QStringList rawLengths;
        for (const auto& name : link.second) {
            const auto edge = std::ranges::find(draft_.edges, name, &DraftEdge::name);
            rawLengths.append(edge->control.isEmpty()
                ? QStringLiteral("hypot(%2.x - %1.x, %2.y - %1.y)").arg(edge->start, edge->end)
                : QStringLiteral("quadlen(%1.x, %1.y, %2.x, %2.y, %3.x, %3.y)")
                    .arg(edge->start, edge->control, edge->end));
        }
        draft.parameters.append({prefix + "design", rawLengths.join(QStringLiteral(" + "))});
        draft.parameters.append({scale, QStringLiteral("positive(%1source + (%2)) / positive(%1design)")
            .arg(prefix, QString::number(link.targetEaseMm, 'g', 17))});
        QHash<QString, QString> transformed;
        const auto fitPoint = [&](const QString& raw) -> QString {
            if (raw.isEmpty())
                return {};
            if (transformed.contains(raw))
                return transformed.value(raw);
            const auto name = prefix + QString::number(transformed.size() + 1);
            const auto coordinate = [&](const QString& axis) {
                return QStringLiteral("%1.%2 + (%3.%2 - %1.%2) * %4").arg(anchor, axis, raw, scale);
            };
            draft.points.append({name, coordinate(QStringLiteral("x")), coordinate(QStringLiteral("y"))});
            transformed.insert(raw, name);
            return name;
        };
        for (auto& edge : draft.edges) {
            if (!targetNames.contains(edge.name))
                continue;
            edge.start = fitPoint(edge.start);
            edge.end = fitPoint(edge.end);
            edge.control = fitPoint(edge.control);
        }
        for (auto& item : candidate.entities_) {
            if (item.piece == target && item.shape == Shape::Point)
                item.draftName = fitPoint(item.draftName);
        }
    }
    if (!candidate.setParametricDraft(std::move(draft), error)
        || !candidate.addSeamLink(std::move(link), error))
        return false;
    QStringList sizes{QString{}};
    for (const auto& size : production_.sizes)
        sizes.append(size.name);
    for (const auto& size : sizes) {
        const auto graded = gradedPattern(candidate, size);
        const auto checks = graded.inspectSeamLinks().checks;
        const auto geometry = analyzePiece(graded, target);
        if (checks.size() != candidate.seamLinks_.size() || !checks.back().withinTolerance
            || geometry.error != ContourError::None || geometry.allowanceFailed)
            return fail(QStringLiteral("The fitted contour, its allowance or the sewing length is invalid in size '%1'.")
                .arg(size.isEmpty() ? QStringLiteral("Base pattern") : size));
    }
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::updateLibraryConstruction(qsizetype index, const PatternDocument& source, QString* error,
                                                const QMap<QString, QString>& replacements,
                                                const QMap<QString, QStringList>& edgeSplits,
                                                const QMap<QString, QStringList>& edgeMerges) {
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (index < 0 || index >= libraryInstances_.size())
        return fail(QStringLiteral("Choose a tracked library instance."));
    auto instance = libraryInstances_[index];
    if (!canTrackLibrarySource(source))
        return fail(QStringLiteral("The library revision contains untracked geometry."));
    if (formulaDigest(draft_, instance) != instance.installedFormulas)
        return fail(QStringLiteral("The instance contains local formula edits or missing objects."));
    auto aliases = replacements;
    QVector<LibrarySplit> splits;
    QVector<LibraryMerge> merges;
    QSet<QString> retiredEdges;
    QSet<QString> inputEdges;
    QSet<QString> outputEdges;
    for (auto split = edgeSplits.cbegin(); split != edgeSplits.cend(); ++split) {
        const auto target = instance.edges.value(split.key());
        const auto original = std::ranges::find(entities_, target, &Entity::draftName);
        if (target.isEmpty() || original == entities_.end() || (original->shape != Shape::Line && original->shape != Shape::Curve)
            || original->layer != Layer::Main || original->piece < 0
            || aliases.contains(split.key()) || split->size() < 2 || split->size() > 32)
            return fail(QStringLiteral("A split must replace one visible outline edge with an ordered chain."));
        inputEdges.insert(split.key());
        for (const auto& name : split.value()) {
            if (outputEdges.contains(name) || (instance.edges.contains(name) && name != split.key())
                || (name == split.key() && name != split->front()))
                return fail(QStringLiteral("An edge belongs to more than one replacement."));
            outputEdges.insert(name);
        }
        if (!chainEnds(source, split.value()))
            return fail(QStringLiteral("A replacement chain must be connected and tangent-continuous, with a consistent direction and edge type."));
        if (split->front() != split.key())
            aliases.insert(split.key(), split->front());
        splits.append({*original, split.value(), {}});
    }
    for (auto merge = edgeMerges.cbegin(); merge != edgeMerges.cend(); ++merge) {
        if (merge->size() < 2 || merge->size() > 32 || aliases.contains(merge->front())
            || outputEdges.contains(merge.key())
            || (instance.edges.contains(merge.key()) && merge.key() != merge->front())
            || !chainEnds(source, {merge.key()}))
            return fail(QStringLiteral("A merge must replace an ordered chain with one visible outline edge."));
        QStringList targets;
        for (const auto& name : merge.value()) {
            const auto id = instance.edges.value(name);
            if (id.isEmpty() || inputEdges.contains(name) || replacements.contains(name))
                return fail(QStringLiteral("The old merge chain must contain distinct installed edges."));
            inputEdges.insert(name);
            targets.append(id);
        }
        const auto ends = chainEnds(*this, targets);
        if (!ends)
            return fail(QStringLiteral("The installed merge chain must be connected and tangent-continuous."));
        if (merge.key() != merge->front())
            aliases.insert(merge->front(), merge.key());
        for (const auto& name : merge->sliced(1)) {
            if (std::ranges::any_of(source.draft_.edges, [&](const auto& edge) { return edge.name == name; }))
                return fail(QStringLiteral("The merged edges must be retired by the source revision."));
            retiredEdges.insert(name);
        }
        merges.append({merge.key(), merge.value(), targets, *ends});
        outputEdges.insert(merge.key());
    }
    for (const auto& name : replacements)
        if (outputEdges.contains(name))
            return fail(QStringLiteral("A replacement destination is already used by a chain."));
    const auto retains = [](const auto& definitions, const auto& bindings) {
        return std::ranges::count_if(definitions, [&](const auto& item) {
            return bindings.contains(item.name);
        }) == bindings.size();
    };
    if (source.draft_.measurements.size() != instance.measurements.size()
        || !retains(source.draft_.measurements, instance.measurements))
        return fail(QStringLiteral("The library revision changes the measurement bindings."));

    QHash<QString, QString> previousNames;
    QHash<QString, QString> previousScalars;
    QHash<QString, QString> redirectedNames;
    QHash<QString, QString> redirectedScalars;
    QSet<QString> consumedAliases;
    const auto applyAliases = [&](auto& bindings, const auto& definitions, auto& previous, auto& redirects) {
        for (auto alias = aliases.cbegin(); alias != aliases.cend(); ++alias) {
            if (!bindings.contains(alias.key()))
                continue;
            const auto exists = [&](const QString& name) {
                return std::ranges::any_of(definitions, [&](const auto& item) { return item.name == name; });
            };
            if (consumedAliases.contains(alias.key()) || exists(alias.key()) || !exists(alias.value()))
                return false;
            if (bindings.contains(alias.value())) {
                if (!replacements.contains(alias.key()) || !exists(alias.value()))
                    return false;
                redirects.insert(bindings.value(alias.key()), bindings.value(alias.value()));
                consumedAliases.insert(alias.key());
                continue;
            }
            bindings.insert(alias.value(), bindings.take(alias.key()));
            previous.insert(alias.value(), alias.key());
            consumedAliases.insert(alias.key());
        }
        return true;
    };
    if (!applyAliases(instance.parameters, source.draft_.parameters, previousScalars, redirectedScalars)
        || !applyAliases(instance.points, source.draft_.points, previousNames, redirectedNames)
        || !applyAliases(instance.edges, source.draft_.edges, previousNames, redirectedNames)
        || consumedAliases.size() != aliases.size())
        return fail(QStringLiteral("Each replacement must map a removed identifier to a distinct new identifier of the same type."));

    // Match equivalent definitions only when both directions are unambiguous.
    const auto matchEquivalent = [&](auto& bindings, const auto& incoming, const auto& installed,
                                     auto& previous, const auto& redirects, auto equivalent) {
        QMap<QString, QStringList> matches;
        QMap<QString, int> reverseCounts;
        for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
            if (retiredEdges.contains(it.key()) || redirects.contains(it.value())
                || std::ranges::any_of(incoming, [&](const auto& item) { return item.name == it.key(); }))
                continue;
            const auto current = std::ranges::find_if(installed, [&](const auto& item) { return item.name == it.value(); });
            if (current == installed.end())
                continue;
            for (const auto& next : incoming) {
                if (!bindings.contains(next.name) && equivalent(*current, next)) {
                    matches[it.key()].append(next.name);
                    ++reverseCounts[next.name];
                }
            }
        }
        for (auto it = matches.cbegin(); it != matches.cend(); ++it) {
            if (it->size() != 1 || reverseCounts.value(it->front()) != 1)
                return false;
            const auto& name = it->front();
            bindings.insert(name, bindings.take(it.key()));
            previous.insert(name, it.key());
        }
        return true;
    };
    const auto rewrite = [&](const QString& formula) {
        auto scalars = instance.measurements;
        scalars.insert(instance.parameters);
        auto objects = instance.points;
        objects.insert(instance.edges);
        return rewriteFormula(formula, scalars, objects);
    };
    const auto parametersMatched = matchEquivalent(instance.parameters, source.draft_.parameters,
        draft_.parameters, previousScalars, redirectedScalars,
        [&](const auto& current, const auto& next) { return current.formula == rewrite(next.formula); });
    const auto pointsMatched = matchEquivalent(instance.points, source.draft_.points, draft_.points, previousNames, redirectedNames,
        [&](const auto& current, const auto& next) {
            return current.x == rewrite(next.x) && current.y == rewrite(next.y);
        });
    const auto edgesMatched = matchEquivalent(instance.edges, source.draft_.edges, draft_.edges, previousNames, redirectedNames,
        [&](const auto& current, const auto& next) {
            return current.start == instance.points.value(next.start) && current.end == instance.points.value(next.end)
                && current.control == instance.points.value(next.control);
        });
    if (!parametersMatched || !pointsMatched || !edgesMatched)
        return fail(QStringLiteral("More than one replacement matches the installed objects. Supply explicit replacements."));

    auto draft = draft_;
    auto originalStructure = source;
    for (const auto& merge : merges) {
        const auto head = std::ranges::find(source.entities_, merge.sourceEdge, &Entity::draftName);
        for (const auto& name : merge.oldNames.sliced(1)) {
            auto restored = *head;
            restored.draftName = name;
            originalStructure.entities_.append(std::move(restored));
        }
        auto expanded = merge.oldNames;
        expanded.front() = merge.sourceEdge;
        for (auto& step : originalStructure.history_) {
            expandMergedName(step.objects, merge.sourceEdge, expanded);
            if (step.rotationPivot.isEmpty())
                std::ranges::sort(step.objects);
        }
        for (qsizetype i = 0; i < originalStructure.seamLinks_.size(); ++i) {
            auto& seam = originalStructure.seamLinks_[i];
            const auto& original = source.seamLinks_[i];
            // Both chains are declared in start-to-end order. Section walks can be reversed.
            const auto expandSection = [&](QVector<QString>& section, const QVector<QString>& before, bool reverseFirst) {
                if (!before.contains(merge.sourceEdge))
                    return;
                auto ids = expanded;
                if (reversedSectionEdge(source, before, reverseFirst, merge.sourceEdge).value_or(false))
                    std::ranges::reverse(ids);
                expandMergedName(section, merge.sourceEdge, ids);
            };
            expandSection(seam.first, original.first, original.reverseFirst);
            expandSection(seam.second, original.second, original.reverseSecond);
        }
    }
    for (const auto& split : splits) {
        const auto additional = split.sourceEdges.sliced(1);
        originalStructure.entities_.removeIf([&](const auto& edge) { return additional.contains(edge.draftName); });
        for (auto& step : originalStructure.history_) {
            if (!collapseSplit(step.objects, split.sourceEdges, false))
                return fail(QStringLiteral("A source procedure addresses only part of the replacement chain."));
        }
        for (auto& seam : originalStructure.seamLinks_) {
            if (!collapseSplit(seam.first, split.sourceEdges, true) || !collapseSplit(seam.second, split.sourceEdges, true))
                return fail(QStringLiteral("A source seam addresses only part of the replacement chain."));
        }
    }
    const auto removeMissing = [&](auto& bindings, const auto& definitions, auto& local, bool geometry) {
        for (auto binding = bindings.begin(); binding != bindings.end();) {
            if (std::ranges::any_of(definitions, [&](const auto& item) { return item.name == binding.key(); })) {
                ++binding;
                continue;
            }
            const auto name = binding.value();
            if (geometry && !retiredEdges.contains(binding.key())) {
                const auto installed = std::ranges::find(entities_, name, &Entity::draftName);
                if (installed == entities_.end() || installed->piece != -1 || installed->layer != Layer::Construction)
                    return false;
                auto restored = *installed;
                restored.draftName = binding.key();
                originalStructure.entities_.append(std::move(restored));
            }
            local.removeIf([&](const auto& item) { return item.name == name; });
            ++binding;
        }
        return true;
    };
    if (!removeMissing(instance.parameters, source.draft_.parameters, draft.parameters, false)
        || !removeMissing(instance.points, source.draft_.points, draft.points, true)
        || !removeMissing(instance.edges, source.draft_.edges, draft.edges, true))
        return fail(QStringLiteral("A removed object is assigned to a detail or used by a procedure. Supply its replacement."));
    for (qsizetype i = originalStructure.entities_.size(); i > 0; --i) {
        auto& entity = originalStructure.entities_[i - 1];
        const bool addedPoint = entity.shape == Shape::Point && !instance.points.contains(entity.draftName);
        const bool addedGuide = (entity.shape == Shape::Line || entity.shape == Shape::Curve)
            && !entity.draftName.startsWith(u'@') && !instance.edges.contains(entity.draftName);
        if ((addedPoint || addedGuide) && entity.piece == -1 && entity.layer == Layer::Construction)
            originalStructure.entities_.removeAt(i - 1);
        else
            entity.draftName = previousNames.value(entity.draftName, entity.draftName);
    }
    const auto restoreNames = [&](QVector<QString>& names, bool sorted) {
        bool changed = false;
        for (auto& name : names) {
            if (const auto previous = previousNames.constFind(name); previous != previousNames.cend()) {
                name = previous.value();
                changed = true;
            }
        }
        if (sorted && changed)
            std::ranges::sort(names);
    };
    for (auto& step : originalStructure.history_) {
        restoreNames(step.objects, step.rotationPivot.isEmpty());
        restoreNames(step.guides, false);
        if (step.followingGuides)
            restoreNames(*step.followingGuides, false);
        step.rotationPivot = previousNames.value(step.rotationPivot, step.rotationPivot);
        step.spreadFormula = rewriteFormula(step.spreadFormula, previousScalars, previousNames);
    }
    for (auto& binding : originalStructure.hiddenSources_)
        binding.name = previousNames.value(binding.name, binding.name);
    for (auto& seam : originalStructure.seamLinks_) {
        restoreNames(seam.first, false);
        restoreNames(seam.second, false);
    }
    if (originalStructure.libraryStructureDigest() != instance.sourceStructure)
        return fail(QStringLiteral("The library revision changes the piece or object structure."));
    const auto dropMissing = [](auto& bindings, const auto& definitions) {
        bindings.removeIf([&](auto it) {
            return std::ranges::none_of(definitions, [&](const auto& item) { return item.name == it.key(); });
        });
    };
    dropMissing(instance.parameters, source.draft_.parameters);
    dropMissing(instance.points, source.draft_.points);
    dropMissing(instance.edges, source.draft_.edges);
    if (source.draft_.points.isEmpty())
        return fail(QStringLiteral("The library revision must retain a parametric construction."));
    auto scalarNames = instance.measurements;
    scalarNames.insert(instance.parameters);
    auto objectNames = instance.points;
    objectNames.insert(instance.edges);
    QSet<QString> usedScalars;
    QSet<QString> usedObjects;
    for (const auto& measurement : draft.measurements)
        usedScalars.insert(measurement.name);
    for (const auto& parameter : draft_.parameters)
        usedScalars.insert(parameter.name);
    for (const auto& size : production_.sizes)
        for (const auto& name : size.measurements.keys())
            usedScalars.insert(name);
    for (const auto& point : draft_.points)
        usedObjects.insert(point.name);
    for (const auto& edge : draft_.edges)
        usedObjects.insert(edge.name);
    for (const auto& parameter : source.draft_.parameters) {
        if (instance.parameters.contains(parameter.name))
            continue;
        const auto name = reserveLibraryName(parameter.name, usedScalars);
        instance.parameters.insert(parameter.name, name);
        scalarNames.insert(parameter.name, name);
        draft.parameters.append(DraftParameter{name, {}});
    }
    for (const auto& point : source.draft_.points) {
        if (instance.points.contains(point.name))
            continue;
        const auto visible = std::ranges::find(source.entities_, point.name, &Entity::draftName);
        if (visible == source.entities_.end() || visible->piece != -1 || visible->shape != Shape::Point
            || visible->layer != Layer::Construction)
            return fail(QStringLiteral("A new formula point must be an unassigned construction point."));
        const auto name = reserveLibraryName(point.name, usedObjects);
        instance.points.insert(point.name, name);
        objectNames.insert(point.name, name);
        draft.points.append(DraftPoint{name, {}, {}});
    }
    for (const auto& measurement : instance.measurements) {
        if (std::ranges::find(draft.measurements, measurement, &Measurement::name) == draft.measurements.end())
            return fail(QStringLiteral("A bound project measurement is missing."));
    }
    auto candidate = *this;
    if (!redirectedNames.isEmpty() || !redirectedScalars.isEmpty()) {
        const auto redirect = [&](QString& name) { name = redirectedNames.value(name, name); };
        const auto redirectAll = [&](QVector<QString>& names) {
            for (auto& name : names)
                redirect(name);
        };
        for (auto& parameter : draft.parameters)
            parameter.formula = rewriteFormula(parameter.formula, redirectedScalars, redirectedNames);
        for (auto& point : draft.points) {
            point.x = rewriteFormula(point.x, redirectedScalars, redirectedNames);
            point.y = rewriteFormula(point.y, redirectedScalars, redirectedNames);
        }
        for (auto& edge : draft.edges) {
            redirect(edge.start);
            redirect(edge.end);
            redirect(edge.control);
        }
        for (auto& step : candidate.history_) {
            redirectAll(step.objects);
            redirectAll(step.guides);
            if (step.followingGuides)
                redirectAll(*step.followingGuides);
            redirect(step.rotationPivot);
            step.spreadFormula = rewriteFormula(step.spreadFormula, redirectedScalars, redirectedNames);
        }
        for (auto& step : candidate.sizeCuts_) {
            redirectAll(step.guides);
            step.countFormula = rewriteFormula(step.countFormula, redirectedScalars, redirectedNames);
        }
    }
    for (const auto& merge : merges) {
        const auto incoming = std::ranges::find(source.draft_.edges, merge.sourceEdge, &DraftEdge::name);
        const auto first = std::ranges::find(draft_.edges, merge.targetEdges.front(), &DraftEdge::name);
        const auto last = std::ranges::find(draft_.edges, merge.targetEdges.back(), &DraftEdge::name);
        if (incoming == source.draft_.edges.end() || first == draft_.edges.end() || last == draft_.edges.end()
            || objectNames.value(incoming->start) != first->start || objectNames.value(incoming->end) != last->end)
            return fail(QStringLiteral("The merge must retain the ordered endpoints of the original chain."));
        auto head = std::ranges::find(candidate.entities_, merge.targetEdges.front(), &Entity::draftName);
        auto merged = *head;
        merged.seamNotches.clear();
        merged.seamSymbols.clear();
        merged.gradedMarks.clear();
        EdgeMarks mergedMarks;
        for (qsizetype i = 0; i < merge.targetEdges.size(); ++i) {
            const auto edge = std::ranges::find(entities_, merge.targetEdges[i], &Entity::draftName);
            if (edge->seamAllowanceMm != merged.seamAllowanceMm || edge->dxfLayer != merged.dxfLayer
                || (i > 0 && edge->bevelAtStart) || (i + 1 < merge.targetEdges.size() && edge->bevelAtEnd))
                return fail(QStringLiteral("The merged edge cannot represent different allowances, layers or inner corner overrides."));
            appendEdgeMarks(mergedMarks, *edge, merge.ends[i], merge.ends[i + 1]);
            if (i + 1 == merge.targetEdges.size())
                merged.bevelAtEnd = edge->bevelAtEnd;
        }
        merged.seamNotches = std::move(mergedMarks.notches);
        merged.seamSymbols = std::move(mergedMarks.symbols);
        if (merged.seamNotches.size() > 100 || merged.seamSymbols.size() > 100)
            return fail(QStringLiteral("There are too many manufacturing marks for one merged edge."));
        for (auto& step : candidate.history_) {
            const auto consumesChain = std::ranges::any_of(merge.targetEdges, [&](const auto& id) { return step.objects.contains(id); });
            if (consumesChain && (step.contour || !step.rotationPivot.isEmpty() || !collapseSplit(step.objects, merge.targetEdges, false)))
                return fail(QStringLiteral("A receiving procedure cannot be mapped to the merged edge."));
        }
        for (auto& seam : candidate.seamLinks_) {
            if (!collapseSplit(seam.first, merge.targetEdges, true) || !collapseSplit(seam.second, merge.targetEdges, true))
                return fail(QStringLiteral("A receiving seam refers to only part of the merged chain."));
        }
        *head = std::move(merged);
    }
    for (auto& split : splits) {
        for (const auto& name : split.sourceEdges) {
            if (!instance.edges.contains(name)) {
                const auto id = reserveLibraryName(name, usedObjects);
                instance.edges.insert(name, id);
                objectNames.insert(name, id);
                draft.edges.append(DraftEdge{id, {}, {}, {}});
            }
            const auto& id = instance.edges.value(name);
            split.targetEdges.append(id);
        }
        const auto old = std::ranges::find(draft_.edges, split.original.draftName, &DraftEdge::name);
        const auto first = std::ranges::find(source.draft_.edges, split.sourceEdges.front(), &DraftEdge::name);
        const auto last = std::ranges::find(source.draft_.edges, split.sourceEdges.back(), &DraftEdge::name);
        if (old == draft_.edges.end() || first == source.draft_.edges.end() || last == source.draft_.edges.end()
            || objectNames.value(first->start) != old->start || objectNames.value(last->end) != old->end)
            return fail(QStringLiteral("The replacement chain must retain the ordered endpoints of the original edge."));
        for (auto& step : candidate.history_) {
            if (!step.objects.contains(split.original.draftName))
                continue;
            if (step.contour || !step.rotationPivot.isEmpty())
                return fail(QStringLiteral("This contour migration needs an explicit replacement for a dependent procedure."));
            for (const auto& id : split.targetEdges.sliced(1))
                step.objects.append(id);
            std::ranges::sort(step.objects);
        }
        const auto expand = [&](QVector<QString>& section, const QVector<QString>& before, bool reverseFirst) {
            const auto id = split.original.draftName;
            if (!before.contains(id))
                return true;
            const auto reversed = reversedSectionEdge(*this, before, reverseFirst, id);
            if (!reversed)
                return false;
            auto expanded = split.targetEdges;
            if (*reversed)
                std::ranges::reverse(expanded);
            expandMergedName(section, id, expanded);
            return true;
        };
        for (qsizetype i = 0; i < candidate.seamLinks_.size(); ++i) {
            auto& seam = candidate.seamLinks_[i];
            const auto& original = seamLinks_[i];
            if (!expand(seam.first, original.first, original.reverseFirst)
                || !expand(seam.second, original.second, original.reverseSecond))
                return fail(QStringLiteral("A receiving seam could not be mapped to the replacement chain."));
        }
        QVector<Entity> segments;
        for (qsizetype i = 0; i < split.targetEdges.size(); ++i) {
            auto edge = split.original;
            edge.draftName = split.targetEdges[i];
            edge.seamNotches.clear();
            edge.seamSymbols.clear();
            edge.gradedMarks.clear();
            if (i != 0)
                edge.bevelAtStart.reset();
            if (i + 1 != split.targetEdges.size())
                edge.bevelAtEnd.reset();
            segments.append(std::move(edge));
        }
        const auto existing = std::ranges::find(candidate.entities_, split.original.draftName, &Entity::draftName);
        *existing = segments.front();
        candidate.entities_.append(segments.sliced(1));
    }
    for (const auto& edge : source.draft_.edges) {
        if (!instance.edges.contains(edge.name)) {
            const auto visible = std::ranges::find(source.entities_, edge.name, &Entity::draftName);
            if (visible == source.entities_.end() || visible->piece != -1 || visible->layer != Layer::Construction)
                return fail(QStringLiteral("A new formula edge must be an unassigned construction guide."));
            const auto name = reserveLibraryName(edge.name, usedObjects);
            instance.edges.insert(edge.name, name);
            objectNames.insert(edge.name, name);
            draft.edges.append(DraftEdge{name, {}, {}, {}});
            auto guide = *visible;
            guide.draftName = name;
            candidate.entities_.append(std::move(guide));
        }
        auto mapped = std::ranges::find(draft.edges, instance.edges.value(edge.name), &DraftEdge::name);
        mapped->start = objectNames.value(edge.start);
        mapped->end = objectNames.value(edge.end);
        mapped->control = objectNames.value(edge.control);
    }
    const auto rewriteLengths = [&](QString& formula) {
        for (const auto& split : splits) {
            QStringList lengths;
            for (const auto& name : split.targetEdges)
                lengths.append(name + ".len");
            const QRegularExpression original(QStringLiteral(R"(\b%1\.len\b)")
                .arg(QRegularExpression::escape(split.original.draftName)));
            formula.replace(original, "(" + lengths.join(QStringLiteral(" + ")) + ")");
        }
        for (const auto& merge : merges) {
            QStringList lengths;
            for (const auto& name : merge.targetEdges)
                lengths.append(QRegularExpression::escape(name + ".len"));
            const auto separator = QStringLiteral(R"(\s*\+\s*)");
            const auto forward = lengths.join(separator);
            std::ranges::reverse(lengths);
            const QRegularExpression chain(QStringLiteral(R"(\(\s*(?:%1|%2)\s*\))")
                .arg(forward, lengths.join(separator)));
            auto remainder = formula.split(chain);
            QStringList sectionLengths;
            for (const auto& name : merge.targetEdges) {
                const auto edge = std::ranges::find(draft_.edges, name, &DraftEdge::name);
                const auto keptPoint = [&](const QString& point) {
                    return std::ranges::find(draft.points, point, &DraftPoint::name) != draft.points.end();
                };
                if (edge == draft_.edges.end() || !keptPoint(edge->start) || !keptPoint(edge->end)
                    || (!edge->control.isEmpty() && !keptPoint(edge->control))) {
                    sectionLengths.clear();
                    break;
                }
                sectionLengths.append(edge->control.isEmpty()
                    ? QStringLiteral("hypot(%2.x - %1.x, %2.y - %1.y)").arg(edge->start, edge->end)
                    : QStringLiteral("quadlen(%1.x, %1.y, %2.x, %2.y, %3.x, %3.y)")
                        .arg(edge->start, edge->control, edge->end));
            }
            for (qsizetype i = 0; i < merge.targetEdges.size(); ++i) {
                const QRegularExpression partial(QStringLiteral(R"(\b%1\.len\b)")
                    .arg(QRegularExpression::escape(merge.targetEdges[i])));
                for (auto& part : remainder) {
                    if (!part.contains(partial))
                        continue;
                    if (sectionLengths.isEmpty())
                        return false;
                    // Stage substitutions so the surviving edge reference is not remapped again.
                    part.replace(partial, QStringLiteral("(%%MERGED%% * %1 / positive(%2))")
                        .arg(sectionLengths[i], sectionLengths.join(QStringLiteral(" + "))));
                }
            }
            formula = remainder.join(merge.targetEdges.front() + ".len");
            formula.replace(QStringLiteral("%%MERGED%%"), merge.targetEdges.front() + ".len");
        }
        return true;
    };
    const auto mappedParameters = instance.parameters.values();
    const auto mappedPoints = instance.points.values();
    for (auto& parameter : draft.parameters)
        if (!mappedParameters.contains(parameter.name) && !rewriteLengths(parameter.formula))
            return fail(QStringLiteral("A formula refers to only part of the merged sewing chain."));
    for (auto& point : draft.points)
        if (!mappedPoints.contains(point.name) && (!rewriteLengths(point.x) || !rewriteLengths(point.y)))
            return fail(QStringLiteral("A point formula refers to only part of the merged sewing chain."));
    for (auto& step : candidate.history_)
        if (!rewriteLengths(step.spreadFormula))
            return fail(QStringLiteral("A modeling formula refers to only part of the merged sewing chain."));
    for (const auto& parameter : source.draft_.parameters) {
        auto mapped = std::ranges::find(draft.parameters, instance.parameters.value(parameter.name), &DraftParameter::name);
        mapped->formula = rewriteFormula(parameter.formula, scalarNames, objectNames);
    }
    for (const auto& point : source.draft_.points) {
        auto mapped = std::ranges::find(draft.points, instance.points.value(point.name), &DraftPoint::name);
        mapped->x = rewriteFormula(point.x, scalarNames, objectNames);
        mapped->y = rewriteFormula(point.y, scalarNames, objectNames);
    }
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    QStringList sizes{QString{}};
    for (const auto& size : production_.sizes)
        sizes.append(size.name);
    for (const auto& size : sizes) {
        const auto previous = gradedPattern(*this, size, nullptr, GradeEdgeNames::Preserve);
        const auto updated = gradedPattern(candidate, size, nullptr, GradeEdgeNames::Preserve);
        if (updated.entities().isEmpty())
            return fail(QStringLiteral("The revision cannot be recalculated in size '%1'.").arg(size));
        for (const auto& split : splits) {
            const auto ends = chainEnds(updated, split.targetEdges);
            if (!ends)
                return fail(QStringLiteral("The replacement chain is invalid in size '%1'.").arg(size));
            const auto oldEdge = std::ranges::find(previous.entities(), split.original.draftName, &Entity::draftName);
            if (oldEdge == previous.entities().end())
                return fail(QStringLiteral("The original edge is missing in size '%1'.").arg(size));
            auto marks = divideEdgeMarks(*oldEdge, *ends);
            if (!marks)
                return fail(QStringLiteral("A manufacturing mark lies on a new split vertex in size '%1'.").arg(size));
            for (qsizetype i = 0; i < split.targetEdges.size(); ++i) {
                auto edge = std::ranges::find(candidate.entities_, split.targetEdges[i], &Entity::draftName);
                if (edge == candidate.entities_.end())
                    return fail(QStringLiteral("A replacement edge is missing."));
                if (size.isEmpty()) {
                    edge->seamNotches = std::move((*marks)[i].notches);
                    edge->seamSymbols = std::move((*marks)[i].symbols);
                } else if ((*marks)[i] != EdgeMarks{edge->seamNotches, edge->seamSymbols}) {
                    edge->gradedMarks.insert(size, std::move((*marks)[i]));
                }
            }
            for (auto& copy : candidate.production_.placements) {
                if (copy.size == size && copy.repeatEdge == split.original.draftName) {
                    const auto [segment, fraction] = splitPosition(*ends, copy.repeatParameter);
                    copy.repeatEdge = split.targetEdges[segment];
                    copy.repeatParameter = fraction;
                }
            }
        }
        for (const auto& merge : merges) {
            const auto ends = chainEnds(previous, merge.targetEdges);
            if (!ends)
                return fail(QStringLiteral("The old merge chain is invalid in size '%1'.").arg(size));
            const auto edge = std::ranges::find(updated.entities(), merge.targetEdges.front(), &Entity::draftName);
            if (edge == updated.entities().end())
                return fail(QStringLiteral("The merged edge is missing in size '%1'.").arg(size));
            if (!size.isEmpty()) {
                EdgeMarks marks;
                for (qsizetype i = 0; i < merge.targetEdges.size(); ++i) {
                    const auto oldEdge = std::ranges::find(previous.entities(), merge.targetEdges[i], &Entity::draftName);
                    appendEdgeMarks(marks, *oldEdge, (*ends)[i], (*ends)[i + 1]);
                }
                if (marks.notches.size() > 100 || marks.symbols.size() > 100)
                    return fail(QStringLiteral("There are too many manufacturing marks for one merged edge in size '%1'.").arg(size));
                auto merged = std::ranges::find(candidate.entities_, merge.targetEdges.front(), &Entity::draftName);
                if (merged == candidate.entities_.end())
                    return fail(QStringLiteral("The merged edge is missing."));
                if (marks != EdgeMarks{merged->seamNotches, merged->seamSymbols})
                    merged->gradedMarks.insert(size, std::move(marks));
            }
            for (auto& copy : candidate.production_.placements) {
                const auto position = merge.targetEdges.indexOf(copy.repeatEdge);
                if (copy.size == size && position >= 0) {
                    copy.repeatEdge = merge.targetEdges.front();
                    copy.repeatParameter = std::lerp((*ends)[position], (*ends)[position + 1], copy.repeatParameter);
                }
            }
        }
        for (qsizetype piece = 0; piece < pieces_.size(); ++piece) {
            const auto before = analyzePiece(previous, piece);
            if (before.error != ContourError::None || before.allowanceFailed)
                continue;
            const auto after = analyzePiece(updated, piece);
            if (after.error != ContourError::None || after.allowanceFailed)
                return fail(QStringLiteral("The revision invalidates the cutting contour of '%1' in size '%2'.")
                    .arg(pieces_[piece].name, size.isEmpty() ? QStringLiteral("Base pattern") : size));
        }
    }
    for (const auto& rule : production_.rules) {
        if (gradingPoints(*this, rule.piece).contains(rule.point)
            && !gradingPoints(candidate, rule.piece).contains(rule.point))
            return fail(QStringLiteral("The revision moves a point used by a manual grading rule."));
    }

    auto& record = candidate.libraryInstances_[index];
    record = instance;
    record.sourceStructure = source.libraryStructureDigest();
    record.installedFormulas = formulaDigest(candidate.draft_, instance);
    record.revision = sourceFormulaRevision(source.draft_, instance);
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::refreshLibraryDependencies(const QString& projectPath, QString* error,
                                                 QSet<QString>* sourceFiles) {
    if (sourceFiles)
        sourceFiles->clear();
    if (libraryInstances_.isEmpty())
        return true;

    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto root = QFileInfo(projectPath).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isFile())
        return fail(QStringLiteral("Save the project before refreshing its library dependencies."));

    QStringList active;
    QHash<QString, PatternDocument> resolved;
    const auto refresh = [&](auto&& visit, const QString& path, PatternDocument& document) -> bool {
        active.append(path);
        const auto directory = QFileInfo(path).absoluteDir();
        for (qsizetype i = 0; i < document.libraryInstances_.size(); ++i) {
            const auto& instance = document.libraryInstances_[i];
            const auto& name = instance.sourceName;
            if (name.contains(u'/') || name.contains(u'\\') || name.contains(u':')
                || !name.endsWith(QStringLiteral(".sofit"), Qt::CaseInsensitive))
                return fail(QStringLiteral("Invalid library filename in '%1': %2").arg(path, name));
            const bool explicitPath = !instance.sourcePath.isEmpty();
            const QFileInfo file(explicitPath ? instance.sourcePath : directory.filePath(name));
            const auto dependency = file.canonicalFilePath();
            if (sourceFiles) {
                sourceFiles->insert(file.absoluteFilePath());
                if (!dependency.isEmpty())
                    sourceFiles->insert(dependency);
            }
            if (!file.isFile() || dependency.isEmpty())
                return fail(QStringLiteral("Library file '%1' was not found.").arg(file.absoluteFilePath()));
            if (!explicitPath && QFileInfo(dependency).absolutePath() != directory.absolutePath())
                return fail(QStringLiteral("Library file '%1' must be in '%2'.").arg(name, directory.absolutePath()));
            if (active.contains(dependency)) {
                auto chain = active;
                chain.append(dependency);
                return fail(QStringLiteral("Cyclic library dependencies: %1").arg(chain.join(QStringLiteral(" → "))));
            }
            if (!resolved.contains(dependency)) {
                if (active.size() >= 64 || active.size() + resolved.size() >= 512)
                    return fail(QStringLiteral("The library dependency graph is too large."));
                PatternDocument source;
                QString detail;
                if (!source.load(dependency, &detail))
                    return fail(QStringLiteral("%1: %2").arg(dependency, detail));
                if (!visit(visit, dependency, source))
                    return false;
                resolved.insert(dependency, std::move(source));
            }
            const auto& source = resolved.constFind(dependency).value();
            QString detail;
            if (!document.updateLibraryConstruction(i, source, &detail))
                return fail(QStringLiteral("%1 ← %2: %3").arg(path, dependency, detail));
        }
        active.removeLast();
        return true;
    };

    auto candidate = *this;
    if (!refresh(refresh, root, candidate))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::setParametricDraft(ParametricDraft draft, QString* error) {
    QVector<Entity> base;
    const auto evaluate = [&] {
        try {
            base = bindDraftGeometry(DraftEvaluation(draft).geometry(draft));
        } catch (const EvaluationError& failure) {
            if (error)
                *error = failure.message;
            return false;
        }
        return validateDraftSizes(draft, base, production_, error)
            && replayModeling(base, production_, draft, error);
    };
    if (!evaluate())
        return false;
    if (std::ranges::any_of(seamLinks_, [](const auto& link) { return !link.gradedLengthMeasurement.isEmpty(); })) {
        auto measuring = *this;
        measuring.entities_ = base;
        const auto checks = measuring.inspectSeamLinks().checks;
        bool changed = false;
        for (qsizetype i = 0; i < seamLinks_.size(); ++i) {
            const auto& name = seamLinks_[i].gradedLengthMeasurement;
            if (name.isEmpty())
                continue;
            const auto measurement = std::ranges::find(draft.measurements, name, &Measurement::name);
            if (measurement == draft.measurements.end() || !(checks[i].firstMm > 0)) {
                if (error)
                    *error = QStringLiteral("The driving sewing chain could not be measured.");
                return false;
            }
            changed |= measurement->mm != checks[i].firstMm;
            measurement->mm = checks[i].firstMm;
        }
        if (changed && !evaluate())
            return false;
    }
    if (!seamLinks_.isEmpty() || !sizeCuts_.isEmpty()) {
        auto candidate = *this;
        candidate.entities_ = base;
        candidate.draft_ = draft;
        if (!candidate.refreshSizeCutPanels(error) || !candidate.validateSeamLinks(error))
            return false;
        pieces_ = std::move(candidate.pieces_);
        sizeCuts_ = std::move(candidate.sizeCuts_);
    }
    entities_ = std::move(base);
    draft_ = std::move(draft);
    if (error)
        error->clear();
    return true;
}
}
