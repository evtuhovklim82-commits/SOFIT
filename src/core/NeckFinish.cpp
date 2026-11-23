#include "NeckFinish.h"

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
        const auto front = point("Front", frontX, frontY);
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
    document.pieces().append({stand ? "Straight neck stand (center back fold)" : "Flat collar (center back fold)",
                              0, 2, {}, true, 90});
}
}
