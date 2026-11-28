#include "MainWindow.h"
#include "FittingBlocks.h"
#include "PatternView.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "PieceGeometry.h"
#include "Production.h"
#include "ProductionDialogs.h"

#include <QApplication>
#include <QEventLoop>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QDir>
#include <QUuid>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QMouseEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QMessageBox>
#include <QTreeWidget>
#include <QWizard>
#include <algorithm>
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

void testCanvasAndWorkspace() {
    MainWindow window;
    window.show();
    QApplication::processEvents();
    auto& view = *window.findChild<PatternView*>();
    auto& tree = *window.findChild<QTreeWidget*>();
    auto* properties = window.findChild<QLabel*>("selectionProperties");
    view.entityDrawn({opc::Shape::Line, {0, 0}, {100, 0}});
    view.entityDrawn({opc::Shape::Line, {0, 40}, {100, 40}});
    view.entityDrawn({opc::Shape::Line, {0, 20}, {100, 20}});
    tree.topLevelItem(2)->setCheckState(1, Qt::Unchecked);
    view.fitDrawing();
    const auto mouse = [&view](QEvent::Type type, QPointF position,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint local = view.mapFromScene(position);
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, local, view.viewport()->mapToGlobal(local), button, buttons, modifiers);
        QApplication::sendEvent(view.viewport(), &event);
    };
    const auto marquee = [&mouse](QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        mouse(QEvent::MouseMove, to, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
    };
    marquee({30, -10}, {70, 55});
    check(tree.selectedItems().isEmpty(), "Left-to-right marquee requires the entire object");
    marquee({70, 55}, {30, -10});
    check(tree.selectedItems().size() == 2 && !tree.topLevelItem(2)->isSelected()
          && properties->text().contains("2 objects selected"), "Crossing marquee excludes hidden objects");
    const QPoint contextPosition = view.mapFromScene({50, 40});
    QTimer::singleShot(0, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        check(menu && menu->actions().size() == 3 && tree.selectedItems().size() == 2,
              "Context menu on a selected edge preserves the multiple selection");
        menu->close();
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, contextPosition,
                              view.viewport()->mapToGlobal(contextPosition));
    QApplication::sendEvent(view.viewport(), &context);
    mouse(QEvent::MouseButtonPress, {50, 40}, Qt::ControlModifier);
    mouse(QEvent::MouseButtonRelease, {50, 40}, Qt::ControlModifier);
    check(tree.selectedItems().size() == 1 && properties->text().contains("End: X 100"),
          "Ctrl toggles an object and updates the properties");
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Edit Geometry", "Double-click opens exact geometry");
        dialog->reject();
    });
    mouse(QEvent::MouseButtonDblClick, {50, 0});

    QTemporaryDir projects;
    const auto original = projects.filePath("original.sofit");
    const auto copy = projects.filePath("working copy.sofit");
    chooseFile(action(window, "&Save"), original);
    mouse(QEvent::MouseButtonPress, {50, 0});
    mouse(QEvent::MouseMove, {65, -10});
    mouse(QEvent::MouseButtonRelease, {65, -10});
    chooseFile(action(window, "Save &As…"), copy);
    opc::PatternDocument document;
    check(document.load(original) && document.entities()[0].a == QPointF(),
          "Save As keeps the original project");
    check(document.load(copy) && document.entities()[0].a != QPointF(), "Save As writes the moved geometry");
    const auto moved = document.entities()[0].a;
    action(window, "Undo Move Object").trigger();
    action(window, "&Save").trigger();
    check(document.load(copy) && document.entities()[0].a == QPointF(), "Undo drag");
    action(window, "Redo Move Object").trigger();
    action(window, "&Save").trigger();
    check(document.load(copy) && document.entities()[0].a == moved, "Redo drag");

    auto* dock = window.findChild<QDockWidget*>("drawingObjects");
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    window.findChild<QToolBar*>("projectAndView")->hide();
    check(window.close(), "Close the saved project");
    MainWindow restored;
    restored.show();
    check(restored.dockWidgetArea(restored.findChild<QDockWidget*>("drawingObjects")) == Qt::LeftDockWidgetArea
          && restored.findChild<QToolBar*>("projectAndView")->isHidden(), "Restore panel position and toolbar visibility");
    action(restored, "Reset Panel Layout").trigger();
    check(restored.dockWidgetArea(restored.findChild<QDockWidget*>("drawingObjects")) == Qt::RightDockWidgetArea
          && !restored.findChild<QToolBar*>("projectAndView")->isHidden(), "Restore default workspace");
    QString error;
    check(restored.openProject(copy, &error) && restored.windowFilePath() == copy,
          "Open a project with a space in its path");
    QSettings().clear();
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
    runDialog<QDialog>(action(window, "Copy Curve as Guide…"), [](QDialog& dialog) {
        dialog.findChild<QDoubleSpinBox*>("curveCopyX")->setValue(20);
        dialog.findChild<QDoubleSpinBox*>("curveCopyControlY")->setValue(5);
        dialog.findChild<QCheckBox*>()->setChecked(true);
        dialog.accept();
    });
    QTemporaryDir guides;
    const auto guideProject = guides.filePath("curve.sofit");
    chooseFile(action(window, "&Save"), guideProject);
    opc::PatternDocument savedGuide;
    check(savedGuide.load(guideProject) && savedGuide.entities().size() == 2, "Save the copied guide");
    const auto& guide = savedGuide.entities().back();
    check(guide.layer == opc::Layer::Construction && guide.piece == -1 && guide.a == QPointF(120, 0)
          && guide.b == QPointF(20, 0) && guide.control == QPointF(70, 45), "Apply guide offsets and direction");
    action(window, "Undo Copy Curve as Guide").trigger();
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

void testCrossingCurvesWorkflow() {
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
    view.entityDrawn({opc::Shape::Curve, {0, 35}, {100, 35}, {50, 65}, -1, opc::Layer::Construction});
    view.entityDrawn({opc::Shape::Curve, {30, 0}, {50, 80}, {60, 60}, -1, opc::Layer::Construction});
    tree.clearSelection();
    tree.topLevelItem(1)->setSelected(true);
    tree.topLevelItem(2)->setSelected(true);
    action(window, "Cut Along Crossing Guides…").trigger();
    const auto panelEdges = tree.topLevelItemCount();
    check(panelEdges > 3, "Cut along two selected quadratic guides");
    action(window, "Undo Cut Along Crossing Guides").trigger();
    check(tree.topLevelItemCount() == 3 && tree.topLevelItem(2)->text(3) == "Construction",
          "Restore both quadratic guides and the original piece");
    action(window, "Redo Cut Along Crossing Guides").trigger();
    check(tree.topLevelItemCount() == panelEdges, "Restore the four panels from the crossing cut");

    QTemporaryDir projects;
    const auto path = projects.filePath("crossing-curves.sofit");
    chooseFile(action(window, "&Save"), path);
    opc::PatternDocument loaded;
    check(loaded.load(path) && loaded.pieces().size() == 4, "Reopen the four panels");
    check(std::ranges::count_if(loaded.entities(), [](const opc::Entity& edge) {
        return edge.shape == opc::Shape::Curve && edge.layer == opc::Layer::Main;
    }) == 8, "Preserve the quadratic seam edges after reopening");
}

void testLibraryChainWorkflow() {
    opc::ParametricDraft draft{
        {{"w", 100}}, {},
        {{"A", "0", "0"}, {"B", "w", "0"}, {"C", "w", "80"}, {"D", "0", "80"}},
        {{"top", "A", "B", {}}, {"right", "B", "C", {}}, {"bottom", "C", "D", {}}, {"left", "D", "A", {}}}
    };
    const auto sourceWith = [](const auto& definitions) {
        opc::PatternDocument document;
        check(document.setParametricDraft(definitions), "Build the source outline");
        document.pieces().append({"Front"});
        for (qsizetype i = 0; i < document.entities().size(); ++i) {
            auto entity = document.entities()[i];
            if (entity.layer == opc::Layer::Main) {
                entity.piece = 0;
                document.replace(i, entity);
            }
        }
        return document;
    };
    const auto source = sourceWith(draft);
    opc::PatternDocument target;
    check(target.insertConstruction(source, nullptr, {}, "outline.sofit"), "Install a tracked outline");
    target.production().sizes = {{"L", {{"w", 120}}}};
    const auto edge = std::ranges::find(target.entities(), "right", &opc::Entity::draftName);
    auto marked = *edge;
    marked.seamAllowanceMm = 3;
    marked.seamNotches = {0.4};
    marked.seamSymbols = {{opc::PieceSymbol::Cross, 0.4, 3, 6, false}};
    target.replace(edge - target.entities().begin(), marked);
    draft.points.append(opc::DraftPoint{"middle", "w", "iflt(w, 110, 40, 15)"});
    draft.edges[1] = {"head", "B", "middle", {}};
    draft.edges.append(opc::DraftEdge{"tail", "middle", "C", {}});
    const auto revised = sourceWith(draft);
    QTemporaryDir projects;
    const auto originalPath = projects.filePath("receiving.sofit");
    const auto revisionPath = projects.filePath("outline.sofit");
    check(target.save(originalPath) && revised.save(revisionPath), "Save the tracked project and its split revision");
    MainWindow window;
    window.show();
    QString error;
    check(window.openProject(originalPath, &error), qPrintable(error));
    const auto update = QStringLiteral("Обновить экземпляр конструкции…");
    const auto undoText = QStringLiteral("Undo Обновление экземпляра конструкции");
    runDialog<QInputDialog>(action(window, update), [&](QInputDialog& instance) {
        QTimer::singleShot(0, [&] {
            auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            check(file, "Open the revised source");
            file->selectFile(revisionPath);
            QTimer::singleShot(0, [] {
                auto* mapping = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                check(mapping, "Request an explicit chain replacement");
                auto* input = mapping->findChild<QPlainTextEdit*>();
                check(input, "Offer a line for topology mapping");
                input->setPlainText("right = head + tail");
                mapping->accept();
            });
            static_cast<QDialog*>(file)->accept();
        });
        instance.accept();
    });
    action(window, "&Save").trigger();
    opc::PatternDocument loaded;
    check(loaded.load(originalPath) && loaded.libraryInstances().front().edges.value("head") == "right"
          && loaded.libraryInstances().front().edges.contains("tail"), "Save the explicit split and preserve the original receiving ID");
    const auto ids = loaded.libraryInstances().front().edges;
    const auto splitMarks = [&](const opc::PatternDocument& project) {
        const auto graded = opc::gradedPattern(project, "L", nullptr, opc::GradeEdgeNames::Preserve);
        const auto tail = std::ranges::find(graded.entities(), ids.value("tail"), &opc::Entity::draftName);
        return tail != graded.entities().end() && tail->seamNotches.size() == 1 && tail->seamSymbols.size() == 1;
    };
    check(splitMarks(loaded), "Save the marks on the other side of the graded split");
    action(window, undoText).trigger();
    action(window, "&Save").trigger();
    check(loaded.load(originalPath) && loaded.libraryInstances().front().edges.contains("right") && !splitMarks(loaded),
          "Undo restores the previous edge and source binding");
    action(window, QStringLiteral("Redo Обновление экземпляра конструкции")).trigger();
    action(window, "&Save").trigger();
    check(loaded.load(originalPath) && loaded.libraryInstances().front().edges == ids && splitMarks(loaded)
          && opc::analyzePiece(opc::gradedPattern(loaded, "L"), 0).error == opc::ContourError::None,
          "Redo restores the same split identifiers and a closed graded contour");

    opc::SeamLink partial;
    partial.first = {ids.value("tail")};
    partial.second = {"left"};
    partial.notchPairs = 1;
    partial.toleranceMm = 100;
    check(loaded.addSeamLink(partial, &error), qPrintable(error));
    const auto partialPath = projects.filePath("partial.sofit");
    const auto mergedPath = projects.filePath("merged.sofit");
    check(loaded.save(partialPath) && source.save(mergedPath), "Save the partial seam and source without its split landmark");
    check(window.openProject(partialPath, &error), qPrintable(error));
    runDialog<QInputDialog>(action(window, update), [&](QInputDialog& instance) {
        QTimer::singleShot(0, [&] {
            auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            check(file, "Open the merged source");
            file->selectFile(mergedPath);
            QTimer::singleShot(0, [] {
                auto* mapping = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                check(mapping && mapping->findChild<QPlainTextEdit*>(), "Request an explicit merge");
                mapping->findChild<QPlainTextEdit*>()->setPlainText("head + tail = right");
                mapping->accept();
            });
            static_cast<QDialog*>(file)->accept();
        });
        instance.accept();
    });
    const auto checkMerged = [&] {
        action(window, "&Save").trigger();
        check(loaded.load(partialPath) && loaded.seamLinks().size() == 1
              && loaded.seamLinks().front().first == QVector<QString>{"right"}
              && loaded.seamLinks().front().firstSpan.value("L").first == 15.0 / 80
              && opc::gradedPattern(loaded, "L").inspectSeamLinks().checks.front().firstMm == 65
              && std::ranges::find(loaded.entities(), "right", &opc::Entity::draftName)->seamAllowanceMm == 3,
              "Save the partial sewing interval and preserve the graded length with no split point");
    };
    checkMerged();
    action(window, undoText).trigger();
    action(window, "&Save").trigger();
    check(loaded.load(partialPath) && loaded.seamLinks().front() == partial && splitMarks(loaded),
          "Undo restores the previous seam binding and graded marks");
    action(window, QStringLiteral("Redo Обновление экземпляра конструкции")).trigger();
    checkMerged();
}

void testSizeCutWorkflow() {
    opc::PatternDocument source;
    opc::ParametricDraft draft;
    draft.measurements = {{"count", 0}};
    draft.points = {{"A", "0", "40"}, {"B", "100", "40"}};
    draft.edges = {{"slash", "A", "B", {}}};
    check(source.setParametricDraft(draft), "Create a named cut guide");
    auto guide = source.entities().back();
    guide.layer = opc::Layer::Construction;
    source.replace(source.entities().size() - 1, guide);
    source.pieces().append({"Front"});
    source.insert(source.entities().size(), {opc::Shape::Rectangle, {}, {100, 80}, {}, 0});
    source.production().sizes = {{"L", {{"count", 1}}}};
    QTemporaryDir projects;
    const auto path = projects.filePath("size-cut.sofit");
    check(source.save(path), "Save the source with a named guide and size");

    MainWindow window;
    window.show();
    QString error;
    check(window.openProject(path, &error), qPrintable(error));
    auto* tree = window.findChild<QTreeWidget*>();
    tree->topLevelItem(source.entities().size() - 2)->setSelected(true);
    runDialog<QInputDialog>(action(window, "Cut by Size Formula…"), [](QInputDialog& dialog) {
        dialog.setTextValue("count");
        dialog.accept();
    });
    action(window, "&Save").trigger();
    opc::PatternDocument loaded;
    check(loaded.load(path) && loaded.sizeCutHistory().size() == 1 && loaded.pieces().size() == 2,
          "Save and reopen the cut history and both panel slots");
    const auto ids = loaded.sizeCutHistory().front().panels;
    action(window, "Undo Cut by Size Formula").trigger();
    action(window, "&Save").trigger();
    check(loaded.load(path) && loaded.sizeCutHistory().isEmpty() && loaded.pieces().size() == 1,
          "Undo the variable cut and its panel slots together");
    action(window, "Redo Cut by Size Formula").trigger();
    action(window, "&Save").trigger();
    check(loaded.load(path) && loaded.sizeCutHistory().front().panels == ids,
          "Redo with the original stable panel identifiers");
    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        QComboBox* sizes = nullptr;
        QComboBox* pieces = nullptr;
        for (auto* combo : dialog.findChildren<QComboBox*>()) {
            if (combo->findText("Base pattern") >= 0)
                sizes = combo;
            if (combo->findText("Front") >= 0)
                pieces = combo;
        }
        check(sizes && pieces && pieces->count() == 1, "Offer only active panels in the base size");
        sizes->setCurrentIndex(sizes->findText("L"));
        check(pieces->count() == 2, "Offer both cut panels in the named size");
        pieces->setCurrentIndex(1);
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Add copy")
                button->click();
        }
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    action(window, "&Save").trigger();
    check(loaded.load(path) && loaded.production().placements.size() == 1
          && loaded.production().placements.front().piece == ids.value("root/1.1")
          && loaded.production().placements.front().size == "L",
          "Persist the generated panel's stable identifier in the size layout");
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

    action(window, "Undo Close and Transfer Dart").trigger();
    view.entityDrawn({opc::Shape::Line, apex, {50, 80}, {}, -1, opc::Layer::Construction});
    view.entityDrawn({opc::Shape::Line, apex, {0, 40}, {}, -1, opc::Layer::Construction});
    tree.clearSelection();
    for (int i = 2; i < 7; ++i)
        tree.topLevelItem(i)->setSelected(true);
    int questions = 0;
    QTimer selectOutlets;
    QObject::connect(&selectOutlets, &QTimer::timeout, [&] {
        auto* field = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        check(field, "Ask for each outlet and then its share");
        if (++questions == 4) {
            field->setTextValue(QStringLiteral("40 35 25"));
            selectOutlets.stop();
        }
        field->accept();
    });
    selectOutlets.start(0);
    action(window, QStringLiteral("Разделить вытачку…")).trigger();
    const auto modeledCount = tree.topLevelItemCount();
    check(questions == 4 && modeledCount > 9, "Model all three selected outlets");
    const auto undoTitle = QStringLiteral("Undo Разделить вытачку");
    const auto redoTitle = QStringLiteral("Redo Разделить вытачку");
    action(window, undoTitle).trigger();
    check(tree.topLevelItemCount() == 7, "Undo restores the source contour, both legs and all three outlets");
    action(window, redoTitle).trigger();
    check(tree.topLevelItemCount() == modeledCount, "Redo restores the three outlets");


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
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QPushButton* helpButton = nullptr;
        check(buttons, "Find the layout dialog buttons");
        for (auto* button : buttons->buttons()) {
            if (buttons->buttonRole(button) == QDialogButtonBox::HelpRole)
                helpButton = qobject_cast<QPushButton*>(button);
        }
        check(helpButton && helpButton->text() == QStringLiteral("Справка"),
              "Show the Russian help entry in the layout dialog");
        auto* helpShortcut = dialog.findChild<QShortcut*>();
        check(helpShortcut && helpShortcut->key() == QKeySequence(Qt::Key_F1), "Bind layout help to F1");
        QTimer::singleShot(0, &dialog, [] {
            auto* help = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            check(help && help->windowTitle() == QStringLiteral("Раскладка лекал")
                  && help->text().contains(QStringLiteral("раппорт")), "Open the Russian layout help");
            help->accept();
        });
        helpButton->click();
        dialog.reject();
    });
    check(action(window, "Construction Tree…").shortcut() == QKeySequence(Qt::Key_F12)
          && action(window, "Measure").shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M),
          "Bind the documented construction and measurement shortcuts by default");
    runDialog<QDialog>(action(window, "GRAFIS ⇄ SOFIT"), [](QDialog& dialog) {
        auto* guide = dialog.findChild<QTextBrowser*>("grafisWorkflowGuide");
        check(guide && guide->toHtml().contains("help.grafis.de"), "Guide links to GRAFIS publisher documentation");
        check(guide->toPlainText().contains("Layout → Fabric Layout"), "Guide names the working SOFIT command");
        check(guide->toPlainText().contains("Rotate Construction")
              && guide->toPlainText().contains("Move Open Contour Dart")
              && guide->toHtml().contains("Tools/Drehen.htm")
              && guide->toHtml().contains("Tools/Abnaeherr.htm"),
              "Guide maps working modeling commands to the official GRAFIS references");
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

void testProjectRecovery() {
    QTemporaryDir projects;
    const auto original = projects.filePath("original.sofit");
    const auto recovered = projects.filePath("recovered.sofit");
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const auto snapshots = directory.filePath("recovery");
    {
        MainWindow editor;
        editor.show();
        auto* view = editor.findChild<PatternView*>();
        view->entityDrawn({opc::Shape::Line, {0, 0}, {100, 0}});
        chooseFile(action(editor, "&Save"), original);
        view->entityDrawn({opc::Shape::Line, {0, 0}, {0, 50}});
        auto* timer = editor.findChild<QTimer*>("projectAutosaveTimer");
        check(timer && timer->interval() == 30000 && timer->isActive(),
              "Run the project autosave timer");
        check(QMetaObject::invokeMethod(timer, "timeout"), "Trigger the autosave timer");
        check(QDir(snapshots).entryList({"*.sofit"}, QDir::Files).size() == 1,
              qPrintable("Write autosave while editing: " + editor.statusBar()->currentMessage()));
        check(opc::ProjectRecovery::available(snapshots).isEmpty(), "Hide snapshots owned by the current editor");
    }
    check(opc::ProjectRecovery::available(snapshots).size() == 1,
          "Keep a snapshot when normal close cleanup does not run");

    MainWindow editor;
    editor.show();
    runDialog<QInputDialog>(action(editor, "Recover Unsaved Project…"), [](QInputDialog& dialog) {
        check(dialog.comboBoxItems().size() == 1 && dialog.comboBoxItems().first().contains("original"),
              "Offer the timestamped snapshot for recovery");
        dialog.accept();
    });
    check(editor.isWindowModified() && editor.windowFilePath().isEmpty(),
          "A recovered project is unsaved and has no original destination");
    auto* view = editor.findChild<PatternView*>();
    view->entityDrawn({opc::Shape::Line, {100, 0}, {0, 50}});
    chooseFile(action(editor, "&Save"), recovered);
    opc::PatternDocument project;
    check(project.load(original) && project.entities().size() == 1, "Do not overwrite the original project");
    check(project.load(recovered) && project.entities().size() == 3 && !editor.isWindowModified(),
          "Save a recovered project after editing");
    check(opc::ProjectRecovery::available(snapshots).isEmpty(), "Clean up after successful Save As");
    QDir(snapshots).rmdir(".");
}

void testMarkerModelImport() {
    QTemporaryDir projects;
    check(projects.isValid(), "Create a marker project directory");
    const auto sourcePath = projects.filePath("source-model.sofit");
    const auto markerPath = projects.filePath("combined-marker.sofit");
    opc::PatternDocument source;
    source.pieces().append({"Panel"});
    source.insert(0, {opc::Shape::Rectangle, {}, {50, 40}, {}, 0});
    check(source.save(sourcePath), "Save a source model for marker import");
    MainWindow markerWindow;
    markerWindow.show();
    const auto command = QStringLiteral("Добавить модели в раскладку…");
    runDialog<QFileDialog>(action(markerWindow, command), [&](QFileDialog& file) {
        file.selectFile(sourcePath);
        QTimer::singleShot(0, [] {
            auto* size = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            check(size && size->comboBoxItems().size() == 1, "Choose the source base size");
            QTimer::singleShot(0, [] {
                auto* quantity = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                check(quantity && quantity->inputMode() == QInputDialog::IntInput, "Set the number of kits");
                quantity->setIntValue(2);
                QTimer::singleShot(0, [] {
                    auto* layout = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    check(layout && layout->findChild<QGraphicsView*>(), "Open the combined marker");
                    for (auto* button : layout->findChildren<QPushButton*>()) {
                        if (button->text() == "Auto arrange")
                            button->click();
                    }
                    layout->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                });
                quantity->accept();
            });
            size->accept();
        });
        static_cast<QDialog&>(file).accept();
    });
    chooseFile(action(markerWindow, "&Save"), markerPath);
    opc::PatternDocument marker;
    check(marker.load(markerPath) && marker.pieces().size() == 1
          && marker.pieces().front().name.startsWith("source-model")
          && marker.production().placements.size() == 2
          && opc::cuttingReport(marker, marker.production()).canExport(marker.production().fabricWidth),
          "Save two arranged kits through the desktop workflow");
}

void testMaterialLay() {
    QTemporaryDir directory;
    opc::PatternDocument project;
    project.pieces() = {{"Shell"}, {"Lining"}};
    project.insert(0, {opc::Shape::Rectangle, {}, {70, 40}, {}, 0});
    project.insert(1, {opc::Shape::Rectangle, {}, {60, 30}, {}, 1});
    project.production().placements = {{0, {}, {}}, {1, {}, {0, 50}},
        {0, {}, {0, 90}, false, opc::CutHand::Left}, {0, {}, {0, 140}, false, opc::CutHand::Right}};
    const auto input = directory.filePath("order.sofit");
    check(project.save(input), "Save a two-material order");
    MainWindow window;
    window.show();
    chooseFile(action(window, "&Open"), input);

    int step = 0;
    QTimer drive;
    QObject::connect(&drive, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal)
            return;
        const auto current = step++;
        if (current == 0) {
            auto* table = modal->findChild<QTableWidget*>();
            check(table && table->rowCount() == 3, "Assign the two pieces and one paired set");
            auto* tableLength = modal->findChild<QDoubleSpinBox*>("layTableLength");
            auto* maximumLayers = modal->findChild<QSpinBox*>("layMaximumLayers");
            check(tableLength && maximumLayers, "Find cutting table and layer limits");
            tableLength->setValue(200);
            maximumLayers->setValue(3);
            auto* profile = modal->findChild<QComboBox*>("cuttingEquipmentProfile");
            auto* saveProfile = modal->findChild<QPushButton*>("saveCuttingEquipmentProfile");
            check(profile && saveProfile, "Find reusable equipment profiles");
            profile->setEditText("Cutting table A");
            saveProfile->click();
            check(QSettings().value("cuttingEquipment/profiles").toMap().contains("Cutting table A"),
                  "Save the named equipment locally");
            tableLength->setValue(1);
            maximumLayers->setValue(1);
            profile->activated(profile->findText("Cutting table A"));
            check(tableLength->value() == 200 && maximumLayers->value() == 3,
                  "Reload both limits from a named profile");
            qobject_cast<QLineEdit*>(table->cellWidget(0, 2))->setText("Shell");
            qobject_cast<QLineEdit*>(table->cellWidget(1, 2))->setText("Lining");
            qobject_cast<QLineEdit*>(table->cellWidget(2, 2))->setText("Shell");
            qobject_cast<QSpinBox*>(table->cellWidget(2, 3))->setValue(2);
        } else if (current == 1 || current == 2 || current == 4 || current == 5 || current == 7 || current == 8) {
            auto* field = qobject_cast<QInputDialog*>(modal);
            check(field, "Configure each material width and layer count");
            if (current == 1 || current == 4 || current == 7)
                field->setDoubleValue(current == 1 ? 100 : current == 4 ? 120 : 200);
            else {
                check(field->intMaximum() == 3, "Apply the layer limit to every lay");
                field->setIntValue(current == 2 ? 3 : 2);
            }
        } else if (current == 3 || current == 6 || current == 9) {
            check(modal->findChild<QGraphicsView*>(), "Open each material/lay marker");
            if (current == 3) {
                auto* warp = modal->findChild<QDoubleSpinBox*>("markerWarpShrinkage");
                auto* weft = modal->findChild<QDoubleSpinBox*>("markerWeftShrinkage");
                check(warp && weft, "Find the two fabric shrinkage inputs");
                warp->setValue(25);
                weft->setValue(20);
            }
        } else if (current == 10) {
            auto* picker = qobject_cast<QFileDialog*>(modal);
            check(picker, "Ask where to save the material lay");
            picker->selectFile(directory.path());
        } else {
            auto* message = qobject_cast<QMessageBox*>(modal);
            check(current == 11 && message && message->icon() == QMessageBox::Information,
                  "Confirm the exported material lay");
            drive.stop();
        }
        modal->accept();
    });
    drive.start(0);
    action(window, QStringLiteral("Многоматериальный настил…")).trigger();
    check(step == 12 && !window.isWindowModified(), "Export the batch without editing the source project");
    const QDir root(directory.path());
    const auto folders = root.entryList({"SOFIT-lay-*"}, QDir::Dirs);
    step = 0;
    QObject::disconnect(&drive, nullptr, nullptr, nullptr);
    QObject::connect(&drive, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal)
            return;
        switch (step++) {
        case 0:
            modal->findChild<QDoubleSpinBox*>("layTableLength")->setValue(1);
            break;
        case 1:
        case 2:
        case 3:
            break;
        default: {
            auto* message = qobject_cast<QMessageBox*>(modal);
            check(step == 5 && message && message->icon() == QMessageBox::Warning
                  && message->text().contains(QStringLiteral("длине стола 1")),
                  "Reject a marker exceeding the cutting table length before choosing an export directory");
            drive.stop();
            break;
        }
        }
        modal->accept();
    });
    drive.start(0);
    action(window, QStringLiteral("Многоматериальный настил…")).trigger();
    check(step == 5, "Stop material export at the table length limit");
    check(folders.size() == 1, "Create one complete material lay folder");
    const QDir output(root.filePath(folders.front()));
    QFile usage(output.filePath("usage.tsv"));
    check(usage.open(QIODevice::ReadOnly), "Read the material consumption report");
    const auto report = usage.readAll();
    check(report.contains("Lining\tmaterial-1.sofit\t100.00\t1\t40.00\t3\t0.1200\t20.00\t25.00")
          && report.contains("Shell\tmaterial-2.sofit\t120.00\t1\t40.00\t2\t0.0800"),
          "Report the length times the layer count independently for each material");
    for (int i = 1; i <= 3; ++i) {
        const auto stem = QStringLiteral("material-%1").arg(i);
        opc::PatternDocument marker;
        check(marker.load(output.filePath(stem + ".sofit"))
              && marker.production().placements.size() == (i == 3 ? 2 : 1)
              && marker.production().placements.front().piece == (i == 1 ? 1 : 0)
              && opc::cuttingReport(marker, marker.production()).canExport(marker.production().fabricWidth)
              && output.exists(stem + ".dxf"),
              "Reopen each material marker and find its DXF");
        if (i == 3) {
            check(marker.production().placements[0].hand == opc::CutHand::Left
                  && marker.production().placements[1].hand == opc::CutHand::Right,
                  "Keep the matched pair together in the second shell lay");
        }
        readPdf(output.filePath(stem + ".pdf"));
    }

    step = 0;
    QObject::disconnect(&drive, nullptr, nullptr, nullptr);
    QObject::connect(&drive, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal)
            return;
        switch (step++) {
        case 0: {
            auto* picker = qobject_cast<QFileDialog*>(modal);
            check(picker, "Select the saved order manifest");
            picker->selectFile(output.filePath("order.sofit-order"));
            break;
        }
        case 1: {
            auto* table = modal->findChild<QTableWidget*>();
            check(table && table->rowCount() == 3
                  && qobject_cast<QLineEdit*>(table->cellWidget(1, 2))->text() == "Lining"
                  && qobject_cast<QSpinBox*>(table->cellWidget(2, 3))->value() == 2,
                  "Restore material and lay assignment");
            check(modal->findChild<QDoubleSpinBox*>("layTableLength")->value() == 200
                  && modal->findChild<QSpinBox*>("layMaximumLayers")->value() == 3,
                  "Restore equipment limits");
            check(modal->findChild<QComboBox*>("cuttingEquipmentProfile")->currentText() == "Cutting table A",
                  "Restore the equipment name with the saved order");
            auto* saveOnly = modal->findChild<QCheckBox*>("saveMaterialOrderOnly");
            check(saveOnly, "Offer saving without manufacturing exports");
            saveOnly->setChecked(true);
            break;
        }
        case 2: {
            auto* width = qobject_cast<QInputDialog*>(modal);
            check(width && width->doubleValue() == 100, "Restore material width");
            break;
        }
        case 3: {
            auto* layers = qobject_cast<QInputDialog*>(modal);
            check(layers && layers->intValue() == 3, "Restore layer count");
            layers->setIntValue(2);
            break;
        }
        case 4:
            check(modal->findChild<QDoubleSpinBox*>("markerWarpShrinkage")->value() == 25,
                  "Restore marker shrinkage");
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
            break;
        case 11: {
            auto* picker = qobject_cast<QFileDialog*>(modal);
            check(picker, "Select the destination for the revised order");
            picker->selectFile(directory.path());
            break;
        }
        default: {
            auto* message = qobject_cast<QMessageBox*>(modal);
            check(step == 13 && message && message->icon() == QMessageBox::Information,
                  "Save the revised order in a new folder");
            drive.stop();
            break;
        }
        }
        modal->accept();
    });
    drive.start(0);
    action(window, QStringLiteral("Открыть производственный заказ…")).trigger();
    auto revised = root.entryList({"SOFIT-lay-*"}, QDir::Dirs);
    revised.removeAll(folders.front());
    check(revised.size() == 1 && !window.isWindowModified(), "Keep the order independent of the open project");
    const QDir revisedOrder(root.filePath(revised.front()));
    check(revisedOrder.exists("order.sofit-order") && revisedOrder.exists("material-1.sofit")
          && revisedOrder.entryList({"*.pdf", "*.dxf"}, QDir::Files).isEmpty(),
          "Save an editable order without cutting exports");
    QFile revisedUsage(revisedOrder.filePath("usage.tsv"));
    check(revisedUsage.open(QIODevice::ReadOnly)
          && revisedUsage.readAll().contains("Lining\tmaterial-1.sofit\t100.00\t1\t40.00\t2\t0.0800"),
          "Use the edited layer count in the regenerated consumption report");

}

void testJointOrder(bool reproject = false) {
    QTemporaryDir directory;
    check(directory.isValid(), "Create a joint order fixture");
    const QDir root(directory.path());
    opc::PatternDocument source;
    source.pieces() = {{"Front"}};
    source.insert(0, {opc::Shape::Rectangle, {}, {70, 40}, {}, 0});
    source.production().sizes = {{"M", {}}};
    const auto path = root.filePath("source.sofit");
    check(source.save(path), "Save the joint order source");
    MainWindow window;
    window.show();
    chooseFile(action(window, "&Open"), path);

    int stage = 0;
    QString savedOrder;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal)
            return;
        if (auto* strategy = modal->findChild<QComboBox*>("orderPlanningStrategy")) {
            if (stage != 0)
                return;
            const auto* table = modal->findChild<QTableWidget*>();
            qobject_cast<QSpinBox*>(table->cellWidget(0, 1))->setValue(1);
            qobject_cast<QSpinBox*>(table->cellWidget(1, 1))->setValue(2);
            strategy->setCurrentIndex(4);
            ++stage;
            auto* accept = modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            QTimer::singleShot(0, accept, &QPushButton::click);
        } else if (modal->findChild<QTableWidget*>("orderPlanningMaterials")) {
            check(stage++ == 1, "Collect material assignments before scheduling");
            modal->accept();
        } else if (auto* machines = modal->findChild<QPlainTextEdit*>("jointOrderMachines")) {
            check(stage++ == 2, "Collect resources for the same order");
            machines->setPlainText(reproject ? "100;1;10;0-9 15-23" : "100;1;10;0-4 10-18");
            modal->findChild<QPlainTextEdit*>("jointOrderRolls")->setPlainText(
                reproject ? QStringLiteral("Основной;100;165;0,35,100,10")
                          : QStringLiteral("Основной;100;120;0,55,10,10"));
            modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        } else if (auto* schedule = modal->findChild<QTableWidget*>("cuttingOrderSchedule")) {
            check((stage == 3 || stage == 7) && schedule->rowCount() == 3,
                  "Preserve the schedule when opening the saved order");
            check(schedule->item(1, 4)->text() == (reproject ? "15" : "10")
                  && schedule->item(2, 5)->text() == (reproject ? "23" : "18"),
                  "Respect machine gaps and roll order");
            ++stage;
            modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        } else if (auto* picker = qobject_cast<QFileDialog*>(modal)) {
            check(stage == 4 || stage == 6 || stage == 8, "Choose the order input or output");
            picker->selectFile(stage == 6 ? savedOrder : root.absolutePath());
            ++stage;
            modal->accept();
        } else if (auto* message = qobject_cast<QMessageBox*>(modal)) {
            if ((stage != 5 && stage != 9) || message->icon() != QMessageBox::Information)
                std::cerr << stage << ": " << message->text().toStdString() << std::endl;
            check((stage == 5 || stage == 9) && message->icon() == QMessageBox::Information,
                  "Save and export the resource-constrained order");
            ++stage;
            timer.stop();
            modal->accept();
        }
    });
    timer.start(0);
    action(window, QStringLiteral("Спланировать заказ по размерам…")).trigger();
    const auto folders = root.entryList({"SOFIT-lay-*"}, QDir::Dirs);
    check(stage == 6 && folders.size() == 1 && !window.isWindowModified(),
          "Keep joint planning independent of the source project");
    const QDir output(root.filePath(folders.front()));
    savedOrder = output.filePath("order.sofit-order");
    QFile manifest(savedOrder);
    check(manifest.open(QIODevice::ReadOnly), "Read the joint schedule manifest");
    const auto order = QJsonDocument::fromJson(manifest.readAll()).object();
    const auto lays = order.value("lays").toArray();
    check(order.value("version").toInt() == 2 && lays.size() == 3, "Write the scheduled order format");
    const auto middle = lays[1].toObject().value("schedule").toObject();
    check(middle.value("rollStartMm").toDouble() == (reproject ? 85 : 40)
          && middle.value("rollEndMm").toDouble() == (reproject ? 125 : 80)
          && output.exists("material-2.pdf") && output.exists("material-2.dxf"),
          "Export the actual scheduled roll segment");
    opc::PatternDocument marker;
    if (reproject) {
        const auto first = lays[0].toObject().value("schedule").toObject();
        const auto last = lays[2].toObject().value("schedule").toObject();
        check(first.value("layerLengthMm").toDouble() == 85 && last.value("rollEndMm").toDouble() == 165
              && marker.load(output.filePath("material-1.sofit"))
              && marker.production().fabricFlaws == QVector<QRectF>{{0, 35, 100, 10}}
              && marker.production().placements.front().position.y() == 45,
              "Repeat projection after initial length 40 clips a defect, then after length 80 overlaps its trailing 5 mm");
    } else {
        check(marker.load(output.filePath("material-2.sofit"))
              && marker.production().fabricFlaws == QVector<QRectF>{{0, 15, 10, 10}}
              && marker.production().placements.front().position.x() >= 10,
              "Persist the defect at its layer-relative position");
    }

    timer.start(0);
    action(window, QStringLiteral("Открыть производственный заказ…")).trigger();
    check(stage == 10 && root.entryList({"SOFIT-lay-*"}, QDir::Dirs).size() == 2
          && !window.isWindowModified(), "Reopen and re-export the saved schedule intact");
}

void testPlannedOrder(bool mixed = false, bool splitToFit = false, bool useCutModes = false, bool minimumMaterial = false,
                      bool multipleMaterials = false) {
    QTemporaryDir directory;
    check(directory.isValid(), "Create a folder for a size-planned order");
    opc::PatternDocument source;
    source.pieces() = {{"Front"}};
    source.insert(0, {opc::Shape::Rectangle, {}, {70, 40}, {}, 0});
    if (multipleMaterials) {
        source.pieces().append({"Lining"});
        source.insert(1, {opc::Shape::Rectangle, {}, {45, 20}, {}, 1});
    }
    source.production().sizes = {{"M", {}}};
    if (minimumMaterial)
        source.production().fabricFlaws = {{0, 0, 1500, 10}};
    if (useCutModes) {
        source.pieces()[0].cutCount = 3;
        opc::FabricPlacement left{0, {}, {}};
        left.hand = opc::CutHand::Left;
        auto right = left;
        right.hand = opc::CutHand::Right;
        opc::FabricPlacement fold{0, {}, {}};
        fold.fold = opc::FoldEdge::Left;
        source.production().placements = {left, right, fold};
    }
    const QDir root(directory.path());
    const auto path = root.filePath("source.sofit");
    check(source.save(path), "Save the source for the planned order");
    MainWindow window;
    window.show();
    chooseFile(action(window, "&Open"), path);
    const int layCount = multipleMaterials ? 2 : minimumMaterial ? 1 : splitToFit ? 6 : mixed ? 2 : 3;
    const int widthDialogs = splitToFit ? 2 : layCount;
    const int setupSteps = multipleMaterials ? 5 : 0;
    const int destinationStep = 2 + widthDialogs + layCount;
    int step = 0;
    QTimer drive;
    QObject::connect(&drive, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal)
            return;
        if (multipleMaterials && step > 0 && modal->findChild<QComboBox*>("orderPlanningStrategy"))
            return;
        if (multipleMaterials && step > 0 && step <= setupSteps) {
            const int setup = step++;
            if (setup == 1) {
                auto* materials = modal->findChild<QTableWidget*>("orderPlanningMaterials");
                check(materials && materials->rowCount() == 2, "Assign a material to each part");
                qobject_cast<QLineEdit*>(materials->cellWidget(1, 1))->setText("Lining");
            } else if (setup % 2 == 0) {
                auto* width = qobject_cast<QInputDialog*>(modal);
                check(width, "Configure the fabric width per material");
                width->setDoubleValue(setup == 2 ? 300 : 400);
            } else {
                check(modal->windowTitle() == "Fabric Flaws", "Configure separate fabric flaw maps");
                if (setup == 3)
                    modal->findChild<QTableWidget*>()->setRowCount(0);
                modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                return;
            }
            modal->accept();
            return;
        }
        const int current = step == 0 ? 0 : step - setupSteps;
        ++step;
        if (current == 0) {
            auto* table = modal->findChild<QTableWidget*>();
            check(table && table->rowCount() == 2, "Present base and named size quantities");
            qobject_cast<QSpinBox*>(table->cellWidget(0, 1))->setValue(mixed ? 3 : 1);
            qobject_cast<QSpinBox*>(table->cellWidget(1, 1))->setValue(mixed ? 7 : 3);
            for (auto* field : modal->findChildren<QSpinBox*>()) {
                if (field->maximum() == 10000)
                    field->setValue(2);
                if ((mixed || minimumMaterial) && field->maximum() == opc::maxAutomaticCopies / source.pieces().size())
                    field->setValue(4);
            }
            if (minimumMaterial)
                modal->findChild<QComboBox*>("orderPlanningStrategy")->setCurrentIndex(multipleMaterials ? 3 : 2);
            if (useCutModes) {
                auto* modes = modal->findChild<QCheckBox*>("copyBaseKitCutModes");
                check(modes && modes->isEnabled(), "Recognize a complete base kit with a pair and a fold");
                modes->setChecked(true);
            }
            auto* confirm = modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            if (multipleMaterials)
                QTimer::singleShot(0, confirm, &QPushButton::click);
            else
                confirm->click();
            return;
        }
        if (current == 1) {
            auto* table = modal->findChild<QTableWidget*>();
            check(table && table->rowCount() == (multipleMaterials ? 8 : minimumMaterial ? 4 : useCutModes || mixed ? 6 : 3)
                  && !table->cellWidget(0, 3)->isEnabled(),
                  "Create fixed lays and preserve their size quantities");
            if (splitToFit)
                modal->findChild<QDoubleSpinBox*>("layTableLength")->setValue(45);
            if (minimumMaterial && !multipleMaterials) {
                for (int row = 0; row < table->rowCount(); ++row)
                    qobject_cast<QLineEdit*>(table->cellWidget(row, 2))->setText(QStringLiteral("Ткань"));
            }
            modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            return;
        }
        const bool widthStep = splitToFit ? current == 2 || current == 7 : current % 2 == 0;
        if (current < destinationStep && widthStep) {
            auto* width = qobject_cast<QInputDialog*>(modal);
            check(width, "Confirm width for each planned lay");
            if (splitToFit)
                width->setDoubleValue(70);

        } else if (current < destinationStep) {
            check(modal->findChild<QGraphicsView*>(), "Arrange each planned marker");
        } else if (current == destinationStep) {
            auto* picker = qobject_cast<QFileDialog*>(modal);
            check(picker, "Select the output folder for the planned order");
            picker->selectFile(root.absolutePath());
        } else {
            auto* message = qobject_cast<QMessageBox*>(modal);
            check(current == destinationStep + 1 && message && message->icon() == QMessageBox::Information,
                  "Confirm the planned order and cutting output");
            drive.stop();
        }
        modal->accept();
    });
    drive.start(0);
    action(window, QStringLiteral("Спланировать заказ по размерам…")).trigger();
    const auto folders = root.entryList({"SOFIT-lay-*"}, QDir::Dirs);
    check(step == destinationStep + setupSteps + 2 && folders.size() == 1 && !window.isWindowModified(),
          "Save a self-contained plan without modifying the active project");
    const QDir output(root.filePath(folders.front()));
    QFile usage(output.filePath("usage.tsv"));
    check(usage.open(QIODevice::ReadOnly), "Read order consumption");
    const auto report = usage.readAll();
    if (!mixed && !useCutModes && !minimumMaterial)
        check(report.contains("material-1.sofit\t1500.00\t1\t40.00\t1")
          && report.contains("material-2.sofit\t1500.00\t1\t40.00\t2")
          && report.contains("material-3.sofit\t1500.00\t1\t40.00\t1"),
          "Persist exact size demand as one base layer and three M layers");
    QMap<QString, int> totals;
    QMap<QString, int> liningTotals;
    for (int i = 1; i <= layCount; ++i) {
        const auto stem = QStringLiteral("material-%1").arg(i);
        opc::PatternDocument marker;
        const auto copyCount = minimumMaterial ? 4 : useCutModes ? 3 : splitToFit || !mixed ? 1 : i == 1 ? 4 : 2;
        check(marker.load(output.filePath(stem + ".sofit"))
              && marker.production().placements.size() == copyCount
              && output.exists(stem + ".dxf") && output.exists(stem + ".pdf"),
              "Save and export every size marker");
        if (minimumMaterial) {
            const auto cutting = opc::cuttingReport(marker, marker.production());
            const auto expectedFlaws = multipleMaterials && marker.production().placements.front().piece == 1
                ? QVector<QRectF>{} : source.production().fabricFlaws;
            check(marker.production().fabricFlaws == expectedFlaws
                  && (!multipleMaterials || marker.production().fabricWidth == (expectedFlaws.isEmpty() ? 300 : 400))
                  && cutting.canExport(marker.production().fabricWidth)
                  && cutting.usage.length >= (expectedFlaws.isEmpty() ? 20 : 50) - 1e-6,
                  "Keep fabric flaws in the measured and exported marker");
        }
        const int layers = multipleMaterials ? 1 : (splitToFit ? i <= 4 : mixed ? i == 1 : i == 2) ? 2 : 1;
        const auto& placements = marker.production().placements;
        if (useCutModes) {
            check(std::ranges::count(placements, opc::CutHand::Left, &opc::FabricPlacement::hand) == 1
                  && std::ranges::count(placements, opc::CutHand::Right, &opc::FabricPlacement::hand) == 1
                  && std::ranges::count(placements, opc::FoldEdge::Left, &opc::FabricPlacement::fold) == 1,
                  "Retain the mirrored pair and left fold in each exported size");
            totals[placements.front().size] += layers;
        } else {
            for (const auto& placement : placements)
                (multipleMaterials && placement.piece == 1 ? liningTotals : totals)[placement.size] += layers;
        }
        if (mixed) {
            const auto fields = report.split('\n')[i].split('\t');
            check(fields.size() >= 6 && fields[3].toInt() == marker.production().placements.size()
                  && fields[5].toInt() == layers, "Record the mixed kit and layer counts in the order");
            if (splitToFit)
                check(fields[4].toDouble() <= 45, "Every replacement marker fits the specified table");
        }
    }
    const QMap<QString, int> expected{{QString{}, mixed ? 3 : 1}, {QStringLiteral("M"), mixed ? 7 : 3}};
    check(totals == expected && (!multipleMaterials || liningTotals == expected),
          "Export exactly the ordered number of kits for each size and material");
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
#ifdef Q_OS_LINUX
    qputenv("XDG_DATA_HOME", QFile::encodeName(preferences.path()));
#else
    QStandardPaths::setTestModeEnabled(true);
#endif
    app.setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
    testProjectRecovery();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    app.setApplicationName("Preferences");
    testStageNavigationAndPreferences();
    testCanvasAndWorkspace();
    testGeometryEditing();
    testModelingWorkflow();
    testContourModelingWorkflow();
    testCrossingCurvesWorkflow();
    testLibraryChainWorkflow();
    testSizeCutWorkflow();
    testMarkerModelImport();
    testMaterialLay();
    testPlannedOrder();
    testPlannedOrder(true);
    testPlannedOrder(true, true);
    testPlannedOrder(false, false, true);
    testPlannedOrder(false, false, false, true);
    testPlannedOrder(false, false, false, true, true);
    testJointOrder();
    testJointOrder(true);
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
        dialog.findChild<QCheckBox*>("pieceFabricOverride")->setChecked(true);
        dialog.findChild<QDoubleSpinBox*>("pieceWeft")->setValue(5);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    check(overlayCount(view) == 1, "Show the derived allowance");

    action(window, "Undo Edit Piece").trigger();
    check(overlayCount(view) == 0, "Undo allowance");
    action(window, "Redo Edit Piece").trigger();
    check(overlayCount(view) == 1, "Redo allowance");
    runDialog<QDialog>(action(window, "Piece, Contour and Allowance…"), [](QDialog& dialog) {
        check(dialog.findChild<QCheckBox*>("pieceFabricOverride")->isChecked()
              && dialog.findChild<QDoubleSpinBox*>("pieceWeft")->value() == 5,
              "Redo individual piece compensation through piece properties");
        dialog.reject();
    });

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
    auto& productAction = action(window, "Product Information…");
    runDialog<QDialog>(productAction, [](QDialog& dialog) {
        auto* code = dialog.findChild<QLineEdit*>("productStyleCode");
        auto* notes = dialog.findChild<QPlainTextEdit*>("productNotes");
        check(code && notes, "Open product information fields");
        code->setText(QStringLiteral("Модель 004"));
        notes->setPlainText(QStringLiteral("Проверка раскроя\nMain fabric"));
        dialog.accept();
    });
    action(window, "Undo Edit Product Information").trigger();
    runDialog<QDialog>(productAction, [](QDialog& dialog) {
        check(dialog.findChild<QLineEdit*>("productStyleCode")->text().isEmpty(), "Undo product information");
        dialog.reject();
    });
    action(window, "Redo Edit Product Information").trigger();
    runDialog<QDialog>(action(window, "Tech Pack Options…"), [](QDialog& dialog) {
        dialog.findChild<QLineEdit*>()->setText("Cutting instructions");
        const auto fields = dialog.findChildren<QPlainTextEdit*>();
        check(fields.size() == 2, "Open tech pack assembly and material fields");
        fields.first()->setPlainText("Shell and interfacing");
        fields.last()->setPlainText("Join the shoulders");
        dialog.accept();
    });
    action(window, "Undo Edit Tech Pack Options").trigger();
    action(window, "Redo Edit Tech Pack Options").trigger();
    const auto techPack = temp.filePath("tech-pack.pdf");
    chooseFile(action(window, "Export Product Information PDF…"), techPack);
    check(pageCount(readPdf(techPack)) == 1, "Export the product sheet through the UI");
    const auto pattern = temp.filePath("piece.sofit");
    chooseFile(action(window, "&Save"), pattern);
    opc::PatternDocument loaded;
    check(loaded.load(pattern) && loaded.pieces().size() == 1 && loaded.pieces()[0].allowanceMm == 12.375
          && !loaded.layerVisible(opc::Layer::Construction)
          && loaded.productInformation().styleCode == QStringLiteral("Модель 004")
          && loaded.productInformation().notes == QStringLiteral("Проверка раскроя\nMain fabric"),
          "Save geometry and product information through the UI");
    check(loaded.productInformation().reportTitle == "Cutting instructions"
          && loaded.productInformation().billOfMaterials == "Shell and interfacing",
          "Save the customized tech pack with the project");

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
        QApplication::clipboard()->setText(QStringLiteral("Size\tMeasurement\tValue (mm)\r\nL\tChest\t1000\r\n"));
        click("Replace table from clipboard");
        check(measurements->rowCount() == 1 && measurements->item(0, 1)->text() == "Chest",
              "Replace the measurement grid from a spreadsheet");
        click("Copy measurement table");
        check(QApplication::clipboard()->text() == QStringLiteral("Size\tMeasurement\tValue (mm)\nL\tChest\t1000"),
              "Copy a reusable measurement grid");
        QApplication::clipboard()->setText(QStringLiteral(
            "L\tChest\t1000\nL\tSleeve\t600\n48/176\tChest\t1040\n48/176\tSleeve\t640\n"
            "52/164\tChest\t1080\n52/164\tSleeve\t604\n52/176\tChest\t1120\n52/176\tSleeve\t644"));
        click("Replace table from clipboard");
        tabs->setCurrentIndex(1);
        auto* rules = tabs->widget(1)->findChild<QTableWidget*>();
        const auto addRule = [&](int row, const QString& size, const QString& x, const QString& y) {
            click("Add grade rule");
            rules->item(row, 0)->setText(size);
            auto* points = qobject_cast<QComboBox*>(rules->cellWidget(row, 2));
            points->setCurrentIndex(points->findData(QPointF(100, 40)));
            rules->item(row, 3)->setText(x);
            rules->item(row, 4)->setText(y);
        };
        addRule(0, "L", "25", "0");
        addRule(1, "48/176", "45", "12");
        addRule(2, "52/164", "65", "4");
        addRule(3, "52/176", "85", "16");
        tabs->setCurrentIndex(0);
        QTimer::singleShot(0, [&dialog] {
            auto* nested = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(nested && nested != &dialog, "Open the intermediate size dialog");
            nested->findChild<QLineEdit*>()->setText("48/170");
            nested->findChild<QDoubleSpinBox*>()->setValue(25);
            nested->findChild<QCheckBox*>()->setChecked(true);
            nested->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        click(QStringLiteral("Добавить промежуточный размер…"));
        check(measurements->rowCount() == 10 && measurements->item(8, 0)->text() == "48/170"
              && measurements->item(8, 2)->text().toDouble() == 1010
              && measurements->item(9, 2)->text().toDouble() == 610,
              "Interpolate both measurements from the current table");
        check(rules->rowCount() == 5 && rules->item(4, 0)->text() == "48/170"
              && rules->item(4, 3)->text().toDouble() == 30
              && rules->item(4, 4)->text().toDouble() == 3,
              "Interpolate the matched grade offsets");
        QTimer::singleShot(0, [&dialog] {
            auto* nested = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(nested && nested != &dialog, "Open the size and height grid dialog");
            auto corners = nested->findChildren<QComboBox*>();
            check(corners.size() == 4, "Use four reference corners");
            corners[0]->setCurrentText("L");
            corners[1]->setCurrentText("52/164");
            corners[2]->setCurrentText("48/176");
            corners[3]->setCurrentText("52/176");
            auto shares = nested->findChildren<QDoubleSpinBox*>();
            shares[0]->setValue(25);
            shares[1]->setValue(75);
            nested->findChild<QLineEdit*>()->setText("49/173");
            nested->findChild<QCheckBox*>()->setChecked(true);
            nested->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        click(QStringLiteral("Добавить по размеру и росту…"));
        check(measurements->rowCount() == 12 && rules->rowCount() == 6,
              "Add the new measurements and point offsets from four corners");

        QTimer::singleShot(0, [] {
            auto* group = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(group && group->findChild<QPlainTextEdit*>(), "Open the batch size and height dialog");
            auto corners = group->findChildren<QComboBox*>();
            corners[0]->setCurrentText("L");
            corners[1]->setCurrentText("52/164");
            corners[2]->setCurrentText("48/176");
            corners[3]->setCurrentText("52/176");
            auto* targets = group->findChild<QPlainTextEdit*>();
            for (auto* button : group->findChildren<QPushButton*>()) {
                if (button->text() == QStringLiteral("Заполнить размерно-ростовой ряд"))
                    button->click();
            }
            const auto generated = targets->toPlainText().split(u'\n');
            check(generated.size() == 21 && generated.contains(QStringLiteral("48/172\t50\t50"))
                  && !generated.contains(QStringLiteral("44/164\t0\t0")),
                  "Generate an evenly stepped production range without duplicating the four references");
            group->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("50/170\t50\t50\n51/174\t75\t75"));
            group->findChild<QCheckBox*>()->setChecked(true);
            group->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        click(QStringLiteral("Группа размеров и ростов…"));
        check(measurements->rowCount() == 16 && rules->rowCount() == 8
              && measurements->item(12, 2)->text().toDouble() == 1060
              && measurements->item(15, 2)->text().toDouble() == 633,
              "Interpolate two new size groups and their point offsets in one operation");

        QApplication::clipboard()->setText(QStringLiteral("L\tWaist\t780\n"));
        click(QStringLiteral("Добавить мерки из буфера"));
        check(measurements->rowCount() == 17 && measurements->item(16, 1)->text() == "Waist"
              && rules->rowCount() == 8, "Append measurements and retain existing sizes and grade rules");
        QApplication::clipboard()->setText(QStringLiteral("L\tHip\t1100\nL\tChest\t999\n"));
        QTimer::singleShot(0, [] {
            auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            check(warning, "Warn about conflicting measurement imports");
            warning->button(QMessageBox::Ok)->click();
        });
        click(QStringLiteral("Добавить мерки из буфера"));
        check(measurements->rowCount() == 17 && measurements->item(0, 2)->text() == "1000",
              "Reject the whole clipboard import when a measurement conflicts");

        QTemporaryDir exchange;
        check(exchange.isValid(), "Create a directory for the reusable size table");
        const auto chooseFile = [](const QString& path) {
            QTimer::singleShot(0, [path] {
                auto* fileDialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                check(fileDialog, "Open the TSV file dialog");
                fileDialog->selectFile(path);
                static_cast<QDialog*>(fileDialog)->accept();
            });
        };
        const auto outputPath = exchange.filePath("export.tsv");
        chooseFile(outputPath);
        click(QStringLiteral("Сохранить мерки в TSV…"));
        QFile outputFile(outputPath);
        check(outputFile.open(QIODevice::ReadOnly) && outputFile.readAll().contains("L\tWaist\t780"),
              "Write the size table to a reusable TSV file");
        const auto inputPath = exchange.filePath("import.tsv");
        QFile inputFile(inputPath);
        const auto rows = QStringLiteral("Size\tMeasurement\tValue (mm)\r\nL\tОбхват бёдер\t980\r\n").toUtf8();
        check(inputFile.open(QIODevice::WriteOnly) && inputFile.write(rows) == rows.size(),
              "Write a UTF-8 table with a Russian measurement name");
        inputFile.close();
        const auto compare = [&](const QString& path, bool match) {
            QTimer::singleShot(0, [&, path, match] {
                auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                check(picker, "Open the reference measurement TSV");
                QTimer::singleShot(0, [match] {
                    auto* result = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    auto* details = result ? result->findChild<QPlainTextEdit*>("measurementComparisonDetails") : nullptr;
                    check(details && (match ? details->toPlainText().isEmpty()
                          : details->toPlainText().contains(QStringLiteral("L / Обхват бёдер"))
                            && details->toPlainText().contains(QStringLiteral("L / Waist"))),
                          "Compare reference values and report absent and extra measurements");
                    result->reject();
                });
                picker->selectFile(path);
                static_cast<QDialog*>(picker)->accept();
            });
            click(QStringLiteral("Сверить с TSV…"));
            check(measurements->rowCount() == 17, "Comparison must leave the project measurements intact");
        };
        compare(outputPath, true);
        compare(inputPath, false);
        const auto libraryKey = QStringLiteral("grading/measurementLibraryDirectory");
        QSettings().setValue(libraryKey, exchange.path());
        QTimer::singleShot(0, [&] {
            auto* catalog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(catalog && catalog->windowTitle() == QStringLiteral("Библиотека мерок…"),
                  "Open the reusable measurement library");
            auto* load = [&]() -> QPushButton* {
                for (auto* button : catalog->findChildren<QPushButton*>()) {
                    if (button->text() == QStringLiteral("Добавить систему в проект…"))
                        return button;
                }
                return nullptr;
            }();
            check(load && load->isEnabled(), "Restore the selected library folder");
            chooseFile(inputPath);
            load->click();
            catalog->reject();
        });
        click(QStringLiteral("Библиотека мерок…"));
        QSettings().remove(libraryKey);
        check(measurements->rowCount() == 18 && measurements->item(17, 1)->text() == QStringLiteral("Обхват бёдер")
              && rules->rowCount() == 8, "Read a TSV file and preserve existing grade rules");

        QTimer::singleShot(0, [] {
            auto* group = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(group && group->findChild<QPlainTextEdit*>(), "Open the batch size and height dialog");
            auto corners = group->findChildren<QComboBox*>();
            check(corners.size() == 4, "Select all four corners for the batch");
            corners[0]->setCurrentText("L");
            corners[1]->setCurrentText("52/164");
            corners[2]->setCurrentText("48/176");
            corners[3]->setCurrentText("52/176");
            group->findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("53/170\t50\t50\n54/174\t75\t75"));
            group->findChild<QCheckBox*>()->setChecked(true);
            // The first row now has two additional measurements and is no longer a compatible corner.
            QTimer::singleShot(0, [] {
                auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                check(warning, "Reject differing reference measurement sets");
                warning->button(QMessageBox::Ok)->click();
            });
            group->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            group->reject();
        });
        click(QStringLiteral("Группа размеров и ростов…"));
        check(measurements->rowCount() == 18 && rules->rowCount() == 8, "Reject the entire incompatible batch");

        const auto addStandardFigure = [&](int standard, const QString& code, const QVector<int>& expected,
                                          const QString& group, const QString& height, const QString& bust,
                                          const QString& unavailable, bool addVerified = false, bool addNumbered = false) {
            QTimer::singleShot(0, [=] {
                auto* reference = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(reference, "Open the size standard");
                auto fields = reference->findChildren<QComboBox*>();
                auto* type = reference->findChild<QComboBox*>("figureStandard");
                check(type && fields.removeOne(type) && fields.size() == 3 && type->count() == 6,
                      "Find the standard and its figure fields");
                type->setCurrentIndex(standard);
                check(reference->windowTitle().contains(code) && fields[0]->count() == expected.size(),
                      "Find the published fullness groups");
                for (int groupIndex = 0; groupIndex < fields[0]->count(); ++groupIndex) {
                    fields[0]->setCurrentIndex(groupIndex);
                    int figures = 0;
                    for (int heightIndex = 0; heightIndex < fields[1]->count(); ++heightIndex) {
                        fields[1]->setCurrentIndex(heightIndex);
                        figures += fields[2]->count();
                    }
                    check(figures == expected[groupIndex], "Match the printed fullness group");
                }
                fields[0]->setCurrentText(group);
                fields[1]->setCurrentText(height);
                check(fields[2]->findText(bust) >= 0 && fields[2]->findText(unavailable) < 0,
                      "Offer listed combinations and exclude unlisted ones");
                fields[2]->setCurrentText(bust);
                if (addVerified) {
                    auto* extra = reference->findChild<QCheckBox*>("verifiedAdditionalMeasures");
                    check(extra && extra->isEnabled(), "Offer the cross-checked measures for the selected figure");
                    extra->setChecked(true);
                }
                if (addNumbered) {
                    auto* extra = reference->findChild<QCheckBox*>("verifiedNumberedTraits");
                    check(extra && extra->isEnabled(), "Offer the additional numbered measurements");
                    extra->setChecked(true);
                }
                reference->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            });
            const int rowsBefore = measurements->rowCount();
            click(QStringLiteral("Типовая фигура ГОСТ…"));
            check(measurements->rowCount() == rowsBefore + (addVerified ? 6 : 3) + (addNumbered ? 82 : 0) && rules->rowCount() == 8,
                  "Append the selected leading and optional measurements from the standard");
        };
        addStandardFigure(0, "31396-2009", {46, 65, 76, 75, 61, 33}, "0", "164", "92", "128");
        addStandardFigure(1, "31399-2009", {48, 75, 80, 51, 47}, "2", "200", "108", "112");
        addStandardFigure(4, "17916-86", {3, 22, 33, 66, 89, 89, 77, 52, 15}, "VI", "86", "48", "52", true);
        addStandardFigure(5, "17917-86", {15, 52, 80, 98, 95, 70, 39, 10}, "IV", "86", "48", "52");
        addStandardFigure(5, "17917-86", {15, 52, 80, 98, 95, 70, 39, 10}, "IV", "146", "72", "108", true, true);
        addStandardFigure(2, "31397-2009", {11, 15, 9}, "2", "164", "156", "160");
        addStandardFigure(3, "31400-2009", {7, 15, 6}, "1", "188", "136", "140");

        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    auto* overlayMenu = action(window, "Size Overlays").menu();
    overlayMenu->aboutToShow();
    overlayMenu->actions().front()->trigger();
    check(overlayCount(view) == 2, "Display a graded size over the base pattern");

    runDialog<QDialog>(action(window, "Fabric Layout…"), [](QDialog& dialog) {
        dialog.findChild<QDoubleSpinBox*>()->setValue(1450);
        dialog.findChild<QDoubleSpinBox*>("markerPieceClearance")->setValue(5.5);
        dialog.findChild<QDoubleSpinBox*>("markerCrossRepeat")->setValue(20);
        dialog.findChild<QDoubleSpinBox*>("markerLengthRepeat")->setValue(40);
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
    check(loaded.load(pattern) && loaded.production().sizes.size() == 15 && loaded.production().rules.size() == 8
          && loaded.production().rules[4].offset == QPointF(30, 3)
          && loaded.production().rules[5].size == "49/173"
          && loaded.production().rules[5].offset == QPointF(50, 10)
          && loaded.production().rules.back().offset == QPointF(70, 12)
          && loaded.production().sizes.front().measurements.value("Waist") == 780
          && loaded.production().sizes.front().measurements.value(QStringLiteral("Обхват бёдер")) == 980
          && loaded.production().sizes[5].name == "49/173"
          && loaded.production().sizes[7].name == "51/174"
          && loaded.production().sizes[8].name == QStringLiteral("ГОСТ 31396-2009 164-92-90")
          && loaded.production().sizes[8].source == QStringLiteral("ГОСТ 31396-2009, таблица 3, полнотная группа 0")
          && loaded.production().sizes[8].measurements.value("height") == 1640
          && loaded.production().sizes[8].measurements.value("bust") == 920
          && loaded.production().sizes[8].measurements.value("hip") == 900
          && loaded.production().sizes[9].name == QStringLiteral("ГОСТ 31399-2009 200-108-92")
          && loaded.production().sizes[9].source == QStringLiteral("ГОСТ 31399-2009, таблица 3, полнотная группа 2")
          && loaded.production().sizes[9].measurements.value("height") == 2000
          && loaded.production().sizes[9].measurements.value("bust") == 1080
          && loaded.production().sizes[9].measurements.value("waist") == 920
          && loaded.production().sizes[10].name == QStringLiteral("ГОСТ 17916-86 86-48-45")
          && loaded.production().sizes[10].source == QStringLiteral("ГОСТ 17916-86, таблица 1, изменения № 1, 2, полнотный ряд VI; таблица 5, сверенные признаки 2, 4, 7")
          && loaded.production().sizes[10].measurements.value("waist") == 450
          && loaded.production().sizes[10].measurements.value("sternal_height") == 656
          && loaded.production().sizes[10].measurements.value("side_neck_height") == 682
          && loaded.production().sizes[10].measurements.value("waist_height") == 482
          && loaded.production().sizes[11].name == QStringLiteral("ГОСТ 17917-86 86-48-45")
          && loaded.production().sizes[11].source == QStringLiteral("ГОСТ 17917-86, таблица 1, изменения № 1, 2, полнотный ряд IV")
          && loaded.production().sizes[11].measurements.value("waist") == 450
          && !loaded.production().sizes[11].measurements.contains("waist_height")
          && loaded.production().sizes[12].name == QStringLiteral("ГОСТ 17917-86 146-72-63")
          && loaded.production().sizes[12].measurements.value("sternal_height") == 1182
          && loaded.production().sizes[12].measurements.value("side_neck_height") == 1232
          && loaded.production().sizes[12].measurements.value("waist_height") == 924
          && loaded.production().sizes[12].measurements.value("gost_5") == 1180
          && loaded.production().sizes[12].measurements.value("gost_13") == 320
          && loaded.production().sizes[13].name == QStringLiteral("ГОСТ 31397-2009 164-156-162")
          && loaded.production().sizes[13].measurements.value("hip") == 1620
          && loaded.production().sizes[14].name == QStringLiteral("ГОСТ 31400-2009 188-136-126")
          && loaded.production().sizes[14].measurements.value("waist") == 1260
          && loaded.production().sizes[5].measurements.value("Chest") == 1050
          && loaded.production().sizes[5].measurements.value("Sleeve") == 631
          && loaded.production().placements.size() == 1 && loaded.production().placements.front().size == "L"
          && loaded.production().fabricWidth == 1450 && loaded.production().pieceClearanceMm == 5.5
          && loaded.production().fabricRepeatMm == QPointF(20, 40)
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
    const auto exportSize = [&](QAction& command, const QString& path, bool dxf = false) {
        const auto chooseSize = [&path](QDialog& dialog) {
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
        };
        if (!dxf) {
            runDialog<QDialog>(command, chooseSize);
            return;
        }
        runDialog<QMessageBox>(command, [&](QMessageBox& warning) {
            check(warning.text().contains("modeling history"), "Explain DXF exchange limitations");
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(dialog, "Choose the DXF export size");
                chooseSize(*dialog);
            });
            warning.button(QMessageBox::Ok)->click();
        });
    };
    exportSize(action(window, "Export &DXF…"), gradedDxfPath, true);
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

    draftTree.topLevelItem(3)->setSelected(true);
    action(draftWindow, "Move Selection to Construction Layer").trigger();
    const auto canvasClick = [&](QPointF point, Qt::KeyboardModifiers modifiers) {
        const auto local = draftView.mapFromScene(point);
        QMouseEvent click(QEvent::MouseButtonPress, local, draftView.viewport()->mapToGlobal(local),
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(draftView.viewport(), &click);
    };
    canvasClick({65, 0}, Qt::NoModifier);
    canvasClick({130, 0}, Qt::ControlModifier);
    check(draftTree.selectedItems().size() == 2 && draftView.selectedIndices().size() == 2,
          "Ctrl-click adds a construction point to the selected line on canvas and in the Objects dock");
    canvasClick({130, 0}, Qt::ControlModifier);
    check(draftTree.selectedItems().size() == 1 && draftView.selectedIndices().size() == 1,
          "Ctrl-click deselects only the targeted point");
    draftTree.topLevelItem(2)->setSelected(true);
    check(draftView.selectedIndices().size() == 2, "Multiple Objects dock selections highlight on the canvas");

    runDialog<QDialog>(action(draftWindow, "Rotate Construction…"), [](QDialog& dialog) {
        auto* pivot = dialog.findChild<QComboBox*>("rotationPivot");
        check(pivot && pivot->count() == 1 && pivot->currentText() == "A",
              "Offer the unselected named point as the pivot");
        dialog.findChild<QDoubleSpinBox*>()->setValue(90);
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto verifyRotation = [&](double angle, QPointF endpoint) {
        action(draftWindow, "&Save").trigger();
        check(loaded.load(draftFile) && loaded.entities().last().b == endpoint,
              "Save and reload rotated line geometry");
        check(angle == 0 ? loaded.modelingHistory().isEmpty()
                         : loaded.modelingHistory().size() == 1 && loaded.modelingHistory()[0].rotationDegrees == angle,
              "Save and reload rotation history in the same undo action");
    };
    verifyRotation(90, {0, 130});
    action(draftWindow, "Undo Rotate Construction").trigger();
    verifyRotation(0, {130, 0});
    action(draftWindow, "Redo Rotate Construction").trigger();
    verifyRotation(90, {0, 130});
    const auto draftDocks = draftWindow.findChildren<QDockWidget*>();
    const auto historyDock = std::ranges::find_if(draftDocks, [](const QDockWidget* dock) {
        return dock->windowTitle() == "Construction Tree";
    });
    check(historyDock != draftDocks.end(), "Rotation opens the construction tree");
    auto* rotationTree = (*historyDock)->findChild<QTreeWidget*>();
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->windowTitle() == "Edit Rotate Construction", "Edit rotation from history");
        dialog->findChild<QDoubleSpinBox*>()->setValue(-90);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    rotationTree->itemActivated(rotationTree->topLevelItem(0)->child(0), 0);
    verifyRotation(-90, {0, -130});
    action(draftWindow, "Undo Edit Modeling Step").trigger();
    verifyRotation(90, {0, 130});
    action(draftWindow, "Redo Edit Modeling Step").trigger();
    verifyRotation(-90, {0, -130});

    const auto sequenceProject = temp.filePath("modeling-sequence.sofit");
    check(loaded.save(sequenceProject), "Save a modeling sequence as a reusable project");
    action(draftWindow, "Undo Edit Modeling Step").trigger();
    action(draftWindow, "Undo Rotate Construction").trigger();
    verifyRotation(0, {130, 0});
    chooseFile(action(draftWindow, QStringLiteral("Применить операции из проекта…")), sequenceProject);
    verifyRotation(-90, {0, -130});
    action(draftWindow, QStringLiteral("Undo Применение последовательности операций")).trigger();
    verifyRotation(0, {130, 0});
    action(draftWindow, QStringLiteral("Redo Применение последовательности операций")).trigger();
    verifyRotation(-90, {0, -130});

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
    runDialog<QDialog>(action(bodiceWindow, QStringLiteral("Part Assistant…")), [](QDialog& dialog) {
        auto* report = dialog.findChild<QTreeWidget*>();
        check(report && report->topLevelItemCount() == 1, "Open the Part Assistant with base-piece diagnostics");
        int productionActions = 0;
        QPushButton* edgeAllowance = nullptr;
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Edge Allowance…")
                edgeAllowance = button;
            if (button->text() == "Finished Measurements…" || button->text() == "Tech Pack Options…"
                || button->text() == "Export Product Information PDF…")
                ++productionActions;
        }
        check(productionActions == 3, "Give access to measurement and tech-pack commands from Part Assistant");
        auto* piece = report->topLevelItem(0)->child(0);
        for (int i = 0; i < piece->childCount(); ++i) {
            auto* row = piece->child(i);
            if (row->text(1).startsWith("Length") && row->text(1).contains(" · Allowance ")) {
                report->setCurrentItem(row);
                break;
            }
        }
        check(edgeAllowance && edgeAllowance->isEnabled(), "Select a seam edge in Part Assistant");
        QTimer::singleShot(0, [] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(editor && editor->windowTitle() == "Allowance for Selected Edges",
                  "Edit the selected seam edge allowance from Part Assistant");
            editor->reject();
        });
        edgeAllowance->click();
        QPushButton* setup = nullptr;
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == "Prepare for Cutting…")
                setup = button;
        }
        report->setCurrentItem(report->topLevelItem(0)->child(0));
        check(setup && setup->isEnabled(), "Prepare the selected piece through Part Assistant");
        QTimer::singleShot(0, [] {
            auto* wizard = qobject_cast<QWizard*>(QApplication::activeModalWidget());
            check(wizard && wizard->currentPage()->title() == "Seam allowance", "Start with allowance settings");
            const auto cornerChoices = wizard->currentPage()->findChildren<QComboBox*>("cuttingCornerTreatment");
            check(cornerChoices.size() > 1, "List independently configurable corners");
            cornerChoices.front()->setCurrentIndex(2);
            wizard->next();
            check(wizard->currentPage()->title() == "Edge markings", "Offer a notch anchored to a seam edge");
            auto* edges = wizard->currentPage()->findChild<QComboBox*>();
            check(edges && edges->count() > 1, "List eligible edges");
            edges->setCurrentIndex(1);
            QTimer::singleShot(0, [] {
                if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    warning->accept();
            });
            wizard->button(QWizard::FinishButton)->click();
            check(wizard->result() == QDialog::Accepted, "Validate and apply the two steps together");
        });
        setup->click();
        QPushButton* drill = nullptr;
        for (auto* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Точка сверления…"))
                drill = button;
        }
        report->setCurrentItem(report->topLevelItem(0)->child(0));
        check(drill && drill->isEnabled(), "Offer drilling for a selected piece");
        QTimer::singleShot(0, [] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(editor && editor->findChildren<QDoubleSpinBox*>().size() == 2,
                  "Open coordinate entry for the drill mark");
            editor->reject();
        });
        drill->click();
        dialog.reject();
    });
    runDialog<QDialog>(action(bodiceWindow, "Diagnose Pieces by Size…"), [](QDialog& dialog) {
        auto* report = dialog.findChild<QTreeWidget*>();
        check(report && report->topLevelItemCount() == 1 && report->topLevelItem(0)->childCount() == 3
              && report->topLevelItem(0)->child(0)->text(1) == "One closed contour",
              "Diagnose the modeled pieces");
        report->setCurrentItem(report->topLevelItem(0)->child(0)->child(0));
        auto* editPiece = dialog.findChild<QDialogButtonBox*>()->buttons().last();
        for (auto* button : dialog.findChild<QDialogButtonBox*>()->buttons()) {
            if (button->text() == QStringLiteral("Свойства детали…"))
                editPiece = button;
        }
        check(editPiece->text() == QStringLiteral("Свойства детали…") && editPiece->isEnabled(),
              "Edit the base piece directly from a selected diagnostic row");
        QTimer::singleShot(0, [] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(editor && editor->windowTitle() == "Piece, Contour and Allowance", "Open the piece editor");
            editor->findChild<QSpinBox*>()->setValue(3);
            editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        editPiece->click();
        check(report->topLevelItem(0)->child(0)->child(0)->text(1) == "3 per kit",
              "Refresh the report after editing the cut quantity");
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
    {
        auto drape = yoke;
        auto draft = yokeDraft;
        draft.points.append({"G", "0", "60"});
        draft.points.append({"H", "width", "60"});
        draft.edges.append({"secondGuide", "G", "H", {}});
        check(drape.setParametricDraft(draft), "Create a second formula slash");
        QVector<qsizetype> guideIndices;
        for (qsizetype i = 0; i < drape.entities().size(); ++i) {
            auto guide = drape.entities()[i];
            if (guide.draftName != "guide" && guide.draftName != "secondGuide")
                continue;
            guide.layer = opc::Layer::Construction;
            drape.replace(i, guide);
            guideIndices.append(i);
        }
        check(guideIndices.size() == 2, "Find both formula slash guides");
        const auto path = temp.filePath("drape.sofit");
        check(drape.save(path), "Save the two-guide fixture");
        MainWindow editor;
        editor.show();
        chooseFile(action(editor, "&Open"), path);
        auto* guideTree = editor.findChild<QTreeWidget*>();
        for (auto index : guideIndices)
            guideTree->topLevelItem(index)->setSelected(true);
        const auto command = QStringLiteral("Раздвижка по нескольким направляющим");
        runDialog<QDialog>(action(editor, command + QStringLiteral("…")), [](QDialog& dialog) {
            auto* accept = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            const auto combos = dialog.findChildren<QComboBox*>();
            combos[0]->setCurrentText("guide");
            combos[2]->setCurrentText("secondGuide");
            accept->click();
            if (dialog.result() != QDialog::Accepted) {
                combos[0]->setCurrentText("secondGuide");
                combos[2]->setCurrentText("guide");
                accept->click();
            }
            check(dialog.result() == QDialog::Accepted, "Apply both slashes in a valid order");
        });
        const auto verifyHistory = [&](qsizetype count) {
            action(editor, "&Save").trigger();
            check(drape.load(path) && drape.modelingHistory().size() == count,
                  "Apply, undo and redo the entire drape sequence in one step");
        };
        verifyHistory(2);
        action(editor, QStringLiteral("Undo ") + command).trigger();
        verifyHistory(0);
        action(editor, QStringLiteral("Redo ") + command).trigger();
        verifyHistory(2);
    }
    const auto yokePath = temp.filePath("yoke.sofit");
    check(yoke.save(yokePath), "Save the formula yoke fixture");
    MainWindow yokeWindow;
    yokeWindow.show();
    chooseFile(action(yokeWindow, "&Open"), yokePath);
    yokeWindow.findChild<QTreeWidget*>()->topLevelItem(yoke.entities().size() - 1)->setSelected(true);
    action(yokeWindow, "Yoke…").trigger();
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
    action(yokeWindow, "Undo Yoke").trigger();
    action(yokeWindow, "&Save").trigger();
    check(yoke.load(yokePath) && yoke.pieces().size() == 1 && !yoke.hasContourHistory(),
          "Undo restores the formula guide and original piece");
    action(yokeWindow, "Redo Yoke").trigger();
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

    check(argc == 2, "DXF fixture directory is required");
    MainWindow exchangeWindow;
    exchangeWindow.show();
    auto& importDxf = action(exchangeWindow, "Import DXF…");
    const auto fixture = QString::fromLocal8Bit(argv[1]) + "/exchange-inches.dxf";
    const auto openImport = [&] {
        runDialog<QFileDialog>(importDxf, [&](QFileDialog& fileDialog) {
            fileDialog.selectFile(fixture);
            QTimer::singleShot(0, [&] {
                auto* dialog = QApplication::activeModalWidget();
                check(dialog && dialog->objectName() == "dxfImportDialog", "Show the DXF preview");
                check(dialog->findChild<QDoubleSpinBox*>("dxfUnits")->value() == 25.4,
                      "DXF preview proposes the declared inches conversion");
                auto* layers = dialog->findChild<QListWidget*>("dxfLayers");
                for (int index = 0; index < layers->count(); ++index) {
                    auto* item = layers->item(index);
                    item->setCheckState(item->data(Qt::UserRole) == "OUTLINE" ? Qt::Checked : Qt::Unchecked);
                }
                auto* accept = dialog->findChild<QPushButton*>("confirmDxfImport");
                check(accept && accept->isEnabled(), "A filtered DXF layer can be imported");
                accept->click();
            });
            static_cast<QDialog&>(fileDialog).accept();
        });
    };
    auto* exchangeTree = exchangeWindow.findChild<QTreeWidget*>("drawingObjects");
    if (!exchangeTree)
        exchangeTree = exchangeWindow.findChild<QTreeWidget*>();
    openImport();
    check(exchangeTree->topLevelItemCount() == 4, "Import one editable piece through File menu");
    action(exchangeWindow, "Undo Import DXF").trigger();
    check(exchangeTree->topLevelItemCount() == 0, "Undo DXF import as one transaction");
    action(exchangeWindow, "Redo Import DXF").trigger();
    check(exchangeTree->topLevelItemCount() == 4, "Redo restores all imported edges");

    const auto exchangePath = temp.filePath("exchange.sofit");
    chooseFile(action(exchangeWindow, "&Save"), exchangePath);
    opc::PatternDocument exchange;
    check(exchange.load(exchangePath) && opc::analyzePiece(exchange, 0).error == opc::ContourError::None,
          "Save and reopen the imported piece");
    const auto exchangePdf = temp.filePath("exchange.pdf");
    QString exchangeError;
    check(opc::exportPatternPdf(exchangePdf, exchange, QVector<bool>(4, true), {{}}, {}, exchangeError) &&
          pageCount(readPdf(exchangePdf)) > 0, "Export imported piece as PDF");
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

    auto& templateView = *templateWindow.findChild<PatternView*>();
    const auto handle = std::ranges::find_if(loaded.entities(), [](const auto& entity) {
        return entity.shape == opc::Shape::Point && entity.draftName == "TopL";
    });
    check(handle != loaded.entities().end(), "Find a visible template construction handle");
    const auto handleIndex = handle - loaded.entities().begin();
    const auto originalHandle = handle->a;
    const auto target = templateView.mapFromScene(originalHandle + QPointF(-35, -20));
    const auto start = templateView.mapFromScene(originalHandle);
    const auto mouse = [&](QEvent::Type type, QPoint position) {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, position, templateView.viewport()->mapToGlobal(position), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(templateView.viewport(), &event);
    };
    mouse(QEvent::MouseButtonPress, start);
    mouse(QEvent::MouseMove, target);
    mouse(QEvent::MouseButtonRelease, target);
    const auto libraryPath = temp.filePath("construction.sofit");
    chooseFile(action(templateWindow, "Save Construction to Library…"), libraryPath);
    opc::PatternDocument library;
    check(library.load(libraryPath) && library.entities()[handleIndex].a != originalHandle,
          "Drag a draft handle and save the result to the library");
    action(templateWindow, "Undo Move Draft Point").trigger();
    action(templateWindow, "&Save").trigger();
    check(loaded.load(skirtPath) && loaded.entities()[handleIndex].a == originalHandle, "Undo the handle move");
    action(templateWindow, "Redo Move Draft Point").trigger();
    action(templateWindow, "&Save").trigger();
    check(loaded.load(skirtPath) && loaded.entities()[handleIndex].a == library.entities()[handleIndex].a,
          "Redo the handle move");
    MainWindow fromLibrary;
    fromLibrary.show();
    runDialog<QFileDialog>(action(fromLibrary, "Preview Library Construction…"), [&](QFileDialog& file) {
        file.selectFile(libraryPath);
        QTimer::singleShot(0, &fromLibrary, [] {
            auto* preview = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(preview && preview->windowTitle() == "Preview Library Construction", "Open the library preview");
            auto* sizes = preview->findChild<QComboBox*>();
            const auto* view = preview->findChild<QGraphicsView*>();
            check(sizes && sizes->count() == 2 && view && view->scene(), "Offer the base pattern and saved size");
            sizes->setCurrentIndex(1);
            const auto gradedWidth = view->scene()->itemsBoundingRect().width();
            sizes->setCurrentIndex(0);
            const auto baseWidth = view->scene()->itemsBoundingRect().width();
            check(baseWidth > 0 && gradedWidth > baseWidth, "Draw a different outline in the graded library preview");
            preview->reject();
        });
        static_cast<QDialog&>(file).accept();
    });
    chooseFile(action(fromLibrary, "New Pattern from Library…"), libraryPath);
    const auto copied = temp.filePath("library-instance.sofit");
    chooseFile(action(fromLibrary, "&Save"), copied);
    check(loaded.load(copied) && loaded.parametricDraft() == library.parametricDraft()
          && opc::saveProduction(loaded.production()) == opc::saveProduction(library.production()),
          "Start an independent editable pattern with the library formulas and sizes");
    chooseFile(action(fromLibrary, "Insert Construction into Current Pattern…"), libraryPath);
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.pieces().size() == 2 * library.pieces().size(),
          "Insert the library construction in the current project");
    action(fromLibrary, "Undo Insert Library Construction").trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.parametricDraft() == library.parametricDraft(),
          "Undo the complete insertion as one action");
    action(fromLibrary, "Redo Insert Library Construction").trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.pieces().size() == 2 * library.pieces().size(),
          "Restore the inserted pieces through Redo");

    runDialog<QFileDialog>(action(fromLibrary, QStringLiteral("Вставить конструкцию с привязкой мерок…")),
        [&](QFileDialog& file) {
            file.selectFile(libraryPath);
            QTimer::singleShot(0, [] {
                auto* binding = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(binding && binding->findChild<QCheckBox*>(), "Offer revision tracking with explicit measurement binding");
                binding->findChild<QCheckBox*>()->setChecked(true);
                binding->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            });
            static_cast<QDialog&>(file).accept();
        });
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().size() == 1, "Save the tracked library instance");
    const auto previousRevision = loaded.libraryInstances().front().revision;
    auto revised = library;
    QString updateError;
    check(revised.moveDraftPoint("TopL", {-1, 0}, &updateError), "Change a source formula in the next revision");
    const auto revisionPath = temp.filePath("construction-next.sofit");
    check(revised.save(revisionPath), "Save a new library revision");
    runDialog<QInputDialog>(action(fromLibrary, QStringLiteral("Обновить экземпляр конструкции…")),
        [&](QInputDialog& selector) {
            QTimer::singleShot(0, [&] {
                auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                check(file, "Choose a library file for the selected instance");
                file->selectFile(revisionPath);
                static_cast<QDialog*>(file)->accept();
            });
            selector.accept();
        });
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().revision != previousRevision,
          "Refresh the selected library instance from disk");
    const auto newRevision = loaded.libraryInstances().front().revision;
    const auto updateLabel = QStringLiteral("Обновление экземпляра конструкции");
    action(fromLibrary, QStringLiteral("Undo ") + updateLabel).trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().revision == previousRevision,
          "Undo the library revision together with its formula changes");
    action(fromLibrary, QStringLiteral("Redo ") + updateLabel).trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().revision == newRevision,
          "Redo the library revision as one change");

    check(revised.save(libraryPath), "Align the tracked source with the installed revision");
    const auto autoLabel = QStringLiteral("Автоматически обновлять связанные конструкции");
    auto nextDraft = revised.parametricDraft();
    const auto oldName = nextDraft.edges.front().name;
    const auto newName = QStringLiteral("renamed_library_outline");
    const auto stableId = loaded.libraryInstances().front().edges.value(oldName);
    const auto edge = std::ranges::find(revised.entities(), oldName, &opc::Entity::draftName);
    check(edge != revised.entities().end(), "Locate the renamed source edge");
    auto revisedEdge = *edge;
    revisedEdge.draftName = newName;
    revised.replace(edge - revised.entities().begin(), revisedEdge);
    nextDraft.edges.front().name = newName;
    const QRegularExpression oldToken(QStringLiteral("\\b") + QRegularExpression::escape(oldName) + QStringLiteral("\\b"));
    for (auto& parameter : nextDraft.parameters)
        parameter.formula.replace(oldToken, newName);
    for (auto& point : nextDraft.points) {
        point.x.replace(oldToken, newName);
        point.y.replace(oldToken, newName);
    }
    check(revised.setParametricDraft(nextDraft, &updateError), qPrintable(updateError));
    action(fromLibrary, autoLabel).trigger();
    check(revised.save(libraryPath),
          "Replace the tracked source atomically while synchronization is enabled");
    QEventLoop synchronization;
    auto& undoSync = action(fromLibrary, QStringLiteral("Undo ") + updateLabel);
    QObject::connect(&undoSync, &QAction::changed, &synchronization, [&] {
        if (undoSync.text() == QStringLiteral("Undo ") + autoLabel)
            synchronization.quit();
    });
    QTimer::singleShot(4000, &synchronization, &QEventLoop::quit);
    synchronization.exec();
    check(undoSync.text() == QStringLiteral("Undo ") + autoLabel,
          "Synchronize a replacement source as one undoable change");
    action(fromLibrary, autoLabel).trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().edges.value(newName) == stableId,
          "Save the automatically migrated edge with its original receiving identifier");
    undoSync.trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().edges.value(oldName) == stableId,
          "Undo the renamed source object together with its saved binding");
    action(fromLibrary, QStringLiteral("Redo ") + autoLabel).trigger();
    action(fromLibrary, "&Save").trigger();
    check(loaded.load(copied) && loaded.libraryInstances().front().edges.value(newName) == stableId,
          "Redo and reopen the topology synchronization with the same object identifier");

    opc::PatternDocument groupGrade;
    groupGrade.pieces().append({"Panel", 5});
    groupGrade.insert(0, {opc::Shape::Curve, {0, 0}, {100, 0}, {50, -20}, 0, opc::Layer::Main});
    groupGrade.insert(1, {opc::Shape::Line, {100, 0}, {100, 100}, {}, 0, opc::Layer::Main});
    groupGrade.insert(2, {opc::Shape::Line, {100, 100}, {0, 100}, {}, 0, opc::Layer::Main});
    groupGrade.insert(3, {opc::Shape::Line, {0, 100}, {0, 0}, {}, 0, opc::Layer::Main});
    groupGrade.production().sizes = {{"Curve"}, {"Piece"}};
    QTimer::singleShot(0, [] {
        auto* grading = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(grading, "Open grading editor for group offsets");
        auto* command = [&]() -> QPushButton* {
            for (auto* button : grading->findChildren<QPushButton*>())
                if (button->text() == QStringLiteral("Offset curve or piece…"))
                    return button;
            return nullptr;
        }();
        check(command, "Find the curve and piece grading command");
        for (int i = 0; i < 2; ++i) {
            QTimer::singleShot(0, [i] {
                auto* group = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                check(group, "Open group offsets");
                const auto selectors = group->findChildren<QComboBox*>();
                check(selectors.size() == 3, "Find size, piece and target selectors");
                selectors[0]->setCurrentIndex(i);
                selectors[2]->setCurrentIndex(1 - i);
                group->findChildren<QDoubleSpinBox*>()[1]->setValue(10);
                group->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            });
            command->click();
        }
        grading->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    const auto groupRules = editGrading(nullptr, groupGrade);
    check(groupRules && groupRules->rules.size() == 8, "Generate rules for three curve points and five piece points");
    groupGrade.production() = *groupRules;
    const auto groupPath = temp.filePath("group-grade.sofit");
    check(groupGrade.save(groupPath) && loaded.load(groupPath), "Reopen saved curve and piece grade rules");
    const auto curveGrade = opc::gradedPattern(loaded, "Curve");
    const auto pieceGrade = opc::gradedPattern(loaded, "Piece");
    check(curveGrade.entities()[0].control == QPointF(50, -10)
          && curveGrade.entities()[0].b == curveGrade.entities()[1].a
          && curveGrade.entities()[2].a == QPointF(100, 100),
          "Move shared endpoints and the curve control without shifting unrelated points");
    for (const auto* result : {&curveGrade, &pieceGrade}) {
        const auto geometry = opc::analyzePiece(*result, 0);
        check(geometry.error == opc::ContourError::None && !geometry.allowanceFailed
              && !geometry.allowance.isEmpty(), "Retain a cutting allowance after group grading");
    }
    check(pieceGrade.entities()[2].a == QPointF(100, 110), "Translate the complete piece");

}
