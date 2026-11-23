#include "Production.h"

#include <QCoreApplication>
#include <QFile>
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

    production.allowHalfTurns = false;
    QTemporaryDir temp;
    check(temp.isValid(), "Create temporary directory");
    const auto file = temp.filePath("production.sofit");
    check(base.save(file), "Save version 6 production data");
    PatternDocument loaded;
    check(loaded.load(file) && saveProduction(loaded.production()) == saveProduction(production)
          && loaded.pieces() == base.pieces(), "Round-trip sizes, rules, placement, width, and piece metadata");
    auto json = saveProduction(production);
    auto version6 = json;
    version6.remove("allowHalfTurns");
    ProductionData legacyProduction;
    check(loadProduction(version6, 2, legacyProduction) && legacyProduction.allowHalfTurns,
          "Keep legacy rotation behavior for version 6 files without the optional setting");
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
