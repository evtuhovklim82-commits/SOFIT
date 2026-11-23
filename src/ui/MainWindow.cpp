#include "MainWindow.h"
#include "PatternView.h"
#include "ParametricDialog.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "PieceMarkings.h"
#include "Production.h"
#include "ProductionDialogs.h"
#include "TemplateWizard.h"
#include "FittingBlocks.h"
#include "EntityPath.h"
#include "Modeling.h"
#include "ContourModeling.h"
#include <QPixmap>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHeaderView>
#include <QHash>
#include <QInputDialog>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QSaveFile>
#include <QStatusBar>
#include <QToolBar>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace {
QString describeContour(opc::ContourError error) {
    using opc::ContourError;
    switch (error) {
    case ContourError::None: return QCoreApplication::translate("MainWindow", "One closed contour");
    case ContourError::Empty: return QCoreApplication::translate("MainWindow", "No objects on the main layer");
    case ContourError::UnsupportedShape: return QCoreApplication::translate("MainWindow", "Use lines, rectangles or quadratic curves on the main layer");
    case ContourError::Degenerate: return QCoreApplication::translate("MainWindow", "A zero-length or folded edge");
    case ContourError::Open: return QCoreApplication::translate("MainWindow", "Open ends");
    case ContourError::Branched: return QCoreApplication::translate("MainWindow", "Multiple edges meet at an endpoint");
    case ContourError::Disconnected: return QCoreApplication::translate("MainWindow", "More than one closed contour");
    case ContourError::SelfIntersecting: return QCoreApplication::translate("MainWindow", "Contour intersects, overlaps, or folds onto itself");
    case ContourError::UncertainCurves: return QCoreApplication::translate("MainWindow", "Curves are too close to verify a simple contour");
    case ContourError::TooComplex: return QCoreApplication::translate("MainWindow", "Contour exceeds the geometry limits");
    }
    return {};
}

QString modelingStepName(const opc::ModelingStep& step) {
    using C = opc::ContourOperation;
    const auto text = [](const char* label) { return QCoreApplication::translate("MainWindow", label); };
    if (!step.contour)
        return text("Shape Silhouette");
    switch (*step.contour) {
    case C::Cut: return text("Cut Piece");
    case C::Relief: return text("Straight Relief Seam");
    case C::Yoke: return text("Straight Yoke");
    case C::Spread: return text("Slash and Spread");
    case C::Pleat: return text("Box Pleat");
    case C::Gather: return text("Gathering Fullness");
    case C::TransferDart:
    case C::TransferDartToOutlet: return text("Close and Transfer Dart");
    case C::MoveOpenDart: return text("Move Open Contour Dart");
    }
    return {};
}

QColor sizeColor(qsizetype index) {
    return QColor::fromHsv((265 + 137 * index) % 360, 205, 188);
}

void fillExportSizes(QComboBox& box, const opc::PatternDocument& document) {
    box.addItem(QCoreApplication::translate("MainWindow", "Base pattern"), QString{});
    for (const auto& size : document.production().sizes)
        box.addItem(size.name, size.name);
}

class PatternCommand : public QUndoCommand {
public:
    PatternCommand(opc::PatternDocument& document, opc::PatternDocument after, const QString& text,
                   PatternView* redraw = nullptr)
        : QUndoCommand(text), document_(document), before_(document), after_(std::move(after)), redraw_(redraw) {}

    PatternCommand(opc::PatternDocument& document, opc::ContourEdit edit, const QString& text,
                   PatternView& redraw)
        : PatternCommand(document, std::move(edit.document), text, &redraw) {
        for (qsizetype i = 0; i < before_.entities().size(); ++i) {
            if (!std::binary_search(edit.removedEntities.cbegin(), edit.removedEntities.cend(), i))
                survivingIndices_.append(i);
        }
        beforeVisibility_.resize(before_.entities().size(), true);
        afterVisibility_.resize(after_.entities().size(), true);
    }

    void undo() override { apply(false); }
    void redo() override { apply(true); }

private:
    void apply(bool forward) {
        const auto& updated = forward ? after_ : before_;
        if (!redraw_) {
            document_ = updated;
            return;
        }

        if (!afterVisibility_.isEmpty()) {
            applyContour(updated, forward);
            return;
        }

        // A draft edit leaves the order of manually drawn objects intact.
        QVector<bool> manualVisibility;
        QHash<QString, bool> draftVisibility;
        for (qsizetype i = 0; i < document_.entities().size(); ++i) {
            const auto& name = document_.entities()[i].draftName;
            const auto visible = redraw_->entityVisible(i);
            if (name.isEmpty())
                manualVisibility.append(visible);
            else
                draftVisibility.insert(name, visible);
        }
        document_ = updated;
        redraw_->setEntities(document_.entities());
        qsizetype manualIndex = 0;
        for (qsizetype i = 0; i < document_.entities().size(); ++i) {
            const auto& name = document_.entities()[i].draftName;
            const bool visible = name.isEmpty() ? manualVisibility[manualIndex++] : draftVisibility.value(name, true);
            if (!visible)
                redraw_->setEntityVisible(i, false);
        }
    }

    void applyContour(const opc::PatternDocument& updated, bool forward) {
        auto& current = forward ? beforeVisibility_ : afterVisibility_;
        auto& next = forward ? afterVisibility_ : beforeVisibility_;
        for (qsizetype i = 0; i < current.size(); ++i)
            current[i] = redraw_->entityVisible(i);
        for (qsizetype i = 0; i < survivingIndices_.size(); ++i) {
            const auto original = survivingIndices_[i];
            if (forward)
                next[i] = current[original];
            else
                next[original] = current[i];
        }
        redraw_->cancelDrawing();
        document_ = updated;
        redraw_->setEntities(document_.entities());
        for (qsizetype i = 0; i < next.size(); ++i) {
            if (!next[i])
                redraw_->setEntityVisible(i, false);
        }
    }

    opc::PatternDocument& document_;
    opc::PatternDocument before_, after_;
    PatternView* redraw_;
    QVector<qsizetype> survivingIndices_;
    QVector<bool> beforeVisibility_, afterVisibility_;
};

class ReplaceEntityCommand : public QUndoCommand {
public:
    ReplaceEntityCommand(opc::PatternDocument& document, PatternView& view, qsizetype index,
                         const opc::Entity& replacement, const QString& text)
        : QUndoCommand(text), document_(document), view_(view), index_(index),
          before_(document.entities()[index]), after_(replacement) {}

    void undo() override { apply(before_); }
    void redo() override { apply(after_); }

private:
    void apply(const opc::Entity& entity) {
        view_.cancelDrawing();
        document_.replace(index_, entity);
        view_.replaceEntity(index_, entity);
    }

    opc::PatternDocument& document_;
    PatternView& view_;
    qsizetype index_;
    opc::Entity before_, after_;
};

class EntityCommand : public QUndoCommand {
public:
    EntityCommand(opc::PatternDocument& document, PatternView& view, qsizetype index,
                  const opc::Entity& entity, bool adding, const QString& text)
        : QUndoCommand(text), document_(document), view_(view), index_(index),
          entity_(entity), adding_(adding), visible_(adding || view.entityVisible(index)) {}

    void redo() override { apply(adding_); }
    void undo() override { apply(!adding_); }

private:
    void apply(bool insert) {
        view_.cancelDrawing();
        if (insert) {
            document_.insert(index_, entity_);
            view_.insertEntity(index_, entity_);
            view_.setEntityVisible(index_, visible_);
        } else {
            visible_ = view_.entityVisible(index_);
            document_.remove(index_);
            view_.removeEntity(index_);
        }
    }

    opc::PatternDocument& document_;
    PatternView& view_;
    qsizetype index_;
    opc::Entity entity_;
    bool adding_;
    bool visible_;
};
}

MainWindow::MainWindow() : view_(new PatternView(&scene_, this)) {
    const auto version = QCoreApplication::applicationVersion();
    setWindowTitle(version.isEmpty() ? QStringLiteral("SOFIT[*]")
                                     : QStringLiteral("SOFIT %1[*]").arg(version));
    resize(1100, 750);
    setCentralWidget(view_);
    setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 5px; padding: 5px; border: 0; }"
        "QToolButton { padding: 6px 9px; border: 1px solid transparent; border-radius: 4px; }"
        "QToolButton:hover { background: #344457; }"
        "QToolButton:checked { background: #244755; border-color: #4f9cb1; }"
        "QTreeView { border: 1px solid #344253; }"
        "QTreeView::item { min-height: 24px; }"
        "QStatusBar { background: #1b2430; }"));

    auto* dock = new QDockWidget(tr("Drawing Objects"), this);
    dock->setObjectName(QStringLiteral("drawingObjects"));
    dock->setMinimumWidth(330);
    objects_ = new QTreeWidget(dock);
    objects_->setHeaderLabels({tr("Object"), tr("Visible"), tr("Piece"), tr("Layer")});
    objects_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    objects_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    objects_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    objects_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    objects_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    objects_->setRootIsDecorated(false);
    objects_->setAlternatingRowColors(true);
    dock->setWidget(objects_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(objects_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = objects_->selectedItems();
        const QSignalBlocker blocker(objects_);
        view_->selectEntity(selected.size() == 1 ? objects_->indexOfTopLevelItem(selected.front()) : -1);
    });
    connect(objects_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 1) {
            view_->setEntityVisible(objects_->indexOfTopLevelItem(item), item->checkState(1) == Qt::Checked);
            refreshPieceDisplay();
        }
    });
    connect(&scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (objects_->signalsBlocked())
            return;
        const QSignalBlocker blocker(objects_);
        const auto index = view_->selectedIndex();
        objects_->clearSelection();
        if (index >= 0 && index < objects_->topLevelItemCount()) {
            auto* item = objects_->topLevelItem(index);
            objects_->setCurrentItem(item);
            objects_->scrollToItem(item);
        }
    });
    connect(&undo_, &QUndoStack::indexChanged, this, &MainWindow::refreshObjects);

    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newDocument);
    file->addAction(tr("&Open"), QKeySequence::Open, this, &MainWindow::openDocument);
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveDocument);
    file->addAction(tr("Export &SVG"), this, &MainWindow::exportSvg);
    file->addAction(tr("Export &DXF…"), this, &MainWindow::exportDxf);
    file->addAction(tr("Export &PDF for Printing…"), this, &MainWindow::exportPdf);

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = undo_.createUndoAction(this, tr("Undo"));
    undoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    edit->addAction(undoAction);
    auto* redoAction = undo_.createRedoAction(this, tr("Redo"));
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    edit->addAction(redoAction);
    edit->addAction(tr("Edit Geometry…"), this, &MainWindow::editSelectedGeometry);
    auto* deleteAction = edit->addAction(tr("Delete Selection"), QKeySequence(Qt::Key_Delete),
                                         this, &MainWindow::deleteSelection);
    deleteAction->setEnabled(false);
    const auto updateDelete = [this, deleteAction] {
        deleteAction->setEnabled(!objects_->selectedItems().isEmpty());
    };
    connect(objects_, &QTreeWidget::itemSelectionChanged, this, updateDelete);
    connect(&scene_, &QGraphicsScene::selectionChanged, this, updateDelete);
    connect(&undo_, &QUndoStack::indexChanged, this, updateDelete);
    connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) { setWindowModified(!clean); });

    auto* modeling = menuBar()->addMenu(tr("&Modeling"));
    auto* split = modeling->addAction(tr("Split Line or Curve…"), this, &MainWindow::splitSelectedEdge);
    auto* join = modeling->addAction(tr("Join Free Endpoints"), this, &MainWindow::joinSelectedEdges);
    join->setToolTip(tr("Select two edges on the same layer and piece. Closes a gap of up to 5 mm."));
    connect(modeling, &QMenu::aboutToShow, this, [this, split, join] {
        const auto selected = selectedObjects();
        const bool edges = std::ranges::all_of(selected, [this](qsizetype index) {
            const auto shape = doc_.entities()[index].shape;
            return shape == opc::Shape::Line || shape == opc::Shape::Curve;
        });
        split->setEnabled(edges && selected.size() == 1);
        join->setEnabled(edges && selected.size() == 2);
    });

    modeling->addSeparator();
    modeling->addAction(tr("Cut Piece…"), this, [this] {
        modelPiece(opc::ContourOperation::Cut, tr("Cut Piece"));
    });
    modeling->addAction(tr("Straight Relief Seam…"), this, [this] {
        modelPiece(opc::ContourOperation::Relief, tr("Straight Relief Seam"));
    });
    modeling->addAction(tr("Straight Yoke…"), this, [this] {
        modelPiece(opc::ContourOperation::Yoke, tr("Straight Yoke"));
    });
    modeling->addAction(tr("Slash and Spread…"), this, [this] {
        modelPiece(opc::ContourOperation::Spread, tr("Slash and Spread"));
    });
    modeling->addAction(tr("Close and Transfer Dart…"), this, [this] {
        modelPiece(opc::ContourOperation::TransferDartToOutlet, tr("Close and Transfer Dart"));
    });
    modeling->addAction(tr("Move Open Contour Dart…"), this, [this] {
        modelPiece(opc::ContourOperation::MoveOpenDart, tr("Move Open Contour Dart"));
    });
    modeling->addSeparator();
    modeling->addAction(tr("Shape Silhouette…"), this, &MainWindow::shapeSilhouette);
    modeling->addAction(tr("Construction Tree…"), this, &MainWindow::showModelingHistory);
    modeling->addAction(tr("Add Box Pleat…"), this, [this] {
        modelPiece(opc::ContourOperation::Pleat, tr("Add Box Pleat"));
    });
    modeling->addAction(tr("Add Gathering Fullness…"), this, [this] {
        modelPiece(opc::ContourOperation::Gather, tr("Add Gathering Fullness"));
    });

    auto* pattern = menuBar()->addMenu(tr("&Pattern"));
    pattern->addAction(tr("Create Piece from Selection…"), this, &MainWindow::createPiece);
    pattern->addAction(tr("Assign Selection to Piece…"), this, &MainWindow::assignPiece);
    pattern->addAction(tr("Remove Selection from Piece"), this, [this] {
        auto updated = doc_;
        bool changed = false;
        for (const auto index : selectedObjects()) {
            auto entity = updated.entities()[index];
            changed |= entity.piece >= 0;
            entity.piece = -1;
            updated.replace(index, entity);
        }
        if (changed)
            applyPatternEdit(std::move(updated), tr("Remove Objects from Piece"));
    });
    pattern->addAction(tr("Piece, Contour and Allowance…"), this, &MainWindow::managePiece);
    pattern->addAction(tr("New Pattern from Template…"), this, [this] {
        if (auto generated = createPatternFromTemplate(this); generated && confirmDiscard()) {
            replaceDocument({}, {});
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(*generated), tr("Create Pattern from Template"), view_));
            view_->fitDrawing();
        }
    });
    pattern->addAction(tr("Parametric Draft…"), this, [this] {
        QString calculationError;
        auto updated = editParametricDraft(this, doc_, &calculationError);
        setModelingError(calculationError);
        if (updated) {
            view_->cancelDrawing();
            const bool firstDrawing = doc_.entities().isEmpty();
            undo_.push(new PatternCommand(doc_, std::move(*updated), tr("Edit Parametric Draft"), view_));
            if (firstDrawing && !doc_.entities().isEmpty())
                view_->fitDrawing();
        }
    });
    pattern->addAction(tr("Sizes, Measurements and Grade Rules…"), this, [this] {
        if (auto production = editGrading(this, doc_)) {
            if (production->useDraftMeasurements == doc_.production().useDraftMeasurements
                && opc::saveProduction(*production) == opc::saveProduction(doc_.production()))
                return;
            auto updated = doc_;
            updated.production() = std::move(*production);
            applyPatternEdit(std::move(updated), tr("Edit Sizes and Grade Rules"));
        }
    });
    pattern->addAction(tr("Fabric Layout…"), this, [this] {
        if (auto production = editFabricLayout(this, doc_)) {
            if (opc::saveProduction(*production) == opc::saveProduction(doc_.production()))
                return;
            auto updated = doc_;
            updated.production() = std::move(*production);
            applyPatternEdit(std::move(updated), tr("Edit Fabric Layout"));
        }
    });
    pattern->addAction(tr("Diagnose Pieces by Size…"), this, &MainWindow::diagnoseGradedPieces);
    auto* seams = pattern->addMenu(tr("Seams"));
    seams->addAction(tr("Measure Selected Lines and Curves"), this, &MainWindow::measureSelection);
    seams->addAction(tr("Check Fitting Block by Size…"), this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Check Fitting Block by Size"));
        auto* form = new QFormLayout(&dialog);
        auto* size = new QComboBox(&dialog);
        fillExportSizes(*size, doc_);
        form->addRow(tr("Size"), size);
        auto* tolerance = new QDoubleSpinBox(&dialog);
        tolerance->setSuffix(tr(" mm"));
        tolerance->setValue(3);
        form->addRow(tr("Tolerance"), tolerance);
        auto* capEase = new QDoubleSpinBox(&dialog);
        capEase->setSuffix(tr(" mm"));
        capEase->setValue(4);
        form->addRow(tr("Ease per sleeve cap section"), capEase);
        auto* report = new QPlainTextEdit(&dialog);
        report->setReadOnly(true);
        form->addRow(report);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);

        const auto refresh = [&] {
            const auto check = opc::checkFittingBlock(doc_, size->currentData().toString(), capEase->value());
            QStringList lines;
            lines.append(check.invalidPieces.isEmpty()
                ? (doc_.pieces().isEmpty() ? tr("No pieces to check.") : tr("All piece contours are closed and simple."))
                : tr("Invalid contours: %1").arg(check.invalidPieces.join(", ")));
            if (check.seams.isEmpty() && check.missingSeams.isEmpty())
                lines.append(tr("No fitting block seam pairs found."));
            for (const auto& missing : check.missingSeams)
                lines.append(tr("%1: could not measure both seams.").arg(missing));
            for (const auto& seam : check.seams) {
                const double difference = seam.second - seam.first;
                const auto mm = [](double value) { return QLocale().toString(value, 'f', 2); };
                lines.append(tr("%1: %2 / %3 mm, difference %4 mm, target %5 mm — %6")
                    .arg(seam.label, mm(seam.first), mm(seam.second), mm(difference), mm(seam.targetDifference),
                         std::abs(difference - seam.targetDifference) <= tolerance->value()
                             ? tr("within tolerance") : tr("outside tolerance")));
            }
            report->setPlainText(lines.join("\n"));
        };
        connect(size, &QComboBox::currentIndexChanged, &dialog, refresh);
        connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        connect(capEase, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        refresh();
        dialog.resize(680, 340);
        dialog.exec();
    });

    seams->addAction(tr("Match Selected Seam Sections…"), this, [this] { matchSeams(selectedObjects()); });
    seams->addAction(tr("Allowance for Selected Edges…"), this, &MainWindow::editEdgeAllowance);
    seams->addAction(tr("Clear Automatic Notches on Selected Edges"), this, &MainWindow::clearEdgeNotches);
    auto* overlay = pattern->addMenu(tr("Size Overlays"));
    connect(overlay, &QMenu::aboutToShow, this, [this, overlay] {
        overlay->clear();
        const auto& sizes = doc_.production().sizes;
        if (sizes.isEmpty())
            overlay->addAction(tr("No sizes defined"))->setEnabled(false);
        for (qsizetype i = 0; i < sizes.size(); ++i) {
            const auto name = sizes[i].name;
            QPixmap swatch(14, 14);
            swatch.fill(sizeColor(i));
            auto* action = overlay->addAction(QIcon(swatch), name);
            action->setCheckable(true);
            action->setChecked(overlaySizes_.contains(name));
            connect(action, &QAction::toggled, this, [this, name](bool visible) {
                if (visible)
                    overlaySizes_.insert(name);
                else
                    overlaySizes_.remove(name);
                refreshPieceDisplay();
            });
        }
    });
    pattern->addSeparator();
    pattern->addAction(tr("Move Selection to Main Layer"), this,
                       [this] { setSelectedLayer(opc::Layer::Main); });
    pattern->addAction(tr("Move Selection to Construction Layer"), this,
                       [this] { setSelectedLayer(opc::Layer::Construction); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(dock->toggleViewAction());
    auto* layers = viewMenu->addMenu(tr("Layers"));
    const std::array layerNames{tr("Main Lines"), tr("Construction Lines"), tr("Seam Allowances")};
    for (int index = 0; index < static_cast<int>(layerNames.size()); ++index) {
        auto* action = layers->addAction(layerNames[index]);
        layerActions_[index] = action;
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, index](bool visible) {
            auto updated = doc_;
            updated.setLayerVisible(static_cast<opc::Layer>(index), visible);
            applyPatternEdit(std::move(updated), tr("Change Layer Visibility"));
        });
    }
    auto* objectSnap = viewMenu->addAction(tr("Snap to Objects"));
    objectSnap->setCheckable(true);
    objectSnap->setChecked(true);
    objectSnap->setToolTip(tr("Snap to points, corners and straight-edge intersections"));
    connect(objectSnap, &QAction::toggled, view_, &PatternView::setSnapToObjects);
    auto* snapAction = viewMenu->addAction(tr("Snap to Grid (10 mm)"));
    snapAction->setCheckable(true);
    connect(snapAction, &QAction::toggled, view_, &PatternView::setSnapToGrid);
    auto* fitAction = viewMenu->addAction(tr("Fit Drawing"), QKeySequence(Qt::Key_F),
                                           view_, &PatternView::fitDrawing);
    viewMenu->addAction(tr("Zoom In"), QKeySequence::ZoomIn, view_, [this] { view_->zoomAtCenter(1.2); });
    viewMenu->addAction(tr("Zoom Out"), QKeySequence::ZoomOut, view_, [this] { view_->zoomAtCenter(1 / 1.2); });
    auto* cancelAction = new QAction(this);
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, [this] {
        view_->cancelDrawing();
        scene_.clearSelection();
        objects_->clearSelection();
    });
    addAction(cancelAction);

    auto* toolbar = new QToolBar(tr("Drawing Tools"), this);
    addToolBar(Qt::LeftToolBarArea, toolbar);
    toolbar->setMovable(false);
    toolbar->setAllowedAreas(Qt::LeftToolBarArea | Qt::RightToolBarArea);
    auto* tools = new QActionGroup(this);
    const auto addTool = [this, toolbar, tools](const QString& text, PatternView::Tool tool, const QString& hint) {
        auto* action = toolbar->addAction(text);
        action->setCheckable(true);
        tools->addAction(action);
        action->setToolTip(hint);
        connect(action, &QAction::triggered, view_, [this, tool, hint] {
            view_->setTool(tool);
            statusBar()->showMessage(hint);
        });
        return action;
    };
    addTool(tr("Select"), PatternView::Tool::Select, tr("Select or drag an object"))->setChecked(true);
    addTool(tr("Point"), PatternView::Tool::Point, tr("Click to place a point"));
    addTool(tr("Line"), PatternView::Tool::Line, tr("Pick the two endpoints"));
    addTool(tr("Rectangle"), PatternView::Tool::Rectangle, tr("Pick opposite corners"));
    addTool(tr("Circle"), PatternView::Tool::Circle, tr("Pick the center, then a point on the circle"));
    addTool(tr("Curve"), PatternView::Tool::Curve, tr("Pick both endpoints, then the Bézier control point"));
    addTool(tr("Dart"), PatternView::Tool::Dart, tr("Pick the first leg end, apex, then the second leg end"));
    addTool(tr("Measure"), PatternView::Tool::Measure, tr("Pick two points to measure the distance"));

    auto* projectToolbar = addToolBar(tr("Project and View"));
    projectToolbar->setMovable(false);
    for (auto* action : file->actions())
        projectToolbar->addAction(action);
    projectToolbar->addSeparator();
    projectToolbar->addAction(undoAction);
    projectToolbar->addAction(redoAction);
    projectToolbar->addAction(deleteAction);
    projectToolbar->addAction(tr("Exact Input…"), QKeySequence(Qt::Key_I), this, &MainWindow::exactInput);
    projectToolbar->addSeparator();
    projectToolbar->addAction(snapAction);
    projectToolbar->addAction(objectSnap);
    projectToolbar->addAction(fitAction);
    connect(view_, &PatternView::entityDrawn, this, &MainWindow::addEntity);
    connect(view_, &PatternView::dartDrawn, this, [this](const opc::Entity& first, const opc::Entity& second) {
        undo_.beginMacro(tr("Draw Dart"));
        addEntity(first);
        addEntity(second);
        undo_.endMacro();
        if (!doc_.layerVisible(opc::Layer::Construction))
            statusBar()->showMessage(tr("Dart added. Show the Construction layer to see it."), 5000);
    });
    connect(view_, &PatternView::dartRejected, this, [this] {
        statusBar()->showMessage(tr("Choose a distinct leg end so the three points form a triangle."), 5000);
    });

    connect(view_, &PatternView::entityMoved, this, &MainWindow::moveEntity);
    connect(view_, &PatternView::measured, this, [this](qreal distance) {
        statusBar()->showMessage(tr("Distance %1 mm").arg(QLocale().toString(distance, 'f', 3)));
    });
    auto* zoom = new QLabel(tr("1.00 px/mm"), statusBar());
    statusBar()->addPermanentWidget(zoom);
    connect(view_, &PatternView::zoomChanged, zoom, [zoom](qreal value) {
        zoom->setText(tr("%1 px/mm").arg(QLocale().toString(value, 'g', 4)));
    });

    const QString emptyCoordinates = tr("X: — mm  Y: — mm");
    auto* coordinates = new QLabel(emptyCoordinates, statusBar());
    statusBar()->addPermanentWidget(coordinates);
    connect(view_, &PatternView::cursorPositionChanged, coordinates,
            [coordinates](const QPointF& position) {
        const QLocale locale;
        coordinates->setText(tr("X: %1 mm  Y: %2 mm")
                                 .arg(locale.toString(position.x(), 'f', 2),
                                      locale.toString(position.y(), 'f', 2)));
    });
    connect(view_, &PatternView::cursorLeftCanvas, coordinates,
            [coordinates, emptyCoordinates] { coordinates->setText(emptyCoordinates); });
    statusBar()->showMessage(tr("Wheel: zoom | Middle drag: pan | Right click or Esc: cancel"));
}

void MainWindow::refreshPieceDisplay() {
    QVector<QPolygonF> allowances;
    for (const auto& geometry : pieces_) {
        if (!geometry.allowance.isEmpty())
            allowances.append(geometry.allowance);
    }
    QVector<QPainterPath> markings;
    if (doc_.layerVisible(opc::Layer::Main)) {
        QVector<bool> visible(pieces_.size(), false);
        for (qsizetype i = 0; i < doc_.entities().size(); ++i) {
            const auto& entity = doc_.entities()[i];
            if (entity.piece >= 0 && entity.layer == opc::Layer::Main && view_->entityVisible(i))
                visible[entity.piece] = true;
        }
        for (qsizetype i = 0; i < pieces_.size(); ++i) {
            if (!visible[i])
                continue;
            const auto marks = opc::buildPieceMarkings(doc_.pieces()[i], pieces_[i],
                                                       doc_.layerVisible(opc::Layer::Allowance));
            QPainterPath path = marks.notches;
            path.addPath(marks.grainline);
            path.addPath(marks.labelPath());
            if (!path.isEmpty())
                markings.append(std::move(path));
        }
    }
    view_->applyLayers(doc_, allowances, markings);

    QVector<QPair<QPainterPath, QColor>> overlays;
    for (qsizetype i = 0; i < doc_.production().sizes.size(); ++i) {
        const auto& size = doc_.production().sizes[i].name;
        if (!overlaySizes_.contains(size))
            continue;
        QVector<qsizetype> sources;
        const auto graded = opc::gradedPattern(doc_, size, &sources);
        QPainterPath path;
        for (qsizetype j = 0; j < graded.entities().size(); ++j) {
            const auto& entity = graded.entities()[j];
            if (entity.piece >= 0 && doc_.layerVisible(entity.layer) && view_->entityVisible(sources[j]))
                path.addPath(opc::entityPath(entity));
        }
        if (!path.isEmpty())
            overlays.append({std::move(path), sizeColor(i)});
    }
    view_->setSizeOverlays(overlays);
}

void MainWindow::refreshObjects() {
    setModelingError({});
    refreshModelingHistory();
    const QSignalBlocker blocker(objects_);
    pieces_.clear();
    for (qsizetype index = 0; index < doc_.pieces().size(); ++index)
        pieces_.append(opc::analyzePiece(doc_, index));
    refreshPieceDisplay();
    for (int index = 0; index < static_cast<int>(layerActions_.size()); ++index)
        layerActions_[index]->setChecked(doc_.layerVisible(static_cast<opc::Layer>(index)));

    objects_->clear();
    const auto selected = view_->selectedIndex();
    for (qsizetype index = 0; index < doc_.entities().size(); ++index) {
        QString name;
        const auto& entity = doc_.entities()[index];
        switch (entity.shape) {
        case opc::Shape::Point: name = tr("Point"); break;
        case opc::Shape::Line: name = tr("Line"); break;
        case opc::Shape::Rectangle: name = tr("Rectangle"); break;
        case opc::Shape::Circle: name = tr("Circle"); break;
        case opc::Shape::Curve: name = tr("Curve"); break;
        }
        if (!entity.draftName.isEmpty())
            name = tr("%1 (%2)").arg(name, entity.draftName);
        auto* item = new QTreeWidgetItem(objects_, {tr("%1 · %2").arg(index + 1).arg(name)});
        if (entity.piece >= 0)
            item->setText(2, doc_.pieces()[entity.piece].name);
        item->setText(3, entity.layer == opc::Layer::Main ? tr("Main") : tr("Construction"));
        item->setCheckState(1, view_->entityVisible(index) ? Qt::Checked : Qt::Unchecked);
        if (index == selected)
            objects_->setCurrentItem(item);
    }
}

QVector<qsizetype> MainWindow::selectedObjects() const {
    QVector<qsizetype> indices;
    for (auto* item : objects_->selectedItems())
        indices.append(objects_->indexOfTopLevelItem(item));
    return indices;
}

qsizetype MainWindow::choosePiece(const QString& title) {
    if (doc_.pieces().isEmpty()) {
        QMessageBox::information(this, title, tr("Create a piece from drawing objects first."));
        return -1;
    }

    QStringList choices;
    for (const auto& piece : doc_.pieces())
        choices.append(tr("%1. %2").arg(choices.size() + 1).arg(piece.name));
    const auto selected = selectedObjects();
    const auto current = selected.isEmpty() ? -1 : doc_.entities()[selected.front()].piece;
    if (choices.size() == 1)
        return 0;
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, title, tr("Piece"), choices,
                                             current < 0 ? 0 : current, false, &accepted);
    return accepted ? choices.indexOf(choice) : -1;
}

void MainWindow::applyPatternEdit(opc::PatternDocument document, const QString& text, bool redraw) {
    QString error;
    if (!document.modelingHistory().isEmpty()
        && !document.validateParametricSizes(document.production(), &error)) {
        setModelingError(error);
        QMessageBox::warning(this, text, error);
        return;
    }
    const auto selected = selectedObjects();
    view_->cancelDrawing();
    undo_.push(new PatternCommand(doc_, std::move(document), text, redraw ? view_ : nullptr));
    for (const auto index : selected)
        objects_->topLevelItem(index)->setSelected(true);
}

void MainWindow::createPiece() {
    const auto selected = selectedObjects();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Create Piece"),
                                 tr("Select objects in the list. Use Ctrl or Shift to select several."));
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Create Piece"), tr("Name"), QLineEdit::Normal,
                                            {}, &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    auto updated = doc_;
    const auto piece = updated.pieces().size();
    updated.pieces().append({name});
    for (const auto index : selected) {
        auto entity = updated.entities()[index];
        entity.piece = piece;
        updated.replace(index, entity);
    }
    applyPatternEdit(std::move(updated), tr("Create Piece"));
}

void MainWindow::assignPiece() {
    const auto selected = selectedObjects();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Assign Selection"), tr("Select objects in the list first."));
        return;
    }
    const auto piece = choosePiece(tr("Assign Selection"));
    if (piece < 0)
        return;

    auto updated = doc_;
    bool changed = false;
    for (const auto index : selected) {
        auto entity = updated.entities()[index];
        changed |= entity.piece != piece;
        entity.piece = piece;
        updated.replace(index, entity);
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Assign Objects to Piece"));
}

void MainWindow::setSelectedLayer(opc::Layer layer) {
    auto updated = doc_;
    bool changed = false;
    for (const auto index : selectedObjects()) {
        auto entity = updated.entities()[index];
        changed |= entity.layer != layer;
        entity.layer = layer;
        updated.replace(index, entity);
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Change Object Layer"));
}

void MainWindow::measureSelection() {
    QStringList rows;
    double total = 0;
    const QLocale locale;
    for (const auto index : selectedObjects()) {
        if (const auto value = opc::seamLength(doc_.entities()[index])) {
            rows.append(tr("Object %1    %2 mm").arg(index + 1).arg(locale.toString(*value, 'f', 3)));
            total += *value;
        }
    }
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Measure Selection"), tr("Select lines or quadratic curves."));
        return;
    }
    rows.prepend(tr("Total of measurable objects    %1 mm").arg(locale.toString(total, 'f', 3)));
    QMessageBox::information(this, tr("Measure Selection"), rows.join('\n'));
}

void MainWindow::matchSeams(const QVector<qsizetype>& indices, bool reverseSecond) {
    const auto title = tr("Match Seam Sections");
    if (indices.size() < 2 || !std::ranges::all_of(indices, [this](auto index) {
            const auto& edge = doc_.entities()[index];
            return edge.layer == opc::Layer::Main && opc::seamLength(edge).has_value();
        })) {
        QMessageBox::information(this, title, tr("Select the lines and quadratic curves of both seams on the main layer."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* table = new QTableWidget(indices.size(), 2, &dialog);
    table->setHorizontalHeaderLabels({tr("Object"), tr("Section")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QVector<QComboBox*> sides;
    const auto firstPiece = doc_.entities()[indices.front()].piece;
    for (qsizetype i = 0; i < indices.size(); ++i) {
        const auto& edge = doc_.entities()[indices[i]];
        auto* item = new QTableWidgetItem(tr("%1 · %2").arg(indices[i] + 1)
            .arg(edge.piece < 0 ? tr("Unassigned") : doc_.pieces()[edge.piece].name));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, item);
        auto* box = new QComboBox(table);
        box->addItems({tr("A"), tr("B")});
        box->setCurrentIndex(i != 0 && (indices.size() == 2 || edge.piece != firstPiece));
        table->setCellWidget(i, 1, box);
        sides.append(box);
    }
    form->addRow(table);

    auto* target = new QDoubleSpinBox(&dialog);
    target->setRange(-100000, 100000);
    target->setSuffix(tr(" mm"));
    form->addRow(tr("Target ease (B − A)"), target);
    auto* tolerance = new QDoubleSpinBox(&dialog);
    tolerance->setRange(0, 100000);
    tolerance->setValue(1);
    tolerance->setSuffix(tr(" mm"));
    form->addRow(tr("Tolerance (±)"), tolerance);
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 20);
    count->setValue(3);
    form->addRow(tr("Notch pairs"), count);
    auto* reverse = new QCheckBox(tr("Reverse correspondence on section B"), &dialog);
    reverse->setChecked(reverseSecond);
    form->addRow(reverse);
    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    form->addRow(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* add = buttons->addButton(tr("Place Paired Notches"), QDialogButtonBox::AcceptRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* size = new QComboBox(&dialog);
    fillExportSizes(*size, doc_);
    form->insertRow(form->rowCount() - 2, tr("Compare graded size"), size);
    std::optional<opc::SeamSection> a, b;
    const auto refresh = [&] {
        QVector<qsizetype> aIndices, bIndices;
        for (qsizetype i = 0; i < indices.size(); ++i)
            (sides[i]->currentIndex() == 0 ? aIndices : bIndices).append(indices[i]);
        a = opc::measureSeamSection(doc_, aIndices);
        b = opc::measureSeamSection(doc_, bIndices);
        if (!a || !b) {
            summary->setText(tr("Assign one connected, open chain to each section."));
            add->setEnabled(false);
            return;
        }
        const auto difference = b->lengthMm - a->lengthMm;
        const bool within = std::abs(difference - target->value()) <= tolerance->value();
        const auto auditable = [this](const opc::SeamSection& section) {
            const auto piece = doc_.entities()[section.edges.front().index].piece;
            return piece >= 0 && pieces_[piece].error == opc::ContourError::None && !pieces_[piece].allowanceFailed;
        };
        const auto number = [](double value) { return QLocale().toString(value, 'f', 3); };
        auto message = tr("A %1 mm, B %2 mm. Ease (B − A) %3 mm. %4")
            .arg(number(a->lengthMm), number(b->lengthMm), number(difference),
                 within ? tr("Within tolerance.") : tr("Outside tolerance."));
        const auto describeStart = [this](const opc::SeamSection& section, bool reversed) {
            const auto& edge = reversed ? section.edges.back() : section.edges.front();
            const auto& entity = doc_.entities()[edge.index];
            const auto point = edge.reversed != reversed ? entity.b : entity.a;
            return tr("object %1 at (%2, %3)").arg(edge.index + 1)
                .arg(QLocale().toString(point.x(), 'f', 2), QLocale().toString(point.y(), 'f', 2));
        };
        message += tr("\nA starts on %1. B starts on %2.")
            .arg(describeStart(*a, false), describeStart(*b, reverse->isChecked()));
        const bool valid = auditable(*a) && auditable(*b);
        if (!valid)
            message += tr("\nNotches require closed pieces with valid cut outlines.");
        const auto sizeName = size->currentData().toString();
        if (!sizeName.isEmpty()) {
            QVector<qsizetype> sourceIndices;
            const auto graded = opc::gradedPattern(doc_, sizeName, &sourceIndices);
            QVector<qsizetype> gradedA, gradedB;
            const QSet<qsizetype> sourceA(aIndices.begin(), aIndices.end());
            const QSet<qsizetype> sourceB(bIndices.begin(), bIndices.end());
            for (qsizetype i = 0; i < sourceIndices.size(); ++i) {
                if (sourceA.contains(sourceIndices[i]))
                    gradedA.append(i);
                if (sourceB.contains(sourceIndices[i]))
                    gradedB.append(i);
            }
            const auto sizeA = opc::measureSeamSection(graded, gradedA);
            const auto sizeB = opc::measureSeamSection(graded, gradedB);
            if (sizeA && sizeB) {
                const auto ease = sizeB->lengthMm - sizeA->lengthMm;
                message += tr("\n%1 · A %2 mm, B %3 mm. Ease %4 mm. %5")
                    .arg(sizeName, number(sizeA->lengthMm), number(sizeB->lengthMm), number(ease),
                         std::abs(ease - target->value()) <= tolerance->value()
                             ? tr("Within tolerance.") : tr("Outside tolerance."));
            } else {
                message += tr("\n%1 · The graded seams could not be measured.").arg(sizeName);
            }
        }
        summary->setText(message);
        add->setEnabled(valid);
    };
    for (auto* side : sides)
        connect(side, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(target, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(size, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    connect(add, &QPushButton::clicked, &dialog, [&] {
        auto updated = doc_;
        bool changed = false;
        for (int i = 1; i <= count->value(); ++i) {
            const auto fraction = static_cast<double>(i) / (count->value() + 1);
            const auto first = a->notchAt(fraction);
            const auto second = b->notchAt(reverse->isChecked() ? 1 - fraction : fraction);
            if (!first || !second) {
                QMessageBox::warning(&dialog, title, tr("A notch falls on a junction. Choose a different number of pairs."));
                return;
            }
            for (const auto& [index, position] : {*first, *second}) {
                auto edge = updated.entities()[index];
                if (std::ranges::any_of(edge.seamNotches, [position](double old) {
                        return std::abs(old - position) < 1e-9;
                    }))
                    continue;
                if (edge.seamNotches.size() >= 100) {
                    QMessageBox::warning(&dialog, title, tr("An edge can hold at most 100 automatic notches."));
                    return;
                }
                edge.seamNotches.append(position);
                updated.replace(index, edge);
                changed = true;
            }
        }
        if (changed)
            applyPatternEdit(std::move(updated), tr("Place Paired Notches"));
        dialog.accept();
    });
    refresh();
    dialog.exec();
}

void MainWindow::editEdgeAllowance() {
    const auto indices = selectedObjects();
    const auto title = tr("Allowance for Selected Edges");
    if (indices.isEmpty() || !std::ranges::all_of(indices, [this](auto index) {
            const auto& edge = doc_.entities()[index];
            return edge.piece >= 0 && edge.layer == opc::Layer::Main && opc::seamLength(edge).has_value();
        })) {
        QMessageBox::information(this, title, tr("Select lines or quadratic curves assigned to pieces on the main layer."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    const auto& first = doc_.entities()[indices.front()];
    auto* inherit = new QCheckBox(tr("Use the default allowance of each piece"), &dialog);
    const auto original = first.seamAllowanceMm.value_or(doc_.pieces()[first.piece].allowanceMm);
    auto* width = new QDoubleSpinBox(&dialog);
    width->setDecimals(3);
    width->setRange(0, std::max(100000.0, original));
    width->setValue(original);
    width->setSuffix(tr(" mm"));
    form->addRow(inherit);
    form->addRow(tr("Allowance"), width);
    connect(inherit, &QCheckBox::toggled, width, &QDoubleSpinBox::setDisabled);
    inherit->setChecked(!first.seamAllowanceMm.has_value());
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto updated = doc_;
        QSet<qsizetype> affected;
        const auto value = inherit->isChecked() ? std::nullopt : std::optional<double>(width->value());
        for (const auto index : indices) {
            auto edge = updated.entities()[index];
            if (edge.seamAllowanceMm == value)
                continue;
            edge.seamAllowanceMm = value;
            affected.insert(edge.piece);
            updated.replace(index, edge);
        }
        for (const auto piece : affected) {
            const auto check = opc::analyzePiece(updated, piece);
            if (value && (check.error != opc::ContourError::None || check.allowanceFailed)) {
                QMessageBox::warning(&dialog, title, tr("The new widths cannot produce a valid cut outline for %1.")
                    .arg(updated.pieces()[piece].name));
                return;
            }
        }
        if (!affected.isEmpty())
            applyPatternEdit(std::move(updated), tr("Edit Edge Allowances"));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::clearEdgeNotches() {
    auto updated = doc_;
    bool changed = false;
    for (const auto index : selectedObjects()) {
        auto edge = updated.entities()[index];
        if (edge.seamNotches.isEmpty())
            continue;
        edge.seamNotches.clear();
        updated.replace(index, edge);
        changed = true;
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Clear Automatic Notches"));
}

void MainWindow::diagnoseGradedPieces() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Diagnose Pieces by Size"));
    QFormLayout form(&dialog);
    QComboBox sizes(&dialog);
    fillExportSizes(sizes, doc_);
    sizes.insertItem(0, tr("All sizes"), QVariant{});
    sizes.setCurrentIndex(0);
    form.addRow(tr("Size"), &sizes);
    QTreeWidget report(&dialog);
    report.setHeaderLabels({tr("Piece"), tr("Contour, allowance and notches")});
    form.addRow(&report);
    QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QMap<QString, QVector<qsizetype>> copyCounts;
    for (const auto& placement : doc_.production().placements) {
        if (placement.piece < 0 || placement.piece >= doc_.pieces().size())
            continue;
        auto& counts = copyCounts[placement.size];
        if (counts.isEmpty())
            counts.resize(doc_.pieces().size());
        ++counts[placement.piece];
    }
    const auto addSize = [&](QTreeWidgetItem* group, const QString& size) {
        const auto graded = opc::gradedPattern(doc_, size);
        if (graded.pieces().isEmpty()) {
            group->setText(1, doc_.pieces().isEmpty() ? tr("No pieces") : tr("Size evaluation failed"));
            return;
        }
        const auto counts = copyCounts.constFind(size);
        if (counts == copyCounts.cend() && !copyCounts.isEmpty())
            group->setText(1, tr("Size is absent from the layout"));
        for (qsizetype i = 0; i < graded.pieces().size(); ++i) {
            const auto& piece = graded.pieces()[i];
            const auto geometry = opc::analyzePiece(graded, i);
            auto* row = new QTreeWidgetItem(group, {piece.name, describeContour(geometry.error)});
            const auto copies = counts == copyCounts.cend() ? 0 : counts->at(i);
            QString count = tr("%1 per kit").arg(piece.cutCount);
            if (counts != copyCounts.cend()) {
                if (copies < piece.cutCount)
                    count = tr("%1 of %2 in layout · missing %3").arg(copies).arg(piece.cutCount).arg(piece.cutCount - copies);
                else if (copies > piece.cutCount)
                    count = tr("%1 of %2 in layout · extra %3").arg(copies).arg(piece.cutCount).arg(copies - piece.cutCount);
                else
                    count = tr("%1 of %2 in layout").arg(copies).arg(piece.cutCount);
            }
            new QTreeWidgetItem(row, {tr("Cut quantity"), count});
            if (geometry.error == opc::ContourError::None) {
                new QTreeWidgetItem(row, {tr("Seam dimensions"),
                    tr("%1 × %2 mm · Perimeter %3 mm")
                        .arg(geometry.bounds.width(), 0, 'f', 2)
                        .arg(geometry.bounds.height(), 0, 'f', 2)
                        .arg(geometry.perimeterMm, 0, 'f', 2)});
            }
            if (geometry.allowanceFailed) {
                new QTreeWidgetItem(row, {tr("Allowance"), tr("Cut outline could not be generated")});
            } else if (!geometry.allowance.isEmpty()) {
                const auto bounds = geometry.allowance.boundingRect();
                new QTreeWidgetItem(row, {tr("Cut dimensions"), tr("%1 × %2 mm")
                    .arg(bounds.width(), 0, 'f', 2).arg(bounds.height(), 0, 'f', 2)});
            }
            if (!piece.grainAngle)
                new QTreeWidgetItem(row, {tr("Grainline"), tr("Grain direction is unspecified")});
            if (!piece.showLabel)
                new QTreeWidgetItem(row, {tr("Label"), tr("Printed label is disabled")});
            qsizetype expected = piece.notches.size();
            for (const auto& edge : graded.entities()) {
                if (edge.piece == i && edge.layer == opc::Layer::Main)
                    expected += edge.seamNotches.size();
            }
            if (expected) {
                const auto marks = opc::buildPieceMarkings(piece, geometry, !geometry.allowance.isEmpty());
                const auto rendered = marks.notches.elementCount() / 2;
                new QTreeWidgetItem(row, {tr("Notches"), tr("%1 of %2 marks drawn").arg(rendered).arg(expected)});
                if (marks.notchesMissingCutOutline)
                    new QTreeWidgetItem(row, {tr("Notches"), tr("%1 marks do not reach the cut outline")
                        .arg(marks.notchesMissingCutOutline)});
            }
            row->setExpanded(true);
        }
    };
    const auto refresh = [&] {
        report.clear();
        for (int i = 1; i < sizes.count(); ++i) {
            if (sizes.currentIndex() != 0 && sizes.currentIndex() != i)
                continue;
            auto* group = new QTreeWidgetItem(&report, {sizes.itemText(i)});
            addSize(group, sizes.itemData(i).toString());
            group->setExpanded(true);
        }
        report.resizeColumnToContents(0);
    };
    connect(&sizes, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    dialog.resize(680, 420);
    dialog.exec();
}

void MainWindow::managePiece() {
    const auto piece = choosePiece(tr("Piece, Contour and Allowance"));
    if (piece < 0)
        return;

    const auto& original = doc_.pieces()[piece];
    const auto& geometry = pieces_[piece];
    using opc::ContourError;
    const auto contour = describeContour(geometry.error);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Piece, Contour and Allowance"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(original.name, &dialog);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Contour check"), new QLabel(contour, &dialog));
    QStringList shortEdges;
    for (qsizetype i = 0; i < doc_.entities().size(); ++i) {
        const auto& edge = doc_.entities()[i];
        if (edge.piece != piece || edge.layer != opc::Layer::Main)
            continue;
        const auto measured = opc::seamLength(edge);
        const bool narrowRectangle = edge.shape == opc::Shape::Rectangle
            && std::min(std::abs(edge.a.x() - edge.b.x()), std::abs(edge.a.y() - edge.b.y())) < 1;
        if ((measured && *measured < 1) || narrowRectangle)
            shortEdges.append(QString::number(i + 1));
    }
    if (!shortEdges.isEmpty()) {
        auto* warning = new QLabel(shortEdges.join(", "), &dialog);
        warning->setWordWrap(true);
        form->addRow(tr("Objects with segments shorter than 1 mm"), warning);
    }
    if (geometry.error == ContourError::None) {
        const QLocale locale;
        form->addRow(tr("Perimeter"), new QLabel(tr("%1 mm").arg(locale.toString(geometry.perimeterMm, 'f', 2)), &dialog));
        form->addRow(tr("Width × height"), new QLabel(tr("%1 × %2 mm").arg(
            locale.toString(geometry.bounds.width(), 'f', 2),
            locale.toString(geometry.bounds.height(), 'f', 2)), &dialog));
    }
    if (geometry.allowanceFailed || (original.allowanceMm > 0 && geometry.error != ContourError::None))
        form->addRow(new QLabel(tr("The stored allowance cannot be drawn on this contour."), &dialog));

    auto* width = new QDoubleSpinBox(&dialog);
    width->setDecimals(3);
    width->setRange(0, std::max(100000.0, original.allowanceMm));
    width->setSpecialValueText(tr("None"));
    width->setSuffix(tr(" mm"));
    width->setValue(original.allowanceMm);
    form->addRow(tr("Seam allowance"), width);
    auto* label = new QCheckBox(tr("Show on piece"), &dialog);
    label->setChecked(original.showLabel);
    form->addRow(tr("Label"), label);
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 9999);
    count->setValue(original.cutCount);
    form->addRow(tr("Cut quantity"), count);
    auto* size = new QLineEdit(original.size, &dialog);
    form->addRow(tr("Size"), size);

    auto* grain = new QCheckBox(tr("Show arrow"), &dialog);
    grain->setChecked(original.grainAngle.has_value());
    form->addRow(tr("Grainline"), grain);
    auto* angle = new QDoubleSpinBox(&dialog);
    angle->setDecimals(2);
    angle->setRange(0, 359.99);
    angle->setValue(original.grainAngle.value_or(90));
    angle->setEnabled(grain->isChecked());
    connect(grain, &QCheckBox::toggled, angle, &QWidget::setEnabled);
    form->addRow(tr("Angle clockwise from right (°)"), angle);

    auto* notches = new QLineEdit(&dialog);
    QStringList positions;
    for (double position : original.notches)
        positions.append(QString::number(position, 'g', 17));
    notches->setText(positions.join(' '));
    notches->setPlaceholderText(tr("Example: 10 35.5 80"));
    notches->setToolTip(tr("Enter up to 100 positions along the contour, in percent from 0 to below 100. "
                           "Separate positions with spaces and use a decimal point."));
    form->addRow(tr("Notches (% of perimeter)"), notches);
    auto edited = original;

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* remove = buttons->addButton(tr("Delete Piece…"), QDialogButtonBox::DestructiveRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (QMessageBox::question(&dialog, tr("Delete Piece"),
                                  tr("Delete the piece and keep its drawing objects?")) == QMessageBox::Yes)
            dialog.done(2);
    });
    const auto displayedWidth = width->value();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty()) {
            name->setFocus();
            return;
        }
        if (width->value() > 0 && width->value() != displayedWidth) {
            auto updated = doc_;
            updated.pieces()[piece].allowanceMm = width->value();
            const auto check = opc::analyzePiece(updated, piece);
            if (check.error != ContourError::None || check.allowanceFailed) {
                QMessageBox::warning(&dialog, tr("Allowance unavailable"),
                    tr("The contour cannot produce an outward allowance at this width. "
                       "Check the contour or try a smaller width."));
                return;
            }
        }
        const auto fields = notches->text().simplified().split(' ', Qt::SkipEmptyParts);
        QVector<double> values;
        for (const auto& field : fields) {
            bool ok = false;
            const double position = field.toDouble(&ok);
            if (!ok || !(position >= 0 && position < 100) || values.size() >= 100) {
                QMessageBox::warning(&dialog, tr("Invalid notches"), notches->toolTip());
                return;
            }
            values.append(position);
        }
        edited.name = name->text().trimmed();
        edited.allowanceMm = width->value() == displayedWidth ? original.allowanceMm : width->value();
        edited.cutCount = count->value();
        edited.size = size->text().trimmed();
        edited.showLabel = label->isChecked();
        edited.grainAngle = grain->isChecked() ? std::optional<double>(angle->value()) : std::nullopt;
        edited.notches = std::move(values);
        dialog.accept();
    });

    const int result = dialog.exec();
    if (result == QDialog::Rejected)
        return;
    auto updated = doc_;
    if (result == 2) {
        if (doc_.hasContourHistory()) {
            QMessageBox::information(this, tr("Delete Piece"),
                tr("Remove contour steps from the construction tree before deleting pieces."));
            return;
        }
        updated.pieces().removeAt(piece);
        updated.production().removePiece(piece);
        for (qsizetype index = 0; index < updated.entities().size(); ++index) {
            auto entity = updated.entities()[index];
            if (entity.piece < piece)
                continue;
            entity.piece = entity.piece == piece ? -1 : entity.piece - 1;
            updated.replace(index, entity);
        }
        applyPatternEdit(std::move(updated), tr("Delete Piece"));
    } else if (edited != original) {
        updated.pieces()[piece] = std::move(edited);
        applyPatternEdit(std::move(updated), tr("Edit Piece"));
    }
}

void MainWindow::moveEntity(qsizetype index, const QPointF& offset) {
    if (!doc_.entities()[index].draftName.isEmpty())
        return;
    auto moved = doc_.entities()[index];
    moved.a += offset;
    moved.b += offset;
    if (moved.shape == opc::Shape::Curve)
        moved.control += offset;
    undo_.push(new ReplaceEntityCommand(doc_, *view_, index, moved, tr("Move Object")));
}

void MainWindow::addEntity(const opc::Entity& entity) {
    undo_.push(new EntityCommand(doc_, *view_, doc_.entities().size(), entity, true, tr("Add Object")));
}

void MainWindow::deleteSelection() {
    auto selected = selectedObjects();
    if (selected.isEmpty())
        return;
    if (std::ranges::any_of(selected, [this](qsizetype index) {
            return !doc_.entities()[index].draftName.isEmpty();
        })) {
        statusBar()->showMessage(tr("To remove a formula-driven object, edit Pattern → Parametric Draft."), 5000);
        return;
    }
    std::sort(selected.begin(), selected.end(), std::greater<>());
    undo_.beginMacro(tr("Delete Objects"));
    for (const auto index : selected)
        undo_.push(new EntityCommand(doc_, *view_, index, doc_.entities().at(index), false, tr("Delete Object")));
    undo_.endMacro();
}

bool MainWindow::canModelEdge(const opc::Entity& edge) {
    if (!edge.draftName.isEmpty()) {
        statusBar()->showMessage(tr("Edit formula-driven edges in Pattern → Parametric Draft."), 5000);
        return false;
    }
    if (edge.piece >= 0 && std::ranges::any_of(doc_.production().rules, [&edge](const auto& rule) {
            return rule.piece == edge.piece;
        })) {
        statusBar()->showMessage(tr("This operation is unavailable on pieces with point grading rules."), 5000);
        return false;
    }
    return true;
}

void MainWindow::splitSelectedEdge() {
    const auto selected = selectedObjects();
    if (selected.size() != 1)
        return;
    const auto index = selected.front();
    const auto edge = doc_.entities()[index];
    if (!canModelEdge(edge))
        return;

    const auto title = tr("Split Line or Curve");
    const auto label = edge.shape == opc::Shape::Curve
        ? tr("Bézier parameter from start (%)")
        : tr("Distance from start (%)");
    bool accepted = false;
    const double percent = QInputDialog::getDouble(this, title, label, 50, 0.01, 99.99, 2, &accepted);
    if (!accepted)
        return;
    if (!edge.seamNotches.isEmpty()) {
        statusBar()->showMessage(tr("Clear automatic notches on this edge before splitting it."), 5000);
        return;
    }
    const auto parts = opc::splitEdge(edge, percent / 100);
    if (!parts) {
        statusBar()->showMessage(tr("A split must produce two nondegenerate edges."), 5000);
        return;
    }

    if (edge.piece >= 0 && edge.layer == opc::Layer::Main) {
        auto trial = doc_;
        trial.replace(index, (*parts)[0]);
        trial.insert(index + 1, (*parts)[1]);
        const auto before = pieces_[edge.piece];
        const auto after = opc::analyzePiece(trial, edge.piece);
        if (before.error == opc::ContourError::None
            && (after.error != opc::ContourError::None || (!before.allowanceFailed && after.allowanceFailed))) {
            statusBar()->showMessage(tr("This split would invalidate the contour or its seam allowance."), 5000);
            return;
        }
    }

    const bool visible = view_->entityVisible(index);
    undo_.beginMacro(title);
    undo_.push(new ReplaceEntityCommand(doc_, *view_, index, (*parts)[0], title));
    undo_.push(new EntityCommand(doc_, *view_, index + 1, (*parts)[1], true, title));
    view_->setEntityVisible(index + 1, visible);
    undo_.endMacro();
}

void MainWindow::joinSelectedEdges() {
    const auto selected = selectedObjects();
    if (selected.size() != 2)
        return;
    const auto first = selected[0];
    const auto second = selected[1];
    const auto edge = doc_.entities()[first];
    if (!canModelEdge(edge) || !canModelEdge(doc_.entities()[second]))
        return;
    const auto joined = opc::joinEdges(doc_, first, second, 5);
    if (!joined) {
        statusBar()->showMessage(tr("Select two edges on the same piece and layer with a unique nearest pair of free ends within 5 mm."), 5000);
        return;
    }

    if (edge.piece >= 0 && edge.layer == opc::Layer::Main) {
        auto trial = doc_;
        trial.replace(first, (*joined)[0]);
        trial.replace(second, (*joined)[1]);
        const auto result = opc::analyzePiece(trial, edge.piece);
        if ((result.error != opc::ContourError::None && result.error != opc::ContourError::Open)
            || result.allowanceFailed) {
            statusBar()->showMessage(tr("This join would produce an invalid contour or seam allowance."), 5000);
            return;
        }
    }

    const auto title = tr("Join Free Endpoints");
    undo_.beginMacro(title);
    undo_.push(new ReplaceEntityCommand(doc_, *view_, first, (*joined)[0], title));
    undo_.push(new ReplaceEntityCommand(doc_, *view_, second, (*joined)[1], title));
    undo_.endMacro();
}

void MainWindow::shapeSilhouette() {
    const auto title = tr("Shape Silhouette");
    const auto piece = choosePiece(title);
    if (piece < 0)
        return;
    const auto geometry = opc::analyzePiece(doc_, piece);
    if (geometry.error != opc::ContourError::None) {
        QMessageBox::warning(this, title, tr("The piece needs one closed, simple contour."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* axis = new QComboBox(&dialog);
    axis->addItems({tr("Center of piece"), tr("Left edge of bounds"), tr("Right edge of bounds")});
    form->addRow(tr("Vertical axis"), axis);
    const auto width = [&](const QString& label, double value) {
        auto* input = new QDoubleSpinBox(&dialog);
        input->setRange(20, 500);
        input->setSuffix(tr(" %"));
        input->setValue(value);
        form->addRow(label, input);
        return input;
    };
    const auto* top = width(tr("Top width"), 100);
    const auto* bottom = width(tr("Bottom width"), 125);
    const auto* length = width(tr("Length from top of piece"), 100);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const bool formulaPiece = std::ranges::any_of(doc_.entities(), [piece](const auto& entity) {
        return entity.piece == piece && !entity.draftName.isEmpty();
    });
    if (formulaPiece) {
        const double fractions[]{0.5, 0, 1};
        auto updated = doc_;
        QString error;
        if (!updated.appendSilhouette(piece, fractions[axis->currentIndex()], top->value() / 100,
                                      bottom->value() / 100, &error, length->value() / 100)) {
            setModelingError(error);
            QMessageBox::warning(this, title, error);
            return;
        }
        view_->cancelDrawing();
        undo_.push(new PatternCommand(doc_, std::move(updated), title, view_));
        return;
    }
    const double axes[]{geometry.bounds.center().x(), geometry.bounds.left(), geometry.bounds.right()};
    auto result = opc::shapeSilhouette(doc_, piece, axes[axis->currentIndex()], top->value() / 100,
                                       bottom->value() / 100, length->value() / 100);
    if (!result.edit) {
        QMessageBox::warning(this, title, result.error);
        return;
    }
    undo_.push(new PatternCommand(doc_, std::move(*result.edit), title, *view_));
}

void MainWindow::showModelingHistory() {
    if (!modelingDock_) {
        modelingDock_ = new QDockWidget(tr("Construction Tree"), this);
        auto* panel = new QWidget(modelingDock_);
        auto* layout = new QVBoxLayout(panel);
        modelingTree_ = new QTreeWidget(panel);
        modelingTree_->setHeaderLabels({tr("Construction"), tr("Result")});
        modelingError_ = new QLabel(panel);
        modelingError_->setWordWrap(true);
        modelingError_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        modelingError_->setTextFormat(Qt::PlainText);
        modelingError_->hide();
        removeModelingStep_ = new QPushButton(tr("Remove Last Step"), panel);
        layout->addWidget(modelingTree_);
        layout->addWidget(modelingError_);
        layout->addWidget(removeModelingStep_);
        modelingDock_->setWidget(panel);
        addDockWidget(Qt::RightDockWidgetArea, modelingDock_);
        connect(modelingTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
            const auto pair = item->data(0, Qt::UserRole).toList();
            if (pair.size() == 2)
                matchSeams({static_cast<qsizetype>(pair[0].toLongLong()),
                            static_cast<qsizetype>(pair[1].toLongLong())}, true);
            else if (const auto step = item->data(0, Qt::UserRole + 1); step.isValid())
                editModelingStep(step.toLongLong());
        });
        connect(removeModelingStep_, &QPushButton::clicked, this, [this] {
            auto updated = doc_;
            QString error;
            if (!updated.removeLastModelingStep(&error)) {
                setModelingError(error);
                return;
            }
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Remove Last Modeling Step"), view_));
        });
    }
    modelingDock_->show();
    refreshModelingHistory();
    modelingDock_->raise();
}

void MainWindow::editModelingStep(qsizetype index) {
    auto settings = doc_.modelingHistory()[index];
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit %1").arg(modelingStepName(settings)));
    QFormLayout form(&dialog);
    const auto number = [&](const QString& label, double value, double minimum, double maximum,
                            const QString& suffix) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setDecimals(6);
        field->setRange(minimum, maximum);
        field->setValue(value);
        field->setSuffix(suffix);
        form.addRow(label, field);
        return field;
    };
    QDoubleSpinBox* axis = nullptr;
    QDoubleSpinBox* top = nullptr;
    QDoubleSpinBox* bottom = nullptr;
    QDoubleSpinBox* length = nullptr;
    QDoubleSpinBox* spread = nullptr;
    if (settings.contour) {
        spread = number(tr("Added width"), settings.spreadMm, 0.000001,
                        std::max(100000., settings.spreadMm), tr(" mm"));
    } else {
        axis = number(tr("Axis from left edge"), settings.axisFraction * 100, 0, 100, tr(" %"));
        top = number(tr("Top width"), settings.topScale * 100, 20, 500, tr(" %"));
        bottom = number(tr("Bottom width"), settings.bottomScale * 100, 20, 500, tr(" %"));
        length = number(tr("Length from top"), settings.lengthScale * 100, 20, 500, tr(" %"));
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto read = [](const QDoubleSpinBox* field, double original, double factor) {
            return std::abs(field->value() - original * factor) <= 0.0000005
                ? original : field->value() / factor;
        };
        auto revised = settings;
        if (spread) {
            revised.spreadMm = read(spread, settings.spreadMm, 1);
        } else {
            revised.axisFraction = read(axis, settings.axisFraction, 100);
            revised.topScale = read(top, settings.topScale, 100);
            revised.bottomScale = read(bottom, settings.bottomScale, 100);
            revised.lengthScale = read(length, settings.lengthScale, 100);
        }
        auto updated = doc_;
        QString error;
        if (!updated.editModelingStep(index, revised, &error)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), error);
            return;
        }
        if (revised != settings) {
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Edit Modeling Step"), view_));
        }
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::setModelingError(const QString& error) {
    if (!modelingDock_ && !error.isEmpty() && !doc_.modelingHistory().isEmpty())
        showModelingHistory();
    if (modelingError_) {
        modelingError_->setText(error);
        modelingError_->setVisible(!error.isEmpty());
    }
}

void MainWindow::refreshModelingHistory() {
    if (!modelingTree_ || modelingDock_->isHidden())
        return;
    modelingTree_->clear();
    auto* root = new QTreeWidgetItem(modelingTree_, {tr("Parametric draft")});
    const auto& draft = doc_.parametricDraft();
    root->setText(1, tr("%1 measurements, %2 formula edges").arg(draft.measurements.size()).arg(draft.edges.size()));
    const auto& history = doc_.modelingHistory();
    QVector<QVector<qsizetype>> gradeSources(doc_.production().sizes.size());
    QVector<opc::PatternDocument> grades;
    const auto seamRow = [&](QTreeWidgetItem* step, qsizetype a, qsizetype b) {
        if (grades.isEmpty()) {
            for (qsizetype i = 0; i < doc_.production().sizes.size(); ++i)
                grades.append(opc::gradedPattern(doc_, doc_.production().sizes[i].name, &gradeSources[i]));
        }
        auto* pair = new QTreeWidgetItem(step, {tr("Paired cut edges")});
        pair->setData(0, Qt::UserRole, QVariantList{QVariant::fromValue(a), QVariant::fromValue(b)});
        pair->setToolTip(0, tr("Open the seam matching and notch placement dialog."));
        const auto add = [&](const QString& size, const opc::Entity& first, const opc::Entity& second) {
            const auto left = opc::seamLength(first);
            const auto right = opc::seamLength(second);
            if (left && right) {
                auto* row = new QTreeWidgetItem(pair, {size, tr("%L1 / %L2 mm · B − A %L3 mm")
                    .arg(*left, 0, 'f', 2).arg(*right, 0, 'f', 2).arg(*right - *left, 0, 'f', 2)});
                // Compare distances along the seam after reversing the second sewing direction.
                auto firstNotches = first.seamNotches;
                auto secondNotches = second.seamNotches;
                if (firstNotches.size() != secondNotches.size()) {
                    new QTreeWidgetItem(row, {tr("Notch counts differ"),
                        tr("%1 / %2").arg(firstNotches.size()).arg(secondNotches.size())});
                } else {
                    std::ranges::sort(firstNotches);
                    std::ranges::sort(secondNotches, std::greater<>());
                    double maximumOffset = 0;
                    for (qsizetype i = 0; i < firstNotches.size(); ++i) {
                        const double offset = firstNotches[i] * *left - (1 - secondNotches[i]) * *right;
                        maximumOffset = std::max(maximumOffset, std::abs(offset));
                    }
                    if (maximumOffset >= 0.005)
                        new QTreeWidgetItem(row, {tr("Notch positions differ"),
                            tr("Up to %L1 mm from the sewing start").arg(maximumOffset, 0, 'f', 2)});
                }
            }
        };
        add(tr("Base pattern"), doc_.entities()[a], doc_.entities()[b]);
        for (qsizetype i = 0; i < grades.size(); ++i) {
            const auto left = gradeSources[i].indexOf(a);
            const auto right = gradeSources[i].indexOf(b);
            const auto size = doc_.production().sizes[i].name;
            if (left >= 0 && right >= 0)
                add(size, grades[i].entities()[left], grades[i].entities()[right]);
            else
                new QTreeWidgetItem(pair, {size, tr("Cannot recalculate")});
        }
    };
    for (qsizetype i = 0; i < history.size(); ++i) {
        const auto& step = history[i];
        auto* item = new QTreeWidgetItem(root, {tr("%1. %2").arg(i + 1).arg(modelingStepName(step))});
        if (!step.contour || opc::usesSpreadWidth(*step.contour)) {
            item->setData(0, Qt::UserRole + 1, QVariant::fromValue(i));
            item->setToolTip(0, tr("Double-click to edit step parameters."));
        }
        if (!step.contour)
            item->setText(1, tr("Width %L1% / %L2% · Length %L3%")
                .arg(step.topScale * 100).arg(step.bottomScale * 100).arg(step.lengthScale * 100));
        else if (step.secondPiece >= 0)
            item->setText(1, doc_.pieces()[step.secondPiece].name);
        else if (step.spreadMm > 0)
            item->setText(1, tr("%L1 mm").arg(step.spreadMm));
        auto* inputs = new QTreeWidgetItem(item, {tr("Source objects"), QString::number(step.objects.size())});
        for (const auto& name : step.objects)
            new QTreeWidgetItem(inputs, {name});
        if (!step.guides.isEmpty()) {
            auto* guides = new QTreeWidgetItem(item, {tr("Guides")});
            for (const auto& name : step.guides)
                new QTreeWidgetItem(guides, {name});
        }
        if (step.secondPiece >= 0) {
            const auto prefix = QStringLiteral("@%1:seam-").arg(i + 1);
            const auto& entities = doc_.entities();
            const auto a = std::ranges::find(entities, prefix + u'A', &opc::Entity::draftName);
            const auto b = std::ranges::find(entities, prefix + u'B', &opc::Entity::draftName);
            if (a != entities.end() && b != entities.end())
                seamRow(item, a - entities.begin(), b - entities.begin());
            else
                new QTreeWidgetItem(item, {tr("Paired cut edges"), tr("Changed by a later contour step")});
        }
    }
    root->setExpanded(true);
    modelingTree_->resizeColumnToContents(0);
    removeModelingStep_->setEnabled(!history.isEmpty());
}

void MainWindow::modelPiece(opc::ContourOperation operation, const QString& title) {
    using opc::ContourOperation;
    auto guides = selectedObjects();
    const bool openDart = operation == ContourOperation::MoveOpenDart;
    const bool dart = openDart || operation == ContourOperation::TransferDartToOutlet;
    if (guides.size() != (dart ? 3 : 1)) {
        QMessageBox::information(this, title, openDart
            ? tr("Select two main-contour dart legs and one construction outlet.")
            : dart ? tr("Select two dart legs and an outlet on the Construction layer.")
                   : tr("Select one construction line with both ends on the piece contour."));
        return;
    }
    const auto piece = choosePiece(title);
    if (piece < 0)
        return;
    if (openDart) {
        std::stable_partition(guides.begin(), guides.end(), [this](qsizetype index) {
            return doc_.entities()[index].layer == opc::Layer::Main;
        });
    } else if (dart) {
        QStringList lines;
        for (const auto index : guides) {
            const auto& line = doc_.entities()[index];
            lines.append(tr("Object %1   (%2, %3) to (%4, %5)").arg(index + 1)
                         .arg(line.a.x()).arg(line.a.y()).arg(line.b.x()).arg(line.b.y()));
        }
        bool accepted = false;
        const auto outlet = QInputDialog::getItem(this, title, tr("Choose the new outlet among the selected lines"),
                                                  lines, 2, false, &accepted);
        if (!accepted)
            return;
        guides.move(lines.indexOf(outlet), 2);
    }
    double width = 0;
    if (opc::usesSpreadWidth(operation)) {
        bool accepted = false;
        width = QInputDialog::getDouble(this, title, tr("Total added width perpendicular to the cut (mm)"),
                                        20, 0.01, 100000, 2, &accepted);
        if (!accepted)
            return;
    }
    const auto named = [](const opc::Entity& entity) { return !entity.draftName.isEmpty(); };
    const bool formula = std::ranges::any_of(doc_.entities(), [piece, named](const auto& entity) {
        return entity.piece == piece && named(entity);
    }) || std::ranges::any_of(guides, [&](qsizetype index) { return named(doc_.entities()[index]); });
    if (formula) {
        auto updated = doc_;
        QString error;
        if (!updated.appendContour(piece, operation, guides, width, &error)) {
            setModelingError(error);
            QMessageBox::warning(this, title, error);
            return;
        }
        view_->cancelDrawing();
        undo_.push(new PatternCommand(doc_, std::move(updated), title, view_));
        showModelingHistory();
        return;
    }
    auto result = opc::modelContour(doc_, piece, operation, guides, width);
    if (!result.edit) {
        QMessageBox::warning(this, title, result.error);
        return;
    }
    undo_.push(new PatternCommand(doc_, std::move(*result.edit), title, *view_));
    statusBar()->showMessage(tr("%1 completed. The new contour passed validation.").arg(title), 5000);
}

void MainWindow::exactInput() {
    geometryDialog(-1);
}

void MainWindow::editSelectedGeometry() {
    const auto selected = selectedObjects();
    if (selected.size() != 1) {
        statusBar()->showMessage(tr("Select one point, line or quadratic curve to edit."), 5000);
        return;
    }
    const auto index = selected.front();
    const auto& entity = doc_.entities()[index];
    if (!canModelEdge(entity))
        return;
    if (entity.shape == opc::Shape::Rectangle || entity.shape == opc::Shape::Circle) {
        statusBar()->showMessage(tr("Select a point, line or quadratic curve to edit."), 5000);
        return;
    }
    geometryDialog(index);
}

void MainWindow::geometryDialog(qsizetype index) {
    const bool editing = index >= 0;
    const auto original = editing ? doc_.entities()[index] : opc::Entity{};
    QDialog dialog(this);
    dialog.setWindowTitle(editing ? tr("Edit Geometry") : tr("Exact Input"));
    QFormLayout form(&dialog);
    auto* shape = new QComboBox(&dialog);
    shape->addItems({tr("Point"), tr("Line"), tr("Rectangle"), tr("Circle"), tr("Curve")});
    switch (view_->tool()) {
    case PatternView::Tool::Point: shape->setCurrentIndex(0); break;
    case PatternView::Tool::Rectangle: shape->setCurrentIndex(2); break;
    case PatternView::Tool::Circle: shape->setCurrentIndex(3); break;
    case PatternView::Tool::Curve: shape->setCurrentIndex(4); break;
    default: shape->setCurrentIndex(1); break;
    }
    if (editing) {
        shape->setCurrentIndex(static_cast<int>(original.shape));
        shape->setEnabled(false);
    }
    form.addRow(tr("Object"), shape);

    const auto field = [&dialog, &form](const QString& label, double minimum = -1e7, double maximum = 1e7) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setDecimals(6);
        spin->setRange(minimum, maximum);
        spin->setGroupSeparatorShown(false);
        form.addRow(label, spin);
        return spin;
    };
    auto* x1 = field(tr("X₁ (mm)"));
    auto* y1 = field(tr("Y₁ (mm)"));
    auto* mode = new QComboBox(&dialog);
    mode->addItems({tr("End coordinates"), tr("Length and angle")});
    form.addRow(tr("Line input"), mode);
    auto* x2 = field(tr("X₂ (mm)"));
    auto* y2 = field(tr("Y₂ (mm)"));
    auto* length = field(tr("Length (mm)"), 0.000001);
    length->setValue(10);
    auto* radius = field(tr("Radius (mm)"), 0.000001);
    radius->setValue(10);
    auto* controlX = field(tr("Control X (mm)"));
    auto* controlY = field(tr("Control Y (mm)"));
    auto* angle = field(tr("Angle (° clockwise from +X)"), -360, 360);
    QCheckBox connected(tr("Move connected endpoints in the same piece and layer"), &dialog);
    if (editing) {
        for (const auto& [spin, value] : {
                 std::pair{x1, original.a.x()}, {y1, original.a.y()},
                 {x2, original.b.x()}, {y2, original.b.y()},
                 {controlX, original.control.x()}, {controlY, original.control.y()}}) {
            spin->setRange(std::min(spin->minimum(), value), std::max(spin->maximum(), value));
            spin->setValue(value);
        }
        const QLineF line(original.a, original.b);
        length->setMaximum(std::max(length->maximum(), line.length()));
        length->setValue(line.length());
        angle->setValue(-line.angle());
        connected.setEnabled(original.piece >= 0);
        connected.setChecked(connected.isEnabled());
        form.addRow(&connected);
    }
    const QPointF shownA(x1->value(), y1->value());
    const QPointF shownB(x2->value(), y2->value());
    const QPointF shownControl(controlX->value(), controlY->value());
    const auto shownLength = length->value();
    const auto shownAngle = angle->value();

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    const auto updateFields = [&] {
        const bool line = shape->currentIndex() == 1;
        const bool polar = line && mode->currentIndex() == 1;
        const bool circle = shape->currentIndex() == 3;
        const bool curve = shape->currentIndex() == 4;
        const bool endpoint = shape->currentIndex() != 0 && !polar && !circle;
        form.setRowVisible(radius, circle);
        form.setRowVisible(controlX, curve);
        form.setRowVisible(controlY, curve);
        form.setRowVisible(mode, line);
        form.setRowVisible(x2, endpoint);
        form.setRowVisible(y2, endpoint);
        form.setRowVisible(length, polar);
        form.setRowVisible(angle, polar);
    };
    connect(shape, &QComboBox::currentIndexChanged, &dialog, updateFields);
    connect(mode, &QComboBox::currentIndexChanged, &dialog, updateFields);
    updateFields();
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto coordinate = [editing](QPointF entered, QPointF displayed, QPointF old) {
            if (!editing)
                return entered;
            return QPointF(entered.x() == displayed.x() ? old.x() : entered.x(),
                           entered.y() == displayed.y() ? old.y() : entered.y());
        };
        const auto a = coordinate({x1->value(), y1->value()}, shownA, original.a);
        auto b = coordinate({x2->value(), y2->value()}, shownB, original.b);
        const auto control = coordinate({controlX->value(), controlY->value()}, shownControl,
                                        original.control);
        if (shape->currentIndex() == 1 && mode->currentIndex() == 1) {
            const auto originalVector = original.b - original.a;
            if (editing && length->value() == shownLength && angle->value() == shownAngle) {
                b = a == original.a ? original.b : a + originalVector;
            } else {
                const double radians = angle->value() * std::numbers::pi / 180;
                b = a + QPointF(length->value() * std::cos(radians), length->value() * std::sin(radians));
            }
        }
        if (editing) {
            if (a != original.a || (original.shape != opc::Shape::Point && b != original.b)
                || (original.shape == opc::Shape::Curve && control != original.control)) {
                auto updated = opc::editGeometry(doc_, index, a, b, control, connected.isChecked());
                if (!updated) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                        tr("The edit would collapse an edge or move a connected object that cannot be edited here."));
                    return;
                }
                applyPatternEdit(std::move(*updated), dialog.windowTitle(), true);
            }
            dialog.accept();
            return;
        }
        switch (shape->currentIndex()) {
        case 0:
            addEntity({opc::Shape::Point, a, a});
            break;
        case 1:
            if (a.x() == b.x() && a.y() == b.y()) {
                QMessageBox::warning(&dialog, tr("Invalid Line"), tr("Choose two different endpoints."));
                return;
            }
            addEntity({opc::Shape::Line, a, b});
            break;
        case 2:
            if (a.x() == b.x() || a.y() == b.y()) {
                QMessageBox::warning(&dialog, tr("Invalid Rectangle"), tr("Width and height must be nonzero."));
                return;
            }
            addEntity({opc::Shape::Rectangle, a, b});
            break;
        case 3:
            addEntity({opc::Shape::Circle, a, a + QPointF(radius->value(), 0)});
            break;
        case 4:
            if (a == b) {
                QMessageBox::warning(&dialog, tr("Invalid Curve"), tr("Choose two different endpoints."));
                return;
            }
            addEntity({opc::Shape::Curve, a, b, QPointF(controlX->value(), controlY->value())});
            break;
        }
        dialog.accept();
    });
    dialog.exec();
}

MainWindow::~MainWindow() {
    // QUndoStack clears its history during destruction and would otherwise refresh the window.
    undo_.blockSignals(true);
}

bool MainWindow::confirmDiscard() {
    if (undo_.isClean())
        return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved Drawing"),
        tr("Save the changes to the current drawing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveDocument());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    event->setAccepted(confirmDiscard());
}

void MainWindow::replaceDocument(opc::PatternDocument document, const QString& path) {
    overlaySizes_.clear();
    undo_.clear();
    doc_ = std::move(document);
    currentPath_ = path;
    setWindowFilePath(path);
    view_->setEntities(doc_.entities());
    refreshObjects();
    view_->fitDrawing();
}

void MainWindow::newDocument() {
    if (confirmDiscard())
        replaceDocument({}, {});
}

void MainWindow::openDocument() {
    if (!confirmDiscard())
        return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Open pattern"), {},
                                                  tr("Patterns (*.sofit *.opcad);;All files (*)"));
    if (path.isEmpty())
        return;
    opc::PatternDocument document;
    QString error;
    if (!document.load(path, &error)) {
        QMessageBox::warning(this, tr("Open failed"), error);
        return;
    }
    replaceDocument(std::move(document), path);
}

bool MainWindow::saveDocument() {
    QString path = currentPath_;
    if (!path.endsWith(".sofit", Qt::CaseInsensitive)) {
        path = QFileDialog::getSaveFileName(this, tr("Save pattern"), path, tr("SOFIT (*.sofit)"));
        if (path.isEmpty())
            return false;
        if (!path.endsWith(".sofit", Qt::CaseInsensitive))
            path += ".sofit";
    }
    QString error;
    if (!doc_.save(path, &error)) {
        QMessageBox::warning(this, tr("Save failed"), error);
        return false;
    }
    currentPath_ = path;
    setWindowFilePath(path);
    undo_.setClean();
    return true;
}

opc::PatternDocument MainWindow::exportPattern(const QString& size, QVector<bool>& visibility,
                                                QVector<opc::PieceGeometry>& geometry) const {
    QVector<qsizetype> sourceIndices;
    auto pattern = opc::gradedPattern(doc_, size, &sourceIndices);
    for (const auto index : sourceIndices)
        visibility.append(view_->entityVisible(index));
    if (size.isEmpty())
        geometry = pieces_;
    else {
        for (qsizetype i = 0; i < pattern.pieces().size(); ++i)
            geometry.append(opc::analyzePiece(pattern, i));
    }
    return pattern;
}

void MainWindow::exportDxf() {
    QString targetSize;
    if (!doc_.production().sizes.isEmpty()) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Export DXF"));
        QFormLayout form(&dialog);
        QComboBox sizes;
        fillExportSizes(sizes, doc_);
        form.addRow(tr("Size"), &sizes);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        targetSize = sizes.currentData().toString();
    }
    auto path = QFileDialog::getSaveFileName(this, tr("Export DXF"), {}, tr("DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".dxf", Qt::CaseInsensitive))
        path += ".dxf";
    QVector<bool> visibility;
    QVector<opc::PieceGeometry> geometry;
    const auto pattern = exportPattern(targetSize, visibility, geometry);
    QString error;
    if (!opc::exportPatternDxf(path, pattern, visibility, geometry, error))
        QMessageBox::warning(this, tr("Export failed"), error);
    else
        statusBar()->showMessage(tr("DXF exported"), 3000);
}

void MainWindow::exportPdf() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export PDF for Printing"));
    QFormLayout form(&dialog);
    QComboBox sizes;
    fillExportSizes(sizes, doc_);
    if (sizes.count() > 1)
        form.addRow(tr("Size"), &sizes);
    QComboBox paper;
    paper.addItem(QStringLiteral("A4"), QPageSize::A4);
    paper.addItem(QStringLiteral("A3"), QPageSize::A3);
    form.addRow(tr("Paper"), &paper);
    QComboBox orientation;
    orientation.addItem(tr("Portrait"), QPageLayout::Portrait);
    orientation.addItem(tr("Landscape"), QPageLayout::Landscape);
    form.addRow(tr("Orientation"), &orientation);
    QLabel instructions(tr("Print at actual size (100%). Large drawings span multiple sheets with 10 mm overlap."));
    instructions.setWordWrap(true);
    form.addRow(&instructions);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto path = QFileDialog::getSaveFileName(this, tr("Export PDF for Printing"), {}, tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";

    QVector<bool> visibility;
    QVector<opc::PieceGeometry> geometry;
    const auto pattern = exportPattern(sizes.currentData().toString(), visibility, geometry);
    QVector<QPolygonF> allowances;
    for (const auto& piece : geometry)
        allowances.append(piece.allowance);

    const opc::PatternPdfOptions options{
        static_cast<QPageSize::PageSizeId>(paper.currentData().toInt()),
        static_cast<QPageLayout::Orientation>(orientation.currentData().toInt())
    };
    QString error;
    if (!opc::exportPatternPdf(path, pattern, visibility, allowances, options, error))
        QMessageBox::warning(this, tr("Export failed"), error);
    else
        statusBar()->showMessage(tr("PDF exported"), 3000);
}

void MainWindow::exportSvg() {
    auto path = QFileDialog::getSaveFileName(this, tr("Export SVG"), {}, tr("SVG (*.svg)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".svg", Qt::CaseInsensitive))
        path += ".svg";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return;
    }

    const auto& entities = doc_.entities();
    const auto isExported = [this](const opc::Entity& entity) {
        return doc_.layerVisible(entity.layer);
    };
    QRectF bounds;
    for (const auto& entity : entities) {
        if (!isExported(entity))
            continue;
        QRectF extent = QRectF(entity.a, entity.b).normalized();
        if (entity.shape == opc::Shape::Circle) {
            const auto radius = QLineF(entity.a, entity.b).length();
            extent = QRectF(entity.a - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
        }
        if (entity.shape == opc::Shape::Curve) {
            QPainterPath curve(entity.a);
            curve.quadTo(entity.control, entity.b);
            extent = curve.boundingRect();
        }
        bounds = bounds.united(extent.adjusted(-5, -5, 5, 5));
    }
    if (doc_.layerVisible(opc::Layer::Allowance)) {
        for (const auto& geometry : pieces_) {
            if (!geometry.allowance.isEmpty())
                bounds = bounds.united(geometry.allowance.boundingRect().adjusted(-5, -5, 5, 5));
        }
    }
    if (bounds.isNull())
        bounds = QRectF(-500, -350, 1000, 700);
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("svg");
    xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
    const auto number = [](qreal value) { return QString::number(value, 'g', 16); };
    const auto attribute = [&xml, &number](const QString& name, qreal value) {
        xml.writeAttribute(name, number(value));
    };
    xml.writeAttribute("width", number(bounds.width()) + "mm");
    xml.writeAttribute("height", number(bounds.height()) + "mm");
    xml.writeAttribute("viewBox", QStringLiteral("%1 %2 %3 %4").arg(
        number(bounds.x()), number(bounds.y()), number(bounds.width()), number(bounds.height())));
    for (const auto& entity : entities) {
        if (!isExported(entity))
            continue;
        switch (entity.shape) {
        case opc::Shape::Point:
            xml.writeStartElement("circle");
            attribute("cx", entity.a.x());
            attribute("cy", entity.a.y());
            attribute("r", 3);
            xml.writeAttribute("fill", "#1e64be");
            break;
        case opc::Shape::Line:
            xml.writeStartElement("line");
            attribute("x1", entity.a.x());
            attribute("y1", entity.a.y());
            attribute("x2", entity.b.x());
            attribute("y2", entity.b.y());
            break;
        case opc::Shape::Circle:
            xml.writeStartElement("circle");
            attribute("cx", entity.a.x());
            attribute("cy", entity.a.y());
            attribute("r", QLineF(entity.a, entity.b).length());
            xml.writeAttribute("fill", "none");
            break;
        case opc::Shape::Curve:
            xml.writeStartElement("path");
            xml.writeAttribute("d", QStringLiteral("M %1 %2 Q %3 %4 %5 %6")
                .arg(number(entity.a.x()), number(entity.a.y()),
                     number(entity.control.x()), number(entity.control.y()),
                     number(entity.b.x()), number(entity.b.y())));
            xml.writeAttribute("fill", "none");
            break;
        case opc::Shape::Rectangle: {
            const auto rect = QRectF(entity.a, entity.b).normalized();
            xml.writeStartElement("rect");
            attribute("x", rect.x());
            attribute("y", rect.y());
            attribute("width", rect.width());
            attribute("height", rect.height());
            xml.writeAttribute("fill", "none");
            break;
        }
        }
        if (entity.shape != opc::Shape::Point) {
            xml.writeAttribute("stroke", entity.layer == opc::Layer::Main ? "#1e64be" : "#58657d");
            if (entity.layer == opc::Layer::Construction)
                xml.writeAttribute("stroke-dasharray", "3 2");
            xml.writeAttribute("stroke-width", "0.5");
        }
        xml.writeEndElement();
    }
    if (doc_.layerVisible(opc::Layer::Allowance)) {
        for (const auto& geometry : pieces_) {
            if (geometry.allowance.isEmpty())
                continue;
            QStringList points;
            for (const auto& point : geometry.allowance)
                points.append(number(point.x()) + "," + number(point.y()));
            xml.writeStartElement("polygon");
            xml.writeAttribute("points", points.join(' '));
            xml.writeAttribute("fill", "none");
            xml.writeAttribute("stroke", "#ac5f13");
            xml.writeAttribute("stroke-width", "0.5");
            xml.writeAttribute("stroke-dasharray", "3 2");
            xml.writeEndElement();
        }
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    if (xml.hasError() || !file.commit())
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
}
