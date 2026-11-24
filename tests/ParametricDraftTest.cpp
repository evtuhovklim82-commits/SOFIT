#include "PatternDocument.h"
#include "BodiceDraft.h"
#include "PatternDxf.h"

#include <cmath>
#include <limits>
#include "PieceGeometry.h"
#include "PatternTemplates.h"
#include "Production.h"

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
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    check(root.value("version").toInt() == 14, "Write the protected SOFIT format");
    return QJsonDocument::fromJson(root.value("payload").toString().toUtf8()).object();
}

void writeProject(const QString& path, const QJsonObject& data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write project fixture");
    const auto bytes = QJsonDocument(data).toJson();
    check(file.write(bytes) == bytes.size(), "Write complete project fixture");
}
void checkModelingHistory() {
    using namespace opc;
    ParametricDraft draft;
    draft.measurements = {{"width", 100}};
    draft.points = {{"A", "0", "0"}, {"B", "width", "0"},
                    {"C", "width", "60"}, {"D", "0", "60"}};
    draft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", {}},
                   {"bottom", "C", "D", {}}, {"left", "D", "A", {}}};
    PatternDocument document;
    QString error;
    check(document.setParametricDraft(draft, &error), "Build a formula rectangle from named edges");
    document.pieces().append({"Panel", 0});
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        auto entity = document.entities()[i];
        if (entity.shape == Shape::Line) {
            entity.piece = 0;
            if (entity.draftName == "bottom")
                entity.seamNotches = {0.5};
            document.replace(i, entity);
        }
    }
    document.production().sizes.append({"L", {{"width", 160}}});
    const bool appended = document.appendSilhouette(0, 0, 1, 1.5, &error)
        && document.appendSilhouette(0, 0, 1, 2, &error);
    check(appended, qPrintable(error));
    const auto width = [](const PatternDocument& pattern) { return analyzePiece(pattern, 0).bounds.width(); };
    check(width(document) == 300 && width(gradedPattern(document, "L")) == 480,
          "Replay both steps in order for base and size measurements");
    check(analyzePiece(document, 0).notchAnchors.size() == 1, "Preserve edge notches through replay");

    draft.measurements[0].mm = 120;
    check(document.setParametricDraft(draft, &error) && width(document) == 360,
          "Rebuild from formulas without compounding the previous result");
    check(document.setParametricDraft(draft, &error) && width(document) == 360,
          "Replay is deterministic");
    auto incompatible = draft;
    incompatible.edges[0].name = "renamed";
    check(!document.setParametricDraft(incompatible, &error) && error.contains("Modeling step 1")
          && document.parametricDraft() == draft && width(document) == 360,
          "Reject a deleted modeling object atomically");
    auto badAllowance = document;
    badAllowance.pieces()[0].allowanceMm = 2;
    check(!badAllowance.validateParametricSizes(badAllowance.production(), &error)
          && error.contains("Size 'L'") && error.contains("seam allowance"),
          "Reject a replay that cannot rebuild the cut outline for a graded size");
    auto invalidSize = document.production();
    invalidSize.sizes[0].measurements["width"] = 0;
    check(!document.validateParametricSizes(invalidSize, &error) && error.contains("Size 'L'"),
          "Diagnose a geometry failure on a non-base size");
    auto manualGrade = document.production();
    manualGrade.rules.append({"L", 0, {0, 0}, {1, 0}});
    check(!document.validateParametricSizes(manualGrade, &error) && error.contains("manual grade"),
          "Reject coordinate-based grade rules for history-driven pieces");

    QTemporaryDir directory;
    const auto path = directory.filePath("history.sofit");
    check(directory.isValid() && document.save(path, &error), "Save modeling history");
    auto json = readProject(path);
    PatternDocument loaded;
    check(json.value("version").toInt() == 11 && loaded.load(path, &error)
          && loaded.modelingHistory() == document.modelingHistory()
          && width(loaded) == 360 && width(gradedPattern(loaded, "L")) == 480, "Round trip and replay format 11");
    auto history = json.value("modelingHistory").toArray();
    auto first = history[0].toObject();
    first["operation"] = "unknown";
    history[0] = first;
    json["modelingHistory"] = history;
    writeProject(path, json);
    check(!loaded.load(path, &error) && width(loaded) == 360, "Reject unknown modeling steps atomically");
    check(loaded.removeLastModelingStep(&error) && width(loaded) == 180
          && loaded.removeLastModelingStep(&error) && width(loaded) == 120,
          "Removing a step replays the remaining steps from the draft");
}

void checkBodiceAndSleeve() {
    using namespace opc;
    BodiceMeasurements measurements{920, 740, 420, 210, 68, 75, 22, 116, 38, 60, 40,
                                    115, 300, 50, 220, 570, 87};
    QString error;
    auto document = draftBodiceAndSleeve(measurements, &error);
    if (!document)
        std::cerr << error.toStdString() << '\n';
    check(document.has_value() && document->pieces().size() == 3, "Draft front, back and sleeve");
    QVector<PieceGeometry> geometry;
    for (qsizetype i = 0; i < 3; ++i) {
        geometry.append(analyzePiece(*document, i));
        check(geometry.back().error == ContourError::None, "Draft a closed sewing contour");
    }
    check(geometry[0].bounds.width() == 245 && geometry[1].bounds.width() == 245
          && geometry[2].bounds.width() == 350, "Draft uses bust and upper arm wearing ease");
    const auto seams = measureBodiceSeams(*document);
    check(seams && seams->frontArmhole > seams->backArmhole && seams->frontCap > seams->backCap,
          "Measure the distinct front and back cap sections");
    qsizetype dartLegs = 0;
    for (const auto& edge : document->entities()) {
        if (edge.draftName.contains("_dart_")) {
            check(edge.layer == Layer::Construction && edge.piece >= 0 && edge.piece < 2,
                  "Preserve the dart on its own bodice half");
            ++dartLegs;
        }
    }
    check(dartLegs == 4, "Draft both legs of each waist dart");

    document->production().sizes.append({"Larger", {{"bust", 960}, {"upper_arm", 330}}});
    const auto graded = gradedPattern(*document, "Larger");
    check(analyzePiece(graded, 0).bounds.width() == 255
          && analyzePiece(graded, 2).bounds.width() == 380, "Recalculate sizes from measurements");

    QTemporaryDir directory;
    check(directory.isValid(), "Create block export directory");
    const auto path = directory.filePath("block.sofit");
    check(document->save(path, &error), "Save a bodice and sleeve project");
    PatternDocument reopened;
    check(reopened.load(path, &error)
          && reopened.parametricDraft() == document->parametricDraft()
          && reopened.pieces() == document->pieces(), "Restore editable bodice and sleeve formulas");
    const auto reopenedSeams = measureBodiceSeams(reopened);
    check(reopenedSeams && std::abs(reopenedSeams->frontCap - seams->frontCap) < 0.001,
          "Restore the sleeve cap measurement");
    check(exportPatternDxf(directory.filePath("block.dxf"), reopened, QVector<bool>(reopened.entities().size(), true), geometry, error),
          "Export the bodice and sleeve as DXF");

    measurements.waist = measurements.bust;
    check(!draftBodiceAndSleeve(measurements), "Reject insufficient waist suppression");
    measurements.waist = std::numeric_limits<double>::quiet_NaN();
    check(!draftBodiceAndSleeve(measurements), "Reject a nonfinite measurement");
}

void checkLowerBlocks() {
    using namespace opc;
    TrouserMeasurements trousers{740, 1000, 40, 60, 195, 270, 600, 1030, 460, 380, 100};
    const auto block = draftTrousers(trousers);
    check(block && block->pieces().size() == 2, "Draft the trouser front and back");
    QHash<QString, double> lengths;
    for (const auto& edge : block->entities()) {
        if (const auto length = seamLength(edge))
            lengths.insert(edge.draftName, *length);
        if (edge.draftName.contains("_guide_") || edge.draftName.contains("_dart_"))
            check(edge.layer == Layer::Construction && edge.piece >= 0, "Keep guides and darts on their pieces");
    }
    check(qAbs(lengths["trouser_front_guide_knee"] + lengths["trouser_back_guide_knee"] - trousers.knee) < 0.001
          && qAbs(lengths["trouser_front_hem"] + lengths["trouser_back_hem"] - trousers.hem) < 0.001,
          "Use the entered per-leg knee and hem circumferences");
    check(lengths["trouser_back_crotch"] > lengths["trouser_front_crotch"], "Use a longer back crotch curve");
    trousers.kneeDepth = trousers.rise;
    check(!draftTrousers(trousers), "Reject coincident knee and seat levels");

    const BodiceMeasurements bodice{920, 740, 420, 210, 68, 75, 22, 116, 38, 60, 40, 115};
    DressSkirtMeasurements skirt{1000, 60, 195, 620, 160, 100};
    const auto dress = draftDress(bodice, skirt);
    check(dress && dress->pieces().size() == 4, "Draft the sleeveless bodice and the two skirt panels");
    const auto seams = checkFittingBlock(*dress);
    for (const auto& seam : seams.seams) {
        if (seam.label.contains("dress waist"))
            check(qAbs(seam.first - 195) < 0.001 && qAbs(seam.second - 195) < 0.001,
                  "Match each quarter waist after closing its darts");
    }
    skirt.dartLength = skirt.hipDepth;
    check(!draftDress(bodice, skirt), "Reject a skirt dart extending to the hip level");
}

}

int main() {
    checkModelingHistory();
    checkBodiceAndSleeve();
    checkLowerBlocks();
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
    ParametricDraft numeric;
    numeric.measurements.append({"height", 50});
    numeric.points.append({"A", "hypot(abs(-3), sqrt(16)) + min(height, 7) - max(2, -1)", "0"});
    PatternDocument calculated;
    check(calculated.setParametricDraft(numeric, &error) && calculated.entities().front().a.x() == 10,
          "Evaluate geometric formula functions and nested expressions");
    numeric.points[0].x = "sqrt(-1)";
    check(!calculated.setParametricDraft(numeric, &error) && error.contains("non-negative")
          && calculated.entities().front().a.x() == 10, "Reject an invalid square root transactionally");
    error.clear();
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
    check(json.value("version").toInt() == 8, "Encode reference size data in the version 8 payload");
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

    auto gradedDocument = document;
    auto& production = gradedDocument.production();
    production.sizes = {{"L", {{"chest", 520}, {"unused", 123}}}, {"S", {{"height", 60}}}, {"Base copy", {}}};
    production.rules = {{"L", 0, {120, 75}, {5, 3}}, {"L", 0, {60, 85}, {2, 0}}};
    QVector<qsizetype> sources;
    const auto large = gradedPattern(gradedDocument, "L", &sources);
    check(large.entities().size() == 10 && sources.size() == 10 && sources[8] == 8,
          "Keep the source object mapping in the graded drawing");
    check(large.entities()[0].a == QPointF(10, 20) && large.entities()[6].b == QPointF(140, 0)
          && large.entities()[7].b == QPointF(145, 78) && large.entities()[8].a == QPointF(145, 78)
          && large.entities()[8].b == QPointF(0, 75) && large.entities()[8].control == QPointF(72, 85),
          "Recompute all coordinates and apply legacy offsets using the base points");
    check(large.pieces()[0].size == "L" && large.entities()[8].draftName.isEmpty()
          && analyzePiece(large, 0).error == ContourError::None,
          "The graded result is a closed contour with independent geometry");
    check(gradedPattern(gradedDocument, "S").entities()[8].control == QPointF(60, 70)
          && gradedPattern(gradedDocument, "Base copy").entities()[8].control == QPointF(60, 85)
          && gradedDocument.entities()[8].control == QPointF(60, 85),
          "Evaluate each size independently and fall back to omitted base measurements");

    auto layout = production;
    layout.fabricWidth = 139;
    layout.placements = {{0, "L", {0, 0}}};
    check(arrangeFabric(gradedDocument, layout).error == FabricLayoutError::TooWide,
          "Fabric layout evaluates the selected size before checking the cutting width");

    auto invalidForSize = changed;
    invalidForSize.parameters[0].formula = "800 / (chest - 520)";
    check(!gradedDocument.setParametricDraft(invalidForSize, &error) && error.contains("Size 'L'")
          && gradedDocument.parametricDraft() == changed,
          "Reject a draft edit atomically if any size fails");

    check(gradedDocument.save(filename, &error), "Save a document with parametrically graded sizes");
    auto gradedJson = readProject(filename);
    check(gradedJson.value("version").toInt() == 9 && loaded.load(filename, &error)
          && loaded.production().sizes[0].measurements.value("chest") == 520
          && gradedPattern(loaded, "L").entities()[8].control == QPointF(72, 85),
          "Round trip version 9 size measurements, formulas and offsets");
    auto legacy = gradedJson;
    legacy["version"] = 8;
    writeProject(filename, legacy);
    check(loaded.load(filename, &error) && !loaded.production().useDraftMeasurements
          && gradedPattern(loaded, "L").entities()[8].control == QPointF(62, 85),
          "Keep version 8 measurement rows as reference data and apply its offsets");
    auto enabled = loaded.production();
    enabled.useDraftMeasurements = true;
    check(loaded.validateParametricSizes(enabled, &error), "Validate enabling size formulas in a legacy project");

    auto brokenDraft = gradedJson.value("parametricDraft").toObject();
    auto brokenParameters = brokenDraft.value("parameters").toArray();
    auto brokenWidth = brokenParameters[0].toObject();
    brokenWidth["formula"] = "800 / (chest - 520)";
    brokenParameters[0] = brokenWidth;
    brokenDraft["parameters"] = brokenParameters;
    gradedJson["parametricDraft"] = brokenDraft;
    writeProject(filename, gradedJson);
    check(!loaded.load(filename, &error) && error.contains("Size 'L'")
          && loaded.parametricDraft() == changed && !loaded.production().useDraftMeasurements,
          "Reject a size-specific failure when loading version 9 without changing the current document");

    const SkirtMeasurements measurements{1000, 600, 80, 400, 35};
    for (const auto style : {SkirtStyle::Gathered, SkirtStyle::ALine}) {
        const auto skirt = draftPullOnSkirt(style, measurements, &error);
        const double width = style == SkirtStyle::ALine ? 740 : 540;
        check(skirt && skirt->pieces()[0].cutCount == 2, "Create a skirt panel with a cut count of two");
        const auto panel = analyzePiece(*skirt, 0);
        check(panel.error == ContourError::None && !panel.allowance.isEmpty()
              && panel.bounds.width() == width && panel.bounds.height() == 635,
              "Build a closed skirt contour to the entered dimensions");
        check(skirt->entities().last().layer == Layer::Construction,
              "Keep the casing fold outside the cutting contour");
    }
    check(!draftPullOnSkirt(SkirtStyle::Gathered, {1000, 20, 80, 0, 35}, &error),
          "Reject a casing deeper than the finished skirt");

    check(document.setParametricDraft({}) && document.entities().size() == 1, "Remove generated objects without deleting manual drawing");
    check(document.save(filename, &error) && readProject(filename).value("version").toInt() == 7,
          "Encode projects without a draft in the version 7 payload");
    check(loaded.load(filename, &error) && loaded.parametricDraft() == ParametricDraft{}
          && loaded.entities().size() == 1, "Loading version 7 clears the previous parametric data");
}
