#include "BodiceDraft.h"
#include "PieceGeometry.h"
#include "NeckFinish.h"
#include "Production.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace opc {

static bool addSkirtDraft(ParametricDraft& draft, const DressSkirtMeasurements& skirt,
                          double waist, double waistEase, const QString& verticalOrigin) {
    const auto positive = [](double v) { return std::isfinite(v) && v > 0 && v <= 3000; };
    const auto ease = [](double v) { return std::isfinite(v) && v >= 0 && v <= 500; };
    if (!positive(waist) || !ease(waistEase))
        return false;
    const double hip = (skirt.hip + skirt.hipEase) / 4;
    const double suppression = hip - (waist + waistEase) / 4;
    const std::array skirtLengths{skirt.hip, skirt.hipDepth, skirt.length, skirt.dartLength};
    if (!std::ranges::all_of(skirtLengths, positive) || !ease(skirt.hipEase)
        || !std::isfinite(skirt.hemFlare) || skirt.hemFlare < 0 || skirt.hemFlare > 1500
        || suppression < 6 || suppression > hip * 0.35
        || skirt.dartLength >= skirt.hipDepth || skirt.hipDepth + 20 >= skirt.length)
        return false;

    draft.measurements.append({
        {"hip", skirt.hip}, {"hip_ease", skirt.hipEase}, {"hip_depth", skirt.hipDepth},
        {"skirt_length", skirt.length}, {"hem_flare", skirt.hemFlare},
        {"skirt_dart_length", skirt.dartLength}
    });
    draft.parameters.append({
        {"skirt_hip", "(hip + hip_ease) / 4"},
        {"skirt_suppression", "skirt_hip - (waist + waist_ease) / 4"},
        {"skirt_waist", "skirt_hip - skirt_suppression / 2"},
        {"skirt_dart_half", "skirt_suppression / 4"},
        {"skirt_hem", "skirt_hip + hem_flare / 4"}
    });
    for (const bool front : {true, false}) {
        const QString p = front ? "DF" : "DB";
        const QString prefix = front ? "skirt_front_" : "skirt_back_";
        const QString offset = front ? "0" : "skirt_hem + 40";
        const auto point = [&](const QString& name, const QString& x, const QString& y) {
            draft.points.append({p + name, offset + " + (" + x + ")",
                                 verticalOrigin + " + (" + y + ")"});
        };
        point("CenterWaist", "0", "0");
        point("SideWaist", "skirt_waist", "0");
        point("SideHip", "skirt_hip", "hip_depth");
        point("HipControl", "skirt_hip", "hip_depth / 2");
        point("SideHem", "skirt_hem", "skirt_length");
        point("CenterHem", "0", "skirt_length");
        point("DartLeft", "skirt_waist / 2 - skirt_dart_half", "0");
        point("DartRight", "skirt_waist / 2 + skirt_dart_half", "0");
        point("DartTip", "skirt_waist / 2", "skirt_dart_length");
        const auto edge = [&](const QString& name, const QString& a, const QString& b,
                              const QString& control = {}) {
            draft.edges.append({prefix + name, p + a, p + b, control.isEmpty() ? control : p + control});
        };
        edge("waist", "CenterWaist", "SideWaist");
        edge("side_hip", "SideWaist", "SideHip", "HipControl");
        edge("side_hem", "SideHip", "SideHem");
        edge("hem", "SideHem", "CenterHem");
        edge("center", "CenterHem", "CenterWaist");
        edge("dart_left", "DartLeft", "DartTip");
        edge("dart_right", "DartTip", "DartRight");
    }
    return true;
}

std::optional<PatternDocument> draftFittedSkirt(double waist, double waistEase,
                                                const DressSkirtMeasurements& skirt, QString* error) {
    PatternDocument document;
    ParametricDraft draft;
    draft.measurements = {{"waist", waist}, {"waist_ease", waistEase}};
    if (!addSkirtDraft(draft, skirt, waist, waistEase, QStringLiteral("0"))) {
        if (error)
            *error = QStringLiteral("Проверьте мерки талии и бёдер, длину юбки и вытачек.");
        return std::nullopt;
    }
    document.pieces().append({
        {QStringLiteral("Юбка, перед со сгибом"), 0, 1, {}, true, 90},
        {QStringLiteral("Юбка, спинка со сгибом"), 0, 1, {}, true, 90}
    });
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto edge = document.entities()[i];
        if (edge.shape == Shape::Point)
            continue;
        edge.piece = edge.draftName.startsWith("skirt_front_") ? 0 : 1;
        if (edge.draftName.contains("_dart_"))
            edge.layer = Layer::Construction;
        document.replace(i, edge);
    }
    for (qsizetype i = 0; i < document.pieces().size(); ++i) {
        if (analyzePiece(document, i).error != ContourError::None) {
            if (error)
                *error = QStringLiteral("Не удалось построить замкнутый контур юбки.");
            return std::nullopt;
        }
    }
    if (!document.addSeamLink({{"skirt_front_side_hip", "skirt_front_side_hem"},
                               {"skirt_back_side_hip", "skirt_back_side_hem"},
                               false, false, 0, 0.1, 2}, error))
        return std::nullopt;
    return document;
}

static std::optional<PatternDocument> draftBodiceBlock(const BodiceMeasurements& m,
                                                       const DressSkirtMeasurements* skirt,
                                                       const BodiceOptions& options,
                                                       QString* error) {
    const auto fail = [error](const QString& message) -> std::optional<PatternDocument> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const auto positive = [](double v) { return std::isfinite(v) && v > 0 && v <= 3000; };
    const auto ease = [](double v) { return std::isfinite(v) && v >= 0 && v <= 500; };
    const std::array bodiceLengths{m.bust, m.waist, m.bodiceLength, m.armholeDepth, m.neckWidth,
        m.frontNeckDepth, m.backNeckDepth, m.shoulderRun, m.shoulderDrop, m.dartLength};
    const std::array sleeveLengths{m.upperArm, m.cuff, m.sleeveLength, m.capHeight};
    if (!std::ranges::all_of(bodiceLengths, positive) || !ease(m.bustEase) || !ease(m.waistEase)
        || (!skirt && (!std::ranges::all_of(sleeveLengths, positive) || !ease(m.upperArmEase))))
        return fail(QStringLiteral("Enter positive lengths and nonnegative wearing ease within the supported ranges."));

    const bool wide = !skirt && options.sleeveStyle == SleeveStyle::Wide;
    const bool puff = !skirt && options.sleeveStyle == SleeveStyle::Puff;
    const bool twoPiece = !skirt && options.sleeveStyle == SleeveStyle::TwoPiece;
    const auto inRange = [](double value, double min, double max) { return value >= min && value <= max; };
    if ((options.neckFinish != NeckFinish::None && !inRange(options.finishDepth, 10, 120))
        || (options.neckFinish == NeckFinish::PointedCollar && !inRange(options.collarPointExtension, 5, 100))
        || (twoPiece && (!inRange(options.elbowShape, 5, 60) || !inRange(options.elbowLevel, 25, 75)))
        || (!skirt && !inRange(options.capEase, 0, 30))
        || !inRange(options.dartIntakeShare, 10, 90) || !inRange(options.dartPosition, 30, 70)
        || ((wide || puff) && !inRange(options.extraWidth, 10, 500))
        || (puff && (!inRange(options.crownRise, 5, 150) || !inRange(options.gatherPerCapSection, 0, 200)
                     || m.capHeight + options.crownRise + 20 >= m.sleeveLength)))
        return fail(QStringLiteral("Check collar, sleeve and waist dart settings."));

    const double chest = (m.bust + m.bustEase) / 4;
    const double suppression = chest - (m.waist + m.waistEase) / 4;
    if (suppression < 6 || suppression > chest * 0.30
        || m.armholeDepth <= m.shoulderDrop + 30
        || std::max(m.frontNeckDepth, m.backNeckDepth) >= m.armholeDepth
        || m.neckWidth + m.shoulderRun >= chest - 10
        || m.bodiceLength <= m.armholeDepth + m.dartLength
        || (!skirt && (m.capHeight + 20 >= m.sleeveLength || m.cuff > m.upperArm + m.upperArmEase)))
        return fail(QStringLiteral("These measurements are outside this block's drafting range. Check the shoulder, "
                                   "waist suppression, dart length and vertical proportions."));

    ParametricDraft draft{
        {{"bust", m.bust}, {"waist", m.waist}, {"bodice_length", m.bodiceLength},
         {"armhole_depth", m.armholeDepth}, {"neck_width", m.neckWidth},
         {"front_neck_depth", m.frontNeckDepth}, {"back_neck_depth", m.backNeckDepth},
         {"shoulder_run", m.shoulderRun}, {"shoulder_drop", m.shoulderDrop},
         {"bust_ease", m.bustEase}, {"waist_ease", m.waistEase}, {"dart_length", m.dartLength},
         {"dart_intake_share", options.dartIntakeShare}, {"dart_position", options.dartPosition}},
        {{"chest", "(bust + bust_ease) / 4"},
         {"suppression", "chest - (waist + waist_ease) / 4"},
         {"dart_half", "positive(suppression * dart_intake_share / 200)"},
         {"side_waist", "chest - suppression + 2 * dart_half"},
         {"dart_center", "dart_half + positive(side_waist - 2 * dart_half) * dart_position / 100"},
         {"shoulder_x", "neck_width + shoulder_run"}},
        {}, {}
    };
    for (const bool front : {true, false}) {
        const QString p = front ? "F" : "B";
        const QString prefix = front ? "front_" : "back_";
        const QString offset = front ? "0" : "chest + 40";
        const auto point = [&](const QString& name, const QString& x, const QString& y) {
            draft.points.append({p + name, "(" + offset + ") + (" + x + ")", y});
        };
        point("Neck", "0", prefix + "neck_depth");
        point("NeckControl", "neck_width", prefix + "neck_depth");
        point("ShoulderStart", "neck_width", "0");
        point("ShoulderEnd", "shoulder_x", "shoulder_drop");
        point("ArmControl", front ? "shoulder_x - 8" : "shoulder_x + 2", "armhole_depth");
        point("Underarm", "chest", "armhole_depth");
        point("SideWaist", "side_waist", "bodice_length");
        point("CenterWaist", "0", "bodice_length");
        point("DartLeft", "dart_center - dart_half", "bodice_length");
        point("DartRight", "dart_center + dart_half", "bodice_length");
        point("DartTip", "dart_center", "bodice_length - dart_length");
        draft.edges.append({
            {prefix + "neck", p + "Neck", p + "ShoulderStart", p + "NeckControl"},
            {prefix + "shoulder", p + "ShoulderStart", p + "ShoulderEnd", {}},
            {prefix + "armhole", p + "ShoulderEnd", p + "Underarm", p + "ArmControl"},
            {prefix + "side", p + "Underarm", p + "SideWaist", {}},
            {prefix + "waist", p + "SideWaist", p + "CenterWaist", {}},
            {prefix + "center", p + "CenterWaist", p + "Neck", {}},
            {prefix + "dart_left", p + "DartLeft", p + "DartTip", {}},
            {prefix + "dart_right", p + "DartTip", p + "DartRight", {}}
        });
    }
    PatternDocument document;
    document.pieces().append({
        {"Bodice front half (center fold)", 0, 1, {}, true, 90},
        {"Bodice back half (center fold)", 0, 1, {}, true, 90}
    });
    if (!skirt) {
        draft.measurements.append({
            {"upper_arm", m.upperArm}, {"upper_arm_ease", m.upperArmEase}, {"cuff", m.cuff},
            {"sleeve_length", m.sleeveLength}, {"cap_height", m.capHeight}, {"sleeve_cap_ease", options.capEase}
        });
        if (wide || puff)
            draft.measurements.append({"sleeve_extra_width", options.extraWidth});
        if (puff)
            draft.measurements.append({{"sleeve_crown_rise", options.crownRise},
                                       {"sleeve_cap_gather", options.gatherPerCapSection}});
        draft.parameters.append({
            {"sleeve_half", puff ? "(upper_arm + upper_arm_ease + sleeve_extra_width) / 2"
                                 : "(upper_arm + upper_arm_ease) / 2"},
            {"sleeve_hem_width", wide || puff ? "cuff + sleeve_extra_width" : "cuff"},
            {"sleeve_crown", puff ? "-sleeve_crown_rise" : "0"},
            {"sleeve_hem_y", "sleeve_length + sleeve_crown"},
            {"sleeve_center", wide || puff ? "2 * (chest + 40) + sleeve_half + sleeve_hem_width / 2"
                                         : "2 * (chest + 40) + sleeve_half"}
        });
        const auto capControl = [&](const QString& prefix, const QString& sign) {
            const QString armhole = "quadlen(" + prefix + "ShoulderEnd.x, " + prefix + "ShoulderEnd.y, "
                + prefix + "ArmControl.x, " + prefix + "ArmControl.y, "
                + prefix + "Underarm.x, " + prefix + "Underarm.y)";
            const QString gather = puff ? " + sleeve_cap_gather" : "";
            return "sleeve_center " + sign + " capcontrol(sleeve_half, cap_height - sleeve_crown, "
                + armhole + " + sleeve_cap_ease" + gather + ")";
        };
        draft.points.append({
            {"SFront", "sleeve_center - sleeve_half", "cap_height"},
            {"SBack", "sleeve_center + sleeve_half", "cap_height"},
            {"SCrown", "sleeve_center", "sleeve_crown"},
            {"SFrontControl", capControl("F", "-"), "sleeve_crown"},
            {"SBackControl", capControl("B", "+"), "sleeve_crown"},
            {"SHemFront", "sleeve_center - sleeve_hem_width / 2", "sleeve_hem_y"},
            {"SHemBack", "sleeve_center + sleeve_hem_width / 2", "sleeve_hem_y"}
        });
        if (twoPiece) {
            draft.measurements.append({
                {"sleeve_elbow_shape", options.elbowShape}, {"sleeve_elbow_level", options.elbowLevel}
            });
            draft.parameters.append(DraftParameter{"sleeve_panel_spacing", "sleeve_half + sleeve_hem_width / 2 + 40"});
            draft.points.append({
                {"SJoinHem", "sleeve_center", "sleeve_hem_y"},
                {"SJoinElbow", "sleeve_center - sleeve_elbow_shape",
                 "cap_height + (sleeve_hem_y - cap_height) * sleeve_elbow_level / 100"}
            });
            // The back panel uses a translated copy of the same sewing edge.
            for (const auto* name : {"SCrown", "SBack", "SBackControl", "SHemBack", "SJoinHem", "SJoinElbow"}) {
                const QString point = QString::fromLatin1(name);
                draft.points.append({point + "Rear", point + ".x + sleeve_panel_spacing", point + ".y"});
            }
            draft.edges.append({
                {"sleeve_front_cap", "SFront", "SCrown", "SFrontControl"},
                {"sleeve_front_join", "SCrown", "SJoinHem", "SJoinElbow"},
                {"sleeve_front_hem", "SJoinHem", "SHemFront", {}},
                {"sleeve_front_seam", "SHemFront", "SFront", {}},
                {"sleeve_back_cap", "SCrownRear", "SBackRear", "SBackControlRear"},
                {"sleeve_back_seam", "SBackRear", "SHemBackRear", {}},
                {"sleeve_back_hem", "SHemBackRear", "SJoinHemRear", {}},
                {"sleeve_back_join", "SJoinHemRear", "SCrownRear", "SJoinElbowRear"}
            });
            document.pieces().append({
                {"Sleeve front panel (mirror pair)", 0, 2, {}, true, 90},
                {"Sleeve back panel (mirror pair)", 0, 2, {}, true, 90}
            });
        } else {
            draft.edges.append({
                {"sleeve_front_cap", "SFront", "SCrown", "SFrontControl"},
                {"sleeve_back_cap", "SCrown", "SBack", "SBackControl"},
                {"sleeve_back_seam", "SBack", "SHemBack", {}},
                {"sleeve_hem", "SHemBack", "SHemFront", {}},
                {"sleeve_front_seam", "SHemFront", "SFront", {}}
            });

            const auto* sleeveName = puff ? "Puff sleeve (mirror pair)"
                                           : wide ? "Wide-hem sleeve (mirror pair)" : "One-piece sleeve (mirror pair)";
            document.pieces().append({sleeveName, 0, 2, {}, true, 90});
        }
        if (puff) {
            draft.measurements.append({"sleeve_band_depth", 30});
            draft.points.append({
                {"SCuffA", "sleeve_center - cuff / 2", "sleeve_hem_y + 40"},
                {"SCuffB", "sleeve_center + cuff / 2", "SCuffA.y"},
                {"SCuffC", "SCuffB.x", "SCuffB.y + sleeve_band_depth"},
                {"SCuffD", "SCuffA.x", "SCuffC.y"}
            });
            draft.edges.append({
                {"band_attach", "SCuffB", "SCuffA", {}},
                {"band_front", "SCuffA", "SCuffD", {}},
                {"band_outer", "SCuffD", "SCuffC", {}},
                {"band_back", "SCuffC", "SCuffB", {}}
            });
            document.pieces().append({"Puff sleeve band (cut in pairs)", 0, 4, {}, true, 0});
        }
    } else {
        if (!addSkirtDraft(draft, *skirt, m.waist, m.waistEase, QStringLiteral("bodice_length + 40")))
            return fail(QStringLiteral("Check the skirt waist suppression, dart, hip depth and length."));
        document.pieces().append({
            {"Dress skirt front half (center fold)", 0, 1, {}, true, 90},
            {"Dress skirt back half (center fold)", 0, 1, {}, true, 90}
        });
    }
    const auto finishStart = document.pieces().size();
    addNeckFinish(draft, document, options);
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto edge = document.entities()[i];
        if (edge.shape == Shape::Point)
            continue;
        if (edge.draftName.startsWith("front_"))
            edge.piece = 0;
        else if (edge.draftName.startsWith("back_"))
            edge.piece = 1;
        else if (edge.draftName.startsWith("finish_"))
            edge.piece = finishStart + (edge.draftName.startsWith("finish_back_") ? 1 : 0);
        else if (edge.draftName.startsWith("band_") || edge.draftName.startsWith("skirt_back_")
                 || (twoPiece && edge.draftName.startsWith("sleeve_back_")))
            edge.piece = 3;
        else
            edge.piece = 2;
        if (puff && (edge.draftName == "sleeve_hem" || edge.draftName == "band_attach"))
            edge.seamNotches = {0.25, 0.5, 0.75};
        if (puff && (edge.draftName == "sleeve_front_cap" || edge.draftName == "sleeve_back_cap"
                     || edge.draftName == "front_armhole" || edge.draftName == "back_armhole"))
            edge.seamNotches = {0.5};
        if (edge.draftName.contains("_dart_") || edge.draftName.contains("_guide_"))
            edge.layer = Layer::Construction;
        document.replace(i, edge);
    }
    for (qsizetype i = 0; i < document.pieces().size(); ++i) {
        if (analyzePiece(document, i).error != ContourError::None)
            return fail(QStringLiteral("The measurements do not produce closed sewing contours."));
    }
    if (twoPiece && !document.addSeamLink({
            {"sleeve_front_join"}, {"sleeve_back_join"}, false, true, 0, 0.1, 2
        }, error))
        return std::nullopt;
    return document;
}

std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& m, QString* error) {
    return draftBodiceAndSleeve(m, BodiceOptions{}, error);
}

std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt, QString* error) {
    return draftDress(bodice, skirt, BodiceOptions{}, error);
}

std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& m,
                                                   const BodiceOptions& options, QString* error) {
    return draftBodiceBlock(m, nullptr, options, error);
}

std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt,
                                          const BodiceOptions& options, QString* error) {
    return draftBodiceBlock(bodice, &skirt, options, error);
}

std::optional<BodiceSeams> measureBodiceSeams(const PatternDocument& document) {
    const auto length = [&](const QString& name) -> std::optional<double> {
        for (const auto& edge : document.entities()) {
            if (edge.draftName == name)
                return seamLength(edge);
        }
        return std::nullopt;
    };
    const auto front = length("front_armhole");
    const auto back = length("back_armhole");
    const auto frontCap = length("sleeve_front_cap");
    const auto backCap = length("sleeve_back_cap");
    if (!front || !back || !frontCap || !backCap)
        return std::nullopt;
    return BodiceSeams{*front, *back, *frontCap, *backCap};
}

bool addLinkedSleeve(PatternDocument& document, const QVector<qsizetype>& front,
                     const QVector<qsizetype>& back, const LinkedSleeveMeasurements& measurements,
                     bool reverseFront, bool reverseBack, QString* error) {
    const auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    const auto& m = measurements;
    if (!(m.lengthMm > m.capRiseMm && m.lengthMm <= 3000 && m.capRiseMm >= 1
          && m.hemWidthMm >= 1 && m.hemWidthMm <= 1000 && m.frontEaseMm >= 0 && m.frontEaseMm <= 200
          && m.backEaseMm >= 0 && m.backEaseMm <= 200 && m.notchPairs >= 0 && m.notchPairs <= 20))
        return fail(QStringLiteral("Check the sleeve length, cap height, hem width, ease and notch count."));
    if (front.size() > 16 || back.size() > 16)
        return fail(QStringLiteral("Select up to 16 edges for each armhole."));
    auto frontSeam = measureSeamSection(document, front);
    auto backSeam = measureSeamSection(document, back);
    if (!frontSeam || !backSeam || std::ranges::any_of(front, [&](qsizetype index) { return back.contains(index); }))
        return fail(QStringLiteral("Select distinct open chains for the front and back armholes."));

    auto draft = document.parametricDraft();
    QSet<QString> occupied;
    QHash<QString, DraftEdge> definitions;
    for (const auto& edge : draft.edges) {
        definitions.insert(edge.name, edge);
        occupied.insert(edge.name);
    }
    for (const auto& point : draft.points)
        occupied.insert(point.name);
    for (const auto& parameter : draft.parameters)
        occupied.insert(parameter.name);
    for (const auto& measurement : draft.measurements)
        occupied.insert(measurement.name);

    QString prefix;
    int sequence = 0;
    do {
        prefix = QStringLiteral("linked_sleeve_%1_").arg(++sequence);
    } while (std::ranges::any_of(occupied, [&](const auto& name) { return name.startsWith(prefix); }));

    draft.measurements += {{prefix + "length", m.lengthMm}, {prefix + "rise", m.capRiseMm},
                          {prefix + "hem", m.hemWidthMm}};
    auto candidate = document;
    QVector<SeamLink> links;
    for (auto [section, reverse, side, ease] : {std::tuple{*frontSeam, reverseFront, QString("front"), m.frontEaseMm},
                                              {*backSeam, reverseBack, QString("back"), m.backEaseMm}}) {
        if (reverse) {
            std::ranges::reverse(section.edges);
            for (auto& edge : section.edges)
                edge.reversed = !edge.reversed;
        }
        const auto piece = document.entities()[section.edges.front().index].piece;
        if (piece < 0 || analyzePiece(document, piece).error != ContourError::None
            || std::ranges::any_of(document.production().rules, [piece](const auto& rule) { return rule.piece == piece; }))
            return fail(QStringLiteral("Each armhole must belong to a closed piece with measurement-based grading."));
        SeamLink link;
        link.reverseFirst = section.edges.front().reversed;
        link.targetEaseMm = ease;
        link.toleranceMm = 0.01;
        link.notchPairs = m.notchPairs;
        link.second = {prefix + side + "_cap"};
        QStringList terms;
        for (const auto& entry : section.edges) {
            auto entity = document.entities()[entry.index];
            if (entity.draftName.isEmpty()) {
                if (!m.twoPiece)
                    return fail(QStringLiteral("Every armhole edge must have an original formula definition."));
                const auto id = prefix + side + QStringLiteral("_source_%1").arg(terms.size() + 1);
                const auto addPoint = [&](const QString& suffix, QPointF point) {
                    const auto name = id + suffix;
                    draft.points.append({name, QString::number(point.x(), 'g', 17),
                                               QString::number(point.y(), 'g', 17)});
                    return name;
                };
                const auto start = addPoint("_start", entity.a);
                const auto end = addPoint("_end", entity.b);
                const auto control = entity.shape == Shape::Curve ? addPoint("_control", entity.control) : QString{};
                draft.edges.append({id, start, end, control});
                entity.draftName = id;
                candidate.replace(entry.index, entity);
            } else if (!definitions.contains(entity.draftName)) {
                return fail(QStringLiteral("The selected modeled output has no original formula definition."));
            }
            terms.append(entity.draftName + ".len");
            link.first.append(entity.draftName);
        }
        draft.parameters.append({prefix + side + "_width", QStringLiteral("capwidth(%1rise, (%2) + %3)")
            .arg(prefix, terms.join(QStringLiteral(" + ")), QString::number(ease, 'g', 17))});
        links.append(std::move(link));
    }

    double right = 0;
    for (const auto& entity : document.entities())
        right = std::max({right, entity.a.x(), entity.b.x(), entity.control.x()});
    const auto frontWidth = prefix + "front_width";
    const auto backWidth = prefix + "back_width";
    const auto centre = prefix + "centre";
    const auto taper = prefix + "taper";
    const auto length = prefix + "length";
    const auto rise = prefix + "rise";
    draft.parameters += {{centre, QString::number(right + 40 + m.hemWidthMm / 2, 'g', 17) + " + " + frontWidth},
                         {taper, QStringLiteral("(%1 + %2 - positive(%3hem)) / 2").arg(frontWidth, backWidth, prefix)}};
    const auto point = [&](const QString& name, const QString& x, const QString& y) {
        draft.points.append({prefix + name, x, y});
    };
    const auto firstPiece = candidate.pieces().size();
    const auto installPiece = [&](const QString& name, const QVector<DraftEdge>& edges) {
        const auto piece = candidate.pieces().size();
        candidate.pieces().append({name.arg(sequence), 10, 2, {}, true, 90});
        draft.edges += edges;
        for (const auto& edge : edges)
            candidate.insert(candidate.entities().size(), {edge.control.isEmpty() ? Shape::Line : Shape::Curve,
                {}, {}, {}, piece, Layer::Main, edge.name});
    };
    const auto hemY = QStringLiteral("%1 + positive(%2 - %1)").arg(rise, length);
    if (m.twoPiece) {
        // Reflect the cap panels about their local axes. Equal hem setbacks
        // give each pair of longitudinal sewing lines identical geometry.
        for (const auto& side : {QStringLiteral("front"), QStringLiteral("back")}) {
            const bool frontPanel = side == u"front";
            const auto stem = side + "_";
            const auto width = prefix + stem + "width";
            const auto origin = frontPanel ? centre : centre + " + 80";
            const auto direction = frontPanel ? QStringLiteral(" - ") : QStringLiteral(" + ");
            const auto outboard = origin + direction + width;
            const auto insideHem = origin + direction + taper + " / 2";
            const auto outerHem = outboard + (frontPanel ? " + " : " - ") + taper + " / 2";
            point(stem + "Crown", origin, "0");
            point(stem + "Underarm", outboard, rise);
            point(stem + "Control", origin + direction + width + " / 2", "0");
            point(stem + "InnerHem", insideHem, hemY);
            point(stem + "OuterHem", outerHem, hemY);
            const auto id = prefix + stem;
            installPiece(frontPanel ? QStringLiteral("Front sleeve panel · %1") : QStringLiteral("Back sleeve panel · %1"), {
                {id + "cap", id + "Crown", id + "Underarm", id + "Control"},
                {id + "underarm_seam", id + "Underarm", id + "OuterHem", {}},
                {id + "hem_edge", id + "OuterHem", id + "InnerHem", {}},
                {id + "upper_seam", id + "InnerHem", id + "Crown", {}}
            });
        }
        for (const auto& name : {QStringLiteral("underarm_seam"), QStringLiteral("upper_seam")}) {
            SeamLink link;
            link.first = {prefix + "front_" + name};
            link.second = {prefix + "back_" + name};
            link.toleranceMm = 0.01;
            link.notchPairs = m.notchPairs;
            links.append(std::move(link));
        }
    } else {
        point("Crown", centre, "0");
        point("Front", centre + " - " + frontWidth, rise);
        point("Back", centre + " + " + backWidth, rise);
        point("FrontControl", centre + " - " + frontWidth + " / 2", "0");
        point("BackControl", centre + " + " + backWidth + " / 2", "0");
        point("FrontHem", centre + " - " + frontWidth + " + " + taper, hemY);
        point("BackHem", centre + " + " + backWidth + " - " + taper, hemY);
        installPiece(QStringLiteral("Sleeve · %1"), {
            {prefix + "front_cap", prefix + "Crown", prefix + "Front", prefix + "FrontControl"},
            {prefix + "back_cap", prefix + "Crown", prefix + "Back", prefix + "BackControl"},
            {prefix + "front_seam", prefix + "Front", prefix + "FrontHem", {}},
            {prefix + "hem_edge", prefix + "FrontHem", prefix + "BackHem", {}},
            {prefix + "back_seam", prefix + "BackHem", prefix + "Back", {}}
        });
    }
    if (!candidate.setParametricDraft(std::move(draft), error))
        return false;
    for (const auto& link : links) {
        if (!candidate.addSeamLink(link, error))
            return false;
    }

    QStringList sizes{QString{}};
    for (const auto& size : candidate.production().sizes)
        sizes.append(size.name);
    for (const auto& size : sizes) {
        const auto grade = gradedPattern(candidate, size);
        const auto report = grade.inspectSeamLinks();
        const auto sizeLabel = size.isEmpty() ? QStringLiteral("the base size") : size;
        if (report.checks.size() < links.size()
            || !std::ranges::all_of(report.checks.sliced(report.checks.size() - links.size()), &SeamLinkCheck::withinTolerance))
            return fail(QStringLiteral("The sleeve sewing lines do not match in %1.").arg(sizeLabel));
        if (m.twoPiece) {
            for (auto piece = firstPiece; piece < candidate.pieces().size(); ++piece) {
                const auto geometry = analyzePiece(grade, piece);
                if (geometry.error != ContourError::None || geometry.allowanceFailed)
                    return fail(QStringLiteral("The sleeve panels or their cutting allowances are invalid in %1.").arg(sizeLabel));
            }
        }
    }
    document = std::move(candidate);
    return true;
}

}
