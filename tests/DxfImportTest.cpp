#include "DxfImport.h"
#include "PatternDxf.h"
#include "Production.h"

#include <QFile>
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
    check(argc == 2, "Fixture directory is required");
    const QString fixtures = QString::fromLocal8Bit(argv[1]);
    QString error;
    auto drawing = opc::readDxf(fixtures + "/exchange-inches.dxf", error);
    check(drawing && error.isEmpty() && drawing->paths.size() == 7, "Parse supported DXF entities");
    check(drawing->skipped.value("LINE") == 2 && drawing->skipped.value("TEXT") == 1 &&
          drawing->skipped.value("INSERT") == 1 && drawing->approximated == 2,
          "Report invalid, 3D, unsupported and approximated entities");
    const auto& legacy = drawing->paths[1].entities;
    check(legacy.front().a == QPointF(0, 0) && legacy.back().b == QPointF(2, 0) &&
          qAbs(legacy[5].b.y() + 1) < 1e-12, "Positive bulge traces a CCW semicircle");
    const auto& arc = drawing->paths[2].entities;
    check(qAbs(arc.front().a.x() - 1.969615506) < 1e-8 &&
          qAbs(arc.back().b.y() - 0.684040287) < 1e-8, "ARC crosses zero degrees in the CCW direction");
    const auto unit = opc::dxfUnitMillimeters(drawing->insertionUnits);
    check(unit && *unit == 25.4 && !opc::dxfUnitMillimeters(0), "Convert DXF inches and detect unspecified units");
    const QSet<QString> selected{"OUTLINE", "CURVES", "CONSTRUCTION", QStringLiteral("ТEST")};
    auto document = drawing->appendTo({}, selected, *unit);
    check(document.pieces().size() == 1 && document.entities().front().b == QPointF(203.2, 0),
          "Build an editable closed piece in millimeters");
    const auto geometry = opc::analyzePiece(document, 0);
    check(geometry.error == opc::ContourError::None && qAbs(geometry.bounds.height() - 101.6) < 1e-8,
          "Analyze a 203.2 by 101.6 mm pattern piece");
    check(drawing->appendTo({}, {"CONSTRUCTION"}, *unit).entities().size() == 1 &&
          drawing->appendTo({}, selected, 1e9).entities().isEmpty(),
          "Filter selected layers and reject over-limit scales atomically");
    QTemporaryDir temp;
    check(temp.isValid(), "Create a working directory");
    const auto sofitPath = temp.filePath("drawing.sofit");
    check(document.save(sofitPath, &error), "Save imported entities to the native format");
    opc::PatternDocument reopened;
    check(reopened.load(sofitPath, &error) && reopened.entities().size() == document.entities().size() &&
          reopened.entities()[document.entities().size() - 2].dxfLayer == QStringLiteral("ТEST"),
          "Reopen imported entities with their DXF layer names");
    const auto dxfPath = temp.filePath("roundtrip.dxf");
    check(opc::exportPatternDxf(dxfPath, reopened, QVector<bool>(reopened.entities().size(), true),
                                {geometry}, error), "Export imported geometry back to DXF");
    const auto roundtrip = opc::readDxf(dxfPath, error);
    check(roundtrip && roundtrip->skipped.isEmpty() && roundtrip->approximated == 0 &&
          roundtrip->insertionUnits == 4, "Read SOFIT export without geometry losses");
    const auto finalDocument = roundtrip->appendTo({}, selected, 1);
    check(finalDocument.entities().size() == reopened.entities().size() &&
          opc::analyzePiece(finalDocument, 0).error == opc::ContourError::None,
          "Preserve object count and rebuild the closed piece from exported LINEs");
    for (qsizetype i = 0; i < reopened.entities().size(); ++i) {
        const auto& actual = finalDocument.entities()[i];
        const auto& expected = reopened.entities()[i];
        check(actual.shape == expected.shape && actual.dxfLayer == expected.dxfLayer &&
              QLineF(actual.a, expected.a).length() < 1e-8 &&
              QLineF(actual.b, expected.b).length() < 1e-8 &&
              QLineF(actual.control, expected.control).length() < 1e-8,
              "Preserve geometry and layer through export and re-import");
    }

    reopened.production().sizes.append(opc::PatternSize{"L", {}});
    for (const auto corner : {QPointF(203.2, 0), QPointF(203.2, -101.6)})
        reopened.production().rules.append({"L", 0, corner, {20, 0}});
    const auto graded = opc::gradedPattern(reopened, "L");
    const auto gradedGeometry = opc::analyzePiece(graded, 0);
    check(gradedGeometry.error == opc::ContourError::None &&
          qAbs(gradedGeometry.bounds.width() - 223.2) < 1e-8,
          "Apply native point grading to both edges sharing an imported corner");
    check(opc::exportPatternDxf(dxfPath, graded, QVector<bool>(graded.entities().size(), true),
                                {gradedGeometry}, error), "Export the selected graded size");
    const auto gradedDxf = opc::readDxf(dxfPath, error);
    check(gradedDxf && qAbs(opc::analyzePiece(gradedDxf->appendTo({}, selected, 1), 0).bounds.width() -
                           223.2) < 1e-8, "Re-import the graded size and verify its reference dimension");

    check(!opc::readDxf(fixtures + "/broken-sequence.dxf", error) && error.contains("SEQEND"),
          "Reject incomplete legacy polylines");
    QFile damaged(temp.filePath("truncated.dxf"));
    check(damaged.open(QIODevice::WriteOnly), "Create truncated fixture");
    damaged.write("0\nSECTION\n2\nENTITIES\n0\nLINE\n10");
    damaged.close();
    check(!opc::readDxf(damaged.fileName(), error) && error.contains("line"), "Reject truncated group pairs");
}
