#include "PatternDocument.h"
#include "PieceGeometry.h"

#include <array>

#include <QHash>
#include <QRegularExpression>
#include <QStringView>
#include <QSet>
#include <cmath>
#include <functional>
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
                                            QString* error) {
    auto draft = base;
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

bool PatternDocument::validateDraftSizes(const ParametricDraft& draft, const QVector<Entity>& base,
                                         const ProductionData& production, QString* error) const {
    if (production.useDraftMeasurements) {
        for (const auto& size : production.sizes) {
            auto generated = evaluateSize(draft, size, error);
            if (!generated)
                return false;
            if (history_.isEmpty() || generated->isEmpty())
                continue;
            auto candidate = *this;
            candidate.entities_ = base;
            auto sized = candidate.bindDraftGeometry(std::move(*generated));
            QString detail;
            if (!replayModeling(sized, production, &detail)) {
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
    if (history_.isEmpty())
        return validateDraftSizes(draft_, entities_, production, error);
    auto candidate = *this;
    candidate.production_ = production;
    return candidate.setParametricDraft(draft_, error);
}

std::optional<QVector<Entity>> PatternDocument::evaluateSizeDraft(const PatternSize& size) const {
    if (!production_.useDraftMeasurements)
        return QVector<Entity>{};
    auto generated = evaluateSize(draft_, size, nullptr);
    if (history_.isEmpty() || !generated || generated->isEmpty())
        return generated;
    auto sized = bindDraftGeometry(std::move(*generated));
    if (!replayModeling(sized, production_, nullptr))
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
        }
        entities.append(replacement);
        indices.remove(original.draftName);
    }
    for (const auto& entity : std::as_const(generated)) {
        if (indices.contains(entity.draftName))
            entities.append(entity);
    }
    return entities;
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
        || !replayModeling(base, production_, error))
        return false;

    entities_ = std::move(base);
    draft_ = std::move(draft);
    if (error)
        error->clear();
    return true;
}
}
