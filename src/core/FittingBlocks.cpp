#include "FittingBlocks.h"

#include "PieceGeometry.h"
#include "Production.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QLineF>
#include <utility>

namespace opc {

std::optional<PatternDocument> draftTrousers(const TrouserMeasurements& m, QString* error) {
    const auto fail = [error](const QString& message) -> std::optional<PatternDocument> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const std::array lengths{m.waist, m.hip, m.hipDepth, m.rise, m.kneeDepth, m.length,
                             m.knee, m.hem, m.dartLength};
    const std::array ease{m.waistEase, m.hipEase};
    if (!std::ranges::all_of(lengths, [](double v) { return std::isfinite(v) && v > 0 && v <= 3000; })
        || !std::ranges::all_of(ease, [](double v) { return std::isfinite(v) && v >= 0 && v <= 500; }))
        return fail(QStringLiteral("Enter positive lengths and nonnegative wearing ease within the supported ranges."));

    const double hip = (m.hip + m.hipEase) / 4;
    const double suppression = hip - (m.waist + m.waistEase) / 4;
    if (suppression < 6 || suppression > hip * 0.35
        || m.dartLength >= m.hipDepth || m.hipDepth + 10 >= m.rise
        || m.rise + 30 >= m.kneeDepth || m.kneeDepth + 30 >= m.length
        || std::min(m.knee, m.hem) <= hip * 0.14
        || std::max(m.knee, m.hem) > hip * 2.5)
        return fail(QStringLiteral("Check the waist suppression, dart, vertical levels and leg widths."));

    ParametricDraft draft{
        {{"waist", m.waist}, {"hip", m.hip}, {"waist_ease", m.waistEase}, {"hip_ease", m.hipEase},
         {"hip_depth", m.hipDepth}, {"rise", m.rise}, {"knee_depth", m.kneeDepth}, {"length", m.length},
         {"knee", m.knee}, {"hem", m.hem}, {"dart_length", m.dartLength}},
        {{"hip_width", "(hip + hip_ease) / 4"},
         {"suppression", "hip_width - (waist + waist_ease) / 4"},
         {"side_waist", "hip_width - suppression / 2"},
         {"dart_half", "suppression / 4"},
         {"front_extension", "hip_width * 0.17"}, {"back_extension", "hip_width * 0.31"},
         {"leg_axis", "(hip_width - (front_extension + back_extension) / 2) / 2"},
         {"back_offset", "hip_width + back_extension + knee + hem + 40"}},
        {}, {}
    };

    for (const bool front : {true, false}) {
        const QString piece = front ? "trouser_front_" : "trouser_back_";
        const QString p = front ? "TF" : "TB";
        const QString offset = front ? "0" : "back_offset";
        const QString extension = front ? "front_extension" : "back_extension";
        const auto point = [&](const QString& name, const QString& x, const QString& y) {
            draft.points.append({p + name, offset + " + (" + x + ")", y});
        };
        point("CenterWaist", "0", "0");
        point("SideWaist", "side_waist", "0");
        point("CenterHip", "0", "hip_depth");
        point("SideHip", "hip_width", "hip_depth");
        point("SideHipControl", "hip_width", "hip_depth / 2");
        point("SideSeat", "hip_width", "rise");
        point("SeatControl", "0", "rise");
        point("Crotch", "-" + extension, "rise");
        point("OuterKnee", "leg_axis + knee / 4", "knee_depth");
        point("InnerKnee", "leg_axis - knee / 4 + (front_extension + back_extension) / 2 - " + extension, "knee_depth");
        point("OuterHem", "leg_axis + hem / 4", "length");
        point("InnerHem", "leg_axis - hem / 4 + (front_extension + back_extension) / 2 - " + extension, "length");
        point("DartLeft", "side_waist / 2 - dart_half", "0");
        point("DartRight", "side_waist / 2 + dart_half", "0");
        point("DartTip", "side_waist / 2", "dart_length");

        const auto edge = [&](const QString& name, const QString& a, const QString& b,
                              const QString& control = {}) {
            draft.edges.append({piece + name, p + a, p + b, control.isEmpty() ? control : p + control});
        };
        edge("waist", "CenterWaist", "SideWaist");
        edge("side_hip", "SideWaist", "SideHip", "SideHipControl");
        edge("side_seat", "SideHip", "SideSeat");
        edge("side_upper", "SideSeat", "OuterKnee");
        edge("side_lower", "OuterKnee", "OuterHem");
        edge("hem", "OuterHem", "InnerHem");
        edge("inseam_lower", "InnerHem", "InnerKnee");
        edge("inseam_upper", "InnerKnee", "Crotch");
        edge("crotch", "Crotch", "CenterHip", "SeatControl");
        edge("rise", "CenterHip", "CenterWaist");
        edge("guide_seat", "Crotch", "SideSeat");
        edge("guide_knee", "InnerKnee", "OuterKnee");
        edge("dart_left", "DartLeft", "DartTip");
        edge("dart_right", "DartTip", "DartRight");
    }
    PatternDocument document;
    document.pieces().append({
        {"Trouser front (mirror pair)", 0, 2, {}, true, 90},
        {"Trouser back (mirror pair)", 0, 2, {}, true, 90}
    });
    if (!document.setParametricDraft(std::move(draft), error))
        return std::nullopt;
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        if (entity.shape == Shape::Point)
            continue;
        entity.piece = entity.draftName.startsWith("trouser_front_") ? 0 : 1;
        if (entity.draftName.contains("_guide_") || entity.draftName.contains("_dart_"))
            entity.layer = Layer::Construction;
        document.replace(i, entity);
    }
    if (!checkFittingBlock(document).invalidPieces.isEmpty())
        return fail(QStringLiteral("The measurements do not produce two closed trouser contours."));
    return document;
}

FittingCheck checkFittingBlock(const PatternDocument& document, const QString& size, double capEase) {
    QVector<qsizetype> sources;
    const auto drawing = gradedPattern(document, size, &sources);
    QHash<QString, Entity> edgesByName;
    for (qsizetype i = 0; i < drawing.entities().size(); ++i) {
        const auto& source = document.entities()[sources[i]];
        if (!source.draftName.isEmpty() && drawing.entities()[i].shape != Shape::Point)
            edgesByName.insert(source.draftName, drawing.entities()[i]);
    }
    FittingCheck result;
    for (qsizetype i = 0; i < document.pieces().size(); ++i) {
        if (i >= drawing.pieces().size() || analyzePiece(drawing, i).error != ContourError::None)
            result.invalidPieces.append(document.pieces()[i].name);
    }
    const auto sum = [&](std::initializer_list<const char*> edges) -> std::optional<double> {
        double total = 0;
        for (const auto* edge : edges) {
            const auto found = edgesByName.constFind(QString::fromLatin1(edge));
            if (found == edgesByName.cend())
                return std::nullopt;
            const auto length = seamLength(*found);
            if (!length)
                return std::nullopt;
            total += *length;
        }
        return total;
    };
    const auto compare = [&](const QString& label, std::optional<double> first,
                             std::optional<double> second, double target = 0) {
        if (first && second)
            result.seams.append({label, *first, *second, target});
        else
            result.missingSeams.append(label);
    };
    const auto sewnWaist = [&](const QString& prefix) -> std::optional<double> {
        const auto waist = edgesByName.constFind(prefix + "waist");
        const auto left = edgesByName.constFind(prefix + "dart_left");
        const auto right = edgesByName.constFind(prefix + "dart_right");
        if (waist == edgesByName.cend() || left == edgesByName.cend() || right == edgesByName.cend())
            return std::nullopt;
        const auto full = seamLength(*waist);
        const double intake = QLineF(left->a, right->b).length();
        if (full && *full > intake)
            return *full - intake;
        return std::nullopt;
    };
    const auto hasEdge = [&](const QString& name) {
        return std::ranges::any_of(document.parametricDraft().edges,
                                   [&](const DraftEdge& edge) { return edge.name == name; });
    };

    if (hasEdge("trouser_front_waist")) {
        compare("Trouser side seam (front, back)",
                sum({"trouser_front_side_hip", "trouser_front_side_seat", "trouser_front_side_upper", "trouser_front_side_lower"}),
                sum({"trouser_back_side_hip", "trouser_back_side_seat", "trouser_back_side_upper", "trouser_back_side_lower"}));
        compare("Trouser inseam above knee (front, back)",
                sum({"trouser_front_inseam_upper"}), sum({"trouser_back_inseam_upper"}));
        compare("Trouser inseam below knee (front, back)",
                sum({"trouser_front_inseam_lower"}), sum({"trouser_back_inseam_lower"}));
    }
    if (hasEdge("front_armhole")) {
        compare("Bodice side seam (front, back)", sum({"front_side"}), sum({"back_side"}));
        compare("Shoulder seam (front, back)", sum({"front_shoulder"}), sum({"back_shoulder"}));
    }
    if (hasEdge("sleeve_front_cap")) {
        compare("Front armhole and sleeve cap", sum({"front_armhole"}), sum({"sleeve_front_cap"}), capEase);
        compare("Back armhole and sleeve cap", sum({"back_armhole"}), sum({"sleeve_back_cap"}), capEase);
        compare("Sleeve underarm seam (front, back)", sum({"sleeve_front_seam"}), sum({"sleeve_back_seam"}));
    }
    if (hasEdge("skirt_front_waist")) {
        compare("Front dress waist (bodice, skirt)",
                sewnWaist("front_"),
                sewnWaist("skirt_front_"));
        compare("Back dress waist (bodice, skirt)",
                sewnWaist("back_"),
                sewnWaist("skirt_back_"));
        compare("Dress skirt side seam (front, back)", sum({"skirt_front_side_hip", "skirt_front_side_hem"}),
                sum({"skirt_back_side_hip", "skirt_back_side_hem"}));
    }
    return result;
}

}
