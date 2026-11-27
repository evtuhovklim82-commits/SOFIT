#include "ContourModeling.h"
#include "Production.h"
#include "PieceMarkings.h"

#include <QHash>
#include <QLineF>
#include <QSet>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace opc {
namespace {
constexpr std::array operationKeys{
    std::pair{ContourOperation::Cut, "cut"},
    std::pair{ContourOperation::Spread, "spread"},
    std::pair{ContourOperation::TransferDartToOutlet, "transfer-dart"},
    std::pair{ContourOperation::MoveOpenDart, "move-open-dart"},
    std::pair{ContourOperation::SplitDart, "split-dart"},
    std::pair{ContourOperation::MergeDarts, "merge-darts"},
    std::pair{ContourOperation::Pleat, "pleat"},
    std::pair{ContourOperation::PleatWithTabs, "pleat-with-tabs"},
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
        if (entity.piece == piece && !isDrillPoint(entity)
            && !(entity.layer == Layer::Construction && entity.dxfLayer == pieceSymbolLayer))
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
            else if (interior) {
                if (edge.shape == Shape::Curve) {
                    if (const auto t = contourEdgeParameter(edge, point))
                        hits.append({*t * length, id});
                } else if (distance > tolerance && distance < length - tolerance
                           && QLineF(point, projection).length() <= tolerance) {
                    hits.append({distance, id});
                }
            }
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
                                    const QVector<qsizetype>& guides, double spreadMm, QString* error, const QString& spreadFormula,
                                    const std::optional<QVector<QString>>& followingGuides,
                                    const QVector<double>& outletWeights) {
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
    step.spreadFormula = spreadFormula.trimmed();
    step.followingGuides = followingGuides;
    step.outletWeights = outletWeights;
    if (!step.spreadFormula.isEmpty() && !usesSpreadWidth(operation))
        return fail(QStringLiteral("A width formula requires a spread, pleat or gather."));
    for (auto index : guides) {
        if (index < 0 || index >= entities_.size() || entities_[index].draftName.isEmpty())
            return fail(QStringLiteral("Choose named formula objects for the modeling guides."));
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
            && (step.objects.contains(entity.draftName) || step.guides.contains(entity.draftName))
            && (!followingGuides || !followingGuides->contains(entity.draftName)))
            candidate.hiddenSources_.append({entity.draftName, entity.piece, entity.layer});
    }
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::appendDrape(qsizetype piece, const QVector<QString>& guides,
                                  const QVector<double>& widths, ContourOperation finish, QString* error) {
    QVector<QString> formulas;
    for (const auto width : widths)
        formulas.append(QString::number(width, 'g', 17));
    return appendDrapeFormulas(piece, guides, formulas, finish, error);
}

bool PatternDocument::appendDrapeFormulas(qsizetype piece, const QVector<QString>& guides,
                                          const QVector<QString>& widths, ContourOperation finish, QString* error) {
    QVector<ContourOperation> operations(guides.size(), ContourOperation::Spread);
    if (!operations.isEmpty())
        operations.last() = finish;
    return appendDrapeFormulas(piece, guides, widths, operations, error);
}

bool PatternDocument::appendDrapeFormulas(qsizetype piece, const QVector<QString>& guides,
                                          const QVector<QString>& widths,
                                          const QVector<ContourOperation>& operations, QString* error) {
    if (guides.size() < 2 || guides.size() != widths.size() || guides.size() != operations.size()
        || !std::ranges::all_of(operations, usesSpreadWidth)) {
        if (error)
            *error = QStringLiteral("Choose at least two named slash guides and give each a width.");
        return false;
    }
    auto candidate = *this;
    for (qsizetype i = 0; i < guides.size(); ++i) {
        const auto& objects = candidate.entities();
        const auto guide = std::ranges::find(objects, guides[i], &Entity::draftName);
        if (guides[i].isEmpty() || guide == objects.end()) {
            if (error)
                *error = QStringLiteral("Slash %1: the guide is missing or already consumed.").arg(i + 1);
            return false;
        }
        const auto following = guides.sliced(i + 1);
        QString stepError;
        const auto width = widths[i].trimmed();
        if (width.isEmpty()) {
            if (error)
                *error = QStringLiteral("Slash %1: enter a width or formula.").arg(i + 1);
            return false;
        }
        bool constant;
        const auto number = width.toDouble(&constant);
        if (!candidate.appendContour(piece, operations[i], {guide - objects.begin()},
                                     constant ? number : 0, &stepError, constant ? QString{} : width, following)) {
            if (error)
                *error = QStringLiteral("Slash %1 (%2): %3").arg(i + 1).arg(guides[i], stepError);
            return false;
        }
    }
    if (!candidate.validateParametricSizes(candidate.production(), error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::appendRotation(const QVector<qsizetype>& selection, qsizetype pivot,
                                     double degrees, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!production_.placements.isEmpty())
        return fail(QStringLiteral("Clear the fabric layout before adding a rotation."));
    if (pivot < 0 || pivot >= entities_.size() || entities_[pivot].draftName.isEmpty())
        return fail(QStringLiteral("Choose a named formula point as the rotation pivot."));

    ModelingStep step;
    step.rotationPivot = entities_[pivot].draftName;
    step.rotationDegrees = degrees;
    for (const auto index : selection) {
        if (index < 0 || index >= entities_.size() || entities_[index].draftName.isEmpty())
            return fail(QStringLiteral("Select named formula construction objects to rotate."));
        step.objects.append(entities_[index].draftName);
    }
    std::ranges::sort(step.objects);
    auto candidate = *this;
    candidate.history_.append(std::move(step));
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::applyModelingSequence(const PatternDocument& source, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (history_.size() >= source.history_.size())
        return fail(QStringLiteral("В исходном проекте нет дополнительных операций."));
    if (!std::equal(history_.cbegin(), history_.cend(), source.history_.cbegin()))
        return fail(QStringLiteral("История текущего проекта должна совпадать с началом исходной истории."));
    if (!production_.placements.isEmpty())
        return fail(QStringLiteral("Перед применением операций очистите раскладку."));

    auto candidate = *this;
    const auto findObject = [&candidate](const QString& name) -> qsizetype {
        const auto found = std::ranges::find(std::as_const(candidate.entities_), name, &Entity::draftName);
        return found == candidate.entities_.cend() ? -1 : found - candidate.entities_.cbegin();
    };
    for (qsizetype i = history_.size(); i < source.history_.size(); ++i) {
        const auto& step = source.history_[i];
        QString detail;
        const auto stepError = [&](const QString& message) {
            return fail(QStringLiteral("Операция %1: %2").arg(i + 1).arg(message));
        };
        if (!step.rotationPivot.isEmpty()) {
            QVector<qsizetype> selection;
            for (const auto& name : step.objects)
                selection.append(findObject(name));
            if (!candidate.appendRotation(selection, findObject(step.rotationPivot), step.rotationDegrees, &detail))
                return stepError(detail);
            continue;
        }

        const auto anchor = step.objects.isEmpty() ? -1 : findObject(step.objects.front());
        if (anchor < 0)
            return stepError(QStringLiteral("Не найдена исходная геометрия."));
        const auto piece = candidate.entities_[anchor].piece;
        auto names = step.objects;
        std::ranges::sort(names);
        if (piece < 0 || names != pieceObjects(candidate.entities_, piece))
            return stepError(QStringLiteral("Состав именованных объектов детали изменился."));
        if (!step.contour) {
            if (!candidate.appendSilhouette(piece, step.axisFraction, step.topScale, step.bottomScale,
                                            &detail, step.lengthScale))
                return stepError(detail);
            continue;
        }

        QVector<qsizetype> guides;
        for (const auto& name : step.guides)
            guides.append(findObject(name));
        if (!candidate.appendContour(piece, *step.contour, guides, step.spreadMm, &detail, step.spreadFormula, step.followingGuides, step.outletWeights))
            return stepError(detail);
        if (candidate.history_.back().topology != step.topology)
            return stepError(QStringLiteral("Связи между контуром и направляющими изменились."));
    }
    *this = std::move(candidate);
    if (error)
        error->clear();
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
    if (!step.rotationPivot.isEmpty()) {
        step.rotationDegrees = settings.rotationDegrees;
    } else if (!step.contour) {
        step.axisFraction = settings.axisFraction;
        step.topScale = settings.topScale;
        step.bottomScale = settings.bottomScale;
        step.lengthScale = settings.lengthScale;
    } else if (usesSpreadWidth(*step.contour) || *step.contour == ContourOperation::SplitDart) {
        step.spreadMm = settings.spreadMm;
        step.outletWeights = settings.outletWeights;
        step.spreadFormula = settings.spreadFormula.trimmed();
        if (!step.spreadFormula.isEmpty() && !usesSpreadWidth(*step.contour))
            return fail(QStringLiteral("A width formula requires a spread, pleat or gather."));
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
    if (!removed.rotationPivot.isEmpty() && !production_.placements.isEmpty()) {
        if (error)
            *error = QStringLiteral("Clear the fabric layout before removing the rotation.");
        return false;
    }
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
                                    const ParametricDraft& draft, QString* error, ModelingStep* capture) const {
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
                    .arg(!step.rotationPivot.isEmpty() ? QStringLiteral("rotate-construction")
                         : step.contour ? contourOperationKey(*step.contour) : QStringLiteral("silhouette"), detail);
            return false;
        };
        if (step.objects.isEmpty())
            return fail(QStringLiteral("No named source objects."));
        QHash<QString, qsizetype> objects;
        for (qsizetype j = 0; j < entities.size(); ++j) {
            if (!entities[j].draftName.isEmpty())
                objects.insert(entities[j].draftName, j);
        }
        if (!step.rotationPivot.isEmpty()) {
            if (!std::isfinite(step.rotationDegrees) || std::abs(step.rotationDegrees) > 360)
                return fail(QStringLiteral("Enter an angle from −360 to 360 degrees."));
            const auto pivotIndex = objects.value(step.rotationPivot, -1);
            if (pivotIndex < 0 || entities[pivotIndex].shape != Shape::Point)
                return fail(QStringLiteral("Rotation pivot '%1' is missing or is no longer a point.")
                            .arg(step.rotationPivot));
            const auto pivot = entities[pivotIndex];
            QTransform rotation;
            rotation.rotate(step.rotationDegrees);
            const auto map = [&](QPointF point) { return pivot.a + rotation.map(point - pivot.a); };
            QSet<QString> rotated;
            for (const auto& name : step.objects) {
                const auto index = objects.value(name, -1);
                if (index < 0 || name == step.rotationPivot || rotated.contains(name))
                    return fail(QStringLiteral("Invalid rotation target '%1'.").arg(name));
                auto& entity = entities[index];
                if (entity.layer != Layer::Construction
                    || (entity.shape != Shape::Point && entity.shape != Shape::Line && entity.shape != Shape::Curve))
                    return fail(QStringLiteral("'%1' must be a construction point, line or curve.").arg(name));
                if (pivot.piece >= 0 && entity.piece != pivot.piece)
                    return fail(QStringLiteral("Choose an unassigned pivot or a pivot on the target's piece."));
                if (std::ranges::any_of(production.rules, [&](const auto& rule) {
                        return rule.piece == entity.piece || rule.piece == pivot.piece;
                    }))
                    return fail(QStringLiteral("Use measurement grading for the pivot and the rotated objects."));
                entity.a = map(entity.a);
                entity.b = entity.shape == Shape::Point ? entity.a : map(entity.b);
                if (entity.shape == Shape::Curve)
                    entity.control = map(entity.control);
                rotated.insert(name);
            }
            continue;
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
        QVector<qsizetype> toTransport;
        for (const auto& name : step.objects) {
            const auto index = objects.value(name);
            const auto& entity = entities[index];
            if (step.followingGuides && entity.layer == Layer::Construction
                && (step.followingGuides->contains(name) || name.startsWith(u'@'))
                && !step.guides.contains(name)) {
                toTransport.append(index);
                continue;
            }
            append(index);
        }
        if (step.followingGuides) {
            QSet<QString> seen;
            for (const auto& name : *step.followingGuides) {
                const auto index = objects.value(name, -1);
                if (index < 0 || seen.contains(name) || step.guides.contains(name)
                    || entities[index].layer != Layer::Construction
                    || (entities[index].piece != piece && entities[index].piece != -1))
                    return fail(QStringLiteral("Invalid or repeated following guide '%1'.").arg(name));
                seen.insert(name);
                if (entities[index].piece == -1)
                    toTransport.append(index);
            }
        }
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
        for (qsizetype j = 0; j < working.entities().size(); ++j) {
            const auto& entity = working.entities()[j];
            if (entity.shape != Shape::Line && !(supportsCurvedBoundary(*step.contour)
                && entity.shape == Shape::Curve))
                return fail(QStringLiteral("Contour history requires compatible source edges and guides."));
        }

        const auto target = step.secondPiece;
        const bool split = dividesPiece(*step.contour);
        if ((split && (target <= piece || target >= pieces_.size())) || (!split && target != -1))
            return fail(QStringLiteral("Invalid output piece."));
        if (split && (std::ranges::any_of(entities, [target](const auto& e) { return e.piece == target; })
                      || pieces_[target].allowanceMm < 0))
            return fail(QStringLiteral("The output piece must be reserved for this step."));

        working.pieces()[0].notches.clear();
        working.pieces()[0].allowanceMm = 0;
        auto width = step.spreadMm;
        if (!step.spreadFormula.isEmpty()) {
            QString detail;
            const auto evaluated = evaluateModelingWidth(draft, step.spreadFormula, &detail);
            if (!evaluated)
                return fail(detail);
            width = *evaluated;
        }
        const auto result = modelContour(working, 0, *step.contour, guideIndices, width, step.outletWeights);
        if (!result.edit)
            return fail(result.error);
        if (!toTransport.isEmpty()) {
            if (!result.edit->spreadMotion)
                return fail(QStringLiteral("Only a spread can transport guides."));
            for (const auto index : toTransport) {
                auto& entity = entities[index];
                const auto offset = result.edit->spreadMotion->offsetFor(entity);
                if (!offset)
                    return fail(QStringLiteral("Guide or fold mark '%1' touches or crosses this cut.").arg(entity.draftName));
                entity.a += *offset;
                entity.b += *offset;
                if (entity.shape == Shape::Curve)
                    entity.control += *offset;
            }
        }
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
                entity.seamSymbols = metadata->seamSymbols;
                entity.gradedMarks = metadata->gradedMarks;
                entity.bevelAtStart = metadata->bevelAtStart;
                entity.bevelAtEnd = metadata->bevelAtEnd;
                entity.dxfLayer = metadata->dxfLayer;
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
