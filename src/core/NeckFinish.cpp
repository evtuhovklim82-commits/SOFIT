#include "NeckFinish.h"

#include "PieceGeometry.h"
#include "Production.h"

#include <algorithm>
#include <cmath>
#include <QStringList>

namespace opc {
namespace {

void addFacing(ParametricDraft& draft, PatternDocument& document, bool front) {
    const QString side = front ? "front" : "back";
    const QString prefix = front ? "NF" : "NB";
    const QString x = front ? "0" : "neck_width + finish_depth + 40";
    const QString y = "finish_y";
    const auto point = [&](const QString& name, const QString& dx, const QString& dy) {
        draft.points.append({prefix + name, x + " + " + dx, y + " + " + dy});
    };
    point("Center", "0", side + "_neck_depth");
    point("Control", "neck_width", side + "_neck_depth");
    point("Shoulder", "neck_width", "0");
    point("OuterCenter", "0", side + "_neck_depth + finish_depth");
    point("OuterControl", "neck_width + finish_depth", side + "_neck_depth + finish_depth");
    point("OuterShoulder", "neck_width + finish_depth", "finish_depth * shoulder_drop / shoulder_run");
    const auto edge = [&](const QString& name, const QString& a, const QString& b,
                          const QString& control = {}) {
        draft.edges.append({"finish_" + side + "_" + name, prefix + a, prefix + b,
                            control.isEmpty() ? control : prefix + control});
    };
    edge("neck", "Center", "Shoulder", "Control");
    edge("shoulder", "Shoulder", "OuterShoulder");
    edge("outer", "OuterShoulder", "OuterCenter", "OuterControl");
    edge("fold", "OuterCenter", "Center");
    document.pieces().append({front ? "Front neck facing (center fold)" : "Back neck facing (center fold)",
                              0, 1, {}, true, 90});
}

} // namespace

void addNeckFinish(ParametricDraft& draft, PatternDocument& document, const BodiceOptions& options) {
    if (options.neckFinish == NeckFinish::None)
        return;
    draft.measurements.append({"finish_depth", options.finishDepth});
    // Place above all garment panels, including a raised puff crown.
    draft.parameters.append(DraftParameter{"finish_y", "-front_neck_depth - back_neck_depth - finish_depth - 400"});
    if (options.neckFinish == NeckFinish::Facing) {
        addFacing(draft, document, true);
        addFacing(draft, document, false);
        return;
    }

    const bool stand = options.neckFinish == NeckFinish::Stand;
    const bool pointed = options.neckFinish == NeckFinish::PointedCollar;
    if (pointed)
        draft.measurements.append({"collar_point_extension", options.collarPointExtension});
    if (stand) {
        draft.parameters.append({
            {"finish_back_length", "quadlen(0, back_neck_depth, neck_width, back_neck_depth, neck_width, 0)"},
            {"finish_front_length", "quadlen(0, front_neck_depth, neck_width, front_neck_depth, neck_width, 0)"}
        });
    }
    const QString shoulderX = stand ? "finish_back_length" : "neck_width";
    const QString frontX = stand ? "finish_back_length + finish_front_length" : "2 * neck_width";
    const QString backY = stand ? "0" : "-back_neck_depth";
    const QString frontY = stand ? "0" : "front_neck_depth";
    const auto section = [&](const QString& suffix, const QString& shift) {
        const auto point = [&](const QString& name, const QString& x, const QString& y) {
            const QString fullName = "NC" + name + suffix;
            draft.points.append({fullName, x, "finish_y + (" + y + ") - " + shift});
            return fullName;
        };
        const auto back = point("Back", "0", backY);
        const QString sectionShoulder = !stand && suffix == "outer"
            ? "neck_width + neck_width * finish_depth / (neck_width + finish_depth)" : shoulderX;
        const auto shoulder = point("Shoulder", sectionShoulder, "0");
        const QString pointExtension = pointed && suffix == "outer" ? " + collar_point_extension" : "";
        const auto front = point("Front", frontX + pointExtension, frontY);
        const auto backControl = stand ? QString{} : point("BackControl", sectionShoulder, backY);
        const auto frontControl = stand ? QString{} : point("FrontControl", sectionShoulder, frontY);
        draft.edges.append({
            {"finish_collar_back_" + suffix, back, shoulder, backControl},
            {"finish_collar_front_" + suffix, shoulder, front, frontControl}
        });
    };
    section("neck", "0");
    section("outer", "finish_depth");
    draft.edges.append({
        {"finish_collar_fold", "NCBackouter", "NCBackneck", {}},
        {"finish_collar_front_end", "NCFrontneck", "NCFrontouter", {}},
        {"finish_collar_guide_shoulder", "NCShoulderneck", "NCShoulderouter", {}}
    });
    const auto* name = stand ? "Straight neck stand (center back fold)"
                     : pointed ? "Pointed collar (center back fold)" : "Flat collar (center back fold)";
    document.pieces().append({name,
                              0, 2, {}, true, 90});
}

bool addLinkedNeckStand(PatternDocument& document, const QVector<qsizetype>& neckline, double depthMm,
                        int notchPairs, QString* error, double riseMm) {
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!(depthMm >= 1 && depthMm <= 300 && riseMm >= 0 && riseMm <= 100)
        || notchPairs < 0 || notchPairs > 20)
        return fail(QStringLiteral("Use a stand depth from 1 to 300 mm, a rise up to 100 mm and up to 20 notch pairs."));
    const auto seam = measureSeamSection(document, neckline);
    if (!seam || neckline.size() > 16)
        return fail(QStringLiteral("Select a continuous open chain of up to 16 neckline edges."));
    const auto piece = document.entities()[seam->edges.front().index].piece;
    if (piece < 0 || piece >= document.pieces().size())
        return fail(QStringLiteral("The neckline must belong to a piece."));
    if (std::ranges::any_of(document.production().rules, [piece](const auto& rule) { return rule.piece == piece; }))
        return fail(QStringLiteral("The neckline must use measurement-based grading."));

    auto draft = document.parametricDraft();
    QSet<QString> occupied;
    QHash<QString, DraftEdge> definitions;
    for (const auto& edge : draft.edges) {
        occupied.insert(edge.name);
        definitions.insert(edge.name, edge);
    }
    for (const auto& point : draft.points)
        occupied.insert(point.name);
    for (const auto& parameter : draft.parameters)
        occupied.insert(parameter.name);
    for (const auto& measurement : draft.measurements)
        occupied.insert(measurement.name);
    QString prefix;
    int unit = 0;
    do {
        prefix = QStringLiteral("neck_stand_%1_").arg(++unit);
    } while (std::ranges::any_of(occupied, [&prefix](const auto& name) { return name.startsWith(prefix); }));

    SeamLink link;
    link.reverseFirst = seam->edges.front().reversed;
    link.notchPairs = notchPairs;
    link.toleranceMm = 0.01;
    QStringList terms;
    for (const auto& section : seam->edges) {
        const auto& entity = document.entities()[section.index];
        const auto definition = definitions.constFind(entity.draftName);
        if (definition == definitions.cend())
            return fail(QStringLiteral("Each neckline edge needs an original formula definition."));
        const auto& a = definition->start;
        const auto& b = definition->end;
        const auto& c = definition->control;
        const auto term = prefix + QStringLiteral("length_%1").arg(terms.size() + 1);
        draft.parameters.append({term, c.isEmpty()
            ? QStringLiteral("hypot(%2.x - %1.x, %2.y - %1.y)").arg(a, b)
            : QStringLiteral("quadlen(%1.x, %1.y, %2.x, %2.y, %3.x, %3.y)").arg(a, c, b)});
        terms.append(term);
        link.first.append(entity.draftName);
    }
    const auto source = analyzePiece(document, piece);
    if (source.error != ContourError::None)
        return fail(QStringLiteral("Close the neckline piece before generating a stand."));
    const auto number = [](double value) { return QString::number(value, 'g', 17); };
    const auto left = number(source.bounds.right() + 40);
    const auto arcLength = terms.join(QStringLiteral(" + "));
    const auto width = prefix + QStringLiteral("width");
    draft.parameters.append({width, riseMm == 0 ? arcLength
        : QStringLiteral("2 * capwidth(%1, (%2) / 2)").arg(number(riseMm), arcLength)});
    const auto right = left + QStringLiteral(" + ") + width;
    const auto neck = number(source.bounds.top());
    const auto top = neck + QStringLiteral(" - ") + number(depthMm);
    draft.points += {{prefix + "A", left, neck}, {prefix + "B", right, neck},
                     {prefix + "C", right, top}, {prefix + "D", left, top}};
    QString neckControl, topControl;
    if (riseMm > 0) {
        const auto centre = left + " + " + width + " / 2";
        const auto shift = " + " + number(2 * riseMm);
        neckControl = prefix + "NeckControl";
        topControl = prefix + "TopControl";
        draft.points += {{neckControl, centre, neck + shift}, {topControl, centre, top + shift}};
    }
    const QVector<DraftEdge> stand{{prefix + "neck", prefix + "A", prefix + "B", neckControl},
        {prefix + "front", prefix + "B", prefix + "C", {}},
        {prefix + "top", prefix + "C", prefix + "D", topControl},
        {prefix + "back", prefix + "D", prefix + "A", {}}};
    draft.edges += stand;
    auto candidate = document;
    const auto target = candidate.pieces().size();
    candidate.pieces().append({QStringLiteral("Neck stand · %1").arg(document.pieces()[piece].name),
                               0, 2, {}, true, 0});
    for (const auto& edge : stand)
        candidate.insert(candidate.entities().size(), {edge.control.isEmpty() ? Shape::Line : Shape::Curve,
            {}, {}, {}, target, Layer::Main, edge.name});
    link.second = {stand.front().name};
    if (!candidate.setParametricDraft(std::move(draft), error) || !candidate.addSeamLink(std::move(link), error))
        return false;

    const auto fits = [](const PatternDocument& pattern) {
        const auto checks = pattern.inspectSeamLinks().checks;
        return !checks.isEmpty() && checks.back().withinTolerance;
    };
    if (!fits(candidate))
        return fail(QStringLiteral("The generated stand does not match the base neckline."));
    for (const auto& size : candidate.production().sizes) {
        if (!fits(gradedPattern(candidate, size.name)))
            return fail(QStringLiteral("The generated stand does not match the neckline in size %1.").arg(size.name));
    }
    document = std::move(candidate);
    return true;
}

}
