#include "MainWindow.h"
#include "PatternView.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "PieceGeometry.h"
#include "Production.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMouseEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWizard>
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
        if (!dialog) std::cerr << "Action: " << command.text().toStdString() << "\n";
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

void testModelingWorkflow() {
    MainWindow window;
    window.show();
    auto& view = *window.findChild<PatternView*>();
    auto& tree = *window.findChild<QTreeWidget*>();
    view.entityDrawn({opc::Shape::Curve, {0, 0}, {100, 0}, {50, 40}});
    tree.topLevelItem(0)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Split Line or Curve…"), [](QInputDialog& dialog) {
        dialog.setDoubleValue(30);
        dialog.accept();
    });
    check(tree.topLevelItemCount() == 2, "Split curve in the editor");
    action(window, "Undo Split Line or Curve").trigger();
    check(tree.topLevelItemCount() == 1, "Undo split atomically");
    action(window, "Redo Split Line or Curve").trigger();
    check(tree.topLevelItemCount() == 2, "Redo split atomically");

    action(window, "Dart").trigger();
    view.setSnapToObjects(false);
    const auto click = [&view](QPointF scenePoint) {
        const QPointF local(view.mapFromScene(scenePoint));
        const QPointF global(view.viewport()->mapToGlobal(local.toPoint()));
        QMouseEvent event(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    click({0, 50});
    click({10, 90});
    click({20, 50});
    check(tree.topLevelItemCount() == 4 && tree.topLevelItem(2)->text(3) == "Construction"
          && tree.topLevelItem(3)->text(3) == "Construction", "Draw a dart on the canvas");
    action(window, "Undo Draw Dart").trigger();
    check(tree.topLevelItemCount() == 2, "Undo both dart legs at once");
    action(window, "Redo Draw Dart").trigger();
    check(tree.topLevelItemCount() == 4, "Redo both dart legs at once");
}

void testContourModelingWorkflow() {
    MainWindow window;
    window.show();
    auto& view = *window.findChild<PatternView*>();
    auto& tree = *window.findChild<QTreeWidget*>();
    view.entityDrawn({opc::Shape::Rectangle, {0, 0}, {100, 80}});
    tree.topLevelItem(0)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Create Piece from Selection…"), [](QInputDialog& dialog) {
        dialog.setTextValue("Front");
        dialog.accept();
    });
    view.entityDrawn({opc::Shape::Point, {500, 500}});
    tree.topLevelItem(1)->setCheckState(1, Qt::Unchecked);
    view.entityDrawn({opc::Shape::Line, {0, 40}, {100, 40}, {}, -1, opc::Layer::Construction});
    tree.clearSelection();
    tree.topLevelItem(2)->setSelected(true);
    action(window, "Cut Piece…").trigger();
    check(tree.topLevelItemCount() == 9 && tree.topLevelItem(1)->text(2) == "Front A"
          && tree.topLevelItem(8)->text(2) == "Front B" && !view.entityVisible(0),
          "The Modeling menu cuts a piece and preserves unrelated object visibility");
    action(window, "Undo Cut Piece").trigger();
    check(tree.topLevelItemCount() == 3 && tree.topLevelItem(0)->text(2) == "Front"
          && tree.topLevelItem(2)->text(3) == "Construction" && !view.entityVisible(1),
          "Undo restores the source outline and cut guide in one step");
    action(window, "Redo Cut Piece").trigger();
    check(tree.topLevelItemCount() == 9 && !view.entityVisible(0), "Redo restores both panels");
    action(window, "Undo Cut Piece").trigger();
    tree.topLevelItem(2)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Slash and Spread…"), [](QInputDialog& dialog) {
        dialog.setDoubleValue(25);
        dialog.accept();
    });
    check(tree.topLevelItemCount() == 9 && tree.topLevelItem(8)->text(2) == "Front",
          "Slash and Spread rebuilds one outline");
    tree.topLevelItem(0)->setCheckState(1, Qt::Checked);
    action(window, "Undo Slash and Spread").trigger();
    check(view.entityVisible(1), "Undo keeps later visibility changes to surviving objects");
    tree.clearSelection();
    tree.topLevelItem(2)->setSelected(true);
    action(window, "Delete Selection").trigger();

    const QPointF apex(50, 35);
    view.dartDrawn({opc::Shape::Line, {40, 0}, apex, {}, -1, opc::Layer::Construction},
                   {opc::Shape::Line, apex, {60, 0}, {}, -1, opc::Layer::Construction});
    view.entityDrawn({opc::Shape::Line, apex, {100, 40}, {}, -1, opc::Layer::Construction});
    tree.clearSelection();
    for (int i = 2; i < 5; ++i)
        tree.topLevelItem(i)->setSelected(true);
    action(window, "Close and Transfer Dart…").trigger();
    check(tree.topLevelItemCount() == 9 && tree.topLevelItem(8)->text(3) == "Main",
          "Dart transfer replaces the original outline and construction guides");

    QTemporaryDir temp;
    const auto patternPath = temp.filePath("transferred.sofit");
    chooseFile(action(window, "&Save"), patternPath);
    opc::PatternDocument document;
    check(document.load(patternPath), "Save the modeled document through the editor");
    const auto geometry = opc::analyzePiece(document, 0);
    check(geometry.error == opc::ContourError::None && geometry.contour.contains(apex),
          "Saved geometry contains the relocated dart");
    QString error;
    const QVector<bool> visible(document.entities().size(), true);
    const auto pdfPath = temp.filePath("transferred.pdf");
    check(opc::exportPatternPdf(pdfPath, document, visible, {}, {}, error),
          "Export the transferred dart to PDF");
    check(pageCount(readPdf(pdfPath)) > 0, "The modeled PDF has a printable page");
    check(opc::exportPatternDxf(temp.filePath("transferred.dxf"), document, visible, {geometry}, error),
          "Export the transferred dart to DXF");

}

}

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    testModelingWorkflow();
    testContourModelingWorkflow();
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

    runDialog<QDialog>(action(window, "Sizes, Measurements and Grade Rules…"), [](QDialog& dialog) {
        const auto click = [&dialog](const QString& text) {
            for (auto* button : dialog.findChildren<QPushButton*>()) {
                if (button->text() == text) {
                    button->click();
                    return;
                }
            }
            check(false, "Find production dialog button");
        };
        auto* tabs = dialog.findChild<QTabWidget*>();
        auto* measurements = tabs->widget(0)->findChild<QTableWidget*>();
        click("Add measurement row");
        measurements->item(0, 0)->setText("L");
        measurements->item(0, 1)->setText("Chest");
        measurements->item(0, 2)->setText("1000");
        tabs->setCurrentIndex(1);
        auto* rules = tabs->widget(1)->findChild<QTableWidget*>();
        click("Add grade rule");
        rules->item(0, 0)->setText("L");
        auto* points = qobject_cast<QComboBox*>(rules->cellWidget(0, 2));
        points->setCurrentIndex(points->findData(QPointF(100, 40)));
        rules->item(0, 3)->setText("25");
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    auto* overlayMenu = action(window, "Size Overlays").menu();
    overlayMenu->aboutToShow();
    overlayMenu->actions().front()->trigger();
    check(overlayCount(view) == 2, "Display a graded size over the base pattern");

    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        dialog.findChild<QDoubleSpinBox*>()->setValue(1450);
        for (auto* combo : dialog.findChildren<QComboBox*>()) {
            if (combo->findText("Base pattern") >= 0)
                combo->setCurrentIndex(combo->findData("L"));
        }
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Add copy")
                button->click();
        }
        bool estimate = false;
        for (auto* label : dialog.findChildren<QLabel*>())
            estimate |= label->text().contains("Copies: 1") && label->text().contains("Invalid contours: 0");
        check(estimate, "Report fabric layout usage");
        auto* layoutView = dialog.findChild<QGraphicsView*>();
        const auto selected = layoutView->scene()->selectedItems();
        check(selected.size() == 1, "Select the added copy");
        selected.front()->setPos(20, 40);
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Turn 180°")
                button->click();
        }
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action(window, "&Save").trigger();
    check(loaded.load(pattern) && loaded.production().sizes.size() == 1 && loaded.production().rules.size() == 1
          && loaded.production().placements.size() == 1 && loaded.production().placements.front().size == "L"
          && loaded.production().fabricWidth == 1450
          && loaded.production().placements.front().position == QPointF(20, 40)
          && loaded.production().placements.front().reversed, "Save grading and layout through the UI");

    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        for (auto* option : dialog.findChildren<QCheckBox*>()) {
            if (option->text() == "One-way nap or print")
                option->setChecked(true);
        }
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Auto arrange")
                button->click();
        }
        bool utilization = false;
        for (auto* label : dialog.findChildren<QLabel*>())
            utilization |= label->text().contains("Fabric utilization:");
        check(utilization, "Show utilization after automatic arrangement");
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action(window, "&Save").trigger();
    check(loaded.load(pattern) && loaded.production().directionalFabric
          && loaded.production().placements.size() == 1 && !loaded.production().placements.front().reversed
          && loaded.production().placements.front().position == QPointF{},
          "Save and reload the automatic arrangement and directional fabric setting");

    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        for (auto* combo : dialog.findChildren<QComboBox*>()) {
            const auto pairMode = combo->findText("Left + right pair");
            if (pairMode >= 0)
                combo->setCurrentIndex(pairMode);
        }
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Add copy")
                button->click();
        }
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Auto arrange")
                button->click();
        }
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action(window, "&Save").trigger();
    check(loaded.load(pattern) && loaded.production().placements.size() == 3
          && loaded.production().placements[1].hand == opc::CutHand::Left
          && loaded.production().placements[2].hand == opc::CutHand::Right
          && !loaded.production().placements[1].reversed && !loaded.production().placements[2].reversed,
          "Add and arrange a mirror pair with one-way fabric through the UI");

    const auto gradedDxfPath = temp.filePath("graded.dxf");
    const auto gradedPdfPath = temp.filePath("graded.pdf");
    const auto exportSize = [&](QAction& command, const QString& path) {
        runDialog<QDialog>(command, [&path](QDialog& dialog) {
            auto* sizes = dialog.findChild<QComboBox*>();
            check(sizes && sizes->findData(QStringLiteral("L")) == 1, "Offer graded size for export");
            sizes->setCurrentIndex(1);
            QTimer::singleShot(0, [&path] {
                auto* save = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                check(save, "Choose graded export file");
                save->selectFile(path);
                static_cast<QDialog*>(save)->accept();
            });
            dialog.accept();
        });
    };
    exportSize(action(window, "Export &DXF…"), gradedDxfPath);
    QFile gradedDxf(gradedDxfPath);
    check(gradedDxf.open(QIODevice::ReadOnly) && gradedDxf.readAll().contains("\n125\n"),
          "Write the shifted X coordinate to DXF");
    exportSize(pdfAction, gradedPdfPath);
    check(pageCount(readPdf(gradedPdfPath)) == 1, "Export the graded pattern to PDF");

    action(window, "&New").trigger();
    check(tree.topLevelItemCount() == 0 && overlayCount(view) == 0, "Clear piece state in a new document");
    chooseFile(action(window, "&Open"), pattern);
    check(tree.topLevelItemCount() == 5 && tree.topLevelItem(4)->text(3) == "Construction"
          && overlayCount(view) == 1 && !action(window, "Construction Lines").isChecked(),
          "Restore group, layers and derived geometry when opening a pattern");

    MainWindow draftWindow;
    draftWindow.show();
    auto& draftTree = *draftWindow.findChild<QTreeWidget*>();
    auto& draftView = *draftWindow.findChild<PatternView*>();
    draftView.entityDrawn({opc::Shape::Line, {0, 10}, {100, 10}});
    draftTree.topLevelItem(0)->setCheckState(1, Qt::Unchecked);
    auto& editDraft = action(draftWindow, "Parametric Draft…");
    runDialog<QDialog>(editDraft, [](QDialog& dialog) {
        auto& tabs = *dialog.findChild<QTabWidget*>();
        const auto append = [&](int tab, const QStringList& values) {
            tabs.setCurrentIndex(tab);
            auto* page = tabs.currentWidget();
            for (auto* button : page->findChildren<QPushButton*>()) {
                if (button->text() == "Add row")
                    button->click();
            }
            auto* table = page->findChild<QTableWidget*>();
            check(table->rowCount() > 0, "Add a row in the draft editor");
            for (int col = 0; col < values.size(); ++col)
                table->item(table->rowCount() - 1, col)->setText(values[col]);
        };
        append(0, {"chest", "400"});
        append(1, {"ease", "10"});
        append(2, {"A", "0", "0"});
        append(2, {"B", "unknown / 4 + ease", "0"});
        append(3, {"AB", "A", "B", {}});
        auto* apply = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        apply->click();
        check(dialog.isVisible(), "Invalid formula keeps the editor open");
        bool hasError = false;
        for (const auto* label : dialog.findChildren<QLabel*>())
            hasError |= label->isVisible() && label->text().contains("Unknown variable");
        check(hasError, "Display the reason for an invalid formula");
        tabs.widget(2)->findChild<QTableWidget*>()->item(1, 1)->setText("chest / 4 + ease");
        apply->click();
    });
    check(draftTree.topLevelItemCount() == 4 && draftTree.topLevelItem(3)->text(0).contains("AB")
          && draftTree.topLevelItem(0)->checkState(1) == Qt::Unchecked,
          "Display generated geometry and preserve manual object visibility");

    runDialog<QDialog>(editDraft, [](QDialog& dialog) {
        auto* tabs = dialog.findChild<QTabWidget*>();
        auto* table = tabs->widget(0)->findChild<QTableWidget*>();
        check(table->item(0, 0)->text() == "chest", "Restore measurements in the editor");
        table->item(0, 1)->setText("480");
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    const auto draftFile = temp.filePath("draft.sofit");
    chooseFile(action(draftWindow, "&Save"), draftFile);
    check(loaded.load(draftFile) && loaded.entities().last().b == QPointF(130, 0),
          "Update the generated line after editing a measurement through the UI");
    action(draftWindow, "Undo Edit Parametric Draft").trigger();
    action(draftWindow, "&Save").trigger();
    check(loaded.load(draftFile) && loaded.entities().last().b == QPointF(110, 0)
          && loaded.parametricDraft().measurements[0].mm == 400, "Undo restores the formula inputs and geometry");
    action(draftWindow, "Redo Edit Parametric Draft").trigger();
    action(draftWindow, "&Save").trigger();
    check(loaded.load(draftFile) && loaded.entities().last().b == QPointF(130, 0),
          "Redo restores the changed geometry");
    action(draftWindow, "&New").trigger();
    chooseFile(action(draftWindow, "&Open"), draftFile);
    check(draftTree.topLevelItemCount() == 4 && draftTree.topLevelItem(3)->text(0).contains("AB"),
          "Open a parametric project through the UI");

    const auto seamFile = temp.filePath("seam-workflow.sofit");
    opc::PatternDocument seamFixture;
    seamFixture.pieces().append({QStringLiteral("Front"), 10});
    seamFixture.pieces().append({QStringLiteral("Back"), 10});
    for (qsizetype piece = 0; piece < 2; ++piece) {
        const auto x = 150.0 * piece;
        const auto width = 100.0 + 5 * piece;
        const QVector<QPointF> corners{{x, 0}, {x + width, 0}, {x + width, 40}, {x, 40}};
        for (qsizetype i = 0; i < corners.size(); ++i)
            seamFixture.insert(seamFixture.entities().size(),
                {opc::Shape::Line, corners[i], corners[(i + 1) % corners.size()], {}, piece});
    }
    check(seamFixture.save(seamFile), "Write seam fixture");
    MainWindow seamWindow;
    seamWindow.show();
    chooseFile(action(seamWindow, "&Open"), seamFile);
    auto* seamTree = seamWindow.findChild<QTreeWidget*>();
    seamTree->topLevelItem(0)->setSelected(true);
    seamTree->topLevelItem(4)->setSelected(true);
    runDialog<QDialog>(action(seamWindow, "Match Selected Seam Sections…"), [](QDialog& dialog) {
        const auto labels = dialog.findChildren<QLabel*>();
        const auto contains = [&labels](const QString& text) {
            for (const auto* label : labels) {
                if (label->text().contains(text))
                    return true;
            }
            return false;
        };
        check(contains("Outside tolerance"), "Compare signed ease against the default tolerance");
        const auto fields = dialog.findChildren<QDoubleSpinBox*>();
        fields.front()->setValue(5);
        check(contains("Within tolerance"), "Update ease result after changing the target");
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Place Paired Notches") {
                button->click();
                return;
            }
        }
        check(false, "Find the paired notch button");
    });
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && loaded.entities()[0].seamNotches.size() == 3
          && loaded.entities()[4].seamNotches.size() == 3, "Save automatic notches from the UI");
    action(seamWindow, "Undo Place Paired Notches").trigger();
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && loaded.entities()[0].seamNotches.isEmpty()
          && loaded.entities()[4].seamNotches.isEmpty(), "Undo both sides in one action");
    action(seamWindow, "Redo Place Paired Notches").trigger();
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && loaded.entities()[4].seamNotches.size() == 3, "Redo paired notches");

    seamTree->clearSelection();
    seamTree->topLevelItem(0)->setSelected(true);
    runDialog<QDialog>(action(seamWindow, "Allowance for Selected Edges…"), [](QDialog& dialog) {
        dialog.findChild<QCheckBox*>()->setChecked(false);
        dialog.findChild<QDoubleSpinBox*>()->setValue(0);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && loaded.entities()[0].seamAllowanceMm == 0
          && !loaded.entities()[4].seamAllowanceMm, "Override one edge and leave the other piece at its default");
    action(seamWindow, "Undo Edit Edge Allowances").trigger();
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && !loaded.entities()[0].seamAllowanceMm, "Undo the override");
    action(seamWindow, "Redo Edit Edge Allowances").trigger();
    action(seamWindow, "&Save").trigger();
    check(loaded.load(seamFile) && loaded.entities()[0].seamAllowanceMm == 0, "Redo the override");

    const auto front = opc::analyzePiece(loaded, 0);
    const auto back = opc::analyzePiece(loaded, 1);
    check(front.allowance.boundingRect() == QRectF(-10, 0, 120, 50)
          && back.allowance.boundingRect() == QRectF(140, -10, 125, 60), "Build both production outlines");
    const QVector<bool> seamVisible(loaded.entities().size(), true);
    const auto seamPdf = temp.filePath("seams.pdf");
    const auto seamDxf = temp.filePath("seams.dxf");
    QString seamError;
    check(opc::exportPatternPdf(seamPdf, loaded, seamVisible, {front.allowance, back.allowance}, {}, seamError),
          "Export PDF with uneven allowance and automatic notches");
    check(pageCount(readPdf(seamPdf)) > 0, "Read the PDF produced for the seam fixture");
    check(opc::exportPatternDxf(seamDxf, loaded, seamVisible, {front, back}, seamError),
          "Export DXF with uneven allowance and automatic notches");
    QFile seamDxfFile(seamDxf);
    check(seamDxfFile.open(QIODevice::ReadOnly), "Read the seam DXF");
    const auto seamDxfData = seamDxfFile.readAll();
    check(seamDxfData.count("\nNOTCH\n") == 7 && seamDxfData.count("\nALLOWANCE\n") == 3,
          "DXF contains six automatic marks and two allowance outlines, plus the layer records");

    MainWindow templateWindow;
    templateWindow.show();
    runDialog<QWizard>(action(templateWindow, "New Pattern from Template…"), [](QWizard& wizard) {
        wizard.findChild<QComboBox*>()->setCurrentIndex(1);
        wizard.next();
        wizard.button(QWizard::FinishButton)->click();
    });
    auto* templateTree = templateWindow.findChild<QTreeWidget*>();
    check(templateTree->topLevelItemCount() == 13, "Create an editable A-line skirt through the wizard");
    action(templateWindow, "Undo Create Pattern from Template").trigger();
    check(templateTree->topLevelItemCount() == 0, "Undo the new template drawing");
    action(templateWindow, "Redo Create Pattern from Template").trigger();

    runDialog<QDialog>(action(templateWindow, "Sizes, Measurements and Grade Rules…"), [](QDialog& dialog) {
        auto* measurements = dialog.findChild<QTabWidget*>()->widget(0)->findChild<QTableWidget*>();
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Add measurement row")
                button->click();
        }
        check(measurements->rowCount() == 1, "Add a template size measurement row");
        measurements->item(0, 0)->setText("L");
        measurements->item(0, 1)->setText("hip");
        measurements->item(0, 2)->setText("1200");
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto skirtPath = temp.filePath("skirt.sofit");
    chooseFile(action(templateWindow, "&Save"), skirtPath);
    check(loaded.load(skirtPath)
          && opc::analyzePiece(loaded, 0).bounds.width() == 740
          && opc::analyzePiece(opc::gradedPattern(loaded, "L"), 0).bounds.width() == 840,
          "Recompute the template contour for the entered size after saving and opening");

}
