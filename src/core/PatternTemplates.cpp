#include "PatternTemplates.h"
#include "PieceGeometry.h"
#include "Production.h"

#include <algorithm>
#include <QSet>

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
        {{"hip", input.hipMm}, {"length", input.lengthMm},
         {"hip_ease", input.hipEaseMm}, {"casing", input.casingMm}},
        {{"half_panel", "(positive(hip) + positive(hip_ease)) / 4"},
         {"casing_depth", "positive(casing)"},
         {"finished_below_casing", "positive(length - casing)"}},
        {{"TopL", "-half_panel", "0"}, {"TopR", "half_panel", "0"},
         {"FoldL", "TopL.x", "casing_depth"}, {"FoldR", "TopR.x", "casing_depth"},
         {"HemL", "FoldL.x - flare / 4", "casing_depth + length"},
         {"HemR", "-HemL.x", "HemL.y"}},
        {{"opening", "TopL", "TopR", {}}, {"casing_right", "TopR", "FoldR", {}},
         {"side_right", "FoldR", "HemR", {}}, {"hem", "HemR", "HemL", {}},
         {"side_left", "HemL", "FoldL", {}}, {"casing_left", "FoldL", "TopL", {}},
         {"casing_fold", "FoldL", "FoldR", {}}}
    };
    if (style == SkirtStyle::ALine)
        draft.measurements.append({"hem_flare", input.hemFlareMm});
    draft.parameters.append(DraftParameter{"flare", style == SkirtStyle::ALine ? "positive(hem_flare)" : "0"});
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

std::optional<PatternDocument> draftShirtCuff(const ShirtCuffMeasurements& input, QString* error) {
    ParametricDraft draft{
        {{"cuff_wrist", input.wristMm}, {"cuff_ease", input.easeMm},
         {"cuff_overlap", input.overlapMm}, {"cuff_depth", input.depthMm}},
        {{"cuff_opening", "positive(cuff_wrist) + positive(cuff_ease)"},
         {"cuff_lap", "positive(cuff_overlap)"},
         {"cuff_width", "cuff_opening + cuff_lap"},
         {"cuff_half_lap", "cuff_lap / 2"},
         {"cuff_fold_y", "positive(cuff_depth)"}},
        {{"CuffTL", "0", "0"}, {"CuffTR", "cuff_width", "0"},
         {"CuffBR", "cuff_width", "2 * cuff_fold_y"}, {"CuffBL", "0", "CuffBR.y"},
         {"CuffFL", "0", "cuff_fold_y"}, {"CuffFR", "cuff_width", "CuffFL.y"},
         {"CuffLapTop", "cuff_opening", "0"}, {"CuffLapBottom", "cuff_opening", "cuff_fold_y"},
         {"CuffButton", "cuff_half_lap", "cuff_fold_y / 2"},
         {"CuffButtonhole", "cuff_opening + cuff_half_lap", "CuffButton.y"}},
        {{"cuff_top", "CuffTL", "CuffTR", {}}, {"cuff_right", "CuffTR", "CuffBR", {}},
         {"cuff_bottom", "CuffBR", "CuffBL", {}}, {"cuff_left", "CuffBL", "CuffTL", {}},
         {"cuff_fold", "CuffFL", "CuffFR", {}},
         {"cuff_overlap_guide", "CuffLapTop", "CuffLapBottom", {}}}
    };
    PatternDocument document;
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    document.pieces().append({QStringLiteral("Манжета рубашки"), 10, 2, {}, true, 0});
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        entity.piece = 0;
        if (entity.shape == Shape::Point || entity.draftName == QStringLiteral("cuff_fold")
            || entity.draftName == QStringLiteral("cuff_overlap_guide"))
            entity.layer = Layer::Construction;
        document.replace(i, entity);
    }
    const auto geometry = analyzePiece(document, 0);
    if (geometry.error != ContourError::None || geometry.allowanceFailed || geometry.allowance.isEmpty()) {
        if (error)
            *error = QStringLiteral("По этим меркам не удалось построить контур манжеты.");
        return std::nullopt;
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

bool addLinkedSeamPocket(PatternDocument& document, qsizetype opening, double depthMm, int notchPairs,
                         QString* error) {
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!(depthMm >= 30 && depthMm <= 500) || notchPairs < 0 || notchPairs > 20)
        return fail(QStringLiteral("Use a pocket depth from 30 to 500 mm and up to 20 notch pairs."));
    const auto section = measureSeamSection(document, {opening});
    if (!section)
        return fail(QStringLiteral("Select a straight seam edge for the pocket opening."));
    const auto& source = document.entities()[opening];
    auto draft = document.parametricDraft();
    const auto edge = std::ranges::find(draft.edges, source.draftName, &DraftEdge::name);
    if (source.piece < 0 || source.shape != Shape::Line || edge == draft.edges.end() || !edge->control.isEmpty()
        || std::ranges::any_of(document.production().rules, [&](const auto& rule) { return rule.piece == source.piece; }))
        return fail(QStringLiteral("The opening must be a straight formula edge with measurement-based grading."));

    QSet<QString> occupied;
    for (const auto& node : draft.measurements)
        occupied.insert(node.name);
    for (const auto& node : draft.parameters)
        occupied.insert(node.name);
    for (const auto& node : draft.points)
        occupied.insert(node.name);
    for (const auto& node : draft.edges)
        occupied.insert(node.name);
    QString prefix;
    int unit = 0;
    do {
        prefix = QStringLiteral("seam_pocket_%1_").arg(++unit);
    } while (std::ranges::any_of(occupied, [&](const auto& name) { return name.startsWith(prefix); }));

    double offset = 0;
    for (const auto& object : document.entities())
        offset = std::max({offset, object.a.x(), object.b.x(), object.control.x()});
    const auto x = QString::number(offset + 40, 'g', 17);
    const auto width = prefix + "width";
    const auto depth = prefix + "depth";
    const auto radius = prefix + "radius";
    const auto right = x + " + " + width;
    draft.measurements.append({depth, depthMm});
    draft.parameters += {{width, QStringLiteral("hypot(%2.x - %1.x, %2.y - %1.y)").arg(edge->start, edge->end)},
                         {radius, QStringLiteral("min(positive(%1) / 4, positive(%2) / 4)").arg(depth, width)}};
    draft.points += {{prefix + "A", x, "0"}, {prefix + "B", right, "0"},
        {prefix + "C", right, depth + " - " + radius}, {prefix + "D", right + " - " + radius, depth},
        {prefix + "E", x + " + " + radius, depth}, {prefix + "F", x, depth + " - " + radius},
        {prefix + "RightControl", right, depth}, {prefix + "LeftControl", x, depth}};
    const QVector<DraftEdge> outline{{prefix + "opening", prefix + "A", prefix + "B", {}},
        {prefix + "side_right", prefix + "B", prefix + "C", {}},
        {prefix + "round_right", prefix + "C", prefix + "D", prefix + "RightControl"},
        {prefix + "bottom", prefix + "D", prefix + "E", {}},
        {prefix + "round_left", prefix + "E", prefix + "F", prefix + "LeftControl"},
        {prefix + "side_left", prefix + "F", prefix + "A", {}}};
    draft.edges += outline;
    auto candidate = document;
    const auto piece = candidate.pieces().size();
    candidate.pieces().append({QStringLiteral("In-seam pocket bag · %1").arg(unit), 10, 2, {}, true, 90});
    for (const auto& boundary : outline)
        candidate.insert(candidate.entities().size(), {boundary.control.isEmpty() ? Shape::Line : Shape::Curve,
            {}, {}, {}, piece, Layer::Main, boundary.name});
    SeamLink link;
    link.first = {source.draftName};
    link.second = {outline.front().name};
    link.notchPairs = notchPairs;
    link.toleranceMm = 0.01;
    if (!candidate.setParametricDraft(std::move(draft), error) || !candidate.addSeamLink(link, error))
        return false;
    const auto matches = [](const PatternDocument& pattern) {
        const auto report = pattern.inspectSeamLinks();
        return !report.checks.isEmpty() && report.checks.back().withinTolerance;
    };
    if (!matches(candidate))
        return fail(QStringLiteral("The pocket bag does not match the opening."));
    for (const auto& size : candidate.production().sizes) {
        if (!matches(gradedPattern(candidate, size.name)))
            return fail(QStringLiteral("The pocket bag does not match the opening in size '%1'.").arg(size.name));
    }
    document = std::move(candidate);
    return true;
}

}
