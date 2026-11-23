#include "MainWindow.h"
#include "PatternView.h"
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHeaderView>
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
#include <cmath>
#include <numbers>

namespace {
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
    setWindowTitle(QStringLiteral("SOFIT 0.3[*]"));
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
    dock->setMinimumWidth(220);
    objects_ = new QTreeWidget(dock);
    objects_->setHeaderLabels({tr("Object"), tr("Visible")});
    objects_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    objects_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    objects_->setRootIsDecorated(false);
    objects_->setAlternatingRowColors(true);
    dock->setWidget(objects_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(objects_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = objects_->selectedItems();
        view_->selectEntity(selected.isEmpty() ? -1 : objects_->indexOfTopLevelItem(selected.front()));
    });
    connect(objects_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 1)
            view_->setEntityVisible(objects_->indexOfTopLevelItem(item), item->checkState(1) == Qt::Checked);
    });
    connect(&scene_, &QGraphicsScene::selectionChanged, this, [this] {
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
    connect(&scene_, &QGraphicsScene::selectionChanged, this,
            [this, deleteAction] { deleteAction->setEnabled(!scene_.selectedItems().isEmpty()); });
    connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) { setWindowModified(!clean); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(dock->toggleViewAction());
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

void MainWindow::refreshObjects() {
    const QSignalBlocker blocker(objects_);
    objects_->clear();
    const auto selected = view_->selectedIndex();
    for (qsizetype index = 0; index < doc_.entities().size(); ++index) {
        QString name;
        switch (doc_.entities()[index].shape) {
        case opc::Shape::Point: name = tr("Point"); break;
        case opc::Shape::Line: name = tr("Line"); break;
        case opc::Shape::Rectangle: name = tr("Rectangle"); break;
        case opc::Shape::Circle: name = tr("Circle"); break;
        case opc::Shape::Curve: name = tr("Curve"); break;
        }
        auto* item = new QTreeWidgetItem(objects_, {tr("%1 · %2").arg(index + 1).arg(name)});
        item->setCheckState(1, view_->entityVisible(index) ? Qt::Checked : Qt::Unchecked);
        if (index == selected)
            objects_->setCurrentItem(item);
    }
}

void MainWindow::moveEntity(qsizetype index, const QPointF& offset) {
    undo_.push(new MoveCommand(doc_, *view_, index, offset, tr("Move Object")));
}

void MainWindow::addEntity(const opc::Entity& entity) {
    undo_.push(new EntityCommand(doc_, *view_, doc_.entities().size(), entity, true, tr("Add Object")));
}

void MainWindow::deleteSelection() {
    const auto index = view_->selectedIndex();
    if (index >= 0)
        undo_.push(new EntityCommand(doc_, *view_, index, doc_.entities().at(index), false, tr("Delete Object")));
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
    undo_.clear();
    doc_ = std::move(document);
    currentPath_ = path;
    setWindowFilePath(path);
    view_->setEntities(doc_.entities());
    refreshObjects();
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
    QRectF bounds;
    for (const auto& entity : entities) {
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
    if (entities.isEmpty())
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
            xml.writeAttribute("stroke", "#1e64be");
            xml.writeAttribute("stroke-width", "0.5");
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    if (xml.hasError() || !file.commit())
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
}
