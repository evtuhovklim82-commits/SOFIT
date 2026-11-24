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

std::optional<PatternDocument> draftBibApron(const ApronMeasurements& input, QString* error) {
    const auto fail = [error](const QString& message) -> std::optional<PatternDocument> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    for (double value : {input.widthMm, input.lengthMm, input.bibWidthMm, input.bibHeightMm,
                         input.waistTieLengthMm, input.neckTieLengthMm, input.tieWidthMm}) {
        if (!std::isfinite(value) || value <= 0 || value > 5000)
            return fail(QStringLiteral("Apron dimensions must be positive and at most 5000 mm."));
    }
    if (input.widthMm <= input.bibWidthMm || input.lengthMm <= input.bibHeightMm
        || input.bibWidthMm <= 2 * input.tieWidthMm
        || input.lengthMm - input.bibHeightMm <= input.tieWidthMm
        || input.waistTieLengthMm <= input.tieWidthMm || input.neckTieLengthMm <= input.tieWidthMm)
        return fail(QStringLiteral("Use a narrower bib, leave room below the bib, and make the ties longer than their width."));

    ParametricDraft draft{
        {{"width", input.widthMm}, {"length", input.lengthMm}, {"bib_width", input.bibWidthMm},
         {"bib_height", input.bibHeightMm}, {"waist_tie_length", input.waistTieLengthMm},
         {"neck_tie_length", input.neckTieLengthMm}, {"tie_width", input.tieWidthMm}},
        {{"half_width", "width / 2"}, {"half_bib", "bib_width / 2"},
         {"flat_tie", "2 * positive(tie_width)"}, {"waist_x", "half_width + 60"},
         {"neck_x", "waist_x + flat_tie + 60"},
         {"bib_clearance", "positive(bib_width - flat_tie)"},
         {"arm_slope_width", "positive(width - bib_width)"},
         {"side_clearance", "positive(length - bib_height - tie_width)"},
         {"waist_tie_clearance", "positive(waist_tie_length - tie_width)"},
         {"neck_tie_clearance", "positive(neck_tie_length - tie_width)"}},
        {{"BibL", "-half_bib", "0"}, {"BibR", "half_bib", "0"},
         {"WaistR", "half_width", "positive(bib_height)"}, {"HemR", "half_width", "length"},
         {"HemL", "-half_width", "length"}, {"WaistL", "-half_width", "WaistR.y"}},
        {{"apron_top", "BibL", "BibR"}, {"apron_arm_r", "BibR", "WaistR"},
         {"apron_side_r", "WaistR", "HemR"}, {"apron_hem", "HemR", "HemL"},
         {"apron_side_l", "HemL", "WaistL"}, {"apron_arm_l", "WaistL", "BibL"}}
    };
    const auto addTie = [&](const QString& prefix) {
        const auto x = prefix + "_x";
        const auto length = prefix + "_tie_length";
        const auto a = prefix + "_a";
        const auto b = prefix + "_b";
        const auto c = prefix + "_c";
        const auto d = prefix + "_d";
        const auto e = prefix + "_e";
        const auto f = prefix + "_f";
        draft.points += {{a, x, "0"}, {b, x + " + flat_tie", "0"},
                         {c, b + ".x", length}, {d, x, length},
                         {e, x + " + tie_width", "0"}, {f, e + ".x", length}};
        draft.edges += {{prefix + "_top", a, b}, {prefix + "_right", b, c},
                        {prefix + "_end", c, d}, {prefix + "_left", d, a},
                        {prefix + "_fold", e, f}};
    };
    addTie("waist");
    addTie("neck");

    PatternDocument document;
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    document.pieces() = {{"Apron body", 10, 1, {}, true, 90},
                         {"Waist tie", 10, 2, {}, true, 90},
                         {"Neck tie", 10, 2, {}, true, 90}};
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto edge = document.entities()[i];
        if (edge.shape == Shape::Point)
            continue;
        edge.piece = edge.draftName.startsWith("waist_") ? 1 : edge.draftName.startsWith("neck_") ? 2 : 0;
        if (edge.draftName.endsWith("_fold"))
            edge.layer = Layer::Construction;
        document.replace(i, edge);
    }
    for (qsizetype i = 0; i < document.pieces().size(); ++i) {
        const auto geometry = analyzePiece(document, i);
        if (geometry.error != ContourError::None || geometry.allowanceFailed || geometry.allowance.isEmpty())
            return fail(QStringLiteral("These measurements do not produce usable apron cutting contours."));
    }
    return document;
}

std::optional<PatternDocument> draftPatchPocket(const PatchPocketMeasurements& input, QString* error) {
    ParametricDraft draft{
        {{"pocket_width", input.widthMm}, {"pocket_height", input.heightMm},
         {"pocket_radius", input.cornerRadiusMm}, {"pocket_turnback", input.turnbackMm}},
        {{"half_width", "positive(pocket_width / 2)"},
         {"radius", "positive(pocket_radius)"},
         {"straight_bottom_half", "positive(half_width - radius)"},
         {"side_height", "positive(pocket_height - radius)"}},
        {{"TopL", "-half_width", "-positive(pocket_turnback)"},
         {"TopR", "half_width", "TopL.y"},
         {"FoldR", "half_width", "0"},
         {"FoldL", "-half_width", "0"},
         {"SideR", "half_width", "side_height"},
         {"CornerR", "half_width", "pocket_height"},
         {"BottomR", "straight_bottom_half", "pocket_height"},
         {"BottomL", "-straight_bottom_half", "pocket_height"},
         {"CornerL", "-half_width", "pocket_height"},
         {"SideL", "-half_width", "side_height"}},
        {{"pocket_top", "TopL", "TopR", {}}, {"pocket_right", "TopR", "SideR", {}},
         {"pocket_round_right", "SideR", "BottomR", "CornerR"},
         {"pocket_bottom", "BottomR", "BottomL", {}},
         {"pocket_round_left", "BottomL", "SideL", "CornerL"},
         {"pocket_left", "SideL", "TopL", {}},
         {"pocket_fold", "FoldL", "FoldR", {}}}
    };
    PatternDocument document;
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    document.pieces().append({"Rounded patch pocket", 8, 1, {}, true, 90});
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        entity.piece = 0;
        if (entity.shape == Shape::Point || entity.draftName == QStringLiteral("pocket_fold"))
            entity.layer = Layer::Construction;
        document.replace(i, entity);
    }
    const auto geometry = analyzePiece(document, 0);
    if (geometry.error != ContourError::None || geometry.allowance.isEmpty()) {
        if (error)
            *error = QStringLiteral("These dimensions do not produce a usable pocket cutting contour.");
        return std::nullopt;
    }
    return document;
}

}
