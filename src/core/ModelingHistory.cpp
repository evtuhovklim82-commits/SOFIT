#include "ContourModeling.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace opc {

bool PatternDocument::appendSilhouette(qsizetype piece, double axisFraction, double topScale,
                                       double bottomScale, QString* error) {
    if (piece < 0 || piece >= pieces_.size()
        || std::ranges::any_of(production_.placements, [piece](const auto& placement) {
            return placement.piece == piece;
        })) {
        if (error)
            *error = QStringLiteral("Choose a formula piece without fabric placements.");
        return false;
    }

    SilhouetteStep step{{}, axisFraction, topScale, bottomScale};
    for (const auto& entity : entities_) {
        if (entity.piece == piece)
            step.objects.append(entity.draftName);
    }
    std::ranges::sort(step.objects);
    auto candidate = *this;
    candidate.history_.append(std::move(step));
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
    candidate.history_.removeLast();
    if (!candidate.setParametricDraft(draft_, error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::replayModeling(QVector<Entity>& entities, const ProductionData& production,
                                    QString* error) const {
    if (history_.isEmpty())
        return true;

    QHash<QString, qsizetype> objects;
    QVector<qsizetype> pieceCounts(pieces_.size());
    for (qsizetype i = 0; i < entities.size(); ++i) {
        const auto& entity = entities[i];
        if (!entity.draftName.isEmpty())
            objects.insert(entity.draftName, i);
        if (entity.piece >= 0 && entity.piece < pieceCounts.size())
            ++pieceCounts[entity.piece];
    }

    for (qsizetype i = 0; i < history_.size(); ++i) {
        const auto fail = [&](const QString& detail) {
            if (error)
                *error = QStringLiteral("Modeling step %1 (silhouette): %2").arg(i + 1).arg(detail);
            return false;
        };
        const auto& step = history_[i];
        if (!(step.axisFraction >= 0 && step.axisFraction <= 1) || step.objects.isEmpty())
            return fail(QStringLiteral("Invalid axis or missing formula objects."));

        QVector<qsizetype> indices;
        QSet<QString> seen;
        qsizetype piece = -1;
        PatternDocument working;
        for (const auto& name : step.objects) {
            const auto index = objects.value(name, -1);
            if (index < 0 || seen.contains(name))
                return fail(QStringLiteral("Formula object '%1' is missing or duplicated.").arg(name));
            seen.insert(name);
            auto entity = entities[index];
            if (indices.isEmpty())
                piece = entity.piece;
            if (piece < 0 || piece >= pieces_.size() || entity.piece != piece)
                return fail(QStringLiteral("The formula objects must stay on one piece."));
            if (entity.shape != Shape::Line && entity.shape != Shape::Curve && entity.shape != Shape::Point)
                return fail(QStringLiteral("The piece must contain formula lines, curves or points."));
            entity.piece = 0;
            entity.draftName.clear();
            working.insert(working.entities().size(), entity);
            indices.append(index);
        }
        if (pieceCounts[piece] != indices.size())
            return fail(QStringLiteral("The piece's object membership changed."));
        if (std::ranges::any_of(production.rules, [piece](const auto& rule) { return rule.piece == piece; }))
            return fail(QStringLiteral("Use size measurements for this piece. Remove its manual grade rules."));

        working.pieces().append(pieces_[piece]);
        const auto bounds = analyzePiece(working, 0).bounds;
        const auto result = shapeSilhouette(working, 0, std::lerp(bounds.left(), bounds.right(), step.axisFraction),
                                            step.topScale, step.bottomScale);
        if (!result.edit)
            return fail(result.error);
        for (qsizetype j = 0; j < indices.size(); ++j) {
            const auto& shaped = result.edit->document.entities()[j];
            auto& entity = entities[indices[j]];
            entity.a = shaped.a;
            entity.b = shaped.b;
            entity.control = shaped.control;
        }
    }
    return true;
}

}
