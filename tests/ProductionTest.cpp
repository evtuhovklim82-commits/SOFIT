#include "Production.h"
#include "PatternDxf.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace opc;

    PatternDocument base;
    base.pieces().append(Piece{"Front", 5});
    base.pieces().append(Piece{"Facing", 0});
    base.pieces()[0].grainAngle = 0;
    base.insert(0, {Shape::Rectangle, {0, 0}, {100, 50}, {}, 0});
    base.insert(1, {Shape::Line, {100, 50}, {50, 25}, {}, 0, Layer::Construction});
    base.insert(2, {Shape::Curve, {0, 0}, {100, 0}, {50, 10}, 0, Layer::Construction});
    base.insert(3, {Shape::Rectangle, {0, 0}, {100, 50}, {}, 1});
    auto& production = base.production();
    production.sizes = {{"M", {{"Chest", 960}}}, {"L", {{"Chest", 1020}, {"Waist", 840}}}};
    production.rules = {{"L", 0, {100, 50}, {20, 0}}, {"L", 0, {50, 10}, {0, 8}}};
    production.fabricWidth = 140;
    production.placements = {{0, "L", {10, 25}, true}, {1, {}, {60, 150}}};
    check(gradingPoints(base, 0).size() == 6, "Enumerate distinct piece control points");

    QVector<qsizetype> indices;
    const auto graded = gradedPattern(base, "L", &indices);
    check(graded.entities().size() == 7 && indices == QVector<qsizetype>{0, 0, 0, 0, 1, 2, 3},
          "Split an edited rectangle into connected edges and track source visibility");
    check(graded.entities()[1].b == QPointF(120, 50) && graded.entities()[2].a == QPointF(120, 50)
          && graded.entities()[4].a == QPointF(120, 50), "Move every coincident point on the piece");
    check(graded.entities()[5].control == QPointF(50, 18) && graded.entities()[5].b == QPointF(100, 0),
          "Move a quadratic handle independently of the endpoints");
    check(graded.entities().last().b == QPointF(100, 50)
          && base.entities().front().b == QPointF(100, 50)
          && gradedPattern(base, "M").entities().front().shape == Shape::Rectangle,
          "Keep the base, other pieces, and unrelated sizes intact");
    const auto geometry = analyzePiece(graded, 0);
    check(geometry.error == ContourError::None && !geometry.allowance.isEmpty()
          && geometry.bounds.width() == 120 && graded.pieces()[0].size == "L",
          "Rebuild the graded contour, allowance, and size label");

    const auto cut = fabricPiece(graded, production.placements.front());
    check(!cut.cut.isEmpty() && cut.grain.dx() == 0 && cut.grain.dy() < 0
          && qAbs(cut.cut.boundingRect().width() - 60) < 1e-7
          && cut.cut.boundingRect().height() > 130,
          "Rotate the cutting outline and seam allowance along the grain");
    const QPolygonF square(QRectF(0, 0, 40, 50));
    auto usage = fabricUsage({square, square.translated(40, 0), square.translated(70, 30), {}}, 100);
    check(usage.length == 80 && usage.outside == 1 && usage.overlaps == 1 && usage.invalid == 1,
          "Measure length and distinguish touching, overlap, outside and invalid placements");
    usage = fabricUsage({cut.cut.translated(10, 25)}, 140);
    check(qAbs(usage.length - cut.cut.boundingRect().height() - 25) < 1e-7 && usage.outside == 0,
          "Measure usage from fabric origin including placement offset");

    check(!usage.utilization(0) && usage.cutArea > 0, "Calculate cutting area and reject unusable totals");
    const auto full = fabricUsage({square, square.translated(40, 0)}, 80);
    check(full.cutArea == 4000 && full.utilization(80) == 1,
          "Compute utilization from polygon area and count touching edges as valid");
    check(!fabricUsage({square, square.translated(1, 1)}, 80).utilization(80),
          "Suppress utilization for invalid arrangements");

    const auto tooClose = fabricUsage({square, square.translated(44, 0)}, 100, 5);
    const auto spaced = fabricUsage({square, square.translated(45, 0)}, 100, 5);
    check(tooClose.overlaps == 1 && spaced.overlaps == 0 && spaced.cutArea == full.cutArea
          && spaced.outside == 0, "Check clearance without changing cut area or the fabric boundary");

    auto batch = production;
    batch.allowHalfTurns = false;
    batch.placements.append({0, "M", {200, 400}, true});
    const auto automatic = arrangeFabric(base, batch);
    check(automatic.error == FabricLayoutError::None && automatic.placements.size() == 3,
          "Arrange mixed sizes including the base");
    QVector<QPolygonF> arrangedCuts;
    for (qsizetype i = 0; i < automatic.placements.size(); ++i) {
        const auto& copy = automatic.placements[i];
        check(copy.piece == batch.placements[i].piece && copy.size == batch.placements[i].size
              && !copy.reversed, "Preserve order, piece and size and respect directional fabric");
        arrangedCuts.append(fabricPiece(gradedPattern(base, copy.size), copy).cut.translated(copy.position));
    }
    const auto arranged = fabricUsage(arrangedCuts, batch.fabricWidth);
    check(!arranged.invalid && !arranged.outside && !arranged.overlaps && arranged.length < 225,
          "Verify that the mixed marker fits and is shorter than stacking");
    auto repeated = batch;
    repeated.placements = arrangeFabric(base, batch).placements;
    auto expected = batch;
    expected.placements = automatic.placements;
    check(saveProduction(repeated) == saveProduction(expected), "Return identical results for identical requests");
    auto buffered = batch;
    buffered.pieceClearanceMm = 12;
    auto bufferedResult = arrangeFabric(base, buffered);
    check(bufferedResult.error == FabricLayoutError::None, "Arrange a marker with a piece clearance");
    buffered.placements = std::move(bufferedResult.placements);
    check(cuttingReport(base, buffered).canExport(buffered.fabricWidth), "Verify clearance before export");
    buffered.pieceClearanceMm = -1;
    check(arrangeFabric(base, buffered).error == FabricLayoutError::InvalidClearance,
          "Reject invalid clearances before attempting to arrange");
    batch.fabricWidth = 59;
    const auto tooWide = arrangeFabric(base, batch);
    check(tooWide.error == FabricLayoutError::TooWide && tooWide.failedCopy == 0
          && tooWide.placements.isEmpty(), "Fail atomically when an allowance exceeds the fabric width");

    PatternDocument triangle;
    triangle.pieces().append(Piece{"Triangle", 0});
    triangle.insert(0, {Shape::Line, {0, 0}, {80, 0}, {}, 0});
    triangle.insert(1, {Shape::Line, {80, 0}, {0, 80}, {}, 0});
    triangle.insert(2, {Shape::Line, {0, 80}, {0, 0}, {}, 0});
    auto& triangleBatch = triangle.production();
    triangleBatch.fabricWidth = 80;
    triangleBatch.placements = {{0, {}, {}}, {0, {}, {}}};
    const auto nested = arrangeFabric(triangle, triangleBatch);
    check(nested.error == FabricLayoutError::None && nested.placements[1].reversed
          && nested.placements[1].position == QPointF{}, "Nest complementary half-turns at full fabric width");
    triangleBatch.allowHalfTurns = false;
    const auto directional = arrangeFabric(triangle, triangleBatch);
    check(directional.error == FabricLayoutError::None && !directional.placements[1].reversed
          && directional.placements[1].position.y() >= 80, "Stack when half-turns are disabled");
    triangle.pieces().append(Piece{"Open", 0});
    triangleBatch.placements.append({1, {}, {}});
    check(arrangeFabric(triangle, triangleBatch).error == FabricLayoutError::InvalidContour
          && arrangeFabric(triangle, triangleBatch).placements.isEmpty(), "Reject incomplete contours atomically");

    triangleBatch.allowHalfTurns = true;
    triangleBatch.fabricWidth = 160;
    triangleBatch.directionalFabric = true;
    triangleBatch.placements = {
        {0, {}, {0, 90}, true, CutHand::Left},
        {0, {}, {80, 90}, false, CutHand::Right, FoldEdge::None, true},
        {0, {}, {4, 0}, false, CutHand::Single, FoldEdge::Left}
    };
    const auto left = fabricPiece(triangle, {0, {}, {}, false, CutHand::Left});
    const auto right = fabricPiece(triangle, triangleBatch.placements[1]);
    check(left.cut.size() == right.cut.size() && !left.cut.isEmpty(), "Build both hands");
    for (qsizetype i = 0; i < left.cut.size(); ++i)
        check(left.cut[i].x() + right.cut[i].x() == 80 && left.cut[i].y() == right.cut[i].y(),
              "Reflect an asymmetric contour across the grain");
    auto report = cuttingReport(triangle, triangleBatch);
    check(report.orientationViolations == 1 && report.foldViolations == 1 && report.rows.size() == 1
          && report.rows[0].left == 1 && report.rows[0].right == 1 && report.rows[0].onFold == 1,
          "Group by piece and size and report rotation and fold violations");
    const auto kit = cuttingKitRows(triangle, report);
    check(kit.size() == 2 && kit[0].copies() == 3 && kit[1].piece == 1 && kit[1].copies() == 0,
          "Include pieces entirely missing from the marker and count a fold as one");
    const auto emptyKit = cuttingKitRows(triangle, {});
    check(emptyKit.size() == 2 && emptyKit[0].size.isEmpty() && emptyKit[0].copies() == 0,
          "Audit the base kit when the marker is empty");
    const auto constrained = arrangeFabric(triangle, triangleBatch);
    check(constrained.error == FabricLayoutError::None && constrained.placements.size() == 3
          && constrained.placements[0].hand == CutHand::Left
          && constrained.placements[1].hand == CutHand::Right
          && constrained.placements[2].position.x() == 0, "Arrange hands and pin fold to left boundary");
    auto onFabric = triangleBatch;
    onFabric.placements = constrained.placements;
    report = cuttingReport(triangle, onFabric);
    check(!report.orientationViolations && !report.foldViolations && !report.usage.invalid
          && !report.usage.outside && !report.usage.overlaps, "Verify actual arranged geometry and constraints");

    auto locked = onFabric;
    locked.fabricWidth = 80;
    locked.directionalFabric = false;
    locked.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::None, true},
                         {0, {}, {}, true, CutHand::Single, FoldEdge::None, true}};
    const auto fixed = arrangeFabric(triangle, locked);
    check(fixed.error == FabricLayoutError::None && !fixed.placements[0].reversed && !fixed.placements[1].reversed
          && cuttingReport(triangle, ProductionData{.fabricWidth = 80, .placements = fixed.placements}).usage.length > 159,
          "Do not turn a fixed copy even if turning would allow nesting");

    auto rightFold = onFabric;
    rightFold.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::Right}};
    check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,
          "Reject a fold side with no straight outer edge");
    rightFold.placements[0].piece = 1;
    triangle.insert(triangle.entities().size(), {Shape::Rectangle, {0, 0}, {60, 20}, {}, 1});
    const auto folded = arrangeFabric(triangle, rightFold);
    check(folded.error == FabricLayoutError::None && folded.placements[0].position.x() == 100,
          "Place a right fold at working width minus cut width");
    triangle.pieces()[1].allowanceMm = 5;
    check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,
          "Reject added uniform allowance on a fold");
    triangle.pieces()[1].allowanceMm = 0;

    production.allowHalfTurns = false;
    QTemporaryDir temp;
    check(temp.isValid(), "Create temporary directory");
    const auto file = temp.filePath("production.sofit");
    check(base.save(file), "Save version 7 production data");
    PatternDocument loaded;
    check(loaded.load(file) && saveProduction(loaded.production()) == saveProduction(production)
          && loaded.pieces() == base.pieces(), "Round-trip sizes, rules, placement, width, and piece metadata");
    auto json = saveProduction(production);
    auto version6 = json;
    version6.remove("allowHalfTurns");
    version6.remove("directionalFabric");
    QJsonArray oldPlacements;
    for (const auto value : version6.value("placements").toArray()) {
        auto row = value.toObject();
        row.remove("hand");
        row.remove("fold");
        row.remove("fixedOrientation");
        oldPlacements.append(row);
    }
    version6["placements"] = oldPlacements;
    ProductionData legacyProduction;
    check(loadProduction(version6, 2, legacyProduction) && legacyProduction.allowHalfTurns,
          "Keep legacy rotation behavior for version 6 files without the optional setting");
    check(!legacyProduction.directionalFabric && legacyProduction.placements.front().reversed
          && legacyProduction.placements.front().hand == CutHand::Single
          && legacyProduction.placements.front().fold == FoldEdge::None
          && !legacyProduction.placements.front().fixedOrientation, "Use compatible defaults for legacy copies");
    QFile saved(file);
    check(saved.open(QIODevice::ReadOnly), "Read saved fixture");
    const auto envelope = QJsonDocument::fromJson(saved.readAll()).object();
    auto legacyRoot = QJsonDocument::fromJson(envelope.value("payload").toString().toUtf8()).object();
    saved.close();
    legacyRoot["version"] = 6;
    legacyRoot["production"] = version6;
    QFile oldFile(temp.filePath("version6.sofit"));
    check(oldFile.open(QIODevice::WriteOnly), "Create version 6 project");
    oldFile.write(QJsonDocument(legacyRoot).toJson());
    oldFile.close();
    PatternDocument openedLegacy;
    check(openedLegacy.load(oldFile.fileName())
          && saveProduction(openedLegacy.production()) == saveProduction(legacyProduction),
          "Open a complete version 6 project");

    rightFold.placements = folded.placements;
    QString dxfError;
    const auto dxfPath = temp.filePath("marker.dxf");
    check(exportFabricDxf(dxfPath, triangle, rightFold, dxfError), "Export a valid folded layout");
    QFile dxf(dxfPath);
    check(dxf.open(QIODevice::ReadOnly), "Read folded-layout DXF");
    const auto dxfBytes = dxf.readAll();
    check(dxfBytes.contains("8\nCUT\n100\nAcDbPolyline\n90\n4\n70\n0\n")
          && dxfBytes.contains("8\nFOLD\n100\nAcDbLine\n10\n160\n"),
          "Omit the fold side from the open CUT polyline and mark it separately in millimeters");
    rightFold.placements[0].position.rx() -= 1;
    check(!exportFabricDxf(temp.filePath("off-fold.dxf"), triangle, rightFold, dxfError),
          "Reject off-fold paths before writing a DXF");

    const auto marker = temp.filePath("marker.sofit");
    check(triangle.save(marker), "Save new layout settings");
    PatternDocument reopenedMarker;
    check(reopenedMarker.load(marker)
          && saveProduction(reopenedMarker.production()) == saveProduction(triangleBatch),
          "Round-trip copied hand, fold, fixed rotation and directional material");
    auto malformed = triangleBatch;
    malformed.placements[0].fold = FoldEdge::Left;
    check(!loadProduction(saveProduction(malformed), 2, reopenedMarker.production())
          && saveProduction(reopenedMarker.production()) == saveProduction(triangleBatch),
          "Reject incompatible hand and fold modes atomically");

    ProductionData repeat;
    repeat.fabricWidth = 160;
    repeat.pieceClearanceMm = 4;
    repeat.fabricRepeatMm = {37, 29};
    repeat.placements = {{1, {}, {}}, {1, {}, {}}, {1, {}, {}}};
    auto registeredLayout = arrangeFabric(triangle, repeat);
    check(registeredLayout.error == FabricLayoutError::None && registeredLayout.placements.size() == 3,
          "Arrange copies with a two-axis repeat and clearance");
    repeat.placements = registeredLayout.placements;
    const auto registered = cuttingReport(triangle, repeat);
    check(registered.canExport(repeat.fabricWidth) && registered.repeatViolations == 0,
          "Verify both repeat axes before marker export");
    check(exportFabricDxf(temp.filePath("registered.dxf"), triangle, repeat, dxfError),
          "Export a registered marker as DXF");
    repeat.placements[1].position.ry() += 0.5;
    check(cuttingReport(triangle, repeat).repeatViolations == 1
          && !exportFabricDxf(temp.filePath("off-repeat.dxf"), triangle, repeat, dxfError),
          "Reject a manually moved copy that misses its repeat");
    triangle.production() = repeat;
    check(triangle.save(marker) && reopenedMarker.load(marker)
          && reopenedMarker.production().fabricRepeatMm == QPointF(37, 29),
          "Round-trip both repeat dimensions in the current schema");
    repeat.fabricRepeatMm.setX(-1);
    check(arrangeFabric(triangle, repeat).error == FabricLayoutError::InvalidRepeat
          && !loadProduction(saveProduction(repeat), 2, reopenedMarker.production()),
          "Reject an invalid repeat on arrangement and on load");

    auto duplicate = production;
    duplicate.rules.append(duplicate.rules.front());
    check(!loadProduction(saveProduction(duplicate), 2, loaded.production())
          && saveProduction(loaded.production()) == json, "Reject duplicate rules without changing existing data");
    loaded.production().removePiece(0);
    check(loaded.production().rules.isEmpty() && loaded.production().placements.size() == 1
          && loaded.production().placements.front().piece == 0, "Remove and remap piece references");

    const auto legacy = temp.filePath("legacy.sofit");
    QFile output(legacy);
    check(output.open(QIODevice::WriteOnly), "Open version 5 fixture");
    output.write(R"({"format":"SOFIT","version":5,"layerVisibility":[true,true,true],"objects":[],
        "pieces":[{"name":"Front","allowanceMm":5,"cutCount":2,"size":"M","showLabel":true,
                   "grainAngle":0,"notches":[10]}]})");
    output.close();
    check(loaded.load(legacy) && loaded.production().sizes.isEmpty() && loaded.production().placements.isEmpty()
          && loaded.pieces().front().cutCount == 2 && loaded.pieces().front().notches == QVector<double>{10},
          "Open a version 5 file and keep its markings");
}
