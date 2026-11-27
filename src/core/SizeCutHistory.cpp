#include "ContourModeling.h"
#include "Production.h"

#include <algorithm>
#include <cmath>

namespace opc {
namespace {
bool applyPanelSettings(ContourEdit& output, const PatternDocument& source, const SizeCutStep& step, QString* error) {
    for (qsizetype i = 0; i < output.panelKeys.size(); ++i) {
        const auto panel = step.panels.value(output.panelKeys[i], -1);
        if (panel < 0 || panel >= source.pieces().size())
            return false;
        output.document.pieces()[i] = source.pieces()[panel];
        const auto sequence = step.silhouettes.constFind(panel);
        if (sequence != step.silhouettes.cend()) {
            for (const auto& shape : sequence.value()) {
                const auto bounds = analyzePiece(output.document, i).bounds;
                auto changed = shapeSilhouette(output.document, i,
                    std::lerp(bounds.left(), bounds.right(), shape.axisFraction),
                    shape.topScale, shape.bottomScale, shape.lengthScale);
                if (!changed.edit) {
                    if (error)
                        *error = changed.error;
                    return false;
                }
                output.document = std::move(changed.edit->document);
            }
        }
        const auto geometry = analyzePiece(output.document, i);
        if (geometry.error != ContourError::None || geometry.allowanceFailed) {
            if (error)
                *error = QStringLiteral("The generated panel '%1' has an invalid contour or allowance.")
                    .arg(source.pieces()[panel].name);
            return false;
        }
    }
    return true;
}
}

bool PatternDocument::isSizeCutPiece(qsizetype piece) const {
    return std::ranges::any_of(sizeCuts_, [piece](const auto& step) {
        return std::ranges::find(step.panels, piece) != step.panels.end();
    });
}

bool PatternDocument::appendSizeCut(qsizetype piece, QVector<QString> guides, QString formula, QString* error) {
    if (piece < 0 || piece >= pieces_.size() || isSizeCutPiece(piece)) {
        if (error)
            *error = QStringLiteral("Choose a source piece without a variable cut.");
        return false;
    }
    if (formula.size() > 4096 || std::ranges::any_of(production_.placements, [piece](const auto& copy) { return copy.piece == piece; })) {
        if (error)
            *error = formula.size() > 4096 ? QStringLiteral("The cut formula is too long.")
                : QStringLiteral("Remove existing placements of the source piece before dividing it.");
        return false;
    }
    auto candidate = *this;
    candidate.sizeCuts_.append(SizeCutStep{piece, std::move(guides), std::move(formula),
                                          {{QStringLiteral("root"), piece}}});
    if (!candidate.refreshSizeCutPanels(error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::appendPanelSilhouette(qsizetype piece, PanelSilhouette silhouette, QString* error) {
    auto candidate = *this;
    for (auto& step : candidate.sizeCuts_) {
        if (std::ranges::find(step.panels, piece) == step.panels.end())
            continue;
        const auto scale = [](double ratio) { return ratio >= 0.2 && ratio <= 5; };
        if (!(silhouette.axisFraction >= 0 && silhouette.axisFraction <= 1) || step.silhouettes.value(piece).size() >= 64
            || !scale(silhouette.topScale) || !scale(silhouette.bottomScale) || !scale(silhouette.lengthScale))
            break;
        step.silhouettes[piece].append(silhouette);
        if (!candidate.refreshSizeCutPanels(error))
            return false;
        *this = std::move(candidate);
        return true;
    }
    if (error)
        *error = QStringLiteral("Invalid cut panel, axis or number of silhouette steps.");
    return false;
}

bool PatternDocument::editSizeCut(qsizetype step, QString formula, QString* error) {
    if (step < 0 || step >= sizeCuts_.size() || formula.trimmed().isEmpty() || formula.size() > 4096) {
        if (error)
            *error = QStringLiteral("Enter a valid cut formula.");
        return false;
    }
    auto candidate = *this;
    candidate.sizeCuts_[step].countFormula = std::move(formula);
    if (!candidate.refreshSizeCutPanels(error))
        return false;
    *this = std::move(candidate);
    return true;
}

bool PatternDocument::refreshSizeCutPanels(QString* error) {
    if (sizeCuts_.isEmpty())
        return true;
    auto candidate = *this;
    for (auto& step : candidate.sizeCuts_) {
        if (std::ranges::any_of(candidate.entities_, [&](const auto& entity) {
            return entity.piece != step.piece && std::ranges::find(step.panels, entity.piece) != step.panels.end();
        })) {
            if (error)
                *error = QStringLiteral("Generated panel identifiers cannot own source objects.");
            return false;
        }
        QStringList sizes{QString{}};
        for (const auto& size : production_.sizes)
            sizes.append(size.name);
        for (const auto& size : sizes) {
            auto result = evaluateSizeCuts(candidate, step.piece, step.guides, step.countFormula, size);
            if (!result.edit) {
                if (error)
                    *error = QStringLiteral("%1: %2").arg(size.isEmpty() ? QStringLiteral("Base pattern") : size, result.error);
                return false;
            }
            auto& output = *result.edit;
            QSet<qsizetype> active;
            for (qsizetype i = 0; i < output.panelKeys.size(); ++i) {
                const auto& key = output.panelKeys[i];
                if (!step.panels.contains(key)) {
                    auto panel = candidate.pieces_[step.piece];
                    panel.name += QStringLiteral(" · %1").arg(key.sliced(5));
                    step.panels.insert(key, candidate.pieces_.size());
                    candidate.pieces_.append(std::move(panel));
                }
                active.insert(step.panels.value(key));
            }
            if (!applyPanelSettings(output, candidate, step, error))
                return false;
            for (const auto& copy : production_.placements) {
                if (copy.size == size && std::ranges::find(step.panels, copy.piece) != step.panels.end()
                    && !active.contains(copy.piece)) {
                    if (error)
                        *error = QStringLiteral("A saved placement refers to a panel absent in size '%1'.").arg(size);
                    return false;
                }
            }
        }
    }
    *this = std::move(candidate);
    return true;
}

PatternDocument gradedPattern(const PatternDocument& document, const QString& size,
                              QVector<qsizetype>* sourceIndices, GradeEdgeNames edgeNames) {
    if (document.sizeCuts_.isEmpty())
        return uncutGradedPattern(document, size, sourceIndices, edgeNames);

    auto projected = uncutGradedPattern(document, size, sourceIndices, GradeEdgeNames::Preserve);
    projected.sizeCuts_.clear();
    for (const auto& step : document.sizeCuts_) {
        auto result = evaluateSizeCuts(document, step.piece, step.guides, step.countFormula, size);
        if (!result.edit || !applyPanelSettings(*result.edit, document, step, nullptr)) {
            if (sourceIndices)
                sourceIndices->clear();
            return {};
        }
        const auto& output = *result.edit;
        for (qsizetype i = projected.entities_.size(); i > 0; --i) {
            const auto& entity = projected.entities_[i - 1];
            if (entity.piece != step.piece && !step.guides.contains(entity.draftName))
                continue;
            if (sourceIndices)
                sourceIndices->removeAt(i - 1);
            projected.entities_.removeAt(i - 1);
        }
        for (auto entity : output.document.entities()) {
            const auto index = step.panels.value(output.panelKeys.at(entity.piece), -1);
            if (index < 0 || index >= projected.pieces_.size()) {
                if (sourceIndices)
                    sourceIndices->clear();
                return {};
            }
            entity.piece = index;
            projected.entities_.append(std::move(entity));
            if (sourceIndices)
                sourceIndices->append(-1);
        }
    }
    if (!size.isEmpty() && edgeNames == GradeEdgeNames::ForSeamLinks && projected.seamLinks_.isEmpty()) {
        for (auto& entity : projected.entities_)
            entity.draftName.clear();
    }
    return projected;
}

}
