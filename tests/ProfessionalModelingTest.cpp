#include "BodiceDraft.h"
#include "ContourModeling.h"
#include "FittingBlocks.h"
#include "Modeling.h"
#include "PieceMarkings.h"
#include "PatternDxf.h"
#include "PatternPdf.h"
#include "PatternTemplates.h"
#include "Production.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
          && loaded.seamLinks() == document.seamLinks()
          && loaded.productInformation() == document.productInformation()
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
    if (!drawing.inspectSeamLinks().notches.isEmpty())
        check(dxfBytes.contains("\n0\nLINE\n100\nAcDbEntity\n8\nNOTCH\n"), "Write actual linked notch lines to DXF");
}

void checkModeling() {
    const Entity ref{Shape::Line, {0, 0}, {10, 0}};
    const Entity secondCurve{Shape::Curve, {10, 0}, {40, 15}, {13, 4}};
    const auto tangent = tangentCurve(ref, secondCurve);
    check(tangent && tangent->control == QPointF(15, 0) && tangent->a == secondCurve.a
          && tangent->b == secondCurve.b, "Align the quadratic tangent and keep the five mm handle");
    auto reversed = secondCurve;
    std::swap(reversed.a, reversed.b);
    const auto reverseTangent = tangentCurve(ref, reversed);
    check(reverseTangent && reverseTangent->control == QPointF(15, 0),
          "Use the same tangent when the selected quadratic runs in reverse");

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
    auto curvedDart = curved;
    auto hem = curvedDart.entities()[2];
    hem.seamAllowanceMm.reset();
    hem.seamNotches.clear();
    curvedDart.replace(2, hem);
    const auto curvedLeft = addGuide(curvedDart, {40, 0}, apex);
    const auto curvedRight = addGuide(curvedDart, apex, {60, 0});
    const auto curvedOutlet = addGuide(curvedDart, apex, {50, 90});
    const auto curvedTransfer = modelContour(curvedDart, 0, ContourOperation::TransferDartToOutlet,
                                             {curvedLeft, curvedRight, curvedOutlet});
    check(curvedTransfer.edit.has_value(), curvedTransfer.error);
    const auto& curvedOutput = curvedTransfer.edit->document;
    double hemLength = 0;
    int curvedEdges = 0;
    for (const auto& edge : curvedOutput.entities()) {
        if (edge.shape == Shape::Curve) {
            hemLength += seamLength(edge).value_or(0);
            ++curvedEdges;
        }
    }
    check(curvedEdges == 2 && std::abs(hemLength - seamLength(hem).value_or(0)) < 0.003,
          "Transfer a dart to the middle of a quadratic edge and preserve both exact subcurves");
    roundTripAndExport(curvedOutput);

    auto splitSource = curvedDart;
    splitSource.pieces()[0].allowanceMm = 0;
    const auto secondOutlet = addGuide(splitSource, apex, {100, 40});
    const auto splitDart = modelContour(splitSource, 0, ContourOperation::SplitDart,
                                        {curvedLeft, curvedRight, curvedOutlet, secondOutlet}, 35);
    check(splitDart.edit.has_value(), "Split dart: " + splitDart.error);
    check(!modelContour(splitSource, 0, ContourOperation::SplitDart,
                        {curvedLeft, curvedRight, curvedOutlet, secondOutlet}, 100).edit,
          "Reject full intake through one outlet in the splitting operation");
    const auto& splitDocument = splitDart.edit->document;
    const auto& splitEdges = splitDocument.entities();
    double splitIntake = 0;
    int dartTips = 0;
    for (qsizetype i = 0; i + 1 < splitEdges.size(); ++i) {
        const auto& a = splitEdges[i];
        const auto& b = splitEdges[i + 1];
        if (a.shape != Shape::Line || b.shape != Shape::Line
            || QLineF(a.b, apex).length() > 13)
            continue;
        check(near(QLineF(a.a, a.b).length(), QLineF(b.a, b.b).length()),
              "Each shortened split dart has equal legs");
        const auto v = a.a - apex, w = b.b - apex;
        const auto intake = std::abs(std::atan2(v.x() * w.y() - v.y() * w.x(), QPointF::dotProduct(v, w)));
        const double share = intake / (2 * std::atan(10.0 / 35));
        check(near(share, 0.35) || near(share, 0.65), "Open each outlet by its own intake share");
        splitIntake += intake;
        ++dartTips;
    }
    check(dartTips == 2 && near(splitIntake, 2 * std::atan(10.0 / 35)),
          "Splitting preserves total angular intake");
    roundTripAndExport(splitDocument);

    auto mergeSource = panel;
    const auto bottomLeft = addGuide(mergeSource, apex, {45, 80});
    const auto bottomRight = addGuide(mergeSource, apex, {55, 80});
    const auto merged = modelContour(mergeSource, 0, ContourOperation::MergeDarts,
                                     {left, right, bottomLeft, bottomRight, outlet});
    check(merged.edit.has_value(), "Merge darts: " + merged.error);
    QVector<QPointF> legs;
    for (const auto& edge : merged.edit->document.entities()) {
        if (edge.a == apex)
            legs.append(edge.b - apex);
        if (edge.b == apex)
            legs.append(edge.a - apex);
    }
    check(legs.size() == 2 && near(QLineF({}, legs[0]).length(), QLineF({}, legs[1]).length()),
          "Merged dart has two equal legs");
    const auto v = legs[0], w = legs[1];
    check(near(std::abs(std::atan2(v.x() * w.y() - v.y() * w.x(), QPointF::dotProduct(v, w))),
               2 * std::atan(10.0 / 35) + 2 * std::atan(5.0 / 45)),
          "Merging preserves total angular intake");
    roundTripAndExport(merged.edit->document);

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
        if (entity.shape != Shape::Line && !(entity.shape == Shape::Curve && construction.contains(entity.draftName)))
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

void checkSeamLinks() {
    ParametricDraft draft;
    draft.measurements = {{"join_x", 60}, {"width", 100}, {"mate", 100}};
    draft.points = {{"A", "0", "0"}, {"B", "join_x", "0"}, {"C", "width", "0"},
                    {"D", "width", "40"}, {"E", "0", "40"},
                    {"F", "0", "80"}, {"G", "mate", "80"}, {"H", "mate", "130"},
                    {"I", "0", "130"}};
    draft.edges = {{"left_top", "A", "B", {}}, {"right_top", "B", "C", {}},
                   {"right", "C", "D", {}}, {"bottom", "D", "E", {}}, {"left", "E", "A", {}},
                   {"mate_top", "F", "G", {}}, {"mate_right", "G", "H", {}},
                   {"mate_bottom", "H", "I", {}}, {"mate_left", "I", "F", {}}};
    auto panel = formulaPanel(draft, {});
    panel.pieces().append({"Mate", 2});
    panel.pieces()[0].allowanceMm = 2;
    for (qsizetype i = 0; i < panel.entities().size(); ++i) {
        auto entity = panel.entities()[i];
        if (!entity.draftName.startsWith("mate_"))
            continue;
        entity.piece = 1;
        panel.replace(i, entity);
    }
    panel.production().sizes.append({"L", {{"join_x", 40}, {"width", 110}, {"mate", 115}}});
    panel.production().sizes.append({"XL", {{"join_x", 70}, {"width", 125}, {"mate", 125}}});
    const SeamLink link{{"left_top", "right_top"}, {"mate_top"}, false, true, 0, 1, 1, 5};
    QString error;
    check(panel.addSeamLink(link, &error), "Link two sewing chains: " + error);
    check(!panel.addSeamLink(link, &error) && panel.seamLinks().size() == 1,
          "Reject a duplicate sewing link");
    auto conflict = link;
    conflict.allowanceMm = 6;
    check(!panel.addSeamLink(conflict, &error) && error.contains("different allowances"),
          "Reject overlapping links with different shared widths");
    auto revised = link;
    revised.allowanceMm = 4;
    check(panel.editSeamLink(0, revised, &error) && panel.editSeamLink(0, link, &error), error);
    const auto base = panel.inspectSeamLinks();
    check(near(base.allowances.value(namedEdge(panel, "left_top")), 5)
          && near(base.allowances.value(namedEdge(panel, "mate_top")), 5)
          && near(analyzePiece(panel, 0).allowance.boundingRect().top(), -5)
          && near(analyzePiece(panel, 1).allowance.boundingRect().top(), 75)
          && base.checks.front().withinTolerance
          && near(base.notches.value(namedEdge(panel, "left_top")).front(), 50.0 / 60),
          "Put the base midpoint on the first edge of the chain");
    for (int replay = 0; replay < 3; ++replay) {
        const auto large = gradedPattern(panel, "L");
        const auto report = large.inspectSeamLinks();
        check(!report.checks.front().withinTolerance
              && near(report.checks.front().secondMm - report.checks.front().firstMm, 5)
              && !report.notches.contains(namedEdge(large, "left_top"))
              && near(report.notches.value(namedEdge(large, "right_top")).front(), 15.0 / 70)
              && analyzePiece(large, 0).notchAnchors.size() == 1
              && analyzePiece(large, 1).notchAnchors.size() == 1,
              "Recalculate chain position, sewing ease and both marks in every replay");
        const auto extraLarge = gradedPattern(panel, "XL").inspectSeamLinks();
        check(extraLarge.checks.front().withinTolerance, "Use independent size measurements");
        check(panel.setParametricDraft(draft, &error), error);
    }

    auto grades = panel.production();
    grades.sizes[0].measurements["join_x"] = 55;
    check(!panel.validateParametricSizes(grades, &error) && error.contains("L")
          && error.contains("junction") && panel.seamLinks() == QVector<SeamLink>{link},
          "Reject a notch junction specific to one size without committing the change");
    auto broken = draft;
    broken.edges.removeFirst();
    check(!panel.setParametricDraft(broken, &error) && error.contains("Seam link 1")
          && panel.parametricDraft() == draft, "Roll back a formula edit that removes a linked edge");
    check(panel.appendSilhouette(1, 0.5, 1.05, 1.05, &error), error);
    check(!panel.inspectSeamLinks().checks.front().withinTolerance,
          "Report ease from the replayed modeling result");
    roundTripAndExport(panel, "L");
    roundTripAndExport(gradedPattern(panel, "L"));
    check(panel.removeSeamLink(0) && panel.inspectSeamLinks().notches.isEmpty()
          && analyzePiece(panel, 0).notchAnchors.isEmpty(),
          "Remove linked notches without leaving stored edge marks");
}

void checkLinkedTrouserBlock() {
    QString error;
    auto trousers = draftTrousersWithSeamLinks({740, 1000, 40, 60, 195, 270, 600, 1030, 460, 380, 100}, &error);
    check(trousers && trousers->seamLinks().size() == 3, "Create the linked trouser block: " + error);
    auto production = trousers->production();
    production.sizes.append({"L", {{"waist", 820}, {"hip", 1080}, {"length", 1060}, {"knee", 490}}});
    check(trousers->validateParametricSizes(production, &error), error);
    trousers->production() = production;
    const auto grade = gradedPattern(*trousers, "L");
    check(grade.inspectSeamLinks().checks.size() == 3
          && analyzePiece(grade, 0).notchAnchors.size() == 2
          && analyzePiece(grade, 1).notchAnchors.size() == 2,
          "Preconfigured inseam marks follow the size table");
    roundTripAndExport(*trousers, "L");
}

void checkRotationHistory() {
    ParametricDraft draft;
    draft.measurements = {{"width", 100}, {"pivot_offset", 0}};
    draft.points = {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "width"},
                    {"D", "0", "width"}, {"P", "width / 2", "width / 2 + pivot_offset"},
                    {"E", "width / 2", "0"}, {"F", "width / 2", "width"},
                    {"Q", "width * 0.7", "width / 2"}};
    draft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
                   {"left", "D", "A", {}}, {"guide", "E", "F", {}}, {"curve", "E", "F", "Q"}};
    auto pattern = formulaPanel(draft, {"guide"});
    const auto curve = namedEdge(pattern, "curve");
    auto edge = pattern.entities()[curve];
    edge.layer = Layer::Construction;
    pattern.replace(curve, edge);
    pattern.production().sizes.append({"L", {{"width", 140}}});
    const auto guide = namedEdge(pattern, "guide");
    const auto pivot = namedEdge(pattern, "P");
    const auto point = namedEdge(pattern, "Q");
    const auto selection = QVector<qsizetype>{guide, point, curve};
    QString error;

    check(!pattern.appendRotation({guide, guide}, pivot, 90, &error)
          && !pattern.appendRotation({guide, pivot}, pivot, 90, &error)
          && !pattern.appendRotation({namedEdge(pattern, "top")}, pivot, 90, &error)
          && !pattern.appendRotation(selection, guide, 90, &error)
          && pattern.modelingHistory().isEmpty() && pattern.entities()[guide].a == QPointF(50, 0),
          "Reject invalid or ambiguous selections atomically");
    check(pattern.appendRotation(selection, pivot, 90, &error), error);
    for (const auto& size : {QString{}, QStringLiteral("L")}) {
        QVector<qsizetype> sources;
        const auto grade = gradedPattern(pattern, size, &sources);
        const double width = size.isEmpty() ? 100 : 140;
        check(sources.contains(guide) && sources.contains(curve) && sources.contains(point),
              "Keep all transformed geometry in the graded output");
        const auto& line = grade.entities()[sources.indexOf(guide)];
        const auto& quadratic = grade.entities()[sources.indexOf(curve)];
        const auto& control = grade.entities()[sources.indexOf(point)];
        check(QLineF(line.a, {width, width / 2}).length() < 0.001
              && QLineF(line.b, {0, width / 2}).length() < 0.001
              && quadratic.a == line.a && quadratic.b == line.b && quadratic.control == control.a
              && near(control.a.x(), width / 2) && near(control.a.y(), width * 0.7)
              && near(analyzePiece(grade, 0).bounds.width(), width),
              "Rigidly rotate points, lines and curve controls around each size's pivot");
    }
    roundTripAndExport(pattern, "L");
    auto edited = pattern;
    auto settings = pattern.modelingHistory().first();
    settings.rotationDegrees = -90;
    check(edited.editModelingStep(0, settings, &error)
          && edited.entities()[guide].a == pattern.entities()[guide].b, "Replay edited angle from formulas");
    check(edited.removeLastModelingStep(&error) && edited.entities()[guide].a == QPointF(50, 0),
          "Removing rotation restores formula geometry");

    check(pattern.appendContour(0, ContourOperation::Cut, {guide}, 0, &error), error);
    QTemporaryDir sequenceDirectory;
    const auto sequenceFile = sequenceDirectory.filePath("sequence.sofit");
    check(pattern.save(sequenceFile, &error), error);
    PatternDocument savedSequence;
    check(savedSequence.load(sequenceFile, &error), error);
    auto replayed = edited;
    check(replayed.applyModelingSequence(savedSequence, &error), error);
    check(replayed.modelingHistory() == pattern.modelingHistory()
          && replayed.pieces() == pattern.pieces()
          && namedEdge(replayed, "@2:seam-A") >= 0,
          "Replay a saved rotation and the contour cut that depends on its output");
    auto incompatible = edited;
    auto invalidGuide = incompatible.entities()[guide];
    invalidGuide.layer = Layer::Main;
    incompatible.replace(guide, invalidGuide);
    check(!incompatible.applyModelingSequence(savedSequence, &error)
          && incompatible.modelingHistory().isEmpty() && incompatible.pieces().size() == 1,
          "An incompatible operation leaves the destination unchanged");
    const auto original = pattern.modelingHistory();
    const auto stableName = QStringLiteral("@2:seam-A");
    const auto seam = pattern.entities()[namedEdge(pattern, stableName)];
    settings.rotationDegrees = 22.5;
    check(!pattern.editModelingStep(0, settings, &error) && error.contains("Modeling step 2")
          && pattern.modelingHistory() == original
          && pattern.entities()[namedEdge(pattern, stableName)].a == seam.a,
          "An invalid downstream cut rolls back the edited angle, contour and generated references");
    auto incompatibleSizes = pattern.production();
    incompatibleSizes.sizes[0].measurements.insert("pivot_offset", 5);
    check(!pattern.validateParametricSizes(incompatibleSizes, &error) && error.contains("Size 'L'")
          && error.contains("Modeling step 2") && pattern.modelingHistory() == original,
          "A rotated guide that misses the contour in one size rejects the proposed size table");
    auto renamedPivot = draft;
    renamedPivot.points[4].name = "OtherPivot";
    check(!pattern.setParametricDraft(renamedPivot, &error) && error.contains("Rotation pivot")
          && pattern.parametricDraft() == draft && pattern.modelingHistory() == original,
          "A missing pivot leaves the active draft and all history intact");
    roundTripAndExport(pattern, "L");
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
        auto linkedSplit = split;
        check(linkedSplit.addSeamLink({{left.draftName}, {right.draftName}, false, true, 0, 1, 2}, &error), error);
        check(!linkedSplit.removeLastModelingStep(&error) && linkedSplit.pieces().size() == 2
              && error.contains("Seam link 1"), "Rollback when a removed split would leave a dangling seam");
        roundTripAndExport(linkedSplit, "L");
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
    const auto envelope = QJsonDocument::fromJson(readFile(legacyPath)).object();
    auto legacy = envelope.value("payload").toString().toUtf8();
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

    auto splitDraft = draft;
    splitDraft.points.append({"O2", "width/2", "height"});
    splitDraft.edges.append({"outlet2", "P", "O2", {}});
    auto splitHistory = formulaPanel(splitDraft, {"cut", "legA", "legB", "outlet", "outlet2"});
    splitHistory.production().sizes = dart.production().sizes;
    check(splitHistory.appendContour(0, ContourOperation::SplitDart,
        {namedEdge(splitHistory, "legA"), namedEdge(splitHistory, "legB"),
         namedEdge(splitHistory, "outlet"), namedEdge(splitHistory, "outlet2")}, 35, &error), error);
    check(splitHistory.validateParametricSizes(splitHistory.production(), &error), error);
    const auto originalDarts = analyzePiece(gradedPattern(splitHistory, "L"), 0).contour;
    auto splitSettings = splitHistory.modelingHistory().front();
    splitSettings.spreadMm = 60;
    check(splitHistory.editModelingStep(0, splitSettings, &error), error);
    check(splitHistory.modelingHistory().front().spreadMm == 60
          && analyzePiece(gradedPattern(splitHistory, "L"), 0).contour != originalDarts,
          "Changing a saved dart share replays the outlet geometry for the graded size");
    splitSettings.spreadMm = 100;
    check(!splitHistory.editModelingStep(0, splitSettings, &error)
          && splitHistory.modelingHistory().front().spreadMm == 60, "Keep the previous share on failed replay");
    roundTripAndExport(splitHistory, "L");

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

void checkCurvedSpread() {
    const ParametricDraft draft{
        {{"width", 100}, {"height", 80}},
        {},
        {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "height"}, {"D", "0", "height"},
         {"Control", "width / 2", "-20"}, {"E", "width / 2", "-10"}, {"F", "width / 2", "height"}},
        {{"top", "A", "B", "Control"}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
         {"left", "D", "A", {}}, {"guide", "E", "F", {}}}
    };
    for (const auto operation : {ContourOperation::Spread, ContourOperation::Pleat, ContourOperation::Gather}) {
        auto pattern = formulaPanel(draft, {"guide"});
        const auto topIndex = namedEdge(pattern, "top");
        auto top = pattern.entities()[topIndex];
        top.piece = 0;
        pattern.replace(topIndex, top);
        pattern.pieces()[0].allowanceMm = 2;
        pattern.production().sizes.append({"L", {{"width", 120}, {"height", 100}}});
        QString error;
        check(pattern.appendContour(0, operation, {namedEdge(pattern, "guide")}, 20, &error), error);
        check(pattern.validateParametricSizes(pattern.production(), &error), error);
        const auto graded = gradedPattern(pattern, "L");
        const auto geometry = analyzePiece(graded, 0);
        check(geometry.error == ContourError::None && !geometry.allowanceFailed
              && near(geometry.bounds.width(), 140), "Spread the curved seam by 20 mm in the graded size");
        check(std::ranges::count(graded.entities(), Shape::Curve, &Entity::shape) == 2,
              "Keep both exact quadratic halves");
        const auto marks = std::ranges::count_if(graded.entities(), [](const Entity& entity) {
            return entity.shape == Shape::Line && entity.layer == Layer::Construction;
        });
        check(marks == (operation == ContourOperation::Pleat ? 3 : operation == ContourOperation::Gather ? 2 : 0),
              "Keep the correct number of pleat or gathering marks");
        roundTripAndExport(pattern, "L");
    }
}

void checkTransportedPleatsAndDerivedContour() {
    const ParametricDraft draft{
        {{"width", 100}, {"height", 80}}, {},
        {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "height"}, {"D", "0", "height"},
         {"E", "width / 4", "height"}, {"F", "width / 4", "0"},
         {"G", "3 * width / 4", "0"}, {"H", "3 * width / 4", "height"},
         {"Control", "3 * width / 4 + 5", "height / 2"}},
        {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
         {"left", "D", "A", {}}, {"first", "E", "F", {}}, {"second", "G", "H", "Control"}}
    };
    auto source = formulaPanel(draft, {"first", "second"});
    source.production().sizes.append({"L", {{"width", 120}, {"height", 90}}});
    auto derived = source;
    QString error;
    check(derived.deriveParametricPiece(0, "Cut panel", &error), error);
    check(derived.moveDraftPoint("B", {0, -5}, &error), error);
    for (const auto& name : {QString{}, QString("L")}) {
        const auto graded = gradedPattern(derived, name);
        check(analyzePiece(graded, 0).contour == analyzePiece(graded, 1).contour,
              "Source and derived contours share formula updates across sizes");
    }
    roundTripAndExport(derived, "L");

    // An assigned second guide has to survive the first cut without becoming a
    // hidden duplicate; the older fold marks also have to survive the next cut.
    auto assigned = source.entities()[namedEdge(source, "second")];
    assigned.piece = 0;
    source.replace(namedEdge(source, "second"), assigned);
    auto pattern = source;
    check(pattern.appendDrapeFormulas(0, {"first", "second"}, {"12", "width * 0.15"},
        {ContourOperation::Pleat, ContourOperation::Pleat}, &error), error);
    for (const auto& [size, width] : {std::pair{QString{}, 127.}, {QString("L"), 150.}}) {
        const auto graded = gradedPattern(pattern, size);
        const auto contour = analyzePiece(graded, 0);
        check(contour.error == ContourError::None && near(contour.bounds.width(), width),
              "Apply both formula widths after transporting the curved guide");
        const auto marks = std::ranges::count_if(graded.entities(), [](const Entity& entity) {
            return entity.piece == 0 && entity.layer == Layer::Construction;
        });
        check(marks == 6, "Retain both sets of pleat marks");
        const auto mark = std::ranges::find_if(graded.entities(), [](const Entity& entity) {
            return entity.piece == 0 && entity.layer == Layer::Construction && entity.shape == Shape::Curve;
        });
        check(mark != graded.entities().end() && near(mark->control.x() - mark->a.x(), 5),
              "Translate the quadratic control with the guide");
    }
    QTemporaryDir directory;
    PatternDocument reopened;
    const auto file = directory.filePath("transported-pleats.sofit");
    check(pattern.save(file, &error) && reopened.load(file, &error)
          && reopened.modelingHistory() == pattern.modelingHistory(), error);
    check(source.applyModelingSequence(reopened, &error), error);
    check(near(analyzePiece(source, 0).bounds.width(), 127), "Replay the transported series from the library");
    roundTripAndExport(reopened, "L");

    PatternDocument destination;
    ParametricDraft inputs;
    inputs.measurements = {{"panel_width", 140}};
    check(destination.setParametricDraft(inputs, &error), error);
    check(!destination.insertConstruction(reopened, &error, {{"width", "unknown"}})
          && destination.pieces().isEmpty(), "Reject an unknown library measurement atomically");
    check(destination.insertConstruction(reopened, &error, {{"width", "panel_width"}}), error);
    check(near(analyzePiece(destination, 0).bounds.width(), 173),
          "Rebind an input used by both the contour and a replayed pleat formula");
}

void checkDrapeSequence() {
    const ParametricDraft draft{
        {{"width", 100}, {"height", 80}}, {},
        {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "height"}, {"D", "0", "height"},
         {"E", "width / 4", "-6"}, {"F", "width / 4", "height"},
         {"G", "3 * width / 4", "-6"}, {"H", "3 * width / 4", "height"},
         {"Control", "width / 2", "-16"}, {"Join", "3 * width / 4", "height / 2"},
         {"Bend", "3 * width / 4 + 9", "height / 4"}},
        {{"top", "A", "B", "Control"}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
         {"left", "D", "A", {}}, {"first", "E", "F", {}},
         {"second", "G", "Join", "Bend"}, {"third", "H", "Join", {}}}
    };
    auto pattern = formulaPanel(draft, {"first", "second", "third"});
    const auto topIndex = namedEdge(pattern, "top");
    auto top = pattern.entities()[topIndex];
    top.piece = 0;
    pattern.replace(topIndex, top);
    pattern.production().sizes.append({"L", {{"width", 120}, {"height", 100}}});
    const auto sourceCount = pattern.entities().size();
    QString error;
    check(!pattern.appendDrape(0, {"first", "first"}, {12, 18}, ContourOperation::Pleat, &error)
          && pattern.entities().size() == sourceCount && pattern.modelingHistory().isEmpty(),
          "Reject a consumed guide atomically after the first slash");
    check(pattern.appendContour(0, ContourOperation::Spread, {namedEdge(pattern, "first")}, 12, &error), error);
    const QVector<qsizetype> chain{namedEdge(pattern, "second"), namedEdge(pattern, "third")};
    check(!pattern.appendContour(0, ContourOperation::Pleat, chain, 0, &error, "110 - width")
          && error.contains("L") && pattern.modelingHistory().size() == 1,
          "Reject a width formula that becomes negative in another size");
    check(pattern.appendContour(0, ContourOperation::Pleat, chain, 0, &error, "width * 0.15"), error);
    check(near(analyzePiece(pattern, 0).bounds.width(), 127), "Evaluate the base measurement for the pleat");
    const auto graded = gradedPattern(pattern, "L");
    const auto geometry = analyzePiece(graded, 0);
    check(pattern.modelingHistory().size() == 2 && geometry.error == ContourError::None
          && near(geometry.bounds.width(), 150), "Replay both slash widths for a graded piece");
    check(std::ranges::count_if(graded.entities(), [](const Entity& entity) {
        return entity.shape == Shape::Curve && entity.piece == 0 && entity.layer == Layer::Construction;
    }) == 3, "Keep the final quadratic pleat marks in the graded piece");
    check(std::ranges::count_if(graded.entities(), [](const Entity& entity) {
        return entity.shape == Shape::Line && entity.piece == 0 && entity.layer == Layer::Construction;
    }) == 3, "Keep each straight segment of the compound pleat marks");
    check(std::ranges::count_if(graded.entities(), [](const Entity& entity) {
        return entity.shape == Shape::Curve && entity.layer == Layer::Main;
    }) == 3,
          "Keep the three exact quadratic segments after two slashes");
    const auto fold = std::ranges::find_if(graded.entities(), [](const Entity& entity) {
        return entity.shape == Shape::Curve && entity.layer == Layer::Construction;
    });
    check(fold != graded.entities().end() && near(fold->control.x() - fold->a.x(), 9),
          "Keep the exact quadratic control offset in the pleat");
    check(!pattern.moveDraftPoint("Bend", {500, 0}, &error),
          "Reject a curved guide that crosses the piece boundary");
    Entity drill{Shape::Point, {50, 40}, {50, 40}, {}, 0, Layer::Construction};
    drill.dxfLayer = drillLayer;
    pattern.insert(pattern.entities().size(), drill);
    check(!fabricPiece(gradedPattern(pattern, "L"), {0, "L", {}}, true).drillPoints.isEmpty(),
          "Keep the drill when replaying curved modeling history");
    roundTripAndExport(pattern, "L");
    QTemporaryDir saved;
    const auto path = saved.filePath("formula-drape.sofit");
    PatternDocument reopened;
    check(pattern.save(path, &error) && reopened.load(path, &error)
          && reopened.modelingHistory() == pattern.modelingHistory()
          && near(analyzePiece(gradedPattern(reopened, "L"), 0).bounds.width(), 150),
          "Keep the formula and its graded result on reopen: " + error);
}

void checkCurvedReliefAndHandles() {
    ParametricDraft draft;
    draft.measurements = {{"width", 100}, {"height", 80}, {"cut_y", 30}, {"bend", 16}};
    draft.points = {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "height"},
                    {"D", "0", "height"}, {"E", "0", "cut_y"}, {"F", "width", "cut_y"},
                    {"Control", "width / 2", "cut_y + bend"}};
    draft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
                   {"left", "D", "A", {}}, {"cut", "E", "F", "Control"}};
    auto pattern = formulaPanel(draft, {"cut"});
    pattern.pieces()[0].allowanceMm = 2;
    pattern.production().sizes.append({"L", {{"width", 140}, {"height", 100}, {"cut_y", 40}}});
    QString error;
    check(pattern.appendContour(0, ContourOperation::Relief, {namedEdge(pattern, "cut")}, 0, &error), error);
    check(pattern.addSeamLink({{"@1:seam-A"}, {"@1:seam-B"}, false, true, 0, 0.1, 2}, &error), error);
    for (int i = 0; i < 100; ++i)
        check(pattern.moveDraftPoint("Control", {0.1, -0.05}, &error), error);
    const auto moved = pattern.parametricDraft();
    check(!pattern.moveDraftPoint("Control", {0, -300}, &error) && pattern.parametricDraft() == moved,
          "Reject a curve that would leave the panel and keep the active formulas");
    for (const auto& size : {QString{}, QString("L")}) {
        auto grade = gradedPattern(pattern, size);
        const auto& a = grade.entities()[namedEdge(grade, "@1:seam-A")];
        const auto& b = grade.entities()[namedEdge(grade, "@1:seam-B")];
        check(a.shape == Shape::Curve && b.shape == Shape::Curve && a.a == b.b && a.b == b.a
              && a.control == b.control && near(a.control.x(), size.isEmpty() ? 60 : 80)
              && near(a.control.y(), size.isEmpty() ? 41 : 51)
              && grade.inspectSeamLinks().checks.first().withinTolerance,
              "Replay moved quadratic sewing edges and paired seam marks for each size");
    }
    roundTripAndExport(pattern, "L");
}

void checkCurvedSourceAndLibraryInsertion() {
    ParametricDraft draft;
    draft.measurements = {{"width", 100}};
    draft.parameters = {{"reach", "positive(width + 1e-2) - 0.01"}};
    draft.points = {{"A", "0", "0"}, {"B", "reach", "0"}, {"C", "B.x", "80"},
                    {"D", "0", "80"}, {"Side", "B.x + 20", "40"},
                    {"Start", "0", "40"}, {"End", "B.x + 10", "40"},
                    {"Handle", "B.x / 2", "50"}};
    draft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", "Side"},
                   {"bottom", "C", "D", {}}, {"left", "D", "A", {}},
                   {"cut", "Start", "End", "Handle"}};
    auto source = formulaPanel(draft, {"cut"});
    const auto sideIndex = namedEdge(source, "right");
    auto side = source.entities()[sideIndex];
    side.piece = 0;
    source.replace(sideIndex, side);
    source.production().sizes.append({"L", {{"width", 130}}});
    QString error;
    check(source.appendContour(0, ContourOperation::Yoke, {namedEdge(source, "cut")}, 0, &error), error);
    check(source.addSeamLink({{"@1:seam-A"}, {"@1:seam-B"}, false, true, 0, 0.1, 2}, &error), error);
    for (const auto& size : {QString{}, QString("L")}) {
        const auto pattern = gradedPattern(source, size);
        const double width = size.isEmpty() ? 100 : 130;
        const auto& edges = pattern.entities();
        check(std::ranges::count_if(edges, [width](const Entity& edge) {
            return edge.shape == Shape::Curve && edge.control.x() == width + 10;
        }) == 2, "Keep the two exact subdivisions of the original contour");
        const auto& seam = edges[namedEdge(pattern, "@1:seam-A")];
        check(seam.shape == Shape::Curve && near(std::max(seam.a.x(), seam.b.x()), width + 10),
              "Attach the yoke seam at the interior of the graded curved edge");
    }
    roundTripAndExport(source, "L");

    auto destination = source;
    destination.production().sizes[0].measurements["width"] = 150;
    check(destination.insertConstruction(source, &error), error);
    check(destination.pieces().size() == 4 && destination.modelingHistory().size() == 2
          && destination.parametricDraft().measurements.size() == 1
          && namedEdge(destination, "End_2") >= 0 && namedEdge(destination, "@2:seam-A") >= 0
          && destination.seamLinks().last().first == QVector<QString>{"@2:seam-A"},
          "Keep destination measurements and remap imported formulas, piece indices and seam references");
    auto graded = gradedPattern(destination, "L");
    for (const auto* name : {"@1:seam-A", "@2:seam-A"}) {
        const auto& seam = graded.entities()[namedEdge(graded, QString::fromLatin1(name))];
        check(near(std::max(seam.a.x(), seam.b.x()), 160),
              "Both constructions use the destination grading values");
    }
    const auto before = destination.parametricDraft();
    check(!destination.insertConstruction({}, &error) && destination.parametricDraft() == before
          && destination.pieces().size() == 4, "Reject a source with no construction atomically");
    const FinishedMeasurement width{"Panel width", "A_2", "B_2"};
    destination.productInformation().finishedMeasurements = {width};
    check(finishedMeasurementMm(destination, width).value_or(-1) == 100
          && finishedMeasurementMm(graded, width).value_or(-1) == 150,
          "Measure the placed pattern points in base and graded sizes");
    auto marker = destination.production();
    marker.fabricWidth = 400;
    marker.fabricRepeatMm = {37, 29};
    marker.placements = {{0, "L", {}, false, CutHand::Left}, {1, "L", {}, false, CutHand::Right}};
    marker.placements[0].repeatEdge = "@1:seam-A";
    marker.placements[1].repeatEdge = "@1:seam-B";
    marker.placements[0].repeatParameter = 0.25;
    marker.placements[1].repeatParameter = 0.75;
    auto arranged = arrangeFabric(destination, marker);
    check(arranged.error == FabricLayoutError::None && arranged.placements.size() == 2,
          "Arrange graded seam matching points on left and mirrored right copies");
    marker.placements = arranged.placements;
    for (const auto& copy : marker.placements) {
        const auto layout = fabricPiece(graded, copy);
        check(layout.repeatPoint.has_value() && matchesFabricRepeat(marker, copy, layout.repeatPoint),
              "Align the transformed seam point to the repeat grid");
    }
    QTemporaryDir directory;
    check(directory.isValid(), "Create a directory for marker outputs");
    check(exportProductInformationPdf(directory.filePath("finished-measurements.pdf"), destination, error), error);
    const auto dxf = directory.filePath("motif.dxf");
    const auto pdf = directory.filePath("motif.pdf");
    check(exportFabricDxf(dxf, destination, marker, error)
          && exportFabricPdf(pdf, destination, marker, {}, error), error);
    destination.production() = marker;
    const auto project = directory.filePath("motif.sofit");
    PatternDocument restored;
    check(destination.save(project, &error) && restored.load(project, &error)
          && saveProduction(restored.production()) == saveProduction(marker)
          && restored.productInformation().finishedMeasurements == QVector<FinishedMeasurement>{width}, error);
    marker.placements[0].position.ry() += 0.5;
    check(cuttingReport(restored, marker).repeatViolations == 1
          && !exportFabricDxf(dxf, restored, marker, error)
          && !exportFabricPdf(pdf, restored, marker, {}, error),
          "Block both marker exports if a seam point is off repeat");
    marker.placements[0].repeatEdge = "missing";
    check(arrangeFabric(restored, marker).error == FabricLayoutError::InvalidRepeat,
          "Report a missing named seam instead of falling back to the bounding box");
    roundTripAndExport(destination, "L");
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
                  size + ": invalid pieces " + checkResult.invalidPieces.join(", ")
                  + "; missing seams " + checkResult.missingSeams.join(", "));
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
    BodiceOptions tailored{NeckFinish::PointedCollar};
    tailored.sleeveStyle = SleeveStyle::TwoPiece;
    tailored.dartIntakeShare = 70;
    tailored.dartPosition = 60;
    QString error;
    auto twoPiece = draftBodiceAndSleeve(m, tailored, &error);
    check(twoPiece && twoPiece->pieces().size() == 5 && twoPiece->seamLinks().size() == 1, error);
    twoPiece->production().sizes.append({"L", {{"sleeve_elbow_shape", 40}, {"sleeve_length", 300}, {"sleeve_elbow_level", 60},
                                               {"collar_point_extension", 42}, {"dart_position", 65}}});
    for (const auto& size : {QString{}, QString("L")}) {
        const auto grade = gradedPattern(*twoPiece, size);
        const auto seam = grade.inspectSeamLinks();
        check(seam.checks.size() == 1 && seam.checks.first().withinTolerance && seam.notches.size() == 2,
              "Sewing curves and paired elbow notches agree after grading");
        const auto& frontJoin = grade.entities()[namedEdge(grade, "sleeve_front_join")];
        check(near(frontJoin.a.x() - frontJoin.control.x(), size.isEmpty() ? 25 : 40)
              && near(frontJoin.control.y(), size.isEmpty() ? 186 : 214.8),
              "Elbow shaping follows the size input");
        const auto& collar = grade.entities()[namedEdge(grade, "finish_collar_front_end")];
        check(near(collar.b.x() - collar.a.x(), size.isEmpty() ? 30 : 42),
              "Collar point extension follows the size input");
        const auto& dart = grade.entities()[namedEdge(grade, "front_dart_left")];
        const auto& otherLeg = grade.entities()[namedEdge(grade, "front_dart_right")];
        const double suppression = (m.bust + m.bustEase - m.waist - m.waistEase) / 4;
        check(near(otherLeg.b.x() - dart.a.x(), suppression * 0.7) && dart.b.x() > 120,
              "Dart intake redistributes waist suppression and remains editable by size");
    }
    auto& pack = twoPiece->productInformation();
    pack.reportTitle = "Jacket construction pack";
    pack.billOfMaterials = "Shell fabric and thread";
    pack.assemblyInstructions = "Match elbow notches before sewing";
    pack.includeSizeMeasurements = true;
    const QTemporaryDir reports;
    const auto pdf = reports.filePath("tech-pack.pdf");
    check(exportProductInformationPdf(pdf, *twoPiece, error) && readFile(pdf).startsWith("%PDF-"), error);
    roundTripAndExport(*twoPiece, "L");
    auto marker = twoPiece->production();
    marker.fabricRepeatMm = {40, 55};
    marker.placements = {{2, "L", {80, 55}}};
    const auto markerPdf = reports.filePath("marker.pdf");
    check(exportFabricPdf(markerPdf, *twoPiece, marker, {}, error)
          && readFile(markerPdf).startsWith("%PDF-"), "Export a graded registered marker as tiled PDF: " + error);
    marker.placements.first().position.rx() += 1;
    check(!exportFabricPdf(markerPdf, *twoPiece, marker, {}, error) && error.contains("repeat"),
          "Prevent PDF export of an unregistered placement");

    auto skirt = draftFittedSkirt(740, 40, {1000, 60, 195, 620, 160, 100}, &error);
    check(skirt.has_value(), error);
    skirt->production().sizes.append({"48/176", {{"waist", 780}, {"hip", 1060}, {"skirt_length", 650}}});
    check(skirt->validateParametricSizes(skirt->production(), &error), error);
    const auto sideSeams = skirt->inspectSeamLinks();
    const auto gradedSkirt = gradedPattern(*skirt, "48/176");
    check(skirt->pieces().size() == 2 && sideSeams.checks.size() == 1 && sideSeams.checks.first().withinTolerance
          && near(analyzePiece(gradedSkirt, 0).bounds.height(), 650),
          "Draft independent fitted skirt panels with linked side seams and a graded length");
    roundTripAndExport(*skirt, "48/176");

    const auto dress = draftDress(m, {1000, 60, 195, 620, 160, 100}, BodiceOptions{NeckFinish::Facing});
    check(dress && dress->pieces().size() == 6 && checkFittingBlock(*dress).missingSeams.isEmpty(),
          "Add facings to the sleeveless dress without losing the skirt seam checks");
}

void checkGarmentProduction(const QString& name, std::optional<PatternDocument> draft,
                            PatternSize larger, PatternSize taller) {
    check(draft.has_value(), name + ": construct base");
    auto& data = draft->production();
    data.sizes = {larger, taller};
    data.pieceClearanceMm = 2;
    data.directionalFabric = true;
    QString error;
    check(draft->validateParametricSizes(data, &error), name + ": validate size and height changes: " + error);
    for (const auto& size : {QString{}, larger.name, taller.name}) {
        const auto fitting = checkFittingBlock(*draft, size);
        check(fitting.invalidPieces.isEmpty() && fitting.missingSeams.isEmpty(),
              name + "/" + size + ": verify closed contours and available seam measurements");
        int capSections = 0;
        int collarSections = 0;
        for (const auto& seam : fitting.seams) {
            const bool cap = seam.label.contains("armhole and sleeve cap");
            const bool collar = seam.label.contains("neckline and collar");
            capSections += cap;
            collarSections += collar;
            const double deviation = std::abs((seam.second - seam.first) - seam.targetDifference);
            if (cap)
                check(near(seam.targetDifference, size == "48/176" ? 5 : 4), "Use the expected cap ease for the size");
            if (cap || collar)
                std::cout << qPrintable(QStringLiteral("%1 / %2 / %3: target %4 mm, measured %5 mm, deviation %6 mm")
                    .arg(name, size.isEmpty() ? QStringLiteral("Base") : size, seam.label)
                    .arg(seam.targetDifference, 0, 'f', 3)
                    .arg(seam.second - seam.first, 0, 'f', 3)
                    .arg(deviation, 0, 'f', 3)) << '\n';
            check(deviation <= (cap || collar ? 0.01 : 2), name + "/" + size + ": " + seam.label
                  + " deviates by " + QString::number(deviation) + " mm");
        }
        if (name == "Shirt construction" || name == "Jacket construction")
            check(capSections == 2 && collarSections == 2, name + "/" + size + ": measure each cap and collar section");
        const auto graded = gradedPattern(*draft, size);
        for (qsizetype piece = 0; piece < graded.pieces().size(); ++piece) {
            const auto geometry = analyzePiece(graded, piece);
            check(geometry.error == ContourError::None && !geometry.allowanceFailed,
                  name + "/" + size + ": verify cutting allowance for " + graded.pieces()[piece].name);
            if (size == taller.name) {
                for (int copy = 0; copy < graded.pieces()[piece].cutCount; ++copy)
                    data.placements.append({piece, size, {}});
            }
        }
    }
    if (name == "A-line skirt") {
        data.fabricRepeatMm = {20, 30};
        for (auto& copy : data.placements)
            copy.repeatEdge = "hem";
    }
    const auto arranged = arrangeFabric(*draft, data);
    check(arranged.error == FabricLayoutError::None, name + ": nest the taller size");
    check(arranged.baselineLengthMm && arranged.optimizedLengthMm
          && *arranged.optimizedLengthMm <= *arranged.baselineLengthMm,
          name + ": retain or improve the verified layout length");
    std::cout << qPrintable(name) << " / marker / " << data.placements.size() << " copies: "
              << *arranged.baselineLengthMm << " -> " << *arranged.optimizedLengthMm << " mm\n";
    data.placements = arranged.placements;
    const auto report = cuttingReport(*draft, data);
    const auto rows = cuttingKitRows(*draft, report);
    check(report.canExport(data.fabricWidth) && rows.size() == draft->pieces().size()
          && std::ranges::all_of(rows, [&](const auto& row) {
              return row.copies() == draft->pieces()[row.piece].cutCount;
          }), name + ": verify marker and cut quantities");

    draft->productInformation().reportTitle = name;
    draft->productInformation().includeSizeMeasurements = true;
    roundTripAndExport(*draft, taller.name);
    QTemporaryDir outputs;
    check(outputs.isValid(), name + ": create export directory");
    const auto pack = outputs.filePath("tech-pack.pdf");
    const auto pdf = outputs.filePath("marker.pdf");
    const auto dxf = outputs.filePath("marker.dxf");
    draft->production() = data;
    const auto acceptanceDirectory = qEnvironmentVariable("SOFIT_ACCEPTANCE_PROJECTS");
    if (!acceptanceDirectory.isEmpty()) {
        const QDir directory(acceptanceDirectory);
        const auto outputName = name.toLower().replace(u' ', u'-') + QStringLiteral(".sofit");
        const auto projectPath = directory.filePath(outputName);
        PatternDocument reopened;
        check(draft->save(projectPath, &error) && reopened.load(projectPath, &error)
              && cuttingReport(reopened, reopened.production()).canExport(reopened.production().fabricWidth),
              name + ": save and reopen acceptance project: " + error);
    }
    check(exportProductInformationPdf(pack, *draft, error), name + ": export tech pack with verified marker: " + error);
    check(exportFabricPdf(pdf, *draft, data, {}, error), name + ": export marker PDF: " + error);
    check(exportFabricDxf(dxf, *draft, data, error), name + ": export marker DXF: " + error);
    for (const auto& file : {pack, pdf}) {
        const auto bytes = readFile(file);
        check(bytes.startsWith("%PDF-") && bytes.contains("/Type /Page")
              && bytes.trimmed().endsWith("%%EOF"), name + ": verify PDF output");
    }
    check(readFile(dxf).contains("ENTITIES"), name + ": verify DXF output");

    if (name == "Trousers" || name == "Jacket construction" || name == "A-line skirt") {
        auto batch = data;
        batch.fabricWidth = name == "Jacket construction" ? 1450 : 1500;
        batch.pieceClearanceMm = 5;
        batch.fabricFlaws = {{100, 100, 75, 120}, {batch.fabricWidth - 170, 500, 60, 80}};
        batch.placements.clear();
        for (const auto& size : {QString{}, larger.name, taller.name}) {
            for (auto copy : data.placements) {
                copy.size = size;
                copy.position = {};
                batch.placements.append(copy);
            }
        }
        QElapsedTimer timer;
        timer.start();
        const auto candidate = arrangeFabric(*draft, batch);
        const auto elapsed = timer.elapsed();
        check(candidate.error == FabricLayoutError::None && candidate.baselineLengthMm
              && candidate.optimizedLengthMm && *candidate.optimizedLengthMm <= *candidate.baselineLengthMm,
              name + ": retain or improve the three-size batch");
        batch.placements = candidate.placements;
        const auto batchReport = cuttingReport(*draft, batch);
        check(batchReport.canExport(batch.fabricWidth)
              && near(batchReport.usage.length, *candidate.optimizedLengthMm)
              && batchReport.rows.size() == 3 * draft->pieces().size()
              && std::ranges::all_of(batchReport.rows, [&](const auto& row) {
                  return row.copies() == draft->pieces()[row.piece].cutCount;
              }), name + ": independently verify mixed-size placement constraints and cut quantities");
        check(exportFabricDxf(dxf, *draft, batch, error) && readFile(dxf).contains("ENTITIES"),
              name + ": export mixed-size marker DXF: " + error);
        check(exportFabricPdf(pdf, *draft, batch, {}, error) && readFile(pdf).trimmed().endsWith("%%EOF"),
              name + ": export mixed-size marker PDF: " + error);
        std::cout << qPrintable(name) << " / mixed-size marker / " << batch.placements.size() << " copies: "
                  << *candidate.baselineLengthMm << " -> " << *candidate.optimizedLengthMm
                  << " mm, " << elapsed << " ms\n";
    }
}

void checkGarmentProductionScenarios() {
    const BodiceMeasurements bodice{920, 740, 420, 210, 68, 75, 22, 116, 38, 60, 40,
                                    115, 300, 50, 220, 285, 87};
    checkGarmentProduction("A-line skirt", draftPullOnSkirt(SkirtStyle::ALine, {1000, 600, 60, 200, 35}),
                           {"48/164", {{"hip", 1040}, {"hip_ease", 70}}},
                           {"48/176", {{"hip", 1040}, {"length", 660}, {"casing", 40}, {"hem_flare", 220}}});
    checkGarmentProduction("Trousers",
                           draftTrousersWithSeamLinks({740, 1000, 40, 60, 195, 270, 600, 1030, 460, 380, 100}),
                           {"48/164", {{"waist", 780}, {"hip", 1040}}},
                           {"48/176", {{"waist", 780}, {"hip", 1040}, {"length", 1090}, {"knee_depth", 630}}});
    checkGarmentProduction("Dress with facings",
                           draftDress(bodice, {1000, 60, 195, 620, 160, 100}, BodiceOptions{NeckFinish::Facing}),
                           {"48/164", {{"bust", 960}, {"waist", 780}, {"hip", 1040}}},
                           {"48/176", {{"bust", 960}, {"waist", 780}, {"hip", 1040},
                                       {"bodice_length", 445}, {"skirt_length", 680}}});
    const PatternSize larger{"48/164", {{"bust", 960}, {"waist", 780}}};
    const PatternSize taller{"48/176", {{"bust", 960}, {"waist", 780}, {"bodice_length", 445},
                                       {"sleeve_length", 320}, {"cap_height", 90}, {"sleeve_cap_ease", 5},
                                       {"neck_width", 70}, {"front_neck_depth", 79}, {"back_neck_depth", 24}}};
    checkGarmentProduction("Shirt construction",
                           draftBodiceAndSleeve(bodice, BodiceOptions{NeckFinish::PointedCollar}), larger, taller);
    BodiceOptions jacket{NeckFinish::PointedCollar};
    jacket.sleeveStyle = SleeveStyle::TwoPiece;
    checkGarmentProduction("Jacket construction", draftBodiceAndSleeve(bodice, jacket), larger, taller);
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

void checkShirtCuff() {
    QString error;
    check(!draftShirtCuff({170, 20, 0, 55}, &error), "Reject a cuff without closure overlap");
    auto cuff = draftShirtCuff({170, 20, 25, 55}, &error);
    check(cuff.has_value(), error);
    check(cuff->pieces().front().cutCount == 2, "A shirt requires two cuffs");
    cuff->production().sizes.append({"L", {{"cuff_wrist", 190}, {"cuff_depth", 60}}});
    check(cuff->validateParametricSizes(cuff->production(), &error), error);
    const auto graded = gradedPattern(*cuff, "L", nullptr, GradeEdgeNames::Preserve);
    const auto fold = std::ranges::find(graded.entities(), QStringLiteral("cuff_fold"), &Entity::draftName);
    check(fold != graded.entities().end() && fold->layer == Layer::Construction
          && fold->a == QPointF(0, 60) && fold->b == QPointF(235, 60), "Replay the cuff fold and overlap");
    const auto cut = fabricPiece(graded, {0, "L", {}}).cut.boundingRect();
    check(near(cut.width() * cut.height(), 255 * 140), "Recalculate the cuff and retain the allowance");
    roundTripAndExport(*cuff, "L");
}

void checkDrillMarks() {
    PatternDocument panel;
    panel.pieces().append({"Drill panel"});
    panel.insert(0, {Shape::Rectangle, {0, 0}, {100, 80}, {}, 0});
    Entity mark{Shape::Point, {20, 30}, {20, 30}, {}, 0, Layer::Construction};
    mark.dxfLayer = drillLayer;
    panel.insert(1, mark);

    QTemporaryDir temporary;
    check(temporary.isValid(), "Create drill test output directory");
    const auto project = temporary.filePath("drill.sofit");
    check(panel.save(project), "Save the drill point");
    check(panel.load(project) && panel.entities()[1].dxfLayer == drillLayer,
          "Reopen the piece with its drill point");
    FabricPlacement copy{0, {}, {10, 15}};
    copy.reversed = true;
    copy.hand = CutHand::Right;
    panel.production().placements = {copy};
    check(fabricPiece(panel, copy, true).drillPoints == QVector<QPointF>{{20, 50}},
          "Rotate and mirror the drill with its cutting contour");

    QString error;
    const auto dxf = temporary.filePath("drill.dxf");
    check(exportFabricDxf(dxf, panel, panel.production(), error), error);
    QFile output(dxf);
    check(output.open(QIODevice::ReadOnly) && output.readAll().contains(
              "0\nPOINT\n100\nAcDbEntity\n8\nSOFIT_DRILL\n100\nAcDbPoint\n10\n30\n20\n-65"),
          "Export the drill to its own DXF layer at the placed coordinate");
    const auto pdf = temporary.filePath("drill.pdf");
    check(exportFabricPdf(pdf, panel, panel.production(), {}, error), error);
    for (const auto type : {PieceSymbol::Cross, PieceSymbol::Circle, PieceSymbol::Buttonhole,
                            PieceSymbol::WarpRepeat, PieceSymbol::WeftRepeat}) {
        for (const auto& stroke : buildPieceSymbol(type, 0, {50, 40}, 8, 25))
            panel.insert(panel.entities().size(), stroke);
    }
    check(panel.save(project) && panel.load(project), "Reopen the production symbols");
    const auto symbolsDxf = temporary.filePath("symbols.dxf");
    check(exportFabricDxf(symbolsDxf, panel, panel.production(), error)
          && readFile(symbolsDxf).contains("8\nSOFIT_SYMBOL\n100\nAcDbPolyline")
          && exportFabricPdf(pdf, panel, panel.production(), {}, error),
          "Export the oriented production symbols in the marker: " + error);
    mark.a = mark.b = {120, 30};
    panel.replace(1, mark);
    check(!exportFabricDxf(dxf, panel, panel.production(), error) && error.contains("drill point")
          && !exportFabricPdf(pdf, panel, panel.production(), {}, error) && error.contains("drill point"),
          "Reject an out-of-contour drill when exporting either marker format");
}

void checkPatchPocket() {
    QString error;
    check(!draftPatchPocket({150, 170, 75, 25}, &error) && !error.isEmpty(),
          "Reject a corner setting that consumes the bottom edge");
    auto pocket = draftPatchPocket({150, 170, 22, 25}, &error);
    check(pocket.has_value(), "Draft a rounded patch pocket: " + error);
    const auto& entities = pocket->entities();
    const auto fold = std::ranges::find(entities, QStringLiteral("pocket_fold"), &Entity::draftName);
    check(fold != entities.end() && fold->layer == Layer::Construction
          && fold->a == QPointF(-75, 0) && fold->b == QPointF(75, 0), "Mark the top turnback fold");
    check(std::ranges::count(entities, Shape::Curve, &Entity::shape) == 2, "Build two rounded corners");
    pocket->production().sizes.append({"L", {{"pocket_width", 180}, {"pocket_height", 190}}});
    check(pocket->validateParametricSizes(pocket->production(), &error), error);
    const auto graded = gradedPattern(*pocket, "L");
    const auto cut = fabricPiece(graded, {0, "L", {}}).cut.boundingRect();
    check(near(cut.width(), 196) && near(cut.height(), 231), "Recalculate finished size and retain 8 mm allowance");
    roundTripAndExport(*pocket, "L");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    checkModeling();
    checkContourHistory();
    checkCurvedReliefAndHandles();
    checkCurvedSpread();
    checkDrapeSequence();
    checkTransportedPleatsAndDerivedContour();
    checkDrillMarks();
    checkCurvedSourceAndLibraryInsertion();
    checkRotationHistory();
    checkSeamLinks();
    checkLinkedTrouserBlock();
    checkTemplates();
    checkGarmentProductionScenarios();
    checkApronKit();
    checkPatchPocket();
    checkShirtCuff();
}
