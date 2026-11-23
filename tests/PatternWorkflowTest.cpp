#include "MainWindow.h"
#include "FittingBlocks.h"
#include "PatternView.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "PieceGeometry.h"
#include "Production.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QMessageBox>
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

void testGeometryEditing() {
    MainWindow window;
    auto& view = *window.findChild<PatternView*>();
    auto& tree = *window.findChild<QTreeWidget*>();
    view.entityDrawn({opc::Shape::Curve, {0, 0}, {100, 0}, {50, -10}});
    view.entityDrawn({opc::Shape::Line, {100, 0}, {50, 80}});
    view.entityDrawn({opc::Shape::Line, {50, 80}, {0, 0}});
    for (qsizetype i = 0; i < 3; ++i)
        tree.topLevelItem(i)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Create Piece from Selection…"), [](QInputDialog& dialog) {
        dialog.setTextValue("Front");
        dialog.accept();
    });
    tree.topLevelItem(1)->setCheckState(1, Qt::Unchecked);
    tree.clearSelection();
    tree.topLevelItem(0)->setSelected(true);
    runDialog<QDialog>(action(window, "Edit Geometry…"), [](QDialog& dialog) {
        const auto fields = dialog.findChildren<QDoubleSpinBox*>();
        check(fields[2]->value() == 100 && fields[7]->value() == -10,
              "Populate endpoint and control coordinates from the selected curve");
        check(dialog.findChild<QCheckBox*>()->isChecked(), "Carry connected endpoints by default");
        fields[2]->setValue(115);
        fields[7]->setValue(-20);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });

    QTemporaryDir temp;
    const auto path = temp.filePath("edited.sofit");
    chooseFile(action(window, "&Save"), path);
    const auto verify = [&](double x, double controlY) {
        opc::PatternDocument loaded;
        check(loaded.load(path) && loaded.entities()[0].b == QPointF(x, 0)
              && loaded.entities()[1].a == QPointF(x, 0)
              && loaded.entities()[0].control == QPointF(50, controlY)
              && opc::analyzePiece(loaded, 0).error == opc::ContourError::None
              && !view.entityVisible(1),
              "Edit, undo and redo the connected curve while preserving visibility and contour closure");
    };
    verify(115, -20);
    action(window, "Undo Edit Geometry").trigger();
    action(window, "&Save").trigger();
    verify(100, -10);
    action(window, "Redo Edit Geometry").trigger();
    action(window, "&Save").trigger();
    verify(115, -20);
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
    tree.topLevelItem(2)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Add Box Pleat…"), [](QInputDialog& dialog) {
        dialog.setDoubleValue(24);
        dialog.accept();
    });
    check(tree.topLevelItemCount() == 12 && tree.topLevelItem(11)->text(3) == "Construction",
          "Add fullness and fold guides from the Modeling menu");
    action(window, "Undo Add Box Pleat").trigger();
    check(tree.topLevelItemCount() == 3 && tree.topLevelItem(2)->text(3) == "Construction",
          "Undo pleat restores the source slash");
    action(window, "Redo Add Box Pleat").trigger();
    check(tree.topLevelItemCount() == 12, "Redo restores pleat guides");
    action(window, "Undo Add Box Pleat").trigger();

    tree.clearSelection();
    tree.topLevelItem(2)->setSelected(true);
    action(window, "Delete Selection").trigger();
    runDialog<QDialog>(action(window, "Shape Silhouette…"), [](QDialog& dialog) { dialog.accept(); });
    check(tree.topLevelItemCount() == 5, "Shape a rectangle into a four-edge silhouette");
    action(window, "Undo Shape Silhouette").trigger();
    check(tree.topLevelItemCount() == 2, "Undo restores the rectangle");
    action(window, "Redo Shape Silhouette").trigger();
    check(tree.topLevelItemCount() == 5, "Redo restores the changed contour");
    action(window, "Undo Shape Silhouette").trigger();

    const QPointF apex(50, 35);
    view.dartDrawn({opc::Shape::Line, {40, 0}, apex, {}, -1, opc::Layer::Construction},
                   {opc::Shape::Line, apex, {60, 0}, {}, -1, opc::Layer::Construction});
    view.entityDrawn({opc::Shape::Line, apex, {100, 40}, {}, -1, opc::Layer::Construction});
    tree.clearSelection();
    for (int i = 2; i < 5; ++i)
        tree.topLevelItem(i)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Close and Transfer Dart…"), [](QInputDialog& dialog) {
        dialog.accept();
    });
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

namespace {
void testStageNavigationAndPreferences() {
    MainWindow window;
    window.show();
    auto* toolbar = window.findChild<QToolBar*>("workflowToolbar");
    check(toolbar && !toolbar->isHidden(), "Workflow toolbar is visible by default");
    const auto buttons = toolbar->findChildren<QToolButton*>(QString{}, Qt::FindDirectChildrenOnly);
    const QList<QPair<QString, QString>> expected{
        {"Construction", "Parametric Draft…"}, {"Modeling", "Slash and Spread…"},
        {"Grading", "Sizes, Measurements and Grade Rules…"},
        {"Pieces", "Piece, Contour and Allowance…"}, {"Layout", "Fabric Layout…"}
    };
    QList<QToolButton*> stages;
    for (auto* button : buttons) {
        if (button->menu())
            stages.append(button);
    }
    check(stages.size() == expected.size(), "Five workflow stages");
    for (qsizetype i = 0; i < expected.size(); ++i) {
        check(stages[i]->text() == expected[i].first, "Workflow stage order and label");
        check(stages[i]->menu()->actions().contains(&action(window, expected[i].second)),
              "Stage toolbar opens menu containing the real command");
    }

    auto* layoutButton = stages.back();
    auto* layoutMenu = layoutButton->menu();
    bool menuOpened = false;
    const auto opening = QObject::connect(layoutMenu, &QMenu::aboutToShow, toolbar, [&] {
        menuOpened = true;
        QTimer::singleShot(0, layoutMenu, &QMenu::close);
    });
    const auto clickPosition = layoutButton->rect().center();
    QMouseEvent click(QEvent::MouseButtonPress, clickPosition, layoutButton->mapToGlobal(clickPosition),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(layoutButton, &click);
    QObject::disconnect(opening);
    check(menuOpened, "Layout opens from the stage button");

    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        bool hasPieceGuidance = false;
        for (auto* label : dialog.findChildren<QLabel*>())
            hasPieceGuidance |= label->text().contains("Pieces → Create Piece from Selection");
        check(hasPieceGuidance, "Empty layout explains the required prior stage");
        dialog.reject();
    });
    runDialog<QDialog>(action(window, "GRAFIS ⇄ SOFIT"), [](QDialog& dialog) {
        auto* guide = dialog.findChild<QTextBrowser*>("grafisWorkflowGuide");
        check(guide && guide->toHtml().contains("help.grafis.de"), "Guide links to GRAFIS publisher documentation");
        check(guide->toPlainText().contains("Layout → Fabric Layout"), "Guide names the working SOFIT command");
        dialog.reject();
    });

    const auto setShortcut = [&](bool save) {
        runDialog<QDialog>(action(window, "Keyboard Shortcuts…"), [&](QDialog& dialog) {
            auto* layout = dialog.findChild<QKeySequenceEdit*>("layout");
            auto* fit = dialog.findChild<QKeySequenceEdit*>("fit");
            auto* controls = dialog.findChild<QDialogButtonBox*>();
            check(layout && fit && controls, "Shortcut editors are available");
            layout->setKeySequence(QKeySequence("Ctrl+Z"));
            controls->button(QDialogButtonBox::Ok)->click();
            check(dialog.isVisible(), "Existing Undo binding blocks shortcut collision");
            layout->setKeySequence(QKeySequence("Ctrl+Alt+L"));
            fit->setKeySequence(layout->keySequence());
            controls->button(QDialogButtonBox::Ok)->click();
            check(dialog.isVisible(), "Two edited commands cannot share a shortcut");
            fit->clear();
            controls->button(save ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
        });
    };
    setShortcut(false);
    check(action(window, "Fabric Layout…").shortcut().isEmpty(), "Cancel keeps the previous bindings");
    setShortcut(true);
    check(action(window, "Fabric Layout…").shortcut() == QKeySequence("Ctrl+Alt+L"), "Apply layout shortcut");
    toolbar->toggleViewAction()->trigger();

    MainWindow restored;
    restored.show();
    restored.activateWindow();
    QApplication::processEvents();
    check(restored.findChild<QToolBar*>("workflowToolbar")->isHidden(), "Toolbar visibility persists");
    check(action(restored, "Fabric Layout…").shortcut() == QKeySequence("Ctrl+Alt+L"), "Shortcut persists");
    check(action(restored, "Fit Drawing").shortcut().isEmpty(), "Disabled shortcut persists");
    auto& layoutCommand = action(restored, "Fabric Layout…");
    bool activated = false;
    QObject::connect(&layoutCommand, &QAction::triggered, &restored, [&] { activated = true; });
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Fabric Layout", "Shortcut opens the actual layout editor");
        dialog->reject();
    });
    QKeyEvent key(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier | Qt::AltModifier);
    QApplication::sendEvent(restored.findChild<PatternView*>(), &key);
    check(activated, "Configured key dispatches the real action");
    runDialog<QDialog>(action(restored, "Keyboard Shortcuts…"), [](QDialog& dialog) {
        auto* controls = dialog.findChild<QDialogButtonBox*>();
        controls->button(QDialogButtonBox::RestoreDefaults)->click();
        controls->button(QDialogButtonBox::Ok)->click();
    });
    check(layoutCommand.shortcut().isEmpty(), "Default reset clears new binding");
    check(action(restored, "Fit Drawing").shortcut() == QKeySequence("F"), "Default reset restores Fit Drawing");
    QSettings().clear();
}
}

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir preferences;
    check(preferences.isValid(), "Isolate workflow preferences");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    app.setOrganizationName("SOFIT Workflow Test");
    app.setApplicationName("Preferences");
    testStageNavigationAndPreferences();
    testGeometryEditing();
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
    QString markerError;
    const auto markerPdf = QFileInfo(pattern).absolutePath() + "/layout.pdf";
    check(opc::exportFabricPdf(markerPdf, loaded, loaded.production(), {}, markerError),
          "Export the graded layout with the 1:1 A4 print renderer");
    const auto markerBytes = readPdf(markerPdf);
    check(pageCount(markerBytes) == 9,
          "Tile the full 1450 mm fabric width across nine A4 sheets at actual size");
    auto invalidMarker = loaded.production();
    invalidMarker.placements[0].reversed = true;
    check(!opc::exportFabricPdf(markerPdf, loaded, invalidMarker, {}, markerError)
          && !markerError.isEmpty() && readPdf(markerPdf) == markerBytes,
          "Decline a forbidden rotation without replacing the existing PDF");

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
    seamFixture.production().sizes.append(opc::PatternSize{"Graded", {}});
    seamFixture.production().rules = {{"Graded", 1, {255, 0}, {10, 0}},
                                      {"Graded", 1, {255, 40}, {10, 0}}};
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
        for (auto* box : dialog.findChildren<QComboBox*>()) {
            const int gradedSize = box->findData(QStringLiteral("Graded"));
            if (gradedSize >= 0)
                box->setCurrentIndex(gradedSize);
        }
        check(contains("Graded") && contains("Outside tolerance"),
              "Compare the same seams on a graded size against the target");
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

    MainWindow bodiceWindow;
    bodiceWindow.show();
    runDialog<QWizard>(action(bodiceWindow, "New Pattern from Template…"), [](QWizard& wizard) {
        wizard.findChild<QComboBox*>()->setCurrentIndex(2);
        wizard.next();
        QTimer::singleShot(0, [] {
            auto* report = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            check(report && report->text().contains("Front: armhole")
                  && report->text().contains("Back: armhole")
                  && report->text().contains("Both sections are within tolerance."),
                  "Show the front and back cap seam report with its tolerance result");
            report->button(QMessageBox::Yes)->click();
        });
        wizard.button(QWizard::FinishButton)->click();
    });
    const auto bodicePath = temp.filePath("bodice.sofit");
    chooseFile(action(bodiceWindow, "&Save"), bodicePath);
    opc::PatternDocument bodice;
    check(bodice.load(bodicePath) && bodice.pieces().size() == 3, "Save all three fitting pieces from the UI");
    const auto initialWidth = opc::analyzePiece(bodice, 0).bounds.width();
    runDialog<QInputDialog>(action(bodiceWindow, "Shape Silhouette…"), [](QInputDialog& chooser) {
        QTimer::singleShot(0, [] {
            auto* settings = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(settings && settings->windowTitle() == "Shape Silhouette", "Open silhouette settings");
            for (auto* field : settings->findChildren<QDoubleSpinBox*>())
                field->setValue(105);
            settings->accept();
        });
        chooser.accept();
    });
    action(bodiceWindow, "Construction Tree…").trigger();
    const auto docks = bodiceWindow.findChildren<QDockWidget*>();
    const auto construction = std::ranges::find_if(docks, [](const QDockWidget* dock) {
        return dock->windowTitle() == "Construction Tree";
    });
    check(construction != docks.end(), "Open the construction dock");
    auto* constructionTree = (*construction)->findChild<QTreeWidget*>();
    check(constructionTree && constructionTree->topLevelItem(0)->childCount() == 1
          && constructionTree->topLevelItem(0)->child(0)->childCount() > 0,
          "Show the recorded modeling step and its named formula objects");
    action(bodiceWindow, "&Save").trigger();
    check(bodice.load(bodicePath) && bodice.modelingHistory().size() == 1
          && opc::analyzePiece(bodice, 0).bounds.width() > initialWidth, "Save the modeled bodice");
    action(bodiceWindow, "Undo Shape Silhouette").trigger();
    check(constructionTree->topLevelItem(0)->childCount() == 0, "Refresh the open construction tree after undo");
    action(bodiceWindow, "&Save").trigger();
    check(bodice.load(bodicePath) && bodice.modelingHistory().isEmpty()
          && opc::analyzePiece(bodice, 0).bounds.width() == initialWidth, "Undo formula geometry and history together");
    action(bodiceWindow, "Redo Shape Silhouette").trigger();
    check(constructionTree->topLevelItem(0)->childCount() == 1, "Refresh the open construction tree after redo");
    action(bodiceWindow, "&Save").trigger();
    check(bodice.load(bodicePath) && bodice.modelingHistory().size() == 1, "Redo modeling history");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle().startsWith("Edit "), "Open construction step settings");
        for (auto* field : dialog->findChildren<QDoubleSpinBox*>()) {
            auto* form = qobject_cast<QFormLayout*>(dialog->layout());
            if (qobject_cast<QLabel*>(form->labelForField(field))->text() == "Length from top")
                field->setValue(120);
        }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    constructionTree->itemActivated(constructionTree->topLevelItem(0)->child(0), 0);
    const auto verifyLength = [&](double expected) {
        action(bodiceWindow, "&Save").trigger();
        check(bodice.load(bodicePath) && bodice.modelingHistory()[0].lengthScale == expected,
              "Save, undo and redo an edited construction step");
    };
    verifyLength(1.2);
    action(bodiceWindow, "Undo Edit Modeling Step").trigger();
    verifyLength(1.05);
    action(bodiceWindow, "Redo Edit Modeling Step").trigger();
    verifyLength(1.2);
    runDialog<QDialog>(action(bodiceWindow, "Diagnose Pieces by Size…"), [](QDialog& dialog) {
        const auto* report = dialog.findChild<QTreeWidget*>();
        check(report && report->topLevelItemCount() == 1 && report->topLevelItem(0)->childCount() == 3
              && report->topLevelItem(0)->child(0)->text(1) == "One closed contour",
              "Diagnose the modeled pieces");
        dialog.reject();
    });
    const auto bodicePdf = temp.filePath("bodice.pdf");
    check(opc::exportPatternPdf(bodicePdf, bodice, QVector<bool>(bodice.entities().size(), true),
                                {}, {}, seamError)
          && pageCount(readPdf(bodicePdf)) > 0, "Export the fitting block as tiled PDF");

    opc::PatternDocument yoke;
    opc::ParametricDraft yokeDraft;
    yokeDraft.measurements = {{"width", 100}};
    yokeDraft.points = {{"A", "0", "0"}, {"B", "width", "0"}, {"C", "width", "80"},
                        {"D", "0", "80"}, {"E", "0", "25"}, {"F", "width", "25"}};
    yokeDraft.edges = {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}},
                       {"left", "D", "A", {}}, {"guide", "E", "F", {}}};
    check(yoke.setParametricDraft(yokeDraft), "Create a formula yoke fixture");
    yoke.pieces().append({"Panel"});
    for (qsizetype i = yokeDraft.points.size(); i < yoke.entities().size(); ++i) {
        auto entity = yoke.entities()[i];
        if (entity.draftName == "guide")
            entity.layer = opc::Layer::Construction;
        else
            entity.piece = 0;
        yoke.replace(i, entity);
    }
    yoke.production().sizes.append({"L", {{"width", 140}}});
    const auto yokePath = temp.filePath("yoke.sofit");
    check(yoke.save(yokePath), "Save the formula yoke fixture");
    MainWindow yokeWindow;
    yokeWindow.show();
    chooseFile(action(yokeWindow, "&Open"), yokePath);
    yokeWindow.findChild<QTreeWidget*>()->topLevelItem(yoke.entities().size() - 1)->setSelected(true);
    action(yokeWindow, "Straight Yoke…").trigger();
    action(yokeWindow, "&Save").trigger();
    check(yoke.load(yokePath) && yoke.pieces().size() == 2 && yoke.hasContourHistory(),
          "Save the replayable yoke from the normal modeling command");
    auto* yokeTree = yokeWindow.findChildren<QDockWidget*>().back()->findChild<QTreeWidget*>();
    auto* yokeStep = yokeTree->topLevelItem(0)->child(0);
    auto* pair = yokeStep->child(yokeStep->childCount() - 1);
    check(pair->text(0) == "Paired cut edges" && pair->childCount() == 2
          && pair->child(1)->text(1).contains("140"), "Show the paired cut edges in the base and graded size");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Match Seam Sections", "Open matching from the cut pair");
        check(dialog->findChild<QCheckBox*>("reverseSeamCorrespondence")->isChecked(), "Reverse the paired edge correspondence");
        for (auto* button : dialog->findChildren<QPushButton*>()) {
            if (button->text() == "Place Paired Notches") {
                button->click();
                return;
            }
        }
        check(false, "Find the paired notch command");
    });
    yokeTree->itemActivated(pair, 0);
    action(yokeWindow, "Undo Place Paired Notches").trigger();
    action(yokeWindow, "Undo Straight Yoke").trigger();
    action(yokeWindow, "&Save").trigger();
    check(yoke.load(yokePath) && yoke.pieces().size() == 1 && !yoke.hasContourHistory(),
          "Undo restores the formula guide and original piece");
    action(yokeWindow, "Redo Straight Yoke").trigger();
    action(yokeWindow, "Redo Place Paired Notches").trigger();
    action(yokeWindow, "&Save").trigger();
    check(yoke.load(yokePath) && yoke.pieces().size() == 2
          && opc::analyzePiece(yoke, 0).notchAnchors.size() == 3
          && opc::analyzePiece(yoke, 1).notchAnchors.size() == 3, "Redo restores both panels and the paired marks");
    auto* currentStep = yokeTree->topLevelItem(0)->child(0);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Match Seam Sections", "Open the modeled seam pair again");
        for (auto* button : dialog->findChildren<QPushButton*>()) {
            if (button->text() == "Save Seam Link") {
                button->click();
                return;
            }
        }
        check(false, "Find the saved seam link command");
    });
    yokeTree->itemActivated(currentStep->child(currentStep->childCount() - 1), 0);
    const auto verifyLink = [&](qsizetype links, int pairs) {
        action(yokeWindow, "&Save").trigger();
        check(yoke.load(yokePath) && yoke.seamLinks().size() == links
              && (links == 0 || yoke.seamLinks().front().notchPairs == pairs),
              "Persist the saved seam link through undo and redo");
    };
    verifyLink(1, 3);
    action(yokeWindow, "Undo Save Seam Link").trigger();
    verifyLink(0, 0);
    action(yokeWindow, "Redo Save Seam Link").trigger();
    verifyLink(1, 3);
    auto* linkedSeams = yokeTree->topLevelItem(1);
    check(linkedSeams && linkedSeams->child(0)->text(1).contains("Within tolerance")
          && linkedSeams->child(0)->child(linkedSeams->child(0)->childCount() - 1)->text(0) == "L",
          "Show base and graded linked seams in the construction tree");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Edit Seam Link 1", "Edit the saved seam link");
        dialog->findChild<QSpinBox*>()->setValue(2);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    yokeTree->itemActivated(linkedSeams->child(0), 0);
    verifyLink(1, 2);
    action(yokeWindow, "Undo Edit Seam Link").trigger();
    verifyLink(1, 3);
    action(yokeWindow, "Redo Edit Seam Link").trigger();
    verifyLink(1, 2);
    runDialog<QDialog>(action(yokeWindow, "Diagnose Pieces by Size…"), [](QDialog& dialog) {
        const auto* report = dialog.findChild<QTreeWidget*>();
        check(report && report->topLevelItemCount() == 2
              && report->topLevelItem(1)->child(0)->text(1).contains("Within tolerance"),
              "Audit linked seams with the cutting and grading checks");
        dialog.reject();
    });
    check(yoke.appendSilhouette(1, 0.5, 1.1, 1.1, &seamError) && yoke.save(yokePath),
          "Lengthen only one side of the marked seam");
    MainWindow mismatchedYoke;
    mismatchedYoke.show();
    chooseFile(action(mismatchedYoke, "&Open"), yokePath);
    action(mismatchedYoke, "Construction Tree…").trigger();
    auto* mismatchTree = mismatchedYoke.findChildren<QDockWidget*>().back()->findChild<QTreeWidget*>();
    auto* cut = mismatchTree->topLevelItem(0)->child(0);
    auto* cutPair = cut->child(cut->childCount() - 1);
    for (int i = 0; i < cutPair->childCount(); ++i) {
        auto* sizeRow = cutPair->child(i);
        check(sizeRow->childCount() == 1 && sizeRow->child(0)->text(0) == "Notch positions differ"
              && sizeRow->child(0)->text(1).contains("mm"), "Report notch displacement along each graded seam");
    }

    runDialog<QDialog>(action(mismatchedYoke, "Fabric Layout…"), [](QDialog& layout) {
        for (auto* button : layout.findChildren<QPushButton*>()) {
            if (button->text() != "Cutting report…")
                continue;
            QTimer::singleShot(0, [] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(dialog && dialog->windowTitle() == "Cutting report", "Open the marker cutting report");
                const auto text = dialog->findChild<QPlainTextEdit*>()->toPlainText();
                check(text.contains("Base · Seam link 1") && text.contains("Outside tolerance"),
                      "Surface linked seam ease in the cutting report");
                dialog->reject();
            });
            button->click();
            layout.reject();
            return;
        }
        check(false, "Find the cutting report command");
    });

    for (const int choice : {3, 4}) {
        const bool trousers = choice == 3;
        const QString name = trousers ? "trousers" : "dress";
        MainWindow fittingWindow;
        fittingWindow.show();
        runDialog<QWizard>(action(fittingWindow, "New Pattern from Template…"), [choice](QWizard& wizard) {
            wizard.findChild<QComboBox*>()->setCurrentIndex(choice);
            wizard.next();
            if (choice == 3) {
                for (auto* field : wizard.findChildren<QCheckBox*>()) {
                    if (field->text().startsWith("Track side seams"))
                        field->setChecked(true);
                }
            }
            wizard.button(QWizard::FinishButton)->click();
        });
        runDialog<QDialog>(action(fittingWindow, "Check Fitting Block by Size…"), [trousers](QDialog& dialog) {
            const auto text = dialog.findChild<QPlainTextEdit*>()->toPlainText();
            check(text.contains(trousers ? "Trouser inseam above knee" : "Front dress waist")
                  && text.contains("within tolerance") && !text.contains("outside tolerance"),
                  "Display the named fitting seams and their tolerance result");
            dialog.reject();
        });
        const auto projectPath = temp.filePath(name + ".sofit");
        chooseFile(action(fittingWindow, "&Save"), projectPath);
        opc::PatternDocument block;
        check(block.load(projectPath) && block.pieces().size() == (trousers ? 2 : 4)
              && block.seamLinks().size() == (trousers ? 3 : 0),
              "Round trip all fitting pieces created through the wizard");
        auto& sizes = block.production().sizes;
        sizes.append({"L", {{"waist", 800}, {"hip", 1060}, {"bust", 980}}});
        const auto graded = opc::gradedPattern(block, "L");
        const auto checks = opc::checkFittingBlock(block, "L");
        check(checks.invalidPieces.isEmpty() && checks.missingSeams.isEmpty() && !checks.seams.isEmpty(),
              "Check all graded contours and seam pairs");
        for (const auto& seam : checks.seams)
            check(qAbs(seam.second - seam.first - seam.targetDifference) < 0.01,
                  "Keep graded fitting seams balanced");
        check(opc::analyzePiece(graded, 0).bounds.width() > opc::analyzePiece(block, 0).bounds.width(),
              "Increase the first piece width using size overrides");

        const auto pdf = temp.filePath(name + ".pdf");
        const auto dxf = temp.filePath(name + ".dxf");
        const QVector<bool> visible(graded.entities().size(), true);
        QVector<opc::PieceGeometry> contours;
        for (qsizetype i = 0; i < graded.pieces().size(); ++i)
            contours.append(opc::analyzePiece(graded, i));
        check(opc::exportPatternPdf(pdf, graded, visible, {}, {}, seamError)
              && pageCount(readPdf(pdf)) > 0, "Print the graded fitting block as tiled PDF");
        check(opc::exportPatternDxf(dxf, graded, visible, contours, seamError),
              "Export the graded fitting block as DXF");
        QFile exported(dxf);
        check(exported.open(QIODevice::ReadOnly) && exported.readAll().contains("ENTITIES"),
              "Read the exported DXF geometry section");

        check(block.save(projectPath) && block.load(projectPath)
              && block.production().sizes[0].measurements.value("hip") == 1060
              && block.parametricDraft().edges.size() > 20,
              "Preserve fitting formulas and size overrides on reopening");

        // A point grade offset on one joining edge must affect the report for that size.
        const QString edgeName = trousers ? "trouser_front_inseam_upper" : "skirt_front_waist";
        for (const auto& edge : block.entities()) {
            if (edge.draftName == edgeName)
                block.production().rules.append({"L", edge.piece, trousers ? edge.a : edge.b, {20, 0}});
        }
        const auto offsetChecks = opc::checkFittingBlock(block, "L");
        bool mismatch = false;
        for (const auto& seam : offsetChecks.seams)
            mismatch |= qAbs(seam.second - seam.first - seam.targetDifference) > 0.01;
        check(mismatch, "Detect a seam length difference after a manual size offset");
    }

    MainWindow apronWindow;
    apronWindow.show();
    runDialog<QWizard>(action(apronWindow, "New Pattern from Template…"), [](QWizard& wizard) {
        wizard.findChild<QComboBox*>()->setCurrentText("Bib apron with waist and neck ties");
        wizard.next();
        wizard.button(QWizard::FinishButton)->click();
    });
    const auto apronPath = temp.filePath("apron.sofit");
    chooseFile(action(apronWindow, "&Save"), apronPath);
    opc::PatternDocument apron;
    check(apron.load(apronPath) && apron.pieces().size() == 3, "Create and save an apron through the wizard");

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
