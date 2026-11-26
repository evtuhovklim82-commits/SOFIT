#include "PieceGeometry.h"
#include "Modeling.h"
#include "ContourModeling.h"
#include "PieceMarkings.h"
#include "PatternDxf.h"
#include "Production.h"

#include <QFile>
#include <QLineF>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace opc;

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}

void append(PatternDocument& document, Shape shape, QPointF a, QPointF b, QPointF control = {}) {
    document.insert(document.entities().size(), {shape, a, b, control, 0});
}

PatternDocument polygon(const QPolygonF& vertices, double allowance = 0) {
    PatternDocument document;
    document.pieces().append({QStringLiteral("Полочка"), allowance});
    for (qsizetype i = 0; i < vertices.size(); ++i)
        append(document, Shape::Line, vertices[i], vertices[(i + 1) % vertices.size()]);
    return document;
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(), "Write fixture");
}

void testSeamTools() {
    auto document = polygon({{0, 0}, {50, 0}, {100, 0}, {100, 40}, {0, 40}}, 10);
    auto first = document.entities()[0];
    first.seamAllowanceMm = 0;
    first.seamNotches = {0.5};
    document.replace(0, first);
    auto second = document.entities()[1];
    std::swap(second.a, second.b);
    second.seamAllowanceMm = 7;
    second.seamNotches = {0.25};
    document.replace(1, second);

    const auto geometry = analyzePiece(document, 0);
    check(geometry.error == ContourError::None && !geometry.allowanceFailed,
          "Accept a straight step between different allowances");
    check(geometry.allowance.contains({50, 0}) && geometry.allowance.contains({50, -7}),
          "Keep the width step at the correct junction");
    check(geometry.notchAnchors.size() == 2 && geometry.notchAnchors[0].p1() == QPointF(25, 0)
          && geometry.notchAnchors[1].p1() == QPointF(87.5, 0),
          "Locate arc-length marks from each source edge's actual start");
    const auto marks = buildPieceMarkings(document.pieces()[0], geometry, true).notches;
    check(marks.elementCount() == 4 && near(marks.elementAt(0).y, 1.5)
          && near(marks.elementAt(2).y, -5.5), "Project marks to zero and nonzero cut widths");

    const auto section = measureSeamSection(document, {0, 1});
    check(section && near(section->lengthMm, 100) && !section->notchAt(0.5),
          "Measure a multi-edge chain and identify a notch on a junction");
    const auto mark = section->notchAt(0.875);
    check(mark && mark->first == 1 && near(mark->second, 0.25), "Map chain distance to the reversed edge");
    check(!measureSeamSection(document, {0, 2}) && !measureSeamSection(document, {0, 0})
          && !measureSeamSection(document, {0, 1, 2, 3, 4}), "Reject disconnected, repeated and closed selections");

    const auto curvedLength = seamLength({Shape::Curve, {0, 0}, {100, 0}, {50, 50}});
    const auto exact = 50 * (std::sqrt(2.0) + std::asinh(1.0));
    check(curvedLength && near(*curvedLength, exact, 0.001), "Quadratic length stays inside the error budget");
    check(!splitEdge(first, 0.5), "Prevent duplicating automatic marks when splitting an edge");

    document.production().sizes.append({QStringLiteral("L")});
    document.production().rules.append({QStringLiteral("L"), 0, {50, 0}, {10, 0}});
    QTemporaryDir temp;
    const auto path = temp.filePath("seam-tools.sofit");
    PatternDocument loaded;
    check(document.save(path) && loaded.load(path), "Round-trip seam-enabled document");
    check(loaded.entities()[0].seamAllowanceMm == 0 && loaded.entities()[1].seamAllowanceMm == 7
          && loaded.entities()[1].seamNotches == QVector<double>{0.25}, "Restore edge settings and marks");
    const auto graded = gradedPattern(loaded, "L");
    const auto gradedGeometry = analyzePiece(graded, 0);
    check(!gradedGeometry.allowanceFailed && gradedGeometry.notchAnchors.size() == 2
          && gradedGeometry.notchAnchors[0].p1() == QPointF(30, 0),
          "Rebuild allowance and notch locations from graded edges");
}

void testLines() {
    auto document = polygon({{0, 0}, {100, 0}, {100, 40}, {0, 40}}, 12.375);
    auto last = document.entities().back();
    std::swap(last.a, last.b);
    document.remove(3);
    document.insert(1, last);
    auto geometry = analyzePiece(document, 0);
    check(geometry.error == ContourError::None, "Unordered and reversed contour edges");
    check(near(geometry.perimeterMm, 280), "Rectangle perimeter");
    check(geometry.bounds == QRectF(0, 0, 100, 40), "Rectangle dimensions");
    check(geometry.allowance.boundingRect() == QRectF(-12.375, -12.375, 124.75, 64.75),
          "Precise outward offset of straight sides");

    auto reversed = polygon({{0, 40}, {100, 40}, {100, 0}, {0, 0}}, 12.375);
    check(analyzePiece(reversed, 0).allowance.boundingRect() == geometry.allowance.boundingRect(),
          "Outward offset is independent of winding");

    document.remove(1);
    check(analyzePiece(document, 0).error == ContourError::Open, "Open contour");
    document.insert(1, last);
    append(document, Shape::Line, {0, 0}, {30, 20});
    check(analyzePiece(document, 0).error == ContourError::Branched, "Branch");
    auto guide = document.entities().back();
    guide.layer = Layer::Construction;
    document.replace(4, guide);
    check(analyzePiece(document, 0).error == ContourError::None, "Construction lines ignored");

    auto crossing = polygon({{0, 0}, {100, 50}, {0, 50}, {70, 0}}, 5);
    check(analyzePiece(crossing, 0).error == ContourError::SelfIntersecting, "Crossing contour");
    auto overlapping = polygon({{0, 0}, {100, 0}, {50, 0}, {100, 50}, {0, 50}}, 5);
    check(analyzePiece(overlapping, 0).error == ContourError::SelfIntersecting, "Overlapping edges");

    append(document, Shape::Rectangle, {200, 100}, {230, 120});
    check(analyzePiece(document, 0).error == ContourError::Disconnected, "Disconnected rings");

    PatternDocument rectangle;
    rectangle.pieces().append({QStringLiteral("Rectangle"), 7});
    append(rectangle, Shape::Rectangle, {8, 10}, {0, 0});
    check(near(analyzePiece(rectangle, 0).perimeterMm, 36), "Rectangle tool contour");

    auto concave = polygon({{0, 0}, {100, 0}, {100, 80}, {60, 80}, {60, 50},
                            {40, 50}, {40, 80}, {0, 80}}, 4);
    check(!analyzePiece(concave, 0).allowanceFailed, "Concave outline with open notch");
    concave.pieces()[0].allowanceMm = 11;
    geometry = analyzePiece(concave, 0);
    check(geometry.error == ContourError::None && geometry.allowanceFailed && geometry.allowance.isEmpty(),
          "Refuse offset pinching off a notch");

    auto acute = polygon({{0, 0}, {100, 0}, {100, 1}}, 5);
    check(analyzePiece(acute, 0).allowanceFailed, "Refuse unbounded acute miter");
}

PatternDocument curvedPiece() {
    PatternDocument document;
    document.pieces().append({QStringLiteral("Рукав"), 8.125});
    append(document, Shape::Curve, {100, 0}, {0, 0}, {50, -50});
    append(document, Shape::Line, {100, 0}, {100, 40});
    append(document, Shape::Line, {100, 40}, {0, 40});
    append(document, Shape::Line, {0, 40}, {0, 0});
    return document;
}

void testCurves() {
    auto document = curvedPiece();
    const auto geometry = analyzePiece(document, 0);
    check(geometry.error == ContourError::None && !geometry.allowanceFailed, "Mixed curve and line contour");
    check(geometry.bounds == QRectF(0, -25, 100, 65), "Quadratic extrema in bounding rectangle");
    const double exactPerimeter = 180 + 50 * (std::sqrt(2.0) + std::asinh(1.0));
    check(near(geometry.perimeterMm, exactPerimeter, 0.01), "Quadratic length within error budget");
    check(near(geometry.allowance.boundingRect().top(), -33.125, 0.01), "Approximate curve allowance");

    auto edge = document.entities().front();
    edge.control = {50, 100};
    document.replace(0, edge);
    check(analyzePiece(document, 0).error == ContourError::SelfIntersecting,
          "Quadratic crossing the base edge");
    edge.control = {150, 0};
    document.replace(0, edge);
    check(analyzePiece(document, 0).error == ContourError::Degenerate, "Folded quadratic");
}

void testFiles() {
    QTemporaryDir temp;
    check(temp.isValid(), "Create temporary directory");
    const auto path = temp.filePath("pattern.sofit");
    auto source = curvedPiece();
    source.pieces().append({QStringLiteral("Спинка"), 0});
    source.insert(0, {Shape::Point, {12, 12}, {12, 12}, {}, 1, Layer::Construction});
    source.setLayerVisible(Layer::Construction, false);
    source.setLayerVisible(Layer::Allowance, false);

    auto& marked = source.pieces()[0];
    marked.cutCount = 2;
    marked.size = QStringLiteral("48");
    marked.showLabel = true;
    marked.grainAngle = 135;
    marked.notches = {12.5, 50, 75};

    PatternDocument loaded;
    check(source.save(path) && loaded.load(path), "Round-trip SOFIT 5");
    check(loaded.pieces() == source.pieces(), "Persist markings");
    const QVector<PieceGeometry> geometry{analyzePiece(loaded, 0), analyzePiece(loaded, 1)};
    const auto marks = buildPieceMarkings(loaded.pieces()[0], geometry[0]);
    check(marks.notches.elementCount() == 6 && marks.grainline.elementCount() == 5
          && marks.label.contains(QStringLiteral("48")), "Generate notch, grainline and label geometry");

    QString error;
    const auto dxfPath = temp.filePath("pattern.dxf");
    check(exportPatternDxf(dxfPath, loaded, QVector<bool>(loaded.entities().size(), true), geometry, error),
          "Write DXF");
    QFile dxf(dxfPath);
    check(dxf.open(QIODevice::ReadOnly), "Read DXF");
    const auto contents = dxf.readAll();
    check(contents.contains("$INSUNITS\n70\n4\n") && contents.contains("0\nSPLINE\n")
          && contents.contains("8\nNOTCH\n") && contents.contains("8\nGRAINLINE\n")
          && contents.contains("0\nTEXT\n") && contents.contains("\\U+0420")
          && !contents.contains("8\nCONSTRUCTION\n") && !contents.contains("8\nALLOWANCE\n"),
          "Export millimeters, curves and markings on visible layers");
    check(loaded.pieces().size() == 2 && loaded.pieces()[0].name == QStringLiteral("Рукав")
          && near(loaded.pieces()[0].allowanceMm, 8.125), "Persist piece name and width");
    check(loaded.entities()[0].piece == 1 && loaded.entities()[0].layer == Layer::Construction
          && !loaded.layerVisible(Layer::Allowance) && !loaded.layerVisible(Layer::Construction)
          && loaded.layerVisible(Layer::Main), "Persist membership, layers and layer visibility");
    check(analyzePiece(loaded, 0).allowance == analyzePiece(source, 0).allowance, "Regenerate stored allowance");

    writeFile(path, R"({"format":"SOFIT","version":4,"pieces":[],"layerVisibility":[true,true,true],
        "objects":[{"type":"point","coordinates":[0,0],"piece":0,"layer":"main"}]})");
    check(!loaded.load(path) && loaded.pieces().size() == 2 && loaded.entities()[0].piece == 1,
          "Reject invalid piece references without overwriting the document");

    writeFile(path, R"({"format":"SOFIT","version":4,"layerVisibility":[true,true,true],
        "pieces":[{"name":"Front","allowanceMm":10}],"objects":[]})");
    check(loaded.load(path) && loaded.pieces()[0] == Piece{QStringLiteral("Front"), 10},
          "Open SOFIT 4 with default markings");

    const auto legacy = temp.filePath("legacy.opcad");
    writeFile(legacy, R"({"format":"OpenPatternCAD","version":1,"segments":[[0,0,15,9]]})");
    check(loaded.load(legacy) && loaded.pieces().isEmpty() && loaded.entities()[0].shape == Shape::Line
          && loaded.entities()[0].piece == -1 && loaded.layerVisible(Layer::Construction), "Open legacy OPCAD");
    writeFile(path, R"({"format":"SOFIT","version":1,"segments":[[0,0,15,9]]})");
    check(loaded.load(path) && loaded.entities().size() == 1, "Open SOFIT 1");
    writeFile(path, R"({"format":"SOFIT","version":2,"objects":[{"type":"rectangle","coordinates":[0,0,15,9]}]})");
    check(loaded.load(path) && loaded.entities()[0].shape == Shape::Rectangle, "Open SOFIT 2");
    writeFile(path, R"({"format":"SOFIT","version":3,"objects":[
        {"type":"quadratic","coordinates":[0,0,15,9,3,14]},{"type":"circle","coordinates":[0,0,15,9]}]})");
    check(loaded.load(path) && loaded.entities()[0].control == QPointF(3, 14)
          && loaded.entities()[1].shape == Shape::Circle, "Open SOFIT 3");
}
}

namespace {
void testModeling() {
    const Entity curve{Shape::Curve, {0, 0}, {100, 5}, {20, 80}, 0, Layer::Construction};
    const auto split = splitEdge(curve, 0.3);
    check(split.has_value(), "Split a quadratic curve");
    const auto pointAt = [](const Entity& edge, double t) {
        return (1 - t) * (1 - t) * edge.a + 2 * t * (1 - t) * edge.control + t * t * edge.b;
    };
    check((*split)[0].b == (*split)[1].a, "Split endpoints are identical");
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        check(QLineF(pointAt((*split)[0], t), pointAt(curve, 0.3 * t)).length() < 1e-9
              && QLineF(pointAt((*split)[1], t), pointAt(curve, 0.3 + 0.7 * t)).length() < 1e-9,
              "Split preserves the curve");
    }
    check((*split)[1].piece == 0 && (*split)[1].layer == Layer::Construction,
          "Split preserves piece and layer");
    check(!splitEdge(curve, 0) && !splitEdge(curve, 1), "Reject endpoint splits");

    auto document = polygon({{0, 0}, {100, 0}, {100, 40}, {0, 40}}, 10);
    auto first = document.entities()[0];
    first.b = {98, 0};
    document.replace(0, first);
    check(analyzePiece(document, 0).error == ContourError::Open, "Fixture has a gap");
    check(!joinEdges(document, 0, 1, 1), "Reject a gap outside tolerance");
    const auto join = joinEdges(document, 0, 1, 5);
    check(join.has_value() && (*join)[0].b == QPointF(99, 0)
          && (*join)[0].b == (*join)[1].a, "Join endpoints at their midpoint");
    document.replace(0, (*join)[0]);
    document.replace(1, (*join)[1]);
    const auto closed = analyzePiece(document, 0);
    check(closed.error == ContourError::None && !closed.allowance.isEmpty(),
          "Joining closes the contour and restores its allowance");

    const auto line = splitEdge(document.entities()[0], 0.5);
    check(line.has_value() && (*line)[0].b == QPointF(49.5, 0), "Split a line by distance");
    document.replace(0, (*line)[0]);
    document.insert(1, (*line)[1]);
    const auto subdivided = analyzePiece(document, 0);
    check(subdivided.error == ContourError::None && !subdivided.allowance.isEmpty()
          && near(closed.perimeterMm, subdivided.perimeterMm), "Split preserves the closed contour");

    PatternDocument ambiguous;
    ambiguous.insert(0, {Shape::Line, {0, 0}, {0, 20}});
    ambiguous.insert(1, {Shape::Line, {2, 0}, {2, 20}});
    check(!joinEdges(ambiguous, 0, 1, 5), "Reject two equally likely joins");
    const auto dart = dartLegs({0, 0}, {5, 20}, {10, 0});
    check(dart.has_value() && (*dart)[0].b == (*dart)[1].a
          && (*dart)[0].layer == Layer::Construction && (*dart)[1].layer == Layer::Construction,
          "Dart creates two construction legs with a shared apex");
    check(!dartLegs({0, 0}, {5, 0}, {10, 0}), "Reject a collinear dart");
}
}

namespace {
qsizetype guide(PatternDocument& document, QPointF a, QPointF b) {
    const auto index = document.entities().size();
    document.insert(index, {Shape::Line, a, b, {}, -1, Layer::Construction});
    return index;
}

double area(const QPolygonF& ring) {
    double sum = 0;
    for (qsizetype i = 0; i < ring.size(); ++i) {
        const auto a = ring[i];
        const auto b = ring[(i + 1) % ring.size()];
        sum += a.x() * b.y() - a.y() * b.x();
    }
    return std::abs(sum) / 2;
}

void testContourCutAndSpread() {
    PatternDocument document;
    document.pieces().append({QStringLiteral("Front"), 4, 2});
    document.insert(0, {Shape::Rectangle, {0, 0}, {100, 80}, {}, 0});
    // Inserting the first cut endpoint late in the ring must survive insertion of the second.
    const auto slash = guide(document, {100, 40}, {0, 40});
    document.insert(2, {Shape::Line, {500, 0}, {500, 50}});
    const auto cut = modelContour(document, 0, ContourOperation::Cut, {slash});
    check(cut.edit.has_value(), qPrintable(cut.error));
    const auto& panels = cut.edit->document;
    check(panels.pieces().size() == 2 && panels.pieces()[0].name == "Front A"
          && panels.pieces()[1].name == "Front B" && panels.pieces()[1].cutCount == 2,
          "Cut makes two independently identified pieces with production metadata");
    for (qsizetype piece = 0; piece < 2; ++piece) {
        const auto geometry = analyzePiece(panels, piece);
        check(geometry.error == ContourError::None && !geometry.allowance.isEmpty()
              && near(area(geometry.contour), 4000) && near(geometry.perimeterMm, 280),
              "Each panel contains the full cutting edge and its own allowance");
    }
    check(panels.entities().front().a == QPointF(500, 0)
          && cut.edit->removedEntities == QVector<qsizetype>{0, 1} && document.entities().size() == 3,
          "Cut preserves unrelated geometry and leaves the original untouched");

    for (const bool reverse : {false, true}) {
        auto source = document;
        if (reverse) {
            auto line = source.entities()[slash];
            std::swap(line.a, line.b);
            source.replace(slash, line);
        }
        const auto spread = modelContour(source, 0, ContourOperation::Spread, {slash}, 20);
        check(spread.edit.has_value(), qPrintable(spread.error));
        const auto geometry = analyzePiece(spread.edit->document, 0);
        check(geometry.error == ContourError::None && near(geometry.bounds.height(), 100)
              && near(area(geometry.contour), 10000) && !geometry.allowanceFailed,
              "Parallel spread adds the specified width in either direction");
    }
    check(!modelContour(document, 0, ContourOperation::Spread, {slash}, 0).edit,
          "Reject zero spread width");

    auto crossingCuts = document;
    const auto vertical = guide(crossingCuts, {50, 0}, {50, 80});
    const auto quarters = cutCrossingGuides(crossingCuts, 0, {slash, vertical});
    check(quarters.edit.has_value(), qPrintable(quarters.error));
    check(quarters.edit->document.pieces().size() == 4
          && crossingCuts.pieces().size() == 1, "Split two crossing guides into four panels atomically");
    for (qsizetype i = 0; i < 4; ++i) {
        const auto panel = analyzePiece(quarters.edit->document, i);
        check(panel.error == ContourError::None && !panel.allowanceFailed && near(area(panel.contour), 2000),
              "Preserve the area and compute an allowance for each quarter");
    }
    const auto diagonal = guide(crossingCuts, {0, 0}, {100, 80});
    const auto sectors = cutCrossingGuides(crossingCuts, 0, {slash, vertical, diagonal});
    check(sectors.edit.has_value(), qPrintable(sectors.error));
    check(sectors.edit->document.pieces().size() == 6, "Deduplicate the common crossing of three cuts");
    double totalArea = 0;
    for (qsizetype i = 0; i < 6; ++i) {
        const auto panel = analyzePiece(sectors.edit->document, i);
        check(panel.error == ContourError::None && !panel.allowanceFailed,
              "Validate all six cut contours and their allowances");
        totalArea += area(panel.contour);
    }
    check(near(totalArea, 8000), "Preserve the seam area across all six panels");

    crossingCuts.remove(diagonal);
    crossingCuts.remove(vertical);
    const auto shortGuide = guide(crossingCuts, {50, 20}, {50, 60});
    check(!cutCrossingGuides(crossingCuts, 0, {slash, shortGuide}).edit && crossingCuts.pieces().size() == 1,
          "Reject a crossing guide with interior endpoints without changing the source");

    auto curvedCuts = polygon({{0, 0}, {100, 0}, {100, 80}, {0, 80}});
    const auto across = guide(curvedCuts, {50, 0}, {50, 80});
    const auto curve = curvedCuts.entities().size();
    curvedCuts.insert(curve, {Shape::Curve, {0, 35}, {100, 35}, {50, 65}, -1, Layer::Construction});
    const auto curvedPanels = cutCrossingGuides(curvedCuts, 0, {curve, across});
    check(curvedPanels.edit.has_value(), qPrintable(curvedPanels.error));
    const auto& curvedResult = curvedPanels.edit->document;
    check(curvedResult.pieces().size() == 4, "Split a quadratic guide at its exact crossing with a straight guide");
    qsizetype curvedEdges = 0;
    for (const auto& edge : curvedResult.entities()) {
        if (edge.shape != Shape::Curve || edge.layer != Layer::Main)
            continue;
        ++curvedEdges;
        check((edge.a == QPointF(50, 50) || edge.b == QPointF(50, 50))
              && (edge.control == QPointF(25, 50) || edge.control == QPointF(75, 50)),
              "Retain the exact quadratic controls on both sides of the cut");
    }
    check(curvedEdges == 4, "Retain a curved seam along both sides of each quadratic segment");

    auto concave = polygon({{0, 0}, {100, 0}, {100, 100}, {70, 100}, {70, 30},
                            {30, 30}, {30, 100}, {0, 100}});
    const auto outside = guide(concave, {0, 70}, {100, 70});
    const auto acrossNotch = modelContour(concave, 0, ContourOperation::Cut, {outside});
    check(!acrossNotch.edit && acrossNotch.error.contains("interior"), "Reject a cut across a concavity");
    const auto alongEdge = guide(concave, {0, 0}, {100, 0});
    check(!modelContour(concave, 0, ContourOperation::Cut, {alongEdge}).edit,
          "Reject a cut on the boundary");

    auto crossing = polygon({{0, 0}, {100, 100}, {100, 0}, {0, 100}});
    const auto crossingGuide = guide(crossing, {0, 50}, {100, 50});
    const auto invalid = modelContour(crossing, 0, ContourOperation::Cut, {crossingGuide});
    check(!invalid.edit && invalid.error.contains("crosses"), "Report a self-intersecting source");
    auto open = polygon({{0, 0}, {100, 0}, {100, 80}, {0, 80}});
    open.remove(0);
    const auto openGuide = guide(open, {0, 40}, {100, 40});
    const auto gap = modelContour(open, 0, ContourOperation::Cut, {openGuide});
    check(!gap.edit && gap.error.contains("open ends"), "Report an open source");

    document.production().rules.append({QStringLiteral("M"), 0, {0, 0}, {2, 0}});
    check(!modelContour(document, 0, ContourOperation::Cut, {slash}).edit
          && document.production().rules.size() == 1, "Guard point grading rules");
}

void testDartTransfer() {
    auto document = polygon({{0, 0}, {100, 0}, {100, 80}, {0, 80}});
    const QPointF apex(50, 35);
    const auto first = guide(document, {40, 0}, apex);
    const auto second = guide(document, apex, {60, 0});
    const auto outlet = guide(document, {100, 40}, apex);
    const auto result = modelContour(document, 0, ContourOperation::TransferDart, {outlet, second, first});
    check(result.edit.has_value(), qPrintable(result.error));
    const auto& edited = result.edit->document;
    const auto geometry = analyzePiece(edited, 0);
    check(geometry.error == ContourError::None && geometry.contour.contains(apex)
          && near(area(geometry.contour), 8000 - 350),
          "Rotate the panel, close the old dart and cut a new wedge through the apex");
    int legs = 0;
    for (const auto& edge : edited.entities()) {
        check(edge.layer == Layer::Main, "Transfer consumes the selected guides");
        if (edge.a == apex || edge.b == apex) {
            ++legs;
            check(near(QLineF(edge.a, edge.b).length(), QLineF(apex, QPointF(100, 40)).length()),
                  "Both relocated dart legs retain the outlet radius");
        }
    }
    check(legs == 2, "Relocated dart is part of the outline");

    QTemporaryDir temp;
    PatternDocument loaded;
    const auto path = temp.filePath("transferred.sofit");
    check(edited.save(path) && loaded.load(path)
          && analyzePiece(loaded, 0).contour == geometry.contour,
          "New contour round-trips through the existing SOFIT file format");

    auto unequal = document.entities()[second];
    unequal.b = {65, 0};
    document.replace(second, unequal);
    const auto rejected = modelContour(document, 0, ContourOperation::TransferDart, {first, second, outlet});
    check(!rejected.edit && rejected.error.contains("equal length"), "Reject unequal old dart legs");
}
}

int main() {
    testSeamTools();
    testLines();
    testCurves();
    testFiles();
    testModeling();
    testContourCutAndSpread();
    testDartTransfer();
}
