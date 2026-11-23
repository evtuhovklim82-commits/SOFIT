#include "BodiceDraft.h"
#include "ContourModeling.h"
#include "PatternDxf.h"
#include "PatternPdf.h"
#include "PatternTemplates.h"
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
          && loaded.modelingHistory() == document.modelingHistory()
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

PatternDocument formulaPanel(const ParametricDraft& draft, const QStringList& construction) {
    PatternDocument panel;
    QString error;
    check(panel.setParametricDraft(draft, &error), error);
    panel.pieces().append({"Panel"});
    for (qsizetype i = 0; i < panel.entities().size(); ++i) {
        auto entity = panel.entities()[i];
        if (entity.shape != Shape::Line)
            continue;
        if (construction.contains(entity.draftName))
            entity.layer = Layer::Construction;
        else
            entity.piece = 0;
        panel.replace(i, entity);
    }
    return panel;
}

qsizetype namedEdge(const PatternDocument& panel, const QString& name) {
    const auto edge = std::ranges::find(panel.entities(), name, &Entity::draftName);
    check(edge != panel.entities().end(), "Find " + name);
    return edge - panel.entities().begin();
}

void checkContourHistory() {
    ParametricDraft draft;
    draft.measurements = {{"width", 100}, {"height", 80}, {"cut_y", 30}};
    draft.points = {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "height"},
                    {"D", "0", "height"}, {"E", "0", "cut_y"}, {"F", "width", "cut_y"}};
    draft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
                   {"left", "D", "A", {}}, {"cut", "E", "F", {}}};
    auto panel = formulaPanel(draft, {"cut"});
    panel.pieces()[0].allowanceMm = 2;
    panel.production().sizes.append({"L", {{"width", 140}, {"height", 100}, {"cut_y", 40}}});
    QString error;
    for (const auto operation : {ContourOperation::Cut, ContourOperation::Relief, ContourOperation::Yoke}) {
        auto split = panel;
        check(split.appendContour(0, operation, {namedEdge(split, "cut")}, 0, &error), error);
        check(split.pieces().size() == 2, "Create two ordered panels");
        const auto a = namedEdge(split, "@1:seam-A");
        const auto b = namedEdge(split, "@1:seam-B");
        const auto left = split.entities()[a];
        const auto right = split.entities()[b];
        check(left.a == right.b && left.b == right.a && near(*seamLength(left), 100),
              "Create oppositely directed matching seams");
        for (const auto [index, notch] : {std::pair{a, 0.25}, std::pair{b, 0.75}}) {
            auto edge = split.entities()[index];
            edge.seamNotches = {notch};
            split.replace(index, edge);
        }
        const auto graded = gradedPattern(split, "L");
        check(near(*seamLength(graded.entities()[a]), 140)
              && near(*seamLength(graded.entities()[b]), 140)
              && analyzePiece(graded, 0).notchAnchors.size() == 1
              && analyzePiece(graded, 1).notchAnchors.size() == 1,
              "Replay both seams, allowances and marks for the graded size");
        roundTripAndExport(split, "L");

        auto resized = draft;
        resized.measurements[0].mm = 110;
        std::ranges::reverse(resized.edges);
        check(split.setParametricDraft(resized, &error), error);
        check(near(*seamLength(split.entities()[namedEdge(split, "@1:seam-A")]), 110)
              && split.entities()[namedEdge(split, "@1:seam-B")].seamNotches == QVector<double>{0.75},
              "Keep generated edge identities and metadata independent of formula table order");
        check(split.appendSilhouette(1, 0.5, 1.1, 1.1, &error), error);
        check(near(*seamLength(split.entities()[namedEdge(split, "@1:seam-B")]), 121),
              "Compose a silhouette operation on an output panel");
        roundTripAndExport(split, "L");

        auto grading = split.production();
        grading.sizes[0].measurements["cut_y"] = 0;
        check(!split.validateParametricSizes(grading, &error)
              && error.contains("Size 'L'") && error.contains("Modeling step 1"), error);
        auto missing = resized;
        missing.edges.removeFirst();
        check(!split.setParametricDraft(missing, &error) && split.parametricDraft() == resized,
              "Reject a missing formula guide atomically");
        check(split.removeLastModelingStep(&error) && split.removeLastModelingStep(&error), error);
        check(split.pieces().size() == 1 && !split.hasContourHistory() && split.modelingHistory().isEmpty()
              && split.entities()[namedEdge(split, "cut")].layer == Layer::Construction,
              "Removing the split restores the source bindings and removes the second panel");
    }

    for (const auto operation : {ContourOperation::Spread, ContourOperation::Pleat, ContourOperation::Gather}) {
        auto spread = panel;
        check(spread.appendContour(0, operation, {namedEdge(spread, "cut")}, 24, &error), error);
        check(near(analyzePiece(spread, 0).bounds.height(), 104)
              && near(analyzePiece(gradedPattern(spread, "L"), 0).bounds.height(), 124),
              "Apply exactly the fixed added fullness in base and graded sizes");
        check(spread.appendSilhouette(0, 0.5, 1, 1, &error, 1.25), error);
        const auto originalHistory = spread.modelingHistory();
        const auto edgeName = spread.entities()[0].draftName;
        auto settings = originalHistory[0];
        settings.spreadMm = 40;
        check(spread.editModelingStep(0, settings, &error), error);
        check(near(analyzePiece(spread, 0).bounds.height(), 150)
              && near(analyzePiece(gradedPattern(spread, "L"), 0).bounds.height(), 175)
              && spread.entities()[0].draftName == edgeName
              && spread.modelingHistory()[1] == originalHistory[1],
              "Update a prior width and replay the downstream length in both sizes");
        settings = spread.modelingHistory()[1];
        settings.lengthScale = 0;
        check(!spread.editModelingStep(1, settings, &error) && error.contains("Modeling step 2")
              && near(analyzePiece(spread, 0).bounds.height(), 150)
              && spread.modelingHistory()[1] == originalHistory[1],
              "An invalid replay preserves the last valid geometry and history");
        roundTripAndExport(spread, "L");
    }

    auto dependent = panel;
    dependent.production().sizes[0].measurements["cut_y"] = 60;
    check(dependent.appendSilhouette(0, 0.5, 1, 1, &error)
          && dependent.appendContour(0, ContourOperation::Cut, {namedEdge(dependent, "cut")}, 0, &error),
          error);
    const auto before = dependent.modelingHistory();
    auto invalidLength = before[0];
    invalidLength.lengthScale = 0.5;
    check(!dependent.editModelingStep(0, invalidLength, &error) && error.contains("Size 'L'")
          && error.contains("Modeling step 2") && dependent.modelingHistory() == before
          && near(*seamLength(dependent.entities()[namedEdge(dependent, "@2:seam-A")]), 100),
          "A downstream failure isolated to a graded size must roll back the entire edit: " + error);

    auto doubleCut = panel;
    auto twoGuides = draft;
    twoGuides.points += {{"G", "0", "cut_y / 2"}, {"H", "width", "cut_y / 2"}};
    twoGuides.edges.append({"otherCut", "G", "H", {}});
    check(doubleCut.setParametricDraft(twoGuides, &error), error);
    auto guideIndex = namedEdge(doubleCut, "otherCut");
    auto guide = doubleCut.entities()[guideIndex];
    guide.layer = Layer::Construction;
    doubleCut.replace(guideIndex, guide);
    check(doubleCut.appendContour(0, ContourOperation::Yoke, {namedEdge(doubleCut, "cut")}, 0, &error), error);
    const auto upper = analyzePiece(doubleCut, 0).bounds.top() == 0 ? 0 : 1;
    check(doubleCut.appendContour(upper, ContourOperation::Cut,
                                 {namedEdge(doubleCut, "otherCut")}, 0, &error), error);
    check(doubleCut.pieces().size() == 3 && doubleCut.modelingHistory().size() == 2,
          "Replay a second cut on generated edges");
    roundTripAndExport(doubleCut, "L");

    const auto seam = namedEdge(doubleCut, "@2:seam-A");
    auto marked = doubleCut.entities()[seam];
    marked.seamNotches = {0.25};
    doubleCut.replace(seam, marked);
    QTemporaryDir temporary;
    check(temporary.isValid(), "Create a directory for the SOFIT 1.8 compatibility fixture");
    const auto legacyPath = temporary.filePath("numeric-seam-names.sofit");
    check(doubleCut.save(legacyPath, &error), error);
    auto legacy = readFile(legacyPath);
    legacy.replace("@1:seam-A", "@1:seam-65").replace("@1:seam-B", "@1:seam-66")
          .replace("@2:seam-A", "@2:seam-65").replace("@2:seam-B", "@2:seam-66");
    QFile file(legacyPath);
    check(file.open(QIODevice::WriteOnly) && file.write(legacy) == legacy.size(),
          "Write the numeric seam names produced by SOFIT 1.8");
    file.close();
    PatternDocument reopened;
    check(reopened.load(legacyPath, &error), error);
    check(reopened.modelingHistory() == doubleCut.modelingHistory()
          && reopened.entities()[namedEdge(reopened, "@2:seam-A")].seamNotches == marked.seamNotches,
          "Restore canonical seam references and preserve notches when opening SOFIT 1.8 files");
    roundTripAndExport(reopened, "L");

    check(doubleCut.removeLastModelingStep(&error) && doubleCut.pieces().size() == 2, error);
    check(namedEdge(doubleCut, "@1:seam-A") >= 0 && namedEdge(doubleCut, "@1:seam-B") >= 0
          && doubleCut.entities()[namedEdge(doubleCut, "otherCut")].layer == Layer::Construction,
          "Removing a dependent cut restores the previous stable edges and its guide");

    auto changedTopology = panel;
    auto size = changedTopology.production();
    size.sizes.clear();
    changedTopology.production() = size;
    // At the larger width the cut starts on the top edge and ends on the right;
    // both cuts are geometrically valid but produce different edge identities.
    auto shifting = draft;
    shifting.points[4].x = "width - 100";
    shifting.points[4].y = "cut_y * (width - 140) * (width - 140) / 1600";
    check(changedTopology.setParametricDraft(shifting, &error), error);
    check(changedTopology.appendContour(0, ContourOperation::Cut, {namedEdge(changedTopology, "cut")}, 0, &error),
          error);
    size.sizes.append({"L", {{"width", 140}}});
    check(!changedTopology.validateParametricSizes(size, &error)
          && error.contains("connectivity changed"), error);

    draft.points += {{"P", "width/2", "35"}, {"M1", "width/2-10", "0"}, {"M2", "width/2+10", "0"},
                     {"O", "width", "40"}};
    draft.edges += {{"legA", "M1", "P", {}}, {"legB", "P", "M2", {}}, {"outlet", "P", "O", {}}};
    auto dart = formulaPanel(draft, {"cut", "legA", "legB", "outlet"});
    dart.production().sizes.append({"L", {{"width", 130}, {"height", 85}}});
    check(dart.appendContour(0, ContourOperation::TransferDartToOutlet,
                            {namedEdge(dart, "legA"), namedEdge(dart, "legB"), namedEdge(dart, "outlet")}, 0, &error),
          error);
    check(analyzePiece(dart, 0).contour.contains(QPointF(50, 35))
          && analyzePiece(gradedPattern(dart, "L"), 0).contour.contains(QPointF(65, 35)),
          "Transfer the dart around the formula apex in both sizes");
    roundTripAndExport(dart, "L");

    draft.edges.removeFirst();
    draft.edges += {{"topLeft", "A", "M1", {}}, {"topRight", "M2", "B", {}}};
    auto openDart = formulaPanel(draft, {"cut", "outlet"});
    openDart.production().sizes.append({"L", {{"width", 130}, {"height", 85}}});
    check(openDart.appendContour(0, ContourOperation::MoveOpenDart,
                                {namedEdge(openDart, "legA"), namedEdge(openDart, "legB"), namedEdge(openDart, "outlet")},
                                0, &error), error);
    check(analyzePiece(openDart, 0).contour.contains(QPointF(100, 40))
          && analyzePiece(gradedPattern(openDart, "L"), 0).contour.contains(QPointF(130, 40)),
          "Close the formula V and open a new outlet in both sizes");
    roundTripAndExport(openDart, "L");
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

void checkApronKit() {
    const ApronMeasurements measurements{700, 780, 260, 250, 600, 420, 25};
    QString error;
    auto invalid = measurements;
    invalid.bibWidthMm = invalid.widthMm;
    check(!draftBibApron(invalid, &error) && !error.isEmpty(), "Reject an oversized apron bib");

    auto draft = draftBibApron(measurements, &error);
    check(draft.has_value(), error);
    draft->production().sizes.append({"Long", {{"length", 850}, {"waist_tie_length", 660}, {"tie_width", 30}}});
    auto invalidGrade = draft->production();
    invalidGrade.sizes[0].measurements["bib_width"] = 750;
    check(!draft->validateParametricSizes(invalidGrade, &error) && error.contains("Size 'Long'")
          && error.contains("arm_slope_width"), "Reject a size that violates the same bib constraint");
    const auto graded = gradedPattern(*draft, "Long");
    check(graded.pieces().size() == 3, "Grade all three apron pieces");
    const auto body = analyzePiece(graded, 0);
    const auto waist = analyzePiece(graded, 1);
    const auto neck = analyzePiece(graded, 2);
    check(near(body.bounds.width(), 700) && near(body.bounds.height(), 850)
          && near(waist.bounds.width(), 60) && near(waist.bounds.height(), 660)
          && near(neck.bounds.width(), 60) && near(neck.bounds.height(), 420)
          && near(waist.allowance.boundingRect().width(), 80),
          "Preserve formula dimensions and cut allowance after grading");
    for (const auto& [i, count] : {std::pair{0, 1}, std::pair{1, 2}, std::pair{2, 2}}) {
        check(graded.pieces()[i].cutCount == count, "Keep apron kit quantities");
        for (int copy = 0; copy < count; ++copy)
            draft->production().placements.append({i, "Long", {}});
    }
    const auto arranged = arrangeFabric(*draft, draft->production());
    check(arranged.error == FabricLayoutError::None, "Arrange the graded apron kit");
    draft->production().placements = arranged.placements;
    const auto marker = cuttingReport(*draft, draft->production());
    check(marker.canExport(draft->production().fabricWidth), "Check apron layout geometry");
    const auto rows = cuttingKitRows(*draft, marker);
    check(rows.size() == 3 && std::ranges::all_of(rows, [&](const auto& row) {
        return row.copies() == draft->pieces()[row.piece].cutCount;
    }), "Verify every piece in the graded kit");
    roundTripAndExport(*draft, "Long");

    QTemporaryDir directory;
    check(directory.isValid(), "Create apron export directory");
    const auto pdf = directory.filePath("apron-marker.pdf");
    const auto dxf = directory.filePath("apron-marker.dxf");
    check(exportFabricPdf(pdf, *draft, draft->production(), {}, error), error);
    check(exportFabricDxf(dxf, *draft, draft->production(), error), error);
    check(readFile(pdf).startsWith("%PDF-") && readFile(pdf).trimmed().endsWith("%%EOF")
          && readFile(dxf).contains("ENTITIES"), "Read back both apron marker exports");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    checkModeling();
    checkContourHistory();
    checkTemplates();
    checkApronKit();
}
