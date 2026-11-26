#include "Production.h"
#include "PatternDxf.h"
#include "PatternPdf.h"

#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
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
    QGuiApplication app(argc, argv);
    using namespace opc;
    std::cerr << std::unitbuf;

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

    PatternDocument markerComparison;
    auto& markerData = markerComparison.production();
    markerData.fabricWidth = 1450;
    markerData.pieceClearanceMm = 5;
    markerData.directionalFabric = true;
    markerData.fabricFlaws = {{100, 0, 50, 100}};
    const QVector<QPointF> dimensions{{351, 242}, {472, 1023}, {595, 567}, {805, 1012},
        {682, 541}, {824, 399}, {340, 636}, {489, 691}, {684, 761}, {270, 694}, {805, 345}, {572, 996}};
    for (qsizetype i = 0; i < dimensions.size(); ++i) {
        markerComparison.pieces().append({QString::number(i), 0, 1, {}, true, 90});
        markerComparison.insert(i, {Shape::Rectangle, {}, dimensions[i], {}, i});
        markerData.placements.append({i, {}, {}});
    }
    const auto improved = arrangeFabric(markerComparison, markerData);
    check(improved.error == FabricLayoutError::None && improved.baselineLengthMm && improved.optimizedLengthMm
          && *improved.baselineLengthMm - *improved.optimizedLengthMm > 60,
          "Improve on the previous three searches with the same directional fabric, clearance and flaw");
    markerData.placements = improved.placements;
    const auto improvedReport = cuttingReport(markerComparison, markerData);
    check(improvedReport.canExport(markerData.fabricWidth)
          && qAbs(improvedReport.usage.length - *improved.optimizedLengthMm) < 1e-6,
          "Verify the optimized cutting length and constraints independently");
    std::cout << "12-copy marker: " << *improved.baselineLengthMm << " -> "
              << *improved.optimizedLengthMm << " mm\n";

    std::cerr << "TRACE PatternDocument triangle;\n";
    PatternDocument triangle;
    std::cerr << "TRACE triangle.pieces().append(Piece{\n";
    triangle.pieces().append(Piece{"Triangle", 0});
    std::cerr << "TRACE triangle.insert(0, {Shape::Line, {0, 0}, {80, 0}, {}, 0});\n";
    triangle.insert(0, {Shape::Line, {0, 0}, {80, 0}, {}, 0});
    std::cerr << "TRACE triangle.insert(1, {Shape::Line, {80, 0}, {0, 80}, {}, 0});\n";
    triangle.insert(1, {Shape::Line, {80, 0}, {0, 80}, {}, 0});
    std::cerr << "TRACE triangle.insert(2, {Shape::Line, {0, 80}, {0, 0}, {}, 0});\n";
    triangle.insert(2, {Shape::Line, {0, 80}, {0, 0}, {}, 0});
    std::cerr << "TRACE auto& triangleBatch = triangle.production();\n";
    auto& triangleBatch = triangle.production();
    std::cerr << "TRACE triangleBatch.fabricWidth = 80;\n";
    triangleBatch.fabricWidth = 80;
    std::cerr << "TRACE triangleBatch.placements = {{0, {}, {}}, {0, {}, {}}};\n";
    triangleBatch.placements = {{0, {}, {}}, {0, {}, {}}};
    std::cerr << "TRACE const auto nested = arrangeFabric(triangle, triangleBatch);\n";
    const auto nested = arrangeFabric(triangle, triangleBatch);
    std::cerr << "TRACE check(nested.error == FabricLayoutError::None && nested.placements[1].reversed\n";
    check(nested.error == FabricLayoutError::None && nested.placements[1].reversed
          && nested.placements[1].position == QPointF{}, "Nest complementary half-turns at full fabric width");
    std::cerr << "TRACE triangleBatch.allowHalfTurns = false;\n";
    triangleBatch.allowHalfTurns = false;
    std::cerr << "TRACE const auto directional = arrangeFabric(triangle, triangleBatch);\n";
    const auto directional = arrangeFabric(triangle, triangleBatch);
    std::cerr << "TRACE check(directional.error == FabricLayoutError::None && !directional.placements[1].reversed\n";
    check(directional.error == FabricLayoutError::None && !directional.placements[1].reversed
          && directional.placements[1].position.y() >= 80, "Stack when half-turns are disabled");
    std::cerr << "TRACE triangle.pieces().append(Piece{\n";
    triangle.pieces().append(Piece{"Open", 0});
    std::cerr << "TRACE triangleBatch.placements.append({1, {}, {}});\n";
    triangleBatch.placements.append({1, {}, {}});
    std::cerr << "TRACE check(arrangeFabric(triangle, triangleBatch).error == FabricLayoutError::InvalidContour\n";
    check(arrangeFabric(triangle, triangleBatch).error == FabricLayoutError::InvalidContour
          && arrangeFabric(triangle, triangleBatch).placements.isEmpty(), "Reject incomplete contours atomically");

    std::cerr << "TRACE triangleBatch.allowHalfTurns = true;\n";
    triangleBatch.allowHalfTurns = true;
    std::cerr << "TRACE triangleBatch.fabricWidth = 160;\n";
    triangleBatch.fabricWidth = 160;
    std::cerr << "TRACE triangleBatch.directionalFabric = true;\n";
    triangleBatch.directionalFabric = true;
    std::cerr << "TRACE triangleBatch.placements = {\n";
    triangleBatch.placements = {
        {0, {}, {0, 90}, true, CutHand::Left},
        {0, {}, {80, 90}, false, CutHand::Right, FoldEdge::None, true},
        {0, {}, {4, 0}, false, CutHand::Single, FoldEdge::Left}
    };
    std::cerr << "TRACE const auto left = fabricPiece(triangle, {0, {}, {}, false, CutHand::Left});\n";
    const auto left = fabricPiece(triangle, {0, {}, {}, false, CutHand::Left});
    std::cerr << "TRACE const auto right = fabricPiece(triangle, triangleBatch.placements[1]);\n";
    const auto right = fabricPiece(triangle, triangleBatch.placements[1]);
    std::cerr << "TRACE check(left.cut.size() == right.cut.size() && !left.cut.isEmpty(),\n";
    check(left.cut.size() == right.cut.size() && !left.cut.isEmpty(), "Build both hands");
    std::cerr << "TRACE for (qsizetype i = 0; i < left.cut.size(); ++i)\n";
    for (qsizetype i = 0; i < left.cut.size(); ++i)
        check(left.cut[i].x() + right.cut[i].x() == 80 && left.cut[i].y() == right.cut[i].y(),
              "Reflect an asymmetric contour across the grain");
    std::cerr << "TRACE auto report = cuttingReport(triangle, triangleBatch);\n";
    auto report = cuttingReport(triangle, triangleBatch);
    std::cerr << "TRACE check(report.orientationViolations == 1 && report.foldViolations == 1 && report.rows.size() == 1\n";
    check(report.orientationViolations == 1 && report.foldViolations == 1 && report.rows.size() == 1
          && report.rows[0].left == 1 && report.rows[0].right == 1 && report.rows[0].onFold == 1,
          "Group by piece and size and report rotation and fold violations");
    std::cerr << "TRACE const auto kit = cuttingKitRows(triangle, report);\n";
    const auto kit = cuttingKitRows(triangle, report);
    std::cerr << "TRACE check(kit.size() == 2 && kit[0].copies() == 3 && kit[1].piece == 1 && kit[1].copies() == 0,\n";
    check(kit.size() == 2 && kit[0].copies() == 3 && kit[1].piece == 1 && kit[1].copies() == 0,
          "Include pieces entirely missing from the marker and count a fold as one");
    std::cerr << "TRACE const auto emptyKit = cuttingKitRows(triangle, {});\n";
    const auto emptyKit = cuttingKitRows(triangle, {});
    std::cerr << "TRACE check(emptyKit.size() == 2 && emptyKit[0].size.isEmpty() && emptyKit[0].copies() == 0,\n";
    check(emptyKit.size() == 2 && emptyKit[0].size.isEmpty() && emptyKit[0].copies() == 0,
          "Audit the base kit when the marker is empty");
    std::cerr << "TRACE const auto constrained = arrangeFabric(triangle, triangleBatch);\n";
    const auto constrained = arrangeFabric(triangle, triangleBatch);
    std::cerr << "TRACE check(constrained.error == FabricLayoutError::None && constrained.placements.size() == 3\n";
    check(constrained.error == FabricLayoutError::None && constrained.placements.size() == 3
          && constrained.placements[0].hand == CutHand::Left
          && constrained.placements[1].hand == CutHand::Right
          && constrained.placements[2].position.x() == 0, "Arrange hands and pin fold to left boundary");
    std::cerr << "TRACE auto onFabric = triangleBatch;\n";
    auto onFabric = triangleBatch;
    std::cerr << "TRACE onFabric.placements = constrained.placements;\n";
    onFabric.placements = constrained.placements;
    std::cerr << "TRACE report = cuttingReport(triangle, onFabric);\n";
    report = cuttingReport(triangle, onFabric);
    std::cerr << "TRACE check(!report.orientationViolations && !report.foldViolations && !report.usage.invalid\n";
    check(!report.orientationViolations && !report.foldViolations && !report.usage.invalid
          && !report.usage.outside && !report.usage.overlaps, "Verify actual arranged geometry and constraints");

    std::cerr << "TRACE auto locked = onFabric;\n";
    auto locked = onFabric;
    std::cerr << "TRACE locked.fabricWidth = 80;\n";
    locked.fabricWidth = 80;
    std::cerr << "TRACE locked.directionalFabric = false;\n";
    locked.directionalFabric = false;
    std::cerr << "TRACE locked.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::None, true},\n";
    locked.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::None, true},
                         {0, {}, {}, true, CutHand::Single, FoldEdge::None, true}};
    std::cerr << "TRACE const auto fixed = arrangeFabric(triangle, locked);\n";
    const auto fixed = arrangeFabric(triangle, locked);
    std::cerr << "TRACE check(fixed.error == FabricLayoutError::None && !fixed.placements[0].reversed && !fixed.placements[1].reversed\n";
    check(fixed.error == FabricLayoutError::None && !fixed.placements[0].reversed && !fixed.placements[1].reversed
          && cuttingReport(triangle, ProductionData{.fabricWidth = 80, .placements = fixed.placements}).usage.length > 159,
          "Do not turn a fixed copy even if turning would allow nesting");

    std::cerr << "TRACE auto rightFold = onFabric;\n";
    auto rightFold = onFabric;
    std::cerr << "TRACE rightFold.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::Right}};\n";
    rightFold.placements = {{0, {}, {}, false, CutHand::Single, FoldEdge::Right}};
    std::cerr << "TRACE check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,\n";
    check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,
          "Reject a fold side with no straight outer edge");
    std::cerr << "TRACE rightFold.placements[0].piece = 1;\n";
    rightFold.placements[0].piece = 1;
    std::cerr << "TRACE triangle.insert(triangle.entities().size(), {Shape::Rectangle, {0, 0}, {60, 20}, {}, 1});\n";
    triangle.insert(triangle.entities().size(), {Shape::Rectangle, {0, 0}, {60, 20}, {}, 1});
    std::cerr << "TRACE const auto folded = arrangeFabric(triangle, rightFold);\n";
    const auto folded = arrangeFabric(triangle, rightFold);
    std::cerr << "TRACE check(folded.error == FabricLayoutError::None && folded.placements[0].position.x() == 100,\n";
    check(folded.error == FabricLayoutError::None && folded.placements[0].position.x() == 100,
          "Place a right fold at working width minus cut width");
    std::cerr << "TRACE triangle.pieces()[1].allowanceMm = 5;\n";
    triangle.pieces()[1].allowanceMm = 5;
    std::cerr << "TRACE check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,\n";
    check(arrangeFabric(triangle, rightFold).error == FabricLayoutError::InvalidContour,
          "Reject added uniform allowance on a fold");
    std::cerr << "TRACE triangle.pieces()[1].allowanceMm = 0;\n";
    triangle.pieces()[1].allowanceMm = 0;

    std::cerr << "TRACE production.allowHalfTurns = false;\n";
    production.allowHalfTurns = false;
    std::cerr << "TRACE QTemporaryDir temp;\n";
    QTemporaryDir temp;
    std::cerr << "TRACE check(temp.isValid(),\n";
    check(temp.isValid(), "Create temporary directory");
    std::cerr << "TRACE const auto file = temp.filePath(\n";
    const auto file = temp.filePath("production.sofit");
    std::cerr << "TRACE check(base.save(file),\n";
    check(base.save(file), "Save version 7 production data");
    std::cerr << "TRACE PatternDocument loaded;\n";
    PatternDocument loaded;
    std::cerr << "TRACE check(loaded.load(file) && saveProduction(loaded.production()) == saveProduction(production)\n";
    check(loaded.load(file) && saveProduction(loaded.production()) == saveProduction(production)
          && loaded.pieces() == base.pieces(), "Round-trip sizes, rules, placement, width, and piece metadata");
    std::cerr << "TRACE auto json = saveProduction(production);\n";
    auto json = saveProduction(production);
    std::cerr << "TRACE auto version6 = json;\n";
    auto version6 = json;
    std::cerr << "TRACE version6.remove(\n";
    version6.remove("allowHalfTurns");
    std::cerr << "TRACE version6.remove(\n";
    version6.remove("directionalFabric");
    std::cerr << "TRACE QJsonArray oldPlacements;\n";
    QJsonArray oldPlacements;
    std::cerr << "TRACE for (const auto value : version6.value(\n";
    for (const auto value : version6.value("placements").toArray()) {
        auto row = value.toObject();
        row.remove("hand");
        row.remove("fold");
        row.remove("fixedOrientation");
        oldPlacements.append(row);
    }
    std::cerr << "TRACE version6[\n";
    version6["placements"] = oldPlacements;
    std::cerr << "TRACE ProductionData legacyProduction;\n";
    ProductionData legacyProduction;
    std::cerr << "TRACE check(loadProduction(version6, 2, legacyProduction) && legacyProduction.allowHalfTurns,\n";
    check(loadProduction(version6, 2, legacyProduction) && legacyProduction.allowHalfTurns,
          "Keep legacy rotation behavior for version 6 files without the optional setting");
    std::cerr << "TRACE check(!legacyProduction.directionalFabric && legacyProduction.placements.front().reversed\n";
    check(!legacyProduction.directionalFabric && legacyProduction.placements.front().reversed
          && legacyProduction.placements.front().hand == CutHand::Single
          && legacyProduction.placements.front().fold == FoldEdge::None
          && !legacyProduction.placements.front().fixedOrientation, "Use compatible defaults for legacy copies");
    std::cerr << "TRACE QFile saved(file);\n";
    QFile saved(file);
    std::cerr << "TRACE check(saved.open(QIODevice::ReadOnly),\n";
    check(saved.open(QIODevice::ReadOnly), "Read saved fixture");
    std::cerr << "TRACE const auto envelope = QJsonDocument::fromJson(saved.readAll()).object();\n";
    const auto envelope = QJsonDocument::fromJson(saved.readAll()).object();
    std::cerr << "TRACE auto legacyRoot = QJsonDocument::fromJson(envelope.value(\n";
    auto legacyRoot = QJsonDocument::fromJson(envelope.value("payload").toString().toUtf8()).object();
    std::cerr << "TRACE saved.close();\n";
    saved.close();
    std::cerr << "TRACE legacyRoot[\n";
    legacyRoot["version"] = 6;
    std::cerr << "TRACE legacyRoot[\n";
    legacyRoot["production"] = version6;
    std::cerr << "TRACE QFile oldFile(temp.filePath(\n";
    QFile oldFile(temp.filePath("version6.sofit"));
    std::cerr << "TRACE check(oldFile.open(QIODevice::WriteOnly),\n";
    check(oldFile.open(QIODevice::WriteOnly), "Create version 6 project");
    std::cerr << "TRACE oldFile.write(QJsonDocument(legacyRoot).toJson());\n";
    oldFile.write(QJsonDocument(legacyRoot).toJson());
    std::cerr << "TRACE oldFile.close();\n";
    oldFile.close();
    std::cerr << "TRACE PatternDocument openedLegacy;\n";
    PatternDocument openedLegacy;
    std::cerr << "TRACE check(openedLegacy.load(oldFile.fileName())\n";
    check(openedLegacy.load(oldFile.fileName())
          && saveProduction(openedLegacy.production()) == saveProduction(legacyProduction),
          "Open a complete version 6 project");

    std::cerr << "TRACE rightFold.placements = folded.placements;\n";
    rightFold.placements = folded.placements;
    std::cerr << "TRACE QString dxfError;\n";
    QString dxfError;
    std::cerr << "TRACE const auto dxfPath = temp.filePath(\n";
    const auto dxfPath = temp.filePath("marker.dxf");
    std::cerr << "TRACE check(exportFabricDxf(dxfPath, triangle, rightFold, dxfError),\n";
    check(exportFabricDxf(dxfPath, triangle, rightFold, dxfError), "Export a valid folded layout");
    std::cerr << "TRACE QFile dxf(dxfPath);\n";
    QFile dxf(dxfPath);
    std::cerr << "TRACE check(dxf.open(QIODevice::ReadOnly),\n";
    check(dxf.open(QIODevice::ReadOnly), "Read folded-layout DXF");
    std::cerr << "TRACE const auto dxfBytes = dxf.readAll();\n";
    const auto dxfBytes = dxf.readAll();
    std::cerr << "TRACE check(dxfBytes.contains(\n";
    check(dxfBytes.contains("8\nCUT\n100\nAcDbPolyline\n90\n4\n70\n0\n")
          && dxfBytes.contains("8\nFOLD\n100\nAcDbLine\n10\n160\n"),
          "Omit the fold side from the open CUT polyline and mark it separately in millimeters");
    std::cerr << "TRACE rightFold.placements[0].position.rx() -= 1;\n";
    rightFold.placements[0].position.rx() -= 1;
    std::cerr << "TRACE check(!exportFabricDxf(temp.filePath(\n";
    check(!exportFabricDxf(temp.filePath("off-fold.dxf"), triangle, rightFold, dxfError),
          "Reject off-fold paths before writing a DXF");

    std::cerr << "TRACE const auto marker = temp.filePath(\n";
    const auto marker = temp.filePath("marker.sofit");
    std::cerr << "TRACE check(triangle.save(marker),\n";
    check(triangle.save(marker), "Save new layout settings");
    std::cerr << "TRACE PatternDocument reopenedMarker;\n";
    PatternDocument reopenedMarker;
    std::cerr << "TRACE check(reopenedMarker.load(marker)\n";
    check(reopenedMarker.load(marker)
          && saveProduction(reopenedMarker.production()) == saveProduction(triangleBatch),
          "Round-trip copied hand, fold, fixed rotation and directional material");
    std::cerr << "TRACE auto malformed = triangleBatch;\n";
    auto malformed = triangleBatch;
    std::cerr << "TRACE malformed.placements[0].fold = FoldEdge::Left;\n";
    malformed.placements[0].fold = FoldEdge::Left;
    std::cerr << "TRACE check(!loadProduction(saveProduction(malformed), 2, reopenedMarker.production())\n";
    check(!loadProduction(saveProduction(malformed), 2, reopenedMarker.production())
          && saveProduction(reopenedMarker.production()) == saveProduction(triangleBatch),
          "Reject incompatible hand and fold modes atomically");

    std::cerr << "TRACE ProductionData repeat;\n";
    ProductionData repeat;
    std::cerr << "TRACE repeat.fabricWidth = 160;\n";
    repeat.fabricWidth = 160;
    std::cerr << "TRACE repeat.pieceClearanceMm = 4;\n";
    repeat.pieceClearanceMm = 4;
    std::cerr << "TRACE repeat.fabricRepeatMm = {37, 29};\n";
    repeat.fabricRepeatMm = {37, 29};
    std::cerr << "TRACE repeat.placements = {{1, {}, {}}, {1, {}, {}}, {1, {}, {}}};\n";
    repeat.placements = {{1, {}, {}}, {1, {}, {}}, {1, {}, {}}};
    std::cerr << "TRACE auto registeredLayout = arrangeFabric(triangle, repeat);\n";
    auto registeredLayout = arrangeFabric(triangle, repeat);
    std::cerr << "TRACE check(registeredLayout.error == FabricLayoutError::None && registeredLayout.placements.size() == 3,\n";
    check(registeredLayout.error == FabricLayoutError::None && registeredLayout.placements.size() == 3,
          "Arrange copies with a two-axis repeat and clearance");
    std::cerr << "TRACE repeat.placements = registeredLayout.placements;\n";
    repeat.placements = registeredLayout.placements;
    std::cerr << "TRACE const auto registered = cuttingReport(triangle, repeat);\n";
    const auto registered = cuttingReport(triangle, repeat);
    std::cerr << "TRACE check(registered.canExport(repeat.fabricWidth) && registered.repeatViolations == 0,\n";
    check(registered.canExport(repeat.fabricWidth) && registered.repeatViolations == 0,
          "Verify both repeat axes before marker export");
    std::cerr << "TRACE check(exportFabricDxf(temp.filePath(\n";
    check(exportFabricDxf(temp.filePath("registered.dxf"), triangle, repeat, dxfError),
          "Export a registered marker as DXF");
    std::cerr << "TRACE repeat.placements[1].position.ry() += 0.5;\n";
    repeat.placements[1].position.ry() += 0.5;
    std::cerr << "TRACE check(cuttingReport(triangle, repeat).repeatViolations == 1\n";
    check(cuttingReport(triangle, repeat).repeatViolations == 1
          && !exportFabricDxf(temp.filePath("off-repeat.dxf"), triangle, repeat, dxfError),
          "Reject a manually moved copy that misses its repeat");
    std::cerr << "TRACE triangle.production() = repeat;\n";
    triangle.production() = repeat;
    std::cerr << "TRACE check(triangle.save(marker) && reopenedMarker.load(marker)\n";
    check(triangle.save(marker) && reopenedMarker.load(marker)
          && reopenedMarker.production().fabricRepeatMm == QPointF(37, 29),
          "Round-trip both repeat dimensions in the current schema");
    std::cerr << "TRACE repeat.fabricRepeatMm.setX(-1);\n";
    repeat.fabricRepeatMm.setX(-1);
    std::cerr << "TRACE check(arrangeFabric(triangle, repeat).error == FabricLayoutError::InvalidRepeat\n";
    check(arrangeFabric(triangle, repeat).error == FabricLayoutError::InvalidRepeat
          && !loadProduction(saveProduction(repeat), 2, reopenedMarker.production()),
          "Reject an invalid repeat on arrangement and on load");

    std::cerr << "TRACE ProductionData flawed;\n";
    ProductionData flawed;
    std::cerr << "TRACE flawed.fabricWidth = 160;\n";
    flawed.fabricWidth = 160;
    std::cerr << "TRACE flawed.fabricFlaws = {QRectF(0, 0, 160, 75)};\n";
    flawed.fabricFlaws = {QRectF(0, 0, 160, 75)};
    std::cerr << "TRACE flawed.placements = {{1, {}, {}}};\n";
    flawed.placements = {{1, {}, {}}};
    std::cerr << "TRACE check(cuttingReport(triangle, flawed).usage.flawConflicts == 1\n";
    check(cuttingReport(triangle, flawed).usage.flawConflicts == 1
          && !exportFabricDxf(temp.filePath("flaw-conflict.dxf"), triangle, flawed, dxfError),
          "Block output when a copy overlaps a fabric flaw");
    std::cerr << "TRACE const auto avoidFlaw = arrangeFabric(triangle, flawed);\n";
    const auto avoidFlaw = arrangeFabric(triangle, flawed);
    std::cerr << "TRACE check(avoidFlaw.error == FabricLayoutError::None && avoidFlaw.placements.front().position.y() >= 75,\n";
    check(avoidFlaw.error == FabricLayoutError::None && avoidFlaw.placements.front().position.y() >= 75,
          "Arrange the copy below a width-spanning flaw");
    std::cerr << "TRACE flawed.placements = avoidFlaw.placements;\n";
    flawed.placements = avoidFlaw.placements;
    std::cerr << "TRACE triangle.production() = flawed;\n";
    triangle.production() = flawed;
    std::cerr << "TRACE check(cuttingReport(triangle, flawed).canExport(flawed.fabricWidth)\n";
    check(cuttingReport(triangle, flawed).canExport(flawed.fabricWidth)
          && triangle.save(marker) && reopenedMarker.load(marker)
          && reopenedMarker.production().fabricFlaws == flawed.fabricFlaws,
          "Verify and round-trip a marker with excluded fabric zones");

    std::cerr << "TRACE PatternDocument defectWindow;\n";
    PatternDocument defectWindow;
    std::cerr << "TRACE defectWindow.pieces().append({\n";
    defectWindow.pieces().append({"Test panel"});
    std::cerr << "TRACE defectWindow.insert(0, {Shape::Rectangle, {}, {40, 40}, {}, 0});\n";
    defectWindow.insert(0, {Shape::Rectangle, {}, {40, 40}, {}, 0});
    std::cerr << "TRACE ProductionData windowMarker;\n";
    ProductionData windowMarker;
    std::cerr << "TRACE windowMarker.fabricWidth = 100;\n";
    windowMarker.fabricWidth = 100;
    std::cerr << "TRACE windowMarker.fabricFlaws = {{0, 0, 15, 100}, {80, 0, 20, 100}, {15, 0, 65, 20}, {30, 60, 50, 40}};\n";
    windowMarker.fabricFlaws = {{0, 0, 15, 100}, {80, 0, 20, 100}, {15, 0, 65, 20}, {30, 60, 50, 40}};
    std::cerr << "TRACE windowMarker.placements = {{0, {}, {}}};\n";
    windowMarker.placements = {{0, {}, {}}};
    std::cerr << "TRACE const auto windowLayout = arrangeFabric(defectWindow, windowMarker);\n";
    const auto windowLayout = arrangeFabric(defectWindow, windowMarker);
    std::cerr << "TRACE check(windowLayout.error == FabricLayoutError::None && windowLayout.baselineLengthMm\n";
    check(windowLayout.error == FabricLayoutError::None && windowLayout.baselineLengthMm
          && windowLayout.optimizedLengthMm && *windowLayout.baselineLengthMm > *windowLayout.optimizedLengthMm
          && windowLayout.placements.front().position == QPointF(30, 20),
          "Use the pocket above a defect when edge candidates miss it");
    std::cerr << "TRACE windowMarker.placements = windowLayout.placements;\n";
    windowMarker.placements = windowLayout.placements;
    std::cerr << "TRACE check(cuttingReport(defectWindow, windowMarker).canExport(windowMarker.fabricWidth),\n";
    check(cuttingReport(defectWindow, windowMarker).canExport(windowMarker.fabricWidth),
          "Validate the shorter marker against every defect");

    std::cerr << "TRACE auto firstModel = defectWindow;\n";
    auto firstModel = defectWindow;
    std::cerr << "TRACE auto secondModel = defectWindow;\n";
    auto secondModel = defectWindow;
    std::cerr << "TRACE firstModel.pieces().front().notches.append(25);\n";
    firstModel.pieces().front().notches.append(25);
    std::cerr << "TRACE secondModel.replace(0, {Shape::Rectangle, {}, {50, 30}, {}, 0});\n";
    secondModel.replace(0, {Shape::Rectangle, {}, {50, 30}, {}, 0});
    std::cerr << "TRACE firstModel.production().sizes.append(PatternSize{\n";
    firstModel.production().sizes.append(PatternSize{"L", {}});
    std::cerr << "TRACE secondModel.production().sizes.append(PatternSize{\n";
    secondModel.production().sizes.append(PatternSize{"L", {}});
    std::cerr << "TRACE firstModel.production().rules.append({\n";
    firstModel.production().rules.append({"L", 0, {40, 40}, {0, 10}});
    std::cerr << "TRACE PatternDocument mixed;\n";
    PatternDocument mixed;
    std::cerr << "TRACE mixed.production().fabricWidth = 120;\n";
    mixed.production().fabricWidth = 120;
    std::cerr << "TRACE QString modelError;\n";
    QString modelError;
    std::cerr << "TRACE check(appendMarkerModel(mixed, firstModel,\n";
    check(appendMarkerModel(mixed, firstModel, "L", "A", 2, modelError)
          && appendMarkerModel(mixed, secondModel, "L", "B", 1, modelError), qPrintable(modelError));
    std::cerr << "TRACE check(mixed.pieces().size() == 2 && mixed.production().placements.size() == 3\n";
    check(mixed.pieces().size() == 2 && mixed.production().placements.size() == 3
          && mixed.pieces()[0].name.startsWith("A") && mixed.pieces()[0].cutCount == 2
          && mixed.pieces()[1].name.startsWith("B") && mixed.pieces()[1].cutCount == 1
          && fabricPiece(mixed, mixed.production().placements.front()).cut.boundingRect().height() == 50,
          "Keep independent geometry for equal size names and count each imported kit");
    std::cerr << "TRACE check(!appendMarkerModel(mixed, firstModel,\n";
    check(!appendMarkerModel(mixed, firstModel, "L", "A", 200, modelError)
          && mixed.pieces().size() == 2 && mixed.production().placements.size() == 3,
          "Reject an oversized import without changing the marker");
    std::cerr << "TRACE const auto firstCopy = mixed.production().placements.front();\n";
    const auto firstCopy = mixed.production().placements.front();
    std::cerr << "TRACE auto reflectedCopy = firstCopy;\n";
    auto reflectedCopy = firstCopy;
    std::cerr << "TRACE reflectedCopy.hand = CutHand::Right;\n";
    reflectedCopy.hand = CutHand::Right;
    std::cerr << "TRACE const auto normal = fabricPiece(mixed, firstCopy, true);\n";
    const auto normal = fabricPiece(mixed, firstCopy, true);
    std::cerr << "TRACE const auto reflected = fabricPiece(mixed, reflectedCopy, true);\n";
    const auto reflected = fabricPiece(mixed, reflectedCopy, true);
    std::cerr << "TRACE check(normal.notches.elementCount() == 2 && reflected.notches.elementCount() == 2\n";
    check(normal.notches.elementCount() == 2 && reflected.notches.elementCount() == 2
          && std::abs(normal.notches.elementAt(0).x + reflected.notches.elementAt(0).x
                      - normal.cut.boundingRect().width()) < 1e-6,
          "Reflect a production notch together with the cutting outline");
    std::cerr << "TRACE auto& mixedOrder = mixed.production();\n";
    auto& mixedOrder = mixed.production();
    std::cerr << "TRACE mixedOrder.fabricFlaws = {{0, 0, 120, 28}, {42, 35, 20, 50}};\n";
    mixedOrder.fabricFlaws = {{0, 0, 120, 28}, {42, 35, 20, 50}};
    std::cerr << "TRACE mixedOrder.pieceClearanceMm = 4;\n";
    mixedOrder.pieceClearanceMm = 4;
    std::cerr << "TRACE mixedOrder.fabricRepeatMm = {10, 10};\n";
    mixedOrder.fabricRepeatMm = {10, 10};
    std::cerr << "TRACE const auto combined = arrangeFabric(mixed, mixedOrder);\n";
    const auto combined = arrangeFabric(mixed, mixedOrder);
    std::cerr << "TRACE check(combined.error == FabricLayoutError::None,\n";
    check(combined.error == FabricLayoutError::None, "Arrange pieces from two models");
    std::cerr << "TRACE mixedOrder.placements = combined.placements;\n";
    mixedOrder.placements = combined.placements;
    std::cerr << "TRACE const auto inspectedMixed = cuttingReport(mixed, mixedOrder);\n";
    const auto inspectedMixed = cuttingReport(mixed, mixedOrder);
    std::cerr << "TRACE const auto kits = cuttingKitRows(mixed, inspectedMixed);\n";
    const auto kits = cuttingKitRows(mixed, inspectedMixed);
    std::cerr << "TRACE check(inspectedMixed.canExport(120) && kits.size() == 2\n";
    check(inspectedMixed.canExport(120) && kits.size() == 2
          && kits[0].copies() == 2 && kits[1].copies() == 1,
          "Preserve individual kit quantities while respecting defects, clearance and repeat");
    std::cerr << "TRACE auto onDefect = mixedOrder;\n";
    auto onDefect = mixedOrder;
    std::cerr << "TRACE onDefect.placements.front().position = {0, 0};\n";
    onDefect.placements.front().position = {0, 0};
    std::cerr << "TRACE check(cuttingReport(mixed, onDefect).usage.flawConflicts > 0,\n";
    check(cuttingReport(mixed, onDefect).usage.flawConflicts > 0,
          "Reject a mixed marker when a kit placement is moved onto a defect");
    std::cerr << "TRACE check(mixed.save(marker) && reopenedMarker.load(marker) && reopenedMarker.pieces() == mixed.pieces()\n";
    check(mixed.save(marker) && reopenedMarker.load(marker) && reopenedMarker.pieces() == mixed.pieces()
          && reopenedMarker.production().fabricFlaws == mixedOrder.fabricFlaws
          && cuttingReport(reopenedMarker, reopenedMarker.production()).canExport(120),
          "Save and reopen a mixed-model marker");
    std::cerr << "TRACE const auto mixedDxf = temp.filePath(\n";
    const auto mixedDxf = temp.filePath("mixed.dxf");
    std::cerr << "TRACE check(exportFabricDxf(mixedDxf, mixed, mixed.production(), dxfError)\n";
    check(exportFabricDxf(mixedDxf, mixed, mixed.production(), dxfError)
          && exportFabricPdf(temp.filePath("mixed.pdf"), mixed, mixed.production(), {}, dxfError), qPrintable(dxfError));
    std::cerr << "TRACE QFile markedMarker(mixedDxf);\n";
    QFile markedMarker(mixedDxf);
    std::cerr << "TRACE check(markedMarker.open(QIODevice::ReadOnly | QIODevice::Text)\n";
    check(markedMarker.open(QIODevice::ReadOnly | QIODevice::Text)
          && markedMarker.readAll().contains("LINE\n100\nAcDbEntity\n8\nNOTCH\n"),
          "Write production notches as DXF lines on their own layer");

    std::cerr << "TRACE PatternDocument compensated;\n";
    PatternDocument compensated;
    std::cerr << "TRACE compensated.pieces().append({\n";
    compensated.pieces().append({"Compensated panel"});
    std::cerr << "TRACE compensated.pieces()[0].grainAngle = 0;\n";
    compensated.pieces()[0].grainAngle = 0;
    std::cerr << "TRACE compensated.insert(0, {Shape::Rectangle, {}, {80, 90}, {}, 0});\n";
    compensated.insert(0, {Shape::Rectangle, {}, {80, 90}, {}, 0});
    std::cerr << "TRACE auto& shrinkage = compensated.production();\n";
    auto& shrinkage = compensated.production();
    std::cerr << "TRACE shrinkage.fabricWidth = 105;\n";
    shrinkage.fabricWidth = 105;
    std::cerr << "TRACE shrinkage.fabricShrinkagePercent = {10, 20};\n";
    shrinkage.fabricShrinkagePercent = {10, 20};
    std::cerr << "TRACE shrinkage.placements = {{0, {}, {}}};\n";
    shrinkage.placements = {{0, {}, {}}};
    std::cerr << "TRACE const auto physical = fabricPiece(compensated, shrinkage.placements.front(), false, shrinkage.fabricShrinkagePercent);\n";
    const auto physical = fabricPiece(compensated, shrinkage.placements.front(), false, shrinkage.fabricShrinkagePercent);
    std::cerr << "TRACE check(qAbs(physical.cut.boundingRect().width() - 100) < 1e-7\n";
    check(qAbs(physical.cut.boundingRect().width() - 100) < 1e-7
          && qAbs(physical.cut.boundingRect().height() - 100) < 1e-7,
          "Apply independent inverse shrinkage after rotating to fabric axes");
    std::cerr << "TRACE check(arrangeFabric(compensated, shrinkage).error == FabricLayoutError::None\n";
    check(arrangeFabric(compensated, shrinkage).error == FabricLayoutError::None
          && qAbs(cuttingReport(compensated, shrinkage).usage.length - 100) < 1e-7,
          "Use compensated geometry for packing and consumption");
    std::cerr << "TRACE const auto compensatedFile = temp.filePath(\n";
    const auto compensatedFile = temp.filePath("compensated.sofit");
    std::cerr << "TRACE check(compensated.save(compensatedFile) && reopenedMarker.load(compensatedFile)\n";
    check(compensated.save(compensatedFile) && reopenedMarker.load(compensatedFile)
          && reopenedMarker.production().fabricShrinkagePercent == QPointF(10, 20),
          "Persist shrinkage with the marker");
    std::cerr << "TRACE const auto shrinkDxf = temp.filePath(\n";
    const auto shrinkDxf = temp.filePath("compensated.dxf");
    std::cerr << "TRACE check(exportFabricDxf(shrinkDxf, compensated, shrinkage, dxfError)\n";
    check(exportFabricDxf(shrinkDxf, compensated, shrinkage, dxfError)
          && exportFabricPdf(temp.filePath("compensated.pdf"), compensated, shrinkage, {}, dxfError),
          qPrintable(dxfError));
    std::cerr << "TRACE QFile compensatedDxf(shrinkDxf);\n";
    QFile compensatedDxf(shrinkDxf);
    std::cerr << "TRACE check(compensatedDxf.open(QIODevice::ReadOnly) && compensatedDxf.readAll().contains(\n";
    check(compensatedDxf.open(QIODevice::ReadOnly) && compensatedDxf.readAll().contains("20\n-100\n"),
          "Export the compensated physical length to DXF");
    std::cerr << "TRACE auto stretched = shrinkage;\n";
    auto stretched = shrinkage;
    std::cerr << "TRACE stretched.fabricShrinkagePercent.setX(-20);\n";
    stretched.fabricShrinkagePercent.setX(-20);
    std::cerr << "TRACE const auto contracted = fabricPiece(compensated, stretched.placements.front(), false,\n";
    const auto contracted = fabricPiece(compensated, stretched.placements.front(), false,
                                        stretched.fabricShrinkagePercent).cut.boundingRect();
    std::cerr << "TRACE ProductionData loadedStretch;\n";
    ProductionData loadedStretch;
    std::cerr << "TRACE check(qAbs(contracted.width() - 75) < 1e-7 && qAbs(contracted.height() - 100) < 1e-7\n";
    check(qAbs(contracted.width() - 75) < 1e-7 && qAbs(contracted.height() - 100) < 1e-7
          && loadProduction(saveProduction(stretched), 1, loadedStretch)
          && loadedStretch.fabricShrinkagePercent == QPointF(-20, 20),
          "Apply and persist signed fabric compensation independently on both axes");
    std::cerr << "TRACE auto tooNarrow = shrinkage;\n";
    auto tooNarrow = shrinkage;
    std::cerr << "TRACE tooNarrow.fabricWidth = 95;\n";
    tooNarrow.fabricWidth = 95;
    std::cerr << "TRACE check(arrangeFabric(compensated, tooNarrow).error == FabricLayoutError::TooWide\n";
    check(arrangeFabric(compensated, tooNarrow).error == FabricLayoutError::TooWide
          && !cuttingReport(compensated, tooNarrow).canExport(tooNarrow.fabricWidth),
          "Reject a fabric that only fit before compensation");
    std::cerr << "TRACE auto invalidShrinkage = shrinkage;\n";
    auto invalidShrinkage = shrinkage;
    std::cerr << "TRACE invalidShrinkage.fabricShrinkagePercent.setY(100);\n";
    invalidShrinkage.fabricShrinkagePercent.setY(100);
    std::cerr << "TRACE check(!loadProduction(saveProduction(invalidShrinkage), 1, reopenedMarker.production())\n";
    check(!loadProduction(saveProduction(invalidShrinkage), 1, reopenedMarker.production())
          && arrangeFabric(compensated, invalidShrinkage).error == FabricLayoutError::InvalidShrinkage,
          "Reject a singular shrinkage factor");

    std::cerr << "TRACE compensated.pieces()[0].fabricShrinkageOverride = QPointF(0, -20);\n";
    compensated.pieces()[0].fabricShrinkageOverride = QPointF(0, -20);
    std::cerr << "TRACE shrinkage.sizes = {{\n";
    shrinkage.sizes = {{"M", {}}};
    std::cerr << "TRACE shrinkage.placements[0].size =\n";
    shrinkage.placements[0].size = "M";
    std::cerr << "TRACE const auto overridden = cuttingReport(compensated, shrinkage);\n";
    const auto overridden = cuttingReport(compensated, shrinkage);
    std::cerr << "TRACE check(qAbs(overridden.usage.length - 80 / 1.2) < 1e-7\n";
    check(qAbs(overridden.usage.length - 80 / 1.2) < 1e-7
          && arrangeFabric(compensated, shrinkage).error == FabricLayoutError::None,
          "Use the piece override for the graded layout and automatic placement");
    std::cerr << "TRACE check(compensated.save(compensatedFile) && reopenedMarker.load(compensatedFile)\n";
    check(compensated.save(compensatedFile) && reopenedMarker.load(compensatedFile)
          && reopenedMarker.pieces()[0].fabricShrinkageOverride == QPointF(0, -20),
          "Round-trip the per-piece compensation in a native project");
    std::cerr << "TRACE check(exportFabricDxf(shrinkDxf, reopenedMarker, reopenedMarker.production(), dxfError)\n";
    if (qEnvironmentVariableIsSet("SOFIT_CLOSE_DXF_BEFORE_REEXPORT"))
        compensatedDxf.close();
    const bool dxfExported = exportFabricDxf(shrinkDxf, reopenedMarker, reopenedMarker.production(), dxfError);
    check(dxfExported, qPrintable(dxfError));
    const bool pdfExported = exportFabricPdf(temp.filePath("piece-compensation.pdf"), reopenedMarker,
                                            reopenedMarker.production(), {}, dxfError);
    check(pdfExported, qPrintable(dxfError));
    std::cerr << "TRACE compensated.pieces()[0].fabricShrinkageOverride = QPointF(100, 0);\n";
    compensated.pieces()[0].fabricShrinkageOverride = QPointF(100, 0);
    std::cerr << "TRACE check(!compensated.save(compensatedFile)\n";
    check(!compensated.save(compensatedFile)
          && !cuttingReport(compensated, shrinkage).canExport(shrinkage.fabricWidth),
          "Reject invalid per-piece compensation");

    std::cerr << "TRACE const auto planned = planSizeLays({{\n";
    const auto planned = planSizeLays({{"S", 0}, {"M", 7}, {"XL", 2}}, 3);
    std::cerr << "TRACE check(planned.size() == 4 && planned[0].size ==\n";
    check(planned.size() == 4 && planned[0].size == "M" && planned[0].layers == 3
          && planned[1].size == "M" && planned[1].layers == 3 && planned[2].size == "M" && planned[2].layers == 1
          && planned[3].size == "XL" && planned[3].layers == 2,
          "Split the exact demand by size into bounded lays");
    std::cerr << "TRACE check(planSizeLays({{\n";
    check(planSizeLays({{"M", 1}}, 0).isEmpty() && planSizeLays({{"M", -1}}, 3).isEmpty()
          && planSizeLays({{"M", 10000}}, 1).isEmpty(), "Reject invalid and oversized plans");

    std::cerr << "TRACE const QMap<QString, int> demand{{QString{}, 7}, {\n";
    const QMap<QString, int> demand{{QString{}, 7}, {"M", 5}, {"XL", 1}};
    std::cerr << "TRACE const auto mixedLays = planKitLays(demand, 3, 4);\n";
    const auto mixedLays = planKitLays(demand, 3, 4);
    std::cerr << "TRACE QMap<QString, int> produced;\n";
    QMap<QString, int> produced;
    std::cerr << "TRACE bool hasMixedSizes = false;\n";
    bool hasMixedSizes = false;
    std::cerr << "TRACE bool hasMultipleCopies = false;\n";
    bool hasMultipleCopies = false;
    std::cerr << "TRACE for (const auto& lay : mixedLays) {\n";
    for (const auto& lay : mixedLays) {
        check(lay.layers >= 1 && lay.layers <= 3, "Honor the layer limit for mixed lays");
        hasMixedSizes |= lay.kitsPerSize.size() > 1;
        int kitsOnLayer = 0;
        for (auto size = lay.kitsPerSize.cbegin(); size != lay.kitsPerSize.cend(); ++size) {
            produced[size.key()] += size.value() * lay.layers;
            kitsOnLayer += size.value();
            hasMultipleCopies |= size.value() > 1;
        }
        check(kitsOnLayer >= 1 && kitsOnLayer <= 4, "Honor the marker kit limit");
    }
    std::cerr << "TRACE check(produced == demand && hasMixedSizes && hasMultipleCopies,\n";
    check(produced == demand && hasMixedSizes && hasMultipleCopies,
          "Deliver exact demand using multiple sizes and repeated kits");
    std::cerr << "TRACE check(planKitLays({{\n";
    check(planKitLays({{"M", 1}}, 3, 0).isEmpty()
          && planKitLays({{"M", -1}}, 3, 2).isEmpty(), "Reject an invalid kit plan");

    std::cerr << "TRACE auto duplicate = production;\n";
    auto duplicate = production;
    std::cerr << "TRACE duplicate.rules.append(duplicate.rules.front());\n";
    duplicate.rules.append(duplicate.rules.front());
    std::cerr << "TRACE check(!loadProduction(saveProduction(duplicate), 2, loaded.production())\n";
    check(!loadProduction(saveProduction(duplicate), 2, loaded.production())
          && saveProduction(loaded.production()) == json, "Reject duplicate rules without changing existing data");
    std::cerr << "TRACE loaded.production().removePiece(0);\n";
    loaded.production().removePiece(0);
    std::cerr << "TRACE check(loaded.production().rules.isEmpty() && loaded.production().placements.size() == 1\n";
    check(loaded.production().rules.isEmpty() && loaded.production().placements.size() == 1
          && loaded.production().placements.front().piece == 0, "Remove and remap piece references");

    std::cerr << "TRACE const auto legacy = temp.filePath(\n";
    const auto legacy = temp.filePath("legacy.sofit");
    std::cerr << "TRACE QFile output(legacy);\n";
    QFile output(legacy);
    std::cerr << "TRACE check(output.open(QIODevice::WriteOnly),\n";
    check(output.open(QIODevice::WriteOnly), "Open version 5 fixture");
    std::cerr << "TRACE output.write(R\n";
    output.write(R"({"format":"SOFIT","version":5,"layerVisibility":[true,true,true],"objects":[],
        "pieces":[{"name":"Front","allowanceMm":5,"cutCount":2,"size":"M","showLabel":true,
                   "grainAngle":0,"notches":[10]}]})");
    std::cerr << "TRACE output.close();\n";
    output.close();
    std::cerr << "TRACE check(loaded.load(legacy) && loaded.production().sizes.isEmpty() && loaded.production().placements.isEmpty()\n";
    check(loaded.load(legacy) && loaded.production().sizes.isEmpty() && loaded.production().placements.isEmpty()
          && loaded.pieces().front().cutCount == 2 && loaded.pieces().front().notches == QVector<double>{10},
          "Open a version 5 file and keep its markings");
}
