#include "PatternDocument.h"
#include "PieceGeometry.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QJsonObject readProject(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Open saved project");
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeProject(const QString& path, const QJsonObject& data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write project fixture");
    const auto bytes = QJsonDocument(data).toJson();
    check(file.write(bytes) == bytes.size(), "Write complete project fixture");
}
}

int main() {
    using namespace opc;
    PatternDocument document;
    document.pieces().append({"Front", 5});
    document.insert(0, {Shape::Line, {10, 20}, {30, 40}, {}, -1, Layer::Construction});

    const ParametricDraft initial{
        {{"chest", 360}, {"height", 50}},
        {{"width", "chest/4 + ease"}, {"ease", "-(-5) + 2 * (3 - 0.5)"}},
        {{"A", "0", "0"}, {"B", "width", "A.y"}, {"C", "B.x", "height"},
         {"D", "0", "C.y"}, {"K", "C.x / 2", "height + 1e1"}},
        {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"hem", "C", "D", "K"}, {"left", "D", "A", {}}}
    };
    QString error;
    check(document.setParametricDraft(initial, &error) && error.isEmpty(), "Evaluate precedence, unary signs and forward references");
    check(document.entities().size() == 10 && document.entities()[2].a == QPointF(100, 0),
          "Generate points in addition to manual objects");
    for (qsizetype i = 6; i < document.entities().size(); ++i) {
        auto edge = document.entities()[i];
        edge.piece = 0;
        document.replace(i, edge);
    }
    const auto geometry = analyzePiece(document, 0);
    check(geometry.error == ContourError::None && !geometry.allowance.isEmpty(), "Use parametric edges as a cutting contour");

    auto changed = initial;
    changed.measurements[0].mm = 440;
    changed.measurements[1].mm = 75;
    check(document.setParametricDraft(changed, &error), "Recalculate after changing two measurements");
    const auto& entities = document.entities();
    check(entities[0].a == QPointF(10, 20) && entities[6].b == QPointF(120, 0)
          && entities[8].shape == Shape::Curve && entities[8].a == QPointF(120, 75)
          && entities[8].b == QPointF(0, 75) && entities[8].control == QPointF(60, 85),
          "Update line endpoints and curve control point while retaining manual geometry");
    check(entities[8].piece == 0 && analyzePiece(document, 0).bounds.width() == 120,
          "Keep piece assignment and update the contour bounds");

    const auto expectFailure = [&](const ParametricDraft& invalid, const QString& diagnostic) {
        check(!document.setParametricDraft(invalid, &error) && error.contains(diagnostic), "Reject invalid draft with a diagnostic");
        check(document.parametricDraft() == changed && document.entities()[8].control == QPointF(60, 85),
              "An invalid edit leaves the previous draft and geometry intact");
    };
    for (const auto& test : {DraftParameter{"missing + 2", "Unknown variable"},
                             DraftParameter{"1 / (2 - 2)", "Division by zero"},
                             DraftParameter{"1e308 * 10", "numeric range"},
                             DraftParameter{"(3 + 2", "Expected ')'"},
                             DraftParameter{"2 +", "Expected a number"},
                             DraftParameter{"B.x", "Cyclic dependency"}}) {
        auto invalid = changed;
        invalid.parameters[0].formula = test.name;
        expectFailure(invalid, test.formula);
    }
    auto invalid = changed;
    invalid.points[0].x = "A.x";
    expectFailure(invalid, "Cyclic dependency");
    invalid = changed;
    invalid.edges[0].end = "missing";
    expectFailure(invalid, "unknown point");
    invalid = changed;
    invalid.parameters[0].name = "chest";
    expectFailure(invalid, "Duplicate name");
    invalid = changed;
    invalid.measurements[0].mm = -1;
    expectFailure(invalid, "non-negative");

    QTemporaryDir temp;
    check(temp.isValid(), "Create temporary project directory");
    const auto filename = temp.filePath("parametric.sofit");
    check(document.save(filename, &error), "Save parametric project");
    auto json = readProject(filename);
    check(json.value("version").toInt() == 8, "Write format version 8");
    PatternDocument loaded;
    check(loaded.load(filename, &error) && loaded.parametricDraft() == changed, "Round trip the complete draft");
    check(loaded.entities()[8].control == QPointF(60, 85) && loaded.entities()[8].piece == 0
          && loaded.entities()[0].layer == Layer::Construction, "Round trip entity metadata and geometry");

    auto objects = json.value("objects").toArray();
    auto curve = objects[8].toObject();
    curve["coordinates"] = QJsonArray{1, 2, 3, 4, 5, 6};
    objects[8] = curve;
    json["objects"] = objects;
    writeProject(filename, json);
    check(loaded.load(filename, &error) && loaded.entities()[8].control == QPointF(60, 85),
          "Recompute derived geometry on load instead of using cached coordinates");
    curve["draftName"] = "unknown";
    objects[8] = curve;
    json["objects"] = objects;
    writeProject(filename, json);
    check(!loaded.load(filename, &error) && loaded.parametricDraft() == changed
          && loaded.entities()[8].control == QPointF(60, 85), "Reject inconsistent saved bindings atomically");

    auto definition = json.value("parametricDraft").toObject();
    auto parameters = definition.value("parameters").toArray();
    auto width = parameters[0].toObject();
    width["formula"] = "undefined";
    parameters[0] = width;
    definition["parameters"] = parameters;
    json["parametricDraft"] = definition;
    writeProject(filename, json);
    check(!loaded.load(filename, &error) && error.contains("Unknown variable") && loaded.parametricDraft() == changed,
          "Validate saved formulas without replacing the current document");

    check(document.setParametricDraft({}) && document.entities().size() == 1, "Remove generated objects without deleting manual drawing");
    check(document.save(filename, &error) && readProject(filename).value("version").toInt() == 7,
          "Continue writing version 7 for projects without a draft");
    check(loaded.load(filename, &error) && loaded.parametricDraft() == ParametricDraft{}
          && loaded.entities().size() == 1, "Loading version 7 clears the previous parametric data");
}
