#include "BodiceDraft.h"
#include "ContourModeling.h"
#include "PatternDxf.h"
#include "PatternPdf.h"
#include "Production.h"

#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace opc;

namespace {

void check(bool condition, const QString& message) {
    if (!condition) {
        std::cerr << qPrintable(message) << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(double a, double b) { return std::abs(a - b) < 0.01; }

qsizetype addGuide(PatternDocument& document, QPointF a, QPointF b) {
    const auto index = document.entities().size();
    document.insert(index, {Shape::Line, a, b, {}, -1, Layer::Construction});
    return index;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Open exported file");
    return file.readAll();
}

void roundTripAndExport(const PatternDocument& document, const QString& size = {}) {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Create temporary directory");
    const auto sofit = temporary.filePath("pattern.sofit");
    QString error;
    PatternDocument loaded;
    check(document.save(sofit, &error), "Save project: " + error);
    check(loaded.load(sofit, &error), "Reopen project: " + error);
    check(loaded.parametricDraft() == document.parametricDraft() && loaded.pieces() == document.pieces()
          && saveProduction(loaded.production()) == saveProduction(document.production()),
          "Round trip formulas, size data and piece metadata");
    check(loaded.entities().size() == document.entities().size(), "Round trip all entities");
    for (qsizetype i = 0; i < loaded.entities().size(); ++i) {
        const auto& a = document.entities()[i];
        const auto& b = loaded.entities()[i];
        check(a.shape == b.shape && a.layer == b.layer && a.piece == b.piece && a.draftName == b.draftName
              && a.a == b.a && a.b == b.b && a.control == b.control
              && a.seamAllowanceMm == b.seamAllowanceMm && a.seamNotches == b.seamNotches,
              "Round trip geometry, construction lines and edge marks");
    }
    const auto drawing = gradedPattern(loaded, size);
    QVector<PieceGeometry> geometry;
    QVector<QPolygonF> allowances;
    for (qsizetype i = 0; i < drawing.pieces().size(); ++i) {
        geometry.append(analyzePiece(drawing, i));
        check(geometry.last().error == ContourError::None && !geometry.last().allowanceFailed,
              "Export only valid contours and requested allowances");
        allowances.append(geometry.last().allowance);
    }
    const QVector<bool> visible(drawing.entities().size(), true);
    const auto pdf = temporary.filePath("pattern.pdf");
    check(exportPatternPdf(pdf, drawing, visible, allowances, {}, error), "Export PDF: " + error);
    const auto pdfBytes = readFile(pdf);
    check(pdfBytes.startsWith("%PDF-") && pdfBytes.contains("/Type /Page"), "Write tiled PDF pages");
    const auto dxf = temporary.filePath("pattern.dxf");
    check(exportPatternDxf(dxf, drawing, visible, geometry, error), "Export DXF: " + error);
    const auto dxfBytes = readFile(dxf);
    check(dxfBytes.contains("ENTITIES") && dxfBytes.contains("LINE") && dxfBytes.contains("EOF"),
          "Write pattern geometry in DXF");
}

void checkModeling() {
    PatternDocument panel;
    panel.pieces().append({"Panel", 4});
    panel.insert(0, {Shape::Rectangle, {0, 0}, {100, 80}, {}, 0});
    const auto slash = addGuide(panel, {50, 0}, {50, 80});
    for (const auto operation : {ContourOperation::Pleat, ContourOperation::Gather}) {
        const auto result = modelContour(panel, 0, operation, {slash}, 24);
        check(result.edit.has_value(), result.error);
        const auto& document = result.edit->document;
        const auto geometry = analyzePiece(document, 0);
        const auto markCount = std::ranges::count(document.entities(), Layer::Construction, &Entity::layer);
        check(near(geometry.bounds.width(), 124) && !geometry.allowance.isEmpty()
              && markCount == (operation == ContourOperation::Pleat ? 3 : 2),
              "Fullness adds exactly the requested width and its construction marks");
        roundTripAndExport(document);
    }
    panel.remove(slash);
    panel.production().sizes.append(PatternSize{"L", {}});
    panel.production().rules.append({"L", 0, {100, 80}, {10, 0}});
    const auto shaped = shapeSilhouette(panel, 0, 0, 0.8, 1.5);
    check(shaped.edit.has_value(), shaped.error);
    const auto& shape = shaped.edit->document;
    const auto base = analyzePiece(shape, 0);
    const auto grade = analyzePiece(gradedPattern(shape, "L"), 0);
    check(near(base.bounds.height(), 80) && near(base.bounds.width(), 150)
          && base.contour.contains(QPointF(80, 0)) && near(grade.bounds.width(), 165)
          && !grade.allowanceFailed, "Narrow the top, flare the hem, and remap the graded corner");
    roundTripAndExport(shape, "L");

    PatternDocument curved;
    curved.pieces().append({"Curved panel", 3});
    curved.insert(0, {Shape::Line, {0, 0}, {100, 0}, {}, 0});
    curved.insert(1, {Shape::Line, {100, 0}, {100, 80}, {}, 0});
    curved.insert(2, {Shape::Curve, {100, 80}, {0, 80}, {50, 100}, 0, Layer::Main, {}, 5, {0.5}});
    curved.insert(3, {Shape::Line, {0, 80}, {0, 0}, {}, 0});
    const auto widened = shapeSilhouette(curved, 0, 50, 1.25, 1.25);
    check(widened.edit.has_value(), widened.error);
    const auto& curve = widened.edit->document.entities()[2];
    const auto wideGeometry = analyzePiece(widened.edit->document, 0);
    check(curve.shape == Shape::Curve && curve.seamAllowanceMm == 5
          && curve.seamNotches == QVector<double>{0.5} && wideGeometry.notchAnchors.size() == 1
          && !wideGeometry.allowance.isEmpty(), "Preserve quadratic edge, custom allowance and notch");
    roundTripAndExport(widened.edit->document);

    panel.pieces()[0].allowanceMm = 0;
    const QPointF apex(50, 35);
    const auto left = addGuide(panel, {40, 0}, apex);
    const auto right = addGuide(panel, apex, {60, 0});
    const auto outlet = addGuide(panel, apex, {100, 40});
    check(!modelContour(panel, 0, ContourOperation::TransferDartToOutlet, {left, right, outlet}).edit,
          "Do not invalidate an existing dart panel grade rule");
    panel.production().rules.clear();
    const auto transferred = modelContour(panel, 0, ContourOperation::TransferDartToOutlet, {left, right, outlet});
    check(transferred.edit.has_value(), transferred.error);
    auto open = transferred.edit->document;
    QVector<qsizetype> guides;
    for (qsizetype i = 0; i < open.entities().size(); ++i) {
        const auto& edge = open.entities()[i];
        if (edge.a == apex || edge.b == apex)
            guides.append(i);
    }
    check(guides.size() == 2, "Find the two V edges in the actual cutting contour");
    const auto nextEdge = std::ranges::max_element(open.entities(), {}, [](const Entity& edge) {
        return QLineF(edge.a, edge.b).length();
    });
    const auto nextOutlet = (nextEdge->a + nextEdge->b) / 2;
    guides.append(addGuide(open, apex, nextOutlet));
    const auto moved = modelContour(open, 0, ContourOperation::MoveOpenDart, guides);
    check(moved.edit.has_value(), moved.error);
    const auto outline = analyzePiece(moved.edit->document, 0);
    check(outline.error == ContourError::None && outline.contour.contains(apex)
          && std::ranges::any_of(outline.contour, [nextOutlet](QPointF point) {
              return QLineF(point, nextOutlet).length() < 0.001;
          }), "Close the previous V and open it on a different edge");
    roundTripAndExport(moved.edit->document);
}

void checkTemplates() {
    const BodiceMeasurements m{920, 740, 420, 210, 68, 75, 22, 116, 38, 60, 40,
                               115, 300, 50, 220, 285, 87};
    const auto normal = draftBodiceAndSleeve(m);
    check(normal.has_value(), "Draft the original set-in sleeve");
    const auto originalSeams = measureBodiceSeams(*normal);
    check(originalSeams.has_value(), "Measure original cap");

    for (const auto finish : {NeckFinish::FlatCollar, NeckFinish::Stand, NeckFinish::Facing}) {
        BodiceOptions options{finish};
        options.sleeveStyle = finish == NeckFinish::Facing ? SleeveStyle::Wide : SleeveStyle::Puff;
        QString error;
        auto draft = draftBodiceAndSleeve(m, options, &error);
        check(draft.has_value(), error);
        draft->production().sizes.append({"L", {{"neck_width", 78}, {"back_neck_depth", 25},
                                                {"front_neck_depth", 83}, {"sleeve_extra_width", 120},
                                                {"cuff", 240}, {"sleeve_cap_gather", 45}}});
        for (const auto& size : {QString{}, QString("L")}) {
            const auto checkResult = checkFittingBlock(*draft, size);
            check(checkResult.invalidPieces.isEmpty() && checkResult.missingSeams.isEmpty(),
                  "Closed base and graded contours with all sewing sections measured");
            int finishMatches = 0;
            int bandMatches = 0;
            for (const auto& seam : checkResult.seams) {
                if (seam.label.contains("neckline")) {
                    ++finishMatches;
                    check(near(seam.first, seam.second), "Match each neckline section after grading");
                }
                if (seam.label.contains("gathered hem")) {
                    ++bandMatches;
                    check(near(seam.second - seam.first, size.isEmpty() ? 100 : 120)
                          && near(seam.second - seam.first, seam.targetDifference),
                          "Measure the exact gathering excess over the sleeve band");
                }
                if (seam.label.contains("sleeve cap") && options.sleeveStyle == SleeveStyle::Puff)
                    check(near(seam.targetDifference, size.isEmpty() ? 39 : 49),
                          "Size report uses the intended gathering rather than the achieved intake");
            }
            check(finishMatches == 2 && bandMatches == (options.sleeveStyle == SleeveStyle::Puff ? 1 : 0),
                  "Report front and back neckline matching and puff hem matching");
        }
        const auto sleeve = measureBodiceSeams(*draft);
        check(sleeve.has_value(), "Measure the modeled sleeve cap");
        if (options.sleeveStyle == SleeveStyle::Wide) {
            check(near(sleeve->frontCap, originalSeams->frontCap)
                  && near(sleeve->backCap, originalSeams->backCap), "Wide hem preserves the existing cap");
        } else {
            const auto geometry = analyzePiece(*draft, 2);
            check(sleeve->frontCap > originalSeams->frontCap && sleeve->backCap > originalSeams->backCap
                  && near(geometry.bounds.height(), m.sleeveLength) && geometry.notchAnchors.size() == 5,
                  "Puff adds cap length, preserves finished length and adds matching marks");
        }
        draft->pieces()[2].allowanceMm = 5;
        if (options.sleeveStyle == SleeveStyle::Puff)
            draft->pieces()[3].allowanceMm = 5;
        roundTripAndExport(*draft, "L");
        const auto firstNeck = std::ranges::find(draft->entities(), QString("front_neck"), &Entity::draftName);
        draft->production().rules.append({"L", 0, firstNeck->control, {20, 15}});
        const auto changed = checkFittingBlock(*draft, "L");
        check(std::ranges::any_of(changed.seams, [](const auto& seam) {
            return seam.label.startsWith("Front neckline") && std::abs(seam.second - seam.first) > 1;
        }), "Report neckline mismatch introduced by manual grading");
    }
    const auto dress = draftDress(m, {1000, 60, 195, 620, 160, 100}, BodiceOptions{NeckFinish::Facing});
    check(dress && dress->pieces().size() == 6 && checkFittingBlock(*dress).missingSeams.isEmpty(),
          "Add facings to the sleeveless dress without losing the skirt seam checks");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    checkModeling();
    checkTemplates();
}
