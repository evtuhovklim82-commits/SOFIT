#include "PatternDocument.h"
#include "PieceGeometry.h"
#include "Production.h"

#include <algorithm>
#include <array>

#include <QHash>
#include <QRegularExpression>
#include <QStringView>
#include <QSet>
#include <cmath>
#include <functional>
#include <iterator>
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
                if (divisor == 0)
                    fail(QStringLiteral("Division by zero"));
                value = finite(value / divisor);
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
            if (name == u"quadlen" && take(u'('))
                return quadraticLength(depth + 1);
            if (name == u"capcontrol" && take(u'('))
                return capControl(depth + 1);
            if (name == u"positive" && take(u'(')) {
                const auto value = expression(depth + 1);
                if (!take(u')'))
                    fail(QStringLiteral("Expected ')'"));
                if (value <= 0)
                    fail(QStringLiteral("Expected a value greater than zero"));
                return value;
            }
            pos_ = end;
            if (peek() == u'.') {
                ++pos_;
                if (peek() != u'x' && peek() != u'y')
                    fail(QStringLiteral("Expected point coordinate x or y"));
                ++pos_;
            }
            return resolve_(source_.mid(start, pos_ - start).toString(), depth + 1);
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
        return finite(number);
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
        const auto length = seamLength({Shape::Curve, {coordinates[0], coordinates[1]},
                                        {coordinates[4], coordinates[5]}, {coordinates[2], coordinates[3]}});
        if (!length)
            fail(QStringLiteral("Cannot measure the quadratic curve"));
        return *length;
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
        for (const auto& edge : draft.edges)
            registerName(objects_, edge.name);
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

std::optional<QVector<Entity>> evaluateSize(const ParametricDraft& base, const PatternSize& size,
                                            QString* error, ParametricDraft& draft) {
    draft = base;
    bool changed = false;
    for (auto& measurement : draft.measurements) {
        const auto value = size.measurements.constFind(measurement.name);
        if (value != size.measurements.cend() && measurement.mm != value.value()) {
            measurement.mm = value.value();
            changed = true;
        }
    }
    if (!changed)
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
    if (history_.isEmpty() && seamLinks_.isEmpty())
        return validateDraftSizes(draft_, entities_, production, error);
    if (draft_ == ParametricDraft{} && history_.isEmpty()) {
        auto candidate = *this;
        candidate.production_ = production;
        return candidate.validateSeamLinks(error);
    }
    auto candidate = *this;
    candidate.production_ = production;
    return candidate.setParametricDraft(draft_, error);
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

bool PatternDocument::deriveParametricPiece(qsizetype source, const QString& name, QString* error) {
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
        if (entity.piece != source || entity.layer != Layer::Main)
            continue;
        const auto edge = edges.constFind(entity.draftName);
        if (edge == edges.cend())
            return fail(QStringLiteral("The source contour must consist of original formula edges."));
        auto shared = edge.value();
        do {
            shared.name = QStringLiteral("derived_edge_%1").arg(++number);
        } while (names.contains(shared.name));
        draft.edges.append(shared);
        entity.draftName = shared.name;
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

bool PatternDocument::insertConstruction(const PatternDocument& source, QString* error,
                                         const QMap<QString, QString>& measurementBindings) {
    if (source.draft_.points.isEmpty()) {
        if (error)
            *error = QStringLiteral("The selected project has no parametric draft.");
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
    const auto reserveName = [](const QString& name, QSet<QString>& used) {
        auto unique = name;
        for (qsizetype index = 2; used.contains(unique); ++index) {
            const auto suffix = QStringLiteral("_%1").arg(index);
            unique = name.left(64 - suffix.size()) + suffix;
        }
        used.insert(unique);
        return unique;
    };
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
        const auto name = shared ? binding : reserveName(measurement.name, scalars);
        if (shared && usedBindings.contains(name))
            return invalidBinding();
        if (shared)
            usedBindings.insert(name);
        scalarNames.insert(measurement.name, name);
        if (!shared)
            draft.measurements.append({name, measurement.mm});
    }
    for (const auto& parameter : source.draft_.parameters)
        scalarNames.insert(parameter.name, reserveName(parameter.name, scalars));
    for (const auto& point : source.draft_.points)
        objectNames.insert(point.name, reserveName(point.name, objects));
    for (const auto& edge : source.draft_.edges)
        objectNames.insert(edge.name, reserveName(edge.name, objects));

    const auto rewriteFormula = [&](const QString& formula) {
        static const QRegularExpression tokens(QStringLiteral(
            R"regex((?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?|([A-Za-z_][A-Za-z_0-9]*)(\.[xy])?)regex"));
        QString result;
        qsizetype end = 0;
        auto matches = tokens.globalMatch(formula);
        while (matches.hasNext()) {
            const auto match = matches.next();
            result += formula.mid(end, match.capturedStart() - end);
            end = match.capturedEnd();
            const auto name = match.captured(1);
            const auto coordinate = match.captured(2);
            const bool function = coordinate.isEmpty() && (name == u"quadlen" || name == u"positive" || name == u"capcontrol")
                && QStringView(formula).sliced(end).trimmed().startsWith(u'(');
            if (name.isEmpty() || function)
                result += match.captured();
            else if (coordinate.isEmpty())
                result += scalarNames.value(name, name);
            else
                result += objectNames.value(name, name) + coordinate;
        }
        return result + formula.mid(end);
    };
    for (const auto& parameter : source.draft_.parameters)
        draft.parameters.append({scalarNames.value(parameter.name), rewriteFormula(parameter.formula)});
    for (const auto& point : source.draft_.points)
        draft.points.append({objectNames.value(point.name), rewriteFormula(point.x), rewriteFormula(point.y)});
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
        step.spreadFormula = rewriteFormula(step.spreadFormula);
        step.rotationPivot = mapObject(step.rotationPivot);
        step.secondPiece = mapPiece(step.secondPiece);
        candidate.history_.append(std::move(step));
    }
    for (auto seam : source.seamLinks_) {
        mapObjects(seam.first);
        mapObjects(seam.second);
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
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::setParametricDraft(ParametricDraft draft, QString* error) {
    QVector<Entity> generated;
    try {
        generated = DraftEvaluation(draft).geometry(draft);
    } catch (const EvaluationError& failure) {
        if (error)
            *error = failure.message;
        return false;
    }

    auto base = bindDraftGeometry(std::move(generated));
    if (!validateDraftSizes(draft, base, production_, error)
        || !replayModeling(base, production_, draft, error))
        return false;

    if (!seamLinks_.isEmpty()) {
        auto candidate = *this;
        candidate.entities_ = base;
        candidate.draft_ = draft;
        if (!candidate.validateSeamLinks(error))
            return false;
    }
    entities_ = std::move(base);
    draft_ = std::move(draft);
    if (error)
        error->clear();
    return true;
}
}
