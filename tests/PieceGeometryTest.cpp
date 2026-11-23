#include "PieceGeometry.h"
#include "Modeling.h"
#include "PieceMarkings.h"
#include "PatternDxf.h"

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

int main() {
    testLines();
    testCurves();
    testFiles();
    testModeling();
}
