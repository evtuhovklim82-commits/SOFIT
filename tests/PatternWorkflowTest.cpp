#include "MainWindow.h"
#include "PatternView.h"

#include <QApplication>
#include <QDialogButtonBox>
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

    action(window, "&New").trigger();
    check(tree.topLevelItemCount() == 0 && overlayCount(view) == 0, "Clear piece state in a new document");
    chooseFile(action(window, "&Open"), pattern);
    check(tree.topLevelItemCount() == 5 && tree.topLevelItem(4)->text(3) == "Construction"
          && overlayCount(view) == 1 && !action(window, "Construction Lines").isChecked(),
          "Restore group, layers and derived geometry when opening a pattern");
}
