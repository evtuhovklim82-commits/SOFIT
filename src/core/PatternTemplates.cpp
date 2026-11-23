#include "PatternTemplates.h"
#include "PieceGeometry.h"

#include <cmath>
#include <utility>

namespace opc {
std::optional<PatternDocument> draftPullOnSkirt(SkirtStyle style, const SkirtMeasurements& input,
                                              QString* error) {
    const auto fail = [error](const QString& message) -> std::optional<PatternDocument> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const auto positive = [](double value) { return std::isfinite(value) && value > 0; };
    if (!positive(input.hipMm) || !positive(input.lengthMm) || !positive(input.hipEaseMm)
        || !positive(input.casingMm) || input.casingMm >= input.lengthMm
        || (style == SkirtStyle::ALine && !positive(input.hemFlareMm))) {
        return fail(QStringLiteral("Enter positive measurements and make the casing shallower than the finished skirt length."));
    }

    // One full-width panel. Cut two identical panels and join at the side seams.
    ParametricDraft draft{
        {{"hip", input.hipMm}, {"length", input.lengthMm}},
        {{"hip_ease", QString::number(input.hipEaseMm, 'g', 17)},
         {"hem_flare", QString::number(style == SkirtStyle::ALine ? input.hemFlareMm : 0, 'g', 17)},
         {"casing", QString::number(input.casingMm, 'g', 17)},
         {"half_panel", "(hip + hip_ease) / 4"}},
        {{"TopL", "-half_panel", "0"}, {"TopR", "half_panel", "0"},
         {"FoldL", "TopL.x", "casing"}, {"FoldR", "TopR.x", "casing"},
         {"HemL", "FoldL.x - hem_flare / 4", "casing + length"},
         {"HemR", "-HemL.x", "HemL.y"}},
        {{"opening", "TopL", "TopR", {}}, {"casing_right", "TopR", "FoldR", {}},
         {"side_right", "FoldR", "HemR", {}}, {"hem", "HemR", "HemL", {}},
         {"side_left", "HemL", "FoldL", {}}, {"casing_left", "FoldL", "TopL", {}},
         {"casing_fold", "FoldL", "FoldR", {}}}
    };
    PatternDocument document;
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;

    document.pieces().append({style == SkirtStyle::ALine ? "A-line skirt panel" : "Gathered skirt panel",
                              10, 2, {}, true, 90});
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        entity.piece = 0;
        if (entity.draftName == QStringLiteral("casing_fold"))
            entity.layer = Layer::Construction;
        document.replace(i, entity);
    }
    const auto geometry = analyzePiece(document, 0);
    if (geometry.error != ContourError::None || geometry.allowance.isEmpty())
        return fail(QStringLiteral("These measurements do not produce a usable cutting contour."));
    return document;
}
}
