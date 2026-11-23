#include "BodiceDraft.h"
#include "PieceGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace opc {

static std::optional<PatternDocument> draftBodiceBlock(const BodiceMeasurements& m,
                                                       const DressSkirtMeasurements* skirt,
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
         {"bust_ease", m.bustEase}, {"waist_ease", m.waistEase}, {"dart_length", m.dartLength}},
        {{"chest", "(bust + bust_ease) / 4"},
         {"suppression", "chest - (waist + waist_ease) / 4"},
         {"side_waist", "chest - suppression / 2"},
         {"dart_half", "suppression / 4"},
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
        point("DartLeft", "side_waist / 2 - dart_half", "bodice_length");
        point("DartRight", "side_waist / 2 + dart_half", "bodice_length");
        point("DartTip", "side_waist / 2", "bodice_length - dart_length");
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
            {"sleeve_length", m.sleeveLength}, {"cap_height", m.capHeight}
        });
        draft.parameters.append({
            {"sleeve_half", "(upper_arm + upper_arm_ease) / 2"},
            {"sleeve_center", "2 * (chest + 40) + sleeve_half"}
        });
        draft.points.append({
            {"SFront", "sleeve_center - sleeve_half", "cap_height"},
            {"SBack", "sleeve_center + sleeve_half", "cap_height"},
            {"SCrown", "sleeve_center", "0"},
            {"SFrontControl", "sleeve_center - sleeve_half * 0.70", "0"},
            {"SBackControl", "sleeve_center + sleeve_half * 0.55", "0"},
            {"SHemFront", "sleeve_center - cuff / 2", "sleeve_length"},
            {"SHemBack", "sleeve_center + cuff / 2", "sleeve_length"}
        });
        draft.edges.append({
            {"sleeve_front_cap", "SFront", "SCrown", "SFrontControl"},
            {"sleeve_back_cap", "SCrown", "SBack", "SBackControl"},
            {"sleeve_back_seam", "SBack", "SHemBack", {}},
            {"sleeve_hem", "SHemBack", "SHemFront", {}},
            {"sleeve_front_seam", "SHemFront", "SFront", {}}
        });

        document.pieces().append({"One-piece sleeve (mirror pair)", 0, 2, {}, true, 90});
    } else {
        const double hip = (skirt->hip + skirt->hipEase) / 4;
        const double suppression = hip - (m.waist + m.waistEase) / 4;
        const std::array skirtLengths{skirt->hip, skirt->hipDepth, skirt->length, skirt->dartLength};
        if (!std::ranges::all_of(skirtLengths, positive) || !ease(skirt->hipEase)
            || !std::isfinite(skirt->hemFlare) || skirt->hemFlare < 0 || skirt->hemFlare > 1500
            || suppression < 6 || suppression > hip * 0.35
            || skirt->dartLength >= skirt->hipDepth || skirt->hipDepth + 20 >= skirt->length)
            return fail(QStringLiteral("Check the skirt waist suppression, dart, hip depth and length."));

        draft.measurements.append({
            {"hip", skirt->hip}, {"hip_ease", skirt->hipEase}, {"hip_depth", skirt->hipDepth},
            {"skirt_length", skirt->length}, {"hem_flare", skirt->hemFlare},
            {"skirt_dart_length", skirt->dartLength}
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
                                     "bodice_length + 40 + (" + y + ")"});
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
        document.pieces().append({
            {"Dress skirt front half (center fold)", 0, 1, {}, true, 90},
            {"Dress skirt back half (center fold)", 0, 1, {}, true, 90}
        });
    }
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
        else
            edge.piece = edge.draftName.startsWith("skirt_back_") ? 3 : 2;
        if (edge.draftName.contains("_dart_"))
            edge.layer = Layer::Construction;
        document.replace(i, edge);
    }
    for (qsizetype i = 0; i < document.pieces().size(); ++i) {
        if (analyzePiece(document, i).error != ContourError::None)
            return fail(QStringLiteral("The measurements do not produce closed sewing contours."));
    }
    return document;
}

std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& m, QString* error) {
    return draftBodiceBlock(m, nullptr, error);
}

std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt, QString* error) {
    return draftBodiceBlock(bodice, &skirt, error);
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

}
