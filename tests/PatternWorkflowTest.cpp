#include "MainWindow.h"
#include "PatternView.h"
#include "PatternPdf.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

QAction& action(const MainWindow& window, const QString& text) {
    for (auto* candidate : window.findChildren<QAction*>()) {
        if (candidate->text() == text)
            return *candidate;
    }
    std::cerr << "Missing action " << text.toStdString() << '\n';
    std::exit(EXIT_FAILURE);
}

template<typename Dialog, typename Edit>
void runDialog(QAction& command, Edit edit) {
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<Dialog*>(QApplication::activeModalWidget());
        check(dialog, "Expected modal dialog");
        edit(*dialog);
    });
    command.trigger();
}

void chooseFile(QAction& command, const QString& path) {
    runDialog<QFileDialog>(command, [&path](QFileDialog& dialog) {
        dialog.selectFile(path);
        static_cast<QDialog&>(dialog).accept();
    });
}

QByteArray readPdf(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Open exported PDF");
    const auto bytes = file.readAll();
    check(bytes.startsWith("%PDF-") && bytes.trimmed().endsWith("%%EOF"), "Complete PDF");
    return bytes;
}

int pageCount(const QByteArray& bytes) {
    const QRegularExpression page(QStringLiteral(R"(/Type\s*/Page\b)"));
    auto matches = page.globalMatch(QString::fromLatin1(bytes));
    int pages = 0;
    while (matches.hasNext()) {
        matches.next();
        ++pages;
    }
    return pages;
}

int overlayCount(const PatternView& view) {
    int count = 0;
    for (const auto* item : view.scene()->items()) {
        if (item->zValue() < 0)
            ++count;
    }
    return count;
}
}

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    MainWindow window;
    window.show();

    auto& view = *window.findChild<PatternView*>();
    auto& tree = *window.findChild<QTreeWidget*>();
    view.entityDrawn({opc::Shape::Line, {0, 0}, {100, 0}});
    view.entityDrawn({opc::Shape::Line, {100, 0}, {100, 40}});
    view.entityDrawn({opc::Shape::Line, {100, 40}, {0, 40}});
    view.entityDrawn({opc::Shape::Line, {0, 40}, {0, 0}});
    view.entityDrawn({opc::Shape::Line, {0, 0}, {50, 20}});
    check(tree.topLevelItemCount() == 5, "Draw objects");

    tree.topLevelItem(4)->setSelected(true);
    action(window, "Move Selection to Construction Layer").trigger();
    check(tree.topLevelItem(4)->text(3) == "Construction", "Assign construction layer");

    for (int index = 0; index < tree.topLevelItemCount(); ++index)
        tree.topLevelItem(index)->setSelected(true);
    check(tree.selectedItems().size() == 5, "Select several objects in the list");

    runDialog<QInputDialog>(action(window, "Create Piece from Selection…"), [](QInputDialog& dialog) {
        dialog.setTextValue(QStringLiteral("Полочка"));
        dialog.accept();
    });
    for (int index = 0; index < tree.topLevelItemCount(); ++index)
        check(tree.topLevelItem(index)->text(2) == QStringLiteral("Полочка"), "Group drawing objects");

    runDialog<QDialog>(action(window, "Piece, Contour and Allowance…"), [](QDialog& dialog) {
        check(dialog.windowTitle() == "Piece, Contour and Allowance", "Open piece measurements");
        bool perimeter = false;
        bool dimensions = false;
        for (const auto* label : dialog.findChildren<QLabel*>()) {
            perimeter |= label->text().contains("280");
            dimensions |= label->text().contains("100") && label->text().contains("40");
        }
        check(perimeter && dimensions, "Show perimeter and dimensions");
        dialog.findChild<QDoubleSpinBox*>()->setValue(12.375);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    check(overlayCount(view) == 1, "Show the derived allowance");

    action(window, "Undo Edit Piece").trigger();
    check(overlayCount(view) == 0, "Undo allowance");
    action(window, "Redo Edit Piece").trigger();
    check(overlayCount(view) == 1, "Redo allowance");

    view.entityMoved(0, {5, 5});
    check(overlayCount(view) == 0, "Hide invalid allowance after boundary edits");
    action(window, "Undo Move Object").trigger();
    check(overlayCount(view) == 1, "Restore allowance after undoing a boundary edit");

    action(window, "Construction Lines").trigger();
    check(overlayCount(view) == 1, "Construction visibility does not affect contour");

    tree.clearSelection();
    tree.topLevelItem(4)->setSelected(true);
    action(window, "Remove Selection from Piece").trigger();
    check(tree.topLevelItem(4)->text(2).isEmpty() && overlayCount(view) == 1,
          "Ungroup a construction object and keep the closed contour");
    action(window, "Assign Selection to Piece…").trigger();
    check(tree.topLevelItem(4)->text(2) == QStringLiteral("Полочка"), "Reassign a hidden construction object");

    QTemporaryDir temp;
    check(temp.isValid(), "Temporary files");
    const auto pattern = temp.filePath("piece.sofit");
    chooseFile(action(window, "&Save"), pattern);
    opc::PatternDocument loaded;
    check(loaded.load(pattern) && loaded.pieces().size() == 1 && loaded.pieces()[0].allowanceMm == 12.375
          && !loaded.layerVisible(opc::Layer::Construction), "Save through the UI");

    const auto svgPath = temp.filePath("piece.svg");
    chooseFile(action(window, "Export &SVG"), svgPath);
    QFile svg(svgPath);
    check(svg.open(QIODevice::ReadOnly), "Export SVG through the UI");
    const auto xml = svg.readAll();
    check(xml.contains("<polygon") && xml.count("<line ") == 4 && xml.contains("mm"),
          "Export the allowance in millimeters and honor layer visibility");

    const auto pdfPath = temp.filePath("piece.pdf");
    // A hidden object far from the piece would otherwise exceed the page limit.
    view.entityDrawn({opc::Shape::Point, {1e6, 1e6}, {}});
    tree.topLevelItem(5)->setCheckState(1, Qt::Unchecked);
    auto& pdfAction = action(window, "Export &PDF for Printing…");
    runDialog<QDialog>(pdfAction, [&pdfPath](QDialog& dialog) {
        check(dialog.windowTitle() == "Export PDF for Printing", "Open PDF printing options");
        const auto choices = dialog.findChildren<QComboBox*>();
        check(choices.size() == 2, "Choose paper and orientation");
        choices[0]->setCurrentIndex(1);
        choices[1]->setCurrentIndex(1);
        QTimer::singleShot(0, [&pdfPath] {
            auto* save = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            check(save, "Choose PDF file");
            save->selectFile(pdfPath);
            static_cast<QDialog*>(save)->accept();
        });
        dialog.accept();
    });
    const auto pdf = readPdf(pdfPath);
    const QRegularExpression a3Landscape(QStringLiteral(R"(/MediaBox\s*\[0\s+0\s+1191(?:\.0+)?\s+842(?:\.0+)?\s*\])"));
    check(pageCount(pdf) == 1 && a3Landscape.match(QString::fromLatin1(pdf)).hasMatch(),
          "Write one A3 landscape page and exclude hidden objects");
    action(window, "Undo Add Object").trigger();

    opc::PatternDocument wide;
    wide.insert(0, {opc::Shape::Rectangle, {-30, -60}, {270, 290}});
    wide.insert(1, {opc::Shape::Line, {0, 0}, {100000, 100000}, {}, -1, opc::Layer::Construction});
    wide.setLayerVisible(opc::Layer::Construction, false);
    QString error;
    check(opc::exportPatternPdf(pdfPath, wide, {true, true}, {}, {}, error), "Export a tiled pattern");
    check(pageCount(readPdf(pdfPath)) == 4, "Tile a 300 by 350 mm drawing across 2 by 2 A4 pages");
    wide.setLayerVisible(opc::Layer::Construction, true);
    check(!opc::exportPatternPdf(pdfPath, wide, {true, true}, {}, {}, error) && !error.isEmpty(),
          "Reject overlarge exports");
    check(!opc::exportPatternPdf(pdfPath, wide, {false, false}, {}, {}, error) && !error.isEmpty(),
          "Reject blank exports");
    check(pageCount(readPdf(pdfPath)) == 4, "Rejected exports leave the previous file intact");

    action(window, "&New").trigger();
    check(tree.topLevelItemCount() == 0 && overlayCount(view) == 0, "Clear piece state in a new document");
    chooseFile(action(window, "&Open"), pattern);
    check(tree.topLevelItemCount() == 5 && tree.topLevelItem(4)->text(3) == "Construction"
          && overlayCount(view) == 1 && !action(window, "Construction Lines").isChecked(),
          "Restore group, layers and derived geometry when opening a pattern");
}
