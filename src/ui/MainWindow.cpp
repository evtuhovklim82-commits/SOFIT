#include "MainWindow.h"
#include "PatternView.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "PieceMarkings.h"
#include "Production.h"
#include "ProductionDialogs.h"
#include "EntityPath.h"
#include <QPixmap>
#include <QCheckBox>
#include <QSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
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

namespace {
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
    PatternCommand(opc::PatternDocument& document, opc::PatternDocument after, const QString& text)
        : QUndoCommand(text), document_(document), before_(document), after_(std::move(after)) {}

    void undo() override { document_ = before_; }
    void redo() override { document_ = after_; }

private:
    opc::PatternDocument& document_;
    opc::PatternDocument before_, after_;
};

class MoveCommand : public QUndoCommand {
public:
    MoveCommand(opc::PatternDocument& document, PatternView& view, qsizetype index,
                const QPointF& offset, const QString& text)
        : QUndoCommand(text), document_(document), view_(view), index_(index),
          before_(document.entities()[index]), after_(before_) {
        after_.a += offset;
        after_.b += offset;
        if (after_.shape == opc::Shape::Curve)
            after_.control += offset;
    }

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
    setWindowTitle(QStringLiteral("SOFIT 0.7[*]"));
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
    pattern->addAction(tr("Sizes, Measurements and Grade Rules…"), this, [this] {
        if (auto production = editGrading(this, doc_)) {
            if (opc::saveProduction(*production) == opc::saveProduction(doc_.production()))
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

void MainWindow::applyPatternEdit(opc::PatternDocument document, const QString& text) {
    const auto selected = selectedObjects();
    view_->cancelDrawing();
    undo_.push(new PatternCommand(doc_, std::move(document), text));
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

void MainWindow::managePiece() {
    const auto piece = choosePiece(tr("Piece, Contour and Allowance"));
    if (piece < 0)
        return;

    const auto& original = doc_.pieces()[piece];
    const auto& geometry = pieces_[piece];
    QString contour;
    using opc::ContourError;
    switch (geometry.error) {
    case ContourError::None: contour = tr("One closed contour"); break;
    case ContourError::Empty: contour = tr("No objects on the main layer"); break;
    case ContourError::UnsupportedShape: contour = tr("Use lines, rectangles or quadratic curves on the main layer"); break;
    case ContourError::Degenerate: contour = tr("A zero-length or folded edge"); break;
    case ContourError::Open: contour = tr("Open ends"); break;
    case ContourError::Branched: contour = tr("Multiple edges meet at an endpoint"); break;
    case ContourError::Disconnected: contour = tr("More than one closed contour"); break;
    case ContourError::SelfIntersecting: contour = tr("Contour intersects, overlaps, or folds onto itself"); break;
    case ContourError::UncertainCurves: contour = tr("Curves are too close to verify a simple contour"); break;
    case ContourError::TooComplex: contour = tr("Contour exceeds the geometry limits"); break;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Piece, Contour and Allowance"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(original.name, &dialog);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Contour check"), new QLabel(contour, &dialog));
    if (geometry.error == ContourError::None) {
        const QLocale locale;
        form->addRow(tr("Perimeter"), new QLabel(tr("%1 mm").arg(locale.toString(geometry.perimeterMm, 'f', 2)), &dialog));
        form->addRow(tr("Width × height"), new QLabel(tr("%1 × %2 mm").arg(
            locale.toString(geometry.bounds.width(), 'f', 2),
            locale.toString(geometry.bounds.height(), 'f', 2)), &dialog));
    }
    if (original.allowanceMm > 0 && (geometry.error != ContourError::None || geometry.allowanceFailed))
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
    undo_.push(new MoveCommand(doc_, *view_, index, offset, tr("Move Object")));
}

void MainWindow::addEntity(const opc::Entity& entity) {
    undo_.push(new EntityCommand(doc_, *view_, doc_.entities().size(), entity, true, tr("Add Object")));
}

void MainWindow::deleteSelection() {
    auto selected = selectedObjects();
    if (selected.isEmpty())
        return;
    std::sort(selected.begin(), selected.end(), std::greater<>());
    undo_.beginMacro(tr("Delete Objects"));
    for (const auto index : selected)
        undo_.push(new EntityCommand(doc_, *view_, index, doc_.entities().at(index), false, tr("Delete Object")));
    undo_.endMacro();
}

void MainWindow::exactInput() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Exact Input"));
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
        const QPointF a(x1->value(), y1->value());
        QPointF b(x2->value(), y2->value());
        switch (shape->currentIndex()) {
        case 0:
            addEntity({opc::Shape::Point, a, a});
            break;
        case 1:
            if (mode->currentIndex() == 1) {
                const double radians = angle->value() * std::numbers::pi / 180;
                b = a + QPointF(length->value() * std::cos(radians), length->value() * std::sin(radians));
            }
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
