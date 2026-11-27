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
    if (neckline.isEmpty() || neckline.size() > 16)
        return fail(QStringLiteral("Select a continuous open chain of up to 16 neckline edges."));
    QMap<qsizetype, QVector<qsizetype>> byPiece;
    for (const auto index : neckline) {
        if (index < 0 || index >= document.entities().size())
            return fail(QStringLiteral("Choose a valid neckline edge."));
        const auto piece = document.entities()[index].piece;
        if (piece < 0 || piece >= document.pieces().size())
            return fail(QStringLiteral("Each neckline edge must belong to a piece."));
        byPiece[piece].append(index);
    }
    QVector<SeamSection> seams;
    QRectF bounds;
    for (auto piece = byPiece.cbegin(); piece != byPiece.cend(); ++piece) {
        if (std::ranges::any_of(document.production().rules, [&](const auto& rule) { return rule.piece == piece.key(); }))
            return fail(QStringLiteral("The neckline must use measurement-based grading."));
        const auto section = measureSeamSection(document, piece.value());
        const auto geometry = analyzePiece(document, piece.key());
        if (!section || geometry.error != ContourError::None)
            return fail(QStringLiteral("Select one open neckline chain on each closed piece."));
        seams.append(*section);
        bounds = bounds.united(geometry.bounds);
    }

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

    QVector<SeamLink> links;
    QStringList partialLengths;
    QStringList terms;
    for (const auto& seam : seams) {
        SeamLink link;
        link.reverseFirst = seam.edges.front().reversed;
        link.notchPairs = notchPairs;
        link.toleranceMm = 0.01;
        QStringList sectionTerms;
        for (const auto& section : seam.edges) {
            const auto& entity = document.entities()[section.index];
            const auto definition = definitions.constFind(entity.draftName);
            if (definition == definitions.cend())
                return fail(QStringLiteral("Each neckline edge needs an original formula definition."));
            sectionTerms.append(entity.draftName + ".len");
            link.first.append(entity.draftName);
        }
        const auto term = prefix + QStringLiteral("length_%1").arg(links.size() + 1);
        draft.parameters.append({term, "(" + sectionTerms.join(QStringLiteral(" + ")) + ")"});
        terms.append(term);
        links.append(std::move(link));
        partialLengths.append(terms.join(QStringLiteral(" + ")));
    }
    const auto number = [](double value) { return QString::number(value, 'g', 17); };
    const auto left = number(bounds.right() + 40);
    const auto arcLength = terms.join(QStringLiteral(" + "));
    const auto width = prefix + QStringLiteral("width");
    draft.parameters.append({width, riseMm == 0 ? arcLength
        : QStringLiteral("2 * capwidth(%1, (%2) / 2)").arg(number(riseMm), arcLength)});
    const auto right = left + QStringLiteral(" + ") + width;
    const auto neck = number(bounds.top());
    const auto top = neck + QStringLiteral(" - ") + number(depthMm);
    draft.points += {{prefix + "A", left, neck}, {prefix + "B", right, neck},
                     {prefix + "C", right, top}, {prefix + "D", left, top}};
    QString neckControl, topControl;
    if (riseMm > 0) {
        const auto centre = left + " + " + width + " / 2";
        const auto shift = " + " + number(2 * riseMm);
        topControl = prefix + "TopControl";
        draft.points.append({topControl, centre, top + shift});
        if (links.size() == 1) {
            neckControl = prefix + "NeckControl";
            draft.points.append({neckControl, centre, neck + shift});
        }
    }
    QVector<DraftEdge> stand;
    auto start = prefix + "A";
    QString previousT = "0";
    for (qsizetype i = 0; i < links.size(); ++i) {
        const bool last = i + 1 == links.size();
        const auto end = last ? prefix + "B" : prefix + QStringLiteral("join_%1").arg(i + 1);
        auto control = neckControl;
        if (riseMm == 0) {
            if (!last)
                draft.points.append({end, left + " + " + partialLengths[i], neck});
        } else if (links.size() > 1) {
            QString t = "1";
            const auto fourRise = number(4 * riseMm);
            if (!last) {
                t = prefix + QStringLiteral("t_%1").arg(i + 1);
                draft.parameters.append({t, QStringLiteral("standarct(%1, %2, %3)")
                    .arg(width, number(riseMm), partialLengths[i])});
                draft.points.append({end, left + " + " + width + " * " + t,
                    QStringLiteral("%1 + %2 * %3 * (1 - %3)").arg(neck, fourRise, t)});
            }
            control = prefix + QStringLiteral("control_%1").arg(i + 1);
            draft.points.append({control,
                QStringLiteral("%1 + %2 * (%3 + %4) / 2").arg(left, width, previousT, t),
                QStringLiteral("%1 + %2 * ((%3 + %4) / 2 - %3 * %4)").arg(neck, fourRise, previousT, t)});
            previousT = t;
        }
        const auto name = prefix + (links.size() == 1 ? QStringLiteral("neck") : QStringLiteral("neck_%1").arg(i + 1));
        stand.append({name, start, end, control});
        links[i].second = {name};
        start = end;
    }
    stand += {{prefix + "front", prefix + "B", prefix + "C", {}},
        {prefix + "top", prefix + "C", prefix + "D", topControl},
        {prefix + "back", prefix + "D", prefix + "A", {}}};
    draft.edges += stand;
    auto candidate = document;
    const auto target = candidate.pieces().size();
    candidate.pieces().append({byPiece.size() == 1
        ? QStringLiteral("Neck stand · %1").arg(document.pieces()[byPiece.firstKey()].name)
        : QStringLiteral("Neck stand · %1 pieces").arg(byPiece.size()),
                               0, 2, {}, true, 0});
    for (const auto& edge : stand)
        candidate.insert(candidate.entities().size(), {edge.control.isEmpty() ? Shape::Line : Shape::Curve,
            {}, {}, {}, target, Layer::Main, edge.name});
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    for (auto& link : links)
        if (!candidate.addSeamLink(std::move(link), error))
            return false;

    const auto fits = [count = links.size()](const PatternDocument& pattern) {
        const auto checks = pattern.inspectSeamLinks().checks;
        return checks.size() >= count
            && std::ranges::all_of(checks.sliced(checks.size() - count), &SeamLinkCheck::withinTolerance);
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
