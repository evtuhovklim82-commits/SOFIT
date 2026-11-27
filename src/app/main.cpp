#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QMessageBox>
#include <QTimer>
#include <QPalette>
#include <QStyleFactory>
#include "../ui/MainWindow.h"
#include "PatternDxf.h"
#include "PatternPdf.h"
#include "Production.h"

namespace {
int productionReport(const opc::PatternDocument& document, const QString& destination, QString& error) {
    QJsonArray sizes;
    QVector<QString> names{QString{}};
    for (const auto& size : document.production().sizes)
        names.append(size.name);
    bool passed = !document.pieces().isEmpty();
    for (const auto& size : names) {
        const auto graded = opc::gradedPattern(document, size);
        QJsonArray pieces, seams;
        bool valid = !graded.pieces().isEmpty();
        for (qsizetype i = 0; i < graded.pieces().size(); ++i) {
            const auto geometry = opc::analyzePiece(graded, i);
            if (document.isSizeCutPiece(i) && geometry.error == opc::ContourError::Empty)
                continue;
            const bool closed = geometry.error == opc::ContourError::None;
            valid &= closed && !geometry.allowanceFailed;
            QJsonObject piece{{"name", graded.pieces()[i].name}, {"closedContour", closed},
                              {"allowanceFailed", geometry.allowanceFailed}};
            if (closed)
                piece.insert("perimeterMm", geometry.perimeterMm);
            pieces.append(piece);
        }
        const auto links = graded.inspectSeamLinks();
        for (const auto& seam : links.checks) {
            valid &= seam.withinTolerance && seam.error.isEmpty();
            seams.append(QJsonObject{{"firstMm", seam.firstMm}, {"secondMm", seam.secondMm},
                {"withinTolerance", seam.withinTolerance}, {"error", seam.error}});
        }
        sizes.append(QJsonObject{{"size", size}, {"passed", valid}, {"pieces", pieces}, {"seams", seams}});
        passed &= valid;
    }
    const auto& data = document.production();
    const auto marker = opc::cuttingReport(document, data);
    const bool hasMarker = !data.placements.isEmpty();
    passed &= !hasMarker || marker.canExport(data.fabricWidth);
    const QJsonObject result{{"passed", passed}, {"sizes", sizes}, {"marker", QJsonObject{
        {"configured", hasMarker}, {"exportable", marker.canExport(data.fabricWidth)},
        {"invalidCopies", marker.usage.invalid}, {"outsideFabric", marker.usage.outside},
        {"overlaps", marker.usage.overlaps}, {"flawConflicts", marker.usage.flawConflicts}, {"offRepeat", marker.repeatViolations},
        {"orientationViolations", marker.orientationViolations}, {"foldViolations", marker.foldViolations}
    }}};
    QSaveFile file(destination);
    const auto contents = QJsonDocument(result).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return 2;
    }
    return passed ? 0 : 1;
}
}

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("SOFIT");
    app.setApplicationVersion(QStringLiteral(SOFIT_VERSION));
    app.setOrganizationName("SOFIT Community");
    app.setWindowIcon(QIcon(QStringLiteral(":/sofit.png")));
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#202833"));
    palette.setColor(QPalette::WindowText, QColor("#ebf1f7"));
    palette.setColor(QPalette::Base, QColor("#141a22"));
    palette.setColor(QPalette::AlternateBase, QColor("#1b2430"));
    palette.setColor(QPalette::Text, QColor("#ebf1f7"));
    palette.setColor(QPalette::Button, QColor("#293544"));
    palette.setColor(QPalette::ButtonText, QColor("#ebf1f7"));
    palette.setColor(QPalette::Highlight, QColor("#77d0e6"));
    palette.setColor(QPalette::HighlightedText, QColor("#101923"));
    palette.setColor(QPalette::ToolTipBase, QColor("#293544"));
    palette.setColor(QPalette::ToolTipText, QColor("#ebf1f7"));
    palette.setColor(QPalette::PlaceholderText, QColor("#a0aebd"));
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        palette.setColor(QPalette::Disabled, role, QColor("#7e8e9f"));
    app.setPalette(palette);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", QObject::tr("SOFIT project to open."), "[project]");
    const QCommandLineOption smokeOption("packaging-smoke", QObject::tr("Open a project and exit after GUI startup."));
    const QCommandLineOption reportOption("production-report",
        QObject::tr("Write a JSON production check for every size and the saved marker."), "file");
    const QCommandLineOption dxfOption("marker-dxf", QObject::tr("Export the saved marker as DXF."), "file");
    const QCommandLineOption pdfOption("marker-pdf", QObject::tr("Export the saved marker as tiled A4 PDF."), "file");
    parser.addOptions({smokeOption, reportOption, dxfOption, pdfOption});
    parser.process(app);
    const auto projects = parser.positionalArguments();
    const bool smoke = parser.isSet(smokeOption);
    const bool report = parser.isSet(reportOption);
    const bool dxf = parser.isSet(dxfOption);
    const bool pdf = parser.isSet(pdfOption);
    const int batchOptions = int(report) + int(dxf) + int(pdf);
    if (projects.size() > 1 || ((smoke || batchOptions) && projects.size() != 1)
        || batchOptions + int(smoke) > 1)
        return 2;

    if (batchOptions) {
        const auto output = parser.value(report ? reportOption : dxf ? dxfOption : pdfOption);
        const auto project = projects.front();
        const auto projectPath = QFileInfo(project).canonicalFilePath();
        QString error;
        int result = 2;
        if (output.isEmpty() || (!projectPath.isEmpty() && QFileInfo(output).canonicalFilePath() == projectPath)) {
            error = QObject::tr("Choose an output file distinct from the input project.");
        } else {
            opc::PatternDocument document;
            if (document.load(project, &error)) {
                if (report)
                    result = productionReport(document, output, error);
                else if (dxf ? opc::exportFabricDxf(output, document, document.production(), error)
                             : opc::exportFabricPdf(output, document, document.production(), {}, error))
                    result = 0;
            }
        }
        if (!error.isEmpty())
            QTextStream(stderr) << error << Qt::endl;
        return result;
    }

    MainWindow window;
    if (!projects.isEmpty()) {
        QString error;
        if (!window.openProject(projects.front(), &error)) {
            if (!smoke)
                QMessageBox::warning(&window, QObject::tr("Open failed"), error);
            return 1;
        }
    }
    window.show();
    if (smoke)
        QTimer::singleShot(250, &app, &QApplication::quit);
    else
        QTimer::singleShot(0, &window, &MainWindow::offerProjectRecovery);
    return app.exec();
}
