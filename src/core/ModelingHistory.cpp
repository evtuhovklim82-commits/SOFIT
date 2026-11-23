#include "ContourModeling.h"

#include <QHash>
#include <QLineF>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>

namespace opc {
namespace {
constexpr std::array operationKeys{
    std::pair{ContourOperation::Cut, "cut"},
    std::pair{ContourOperation::Spread, "spread"},
    std::pair{ContourOperation::TransferDartToOutlet, "transfer-dart"},
    std::pair{ContourOperation::MoveOpenDart, "move-open-dart"},
    std::pair{ContourOperation::Pleat, "pleat"},
    std::pair{ContourOperation::Gather, "gather"},
    std::pair{ContourOperation::Relief, "relief"},
    std::pair{ContourOperation::Yoke, "yoke"},
};

bool placed(const ProductionData& production, qsizetype piece) {
    return std::ranges::any_of(production.placements, [piece](const auto& p) { return p.piece == piece; });
}

QVector<QString> pieceObjects(const QVector<Entity>& entities, qsizetype piece) {
    QVector<QString> names;
    for (const auto& entity : entities) {
        if (entity.piece == piece)
            names.append(entity.draftName);
    }
    std::ranges::sort(names);
    return names;
}

// Record which named endpoints meet, and the order in which each oriented edge
// meets the guide ends. This guards the identity of output edges as sizes change.
QVector<QString> boundaryTopology(const PatternDocument& source, const QVector<qsizetype>& guides) {
    constexpr double tolerance = 1e-6;
    const auto& entities = source.entities();
    QVector<QString> topology;
    for (const auto& edge : entities) {
        if (edge.layer != Layer::Main)
            continue;
        const auto direction = edge.b - edge.a;
        const double length = std::hypot(direction.x(), direction.y());
        if (length <= tolerance)
            return {};
        const auto unit = direction / length;
        QVector<std::pair<double, QString>> hits;
        const auto endpoint = [&](QPointF point, const QString& id, bool interior) {
            const auto offset = point - edge.a;
            const auto distance = QPointF::dotProduct(offset, unit);
            const QPointF projection = edge.a + distance * unit;
            if (QLineF(point, edge.a).length() <= tolerance)
                hits.append({0, id});
            else if (QLineF(point, edge.b).length() <= tolerance)
                hits.append({length, id});
            else if (interior && distance > tolerance && distance < length - tolerance
                     && QLineF(point, projection).length() <= tolerance)
                hits.append({distance, id});
        };
        for (qsizetype i = 0; i < entities.size(); ++i) {
            const auto& other = entities[i];
            if (other.layer == Layer::Main) {
                endpoint(other.a, QStringLiteral("e%1a").arg(i), false);
                endpoint(other.b, QStringLiteral("e%1b").arg(i), false);
            }
        }
        for (qsizetype i = 0; i < guides.size(); ++i) {
            const auto& guide = entities[guides[i]];
            endpoint(guide.a, QStringLiteral("g%1a").arg(i), true);
            endpoint(guide.b, QStringLiteral("g%1b").arg(i), true);
        }
        std::ranges::sort(hits);
        QStringList order;
        for (const auto& hit : hits)
            order.append(hit.second);
        topology.append(order.join(u','));
    }
    return topology;
}
}

QString contourOperationKey(ContourOperation operation) {
    for (const auto& [value, key] : operationKeys) {
        if (value == operation)
            return QString::fromLatin1(key);
    }
    return {};
}

std::optional<ContourOperation> contourOperationFromKey(const QString& key) {
    for (const auto& [value, name] : operationKeys) {
        if (key == QLatin1String(name))
            return value;
    }
    return {};
}

bool PatternDocument::appendSilhouette(qsizetype piece, double axisFraction, double topScale,
                                       double bottomScale, QString* error, double lengthScale) {
    if (piece < 0 || piece >= pieces_.size() || placed(production_, piece)) {
        if (error)
            *error = QStringLiteral("Choose a formula piece without fabric placements.");
        return false;
    }

    ModelingStep step{pieceObjects(entities_, piece), axisFraction, topScale, bottomScale};
    auto candidate = *this;
    step.lengthScale = lengthScale;
    candidate.history_.append(std::move(step));
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::appendContour(qsizetype piece, ContourOperation operation,
                                    const QVector<qsizetype>& guides, double spreadMm, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (piece < 0 || piece >= pieces_.size() || placed(production_, piece))
        return fail(QStringLiteral("Choose a formula piece without fabric placements."));
    if (!pieces_[piece].notches.isEmpty())
        return fail(QStringLiteral("Clear perimeter notches before changing the contour."));
    if (!std::isfinite(spreadMm) || spreadMm < 0)
        return fail(QStringLiteral("Invalid modeling width."));
    if (contourOperationKey(operation).isEmpty())
        return fail(QStringLiteral("Unknown contour operation."));

    ModelingStep step;
    step.objects = pieceObjects(entities_, piece);
    step.contour = operation;
    step.spreadMm = spreadMm;
    for (auto index : guides) {
        if (index < 0 || index >= entities_.size() || entities_[index].draftName.isEmpty())
            return fail(QStringLiteral("Choose named formula lines for the modeling guides."));
        step.guides.append(entities_[index].draftName);
    }

    auto candidate = *this;
    if (dividesPiece(operation)) {
        auto panel = pieces_[piece];
        panel.name += operation == ContourOperation::Yoke ? QStringLiteral(" · Yoke")
            : operation == ContourOperation::Relief ? QStringLiteral(" · Relief panel") : QStringLiteral(" · B");
        step.secondPiece = candidate.pieces_.size();
        candidate.pieces_.append(panel);
    }
    candidate.history_.append(step);
    // Capture connectivity from a replay of the formulas, including prior steps.
    if (!candidate.captureContourTopology(step, error))
        return false;
    candidate.history_.last().topology = step.topology;
    for (const auto& entity : entities_) {
        if (!entity.draftName.isEmpty() && !entity.draftName.startsWith(u'@')
            && (entity.piece == piece || step.guides.contains(entity.draftName)))
            candidate.hiddenSources_.append({entity.draftName, entity.piece, entity.layer});
    }
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::editModelingStep(qsizetype index, const ModelingStep& settings, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (index < 0 || index >= history_.size())
        return fail(QStringLiteral("Choose a modeling step."));
    auto candidate = *this;
    auto& step = candidate.history_[index];
    if (!step.contour) {
        step.axisFraction = settings.axisFraction;
        step.topScale = settings.topScale;
        step.bottomScale = settings.bottomScale;
        step.lengthScale = settings.lengthScale;
    } else if (usesSpreadWidth(*step.contour)) {
        step.spreadMm = settings.spreadMm;
    } else {
        return fail(QStringLiteral("This step has no numeric settings."));
    }
    if (step == history_[index]) {
        if (error)
            error->clear();
        return true;
    }
    if (!production_.placements.isEmpty())
        return fail(QStringLiteral("Clear the fabric layout before editing the construction history."));
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::removeLastModelingStep(QString* error) {
    if (history_.isEmpty()) {
        if (error)
            *error = QStringLiteral("There is no modeling step to remove.");
        return false;
    }
    auto candidate = *this;
    const auto removed = candidate.history_.takeLast();
    if (removed.secondPiece >= 0) {
        const auto piece = removed.secondPiece;
        const bool occupied = placed(production_, piece)
            || std::ranges::any_of(production_.rules, [piece](const auto& rule) { return rule.piece == piece; })
            || std::ranges::any_of(entities_, [piece](const auto& entity) {
                return entity.piece == piece && !entity.draftName.startsWith(u'@');
            });
        if (occupied || piece >= pieces_.size()) {
            if (error)
                *error = QStringLiteral("Remove the extra panel's manual objects, grade rules and placements first.");
            return false;
        }
        candidate.pieces_.removeAt(piece);
        candidate.production_.removePiece(piece);
        for (auto& entity : candidate.entities_) {
            if (entity.piece > piece)
                --entity.piece;
        }
        for (auto& binding : candidate.hiddenSources_) {
            if (binding.piece > piece)
                --binding.piece;
        }
    }
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    candidate.hiddenSources_.removeIf([&](const SourceBinding& source) {
        return std::ranges::find(candidate.entities_, source.name, &Entity::draftName) != candidate.entities_.end();
    });
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::replayModeling(QVector<Entity>& entities, const ProductionData& production,
                                    QString* error, ModelingStep* capture) const {
    QHash<QString, Entity> outputMetadata;
    if (hasContourHistory()) {
        for (const auto& entity : entities_) {
            if (entity.draftName.startsWith(u'@'))
                outputMetadata.insert(entity.draftName, entity);
        }
    }
    for (qsizetype i = 0; i < history_.size(); ++i) {
        const auto& step = history_[i];
        const auto fail = [&](const QString& detail) {
            if (error)
                *error = QStringLiteral("Modeling step %1 (%2): %3").arg(i + 1)
                    .arg(step.contour ? contourOperationKey(*step.contour) : QStringLiteral("silhouette"), detail);
            return false;
        };
        if (step.objects.isEmpty())
            return fail(QStringLiteral("No named source objects."));
        QHash<QString, qsizetype> objects;
        for (qsizetype j = 0; j < entities.size(); ++j) {
            if (!entities[j].draftName.isEmpty())
                objects.insert(entities[j].draftName, j);
        }
        const auto anchor = objects.value(step.objects.first(), -1);
        if (anchor < 0)
            return fail(QStringLiteral("Source object '%1' is missing.").arg(step.objects.first()));
        const auto piece = entities[anchor].piece;
        auto names = step.objects;
        std::ranges::sort(names);
        if (piece < 0 || piece >= pieces_.size() || names != pieceObjects(entities, piece)
            || names.first().isEmpty() || std::adjacent_find(names.begin(), names.end()) != names.end())
            return fail(QStringLiteral("The piece's named object membership changed."));

        const auto hasRules = [&](qsizetype p) {
            return std::ranges::any_of(production.rules, [p](const auto& rule) { return rule.piece == p; });
        };
        if (hasRules(piece) || (step.secondPiece >= 0 && hasRules(step.secondPiece)))
            return fail(QStringLiteral("Use size measurements for this piece. Remove its manual grade rules."));

        PatternDocument working;
        working.pieces().append(pieces_[piece]);
        QVector<qsizetype> indices;
        QVector<qsizetype> guideIndices;
        QHash<QString, qsizetype> local;
        const auto append = [&](qsizetype index) {
            auto entity = entities[index];
            local.insert(entity.draftName, working.entities().size());
            indices.append(index);
            entity.piece = entity.piece == piece ? 0 : -1;
            entity.draftName.clear();
            working.insert(working.entities().size(), entity);
        };
        for (const auto& name : step.objects)
            append(objects.value(name));
        if (!step.contour) {
            if (!(step.axisFraction >= 0 && step.axisFraction <= 1))
                return fail(QStringLiteral("Invalid axis."));
            for (const auto& entity : working.entities()) {
                if (entity.shape != Shape::Line && entity.shape != Shape::Curve && entity.shape != Shape::Point)
                    return fail(QStringLiteral("The piece must contain formula lines, curves or points."));
            }
            const auto bounds = analyzePiece(working, 0).bounds;
            const auto result = shapeSilhouette(working, 0, std::lerp(bounds.left(), bounds.right(), step.axisFraction),
                                                step.topScale, step.bottomScale, step.lengthScale);
            if (!result.edit)
                return fail(result.error);
            for (qsizetype j = 0; j < indices.size(); ++j) {
                const auto& shaped = result.edit->document.entities()[j];
                auto& entity = entities[indices[j]];
                entity.a = shaped.a;
                entity.b = shaped.b;
                entity.control = shaped.control;
            }
            continue;
        }

        for (const auto& name : step.guides) {
            const auto index = objects.value(name, -1);
            if (index < 0 || (entities[index].piece != -1 && entities[index].piece != piece))
                return fail(QStringLiteral("Guide '%1' is missing or belongs to a different piece.").arg(name));
            if (!local.contains(name))
                append(index);
            guideIndices.append(local.value(name));
        }
        if (std::ranges::any_of(working.entities(), [](const auto& entity) { return entity.shape != Shape::Line; }))
            return fail(QStringLiteral("Contour history requires straight contour edges and straight guides."));

        const auto target = step.secondPiece;
        const bool split = dividesPiece(*step.contour);
        if ((split && (target <= piece || target >= pieces_.size())) || (!split && target != -1))
            return fail(QStringLiteral("Invalid output piece."));
        if (split && (std::ranges::any_of(entities, [target](const auto& e) { return e.piece == target; })
                      || pieces_[target].allowanceMm < 0))
            return fail(QStringLiteral("The output piece must be reserved for this step."));

        working.pieces()[0].notches.clear();
        working.pieces()[0].allowanceMm = 0;
        const auto result = modelContour(working, 0, *step.contour, guideIndices, step.spreadMm);
        if (!result.edit)
            return fail(result.error);
        auto topology = boundaryTopology(working, guideIndices);
        if (capture && i == history_.size() - 1) {
            capture->topology = topology;
        } else if (topology.isEmpty() || topology != step.topology) {
            return fail(QStringLiteral("Contour or guide connectivity changed."));
        }

        const auto& modeled = result.edit->document.entities();
        auto validated = result.edit->document;
        validated.pieces()[0] = pieces_[piece];
        if (split)
            validated.pieces()[1] = pieces_[target];
        QVector<Entity> outputs;
        for (qsizetype j = 0; j < modeled.size(); ++j) {
            auto entity = modeled[j];
            const auto localPiece = entity.piece;
            entity.piece = localPiece == 0 ? piece : target;
            entity.draftName = QStringLiteral("@%1:%2").arg(i + 1).arg(j + 1);
            const auto lastInPanel = j + 1 == modeled.size() || modeled[j + 1].piece != localPiece;
            if (split && lastInPanel)
                entity.draftName = QStringLiteral("@%1:seam-%2").arg(i + 1).arg(QChar(localPiece == 0 ? u'A' : u'B'));
            const auto metadata = outputMetadata.constFind(entity.draftName);
            if (metadata != outputMetadata.cend()) {
                if (metadata->piece != entity.piece || metadata->layer != entity.layer)
                    return fail(QStringLiteral("A generated edge was reassigned or moved to another layer."));
                entity.seamAllowanceMm = metadata->seamAllowanceMm;
                entity.seamNotches = metadata->seamNotches;
            }
            outputs.append(entity);
            entity.piece = localPiece;
            validated.replace(j, entity);
        }
        for (qsizetype j = 0; j < validated.pieces().size(); ++j) {
            const auto geometry = analyzePiece(validated, j);
            if (geometry.error != ContourError::None || geometry.allowanceFailed)
                return fail(QStringLiteral("The resulting contour or seam allowance is invalid."));
        }
        std::ranges::sort(indices, std::greater<>());
        for (auto index : indices)
            entities.removeAt(index);
        entities += outputs;
    }
    return true;
}
}
