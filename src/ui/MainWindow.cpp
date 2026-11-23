#include "MainWindow.h"
#include <QAction>
#include <QFileDialog>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextStream>
#include <QWheelEvent>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal gridStep = 10.0; // Scene units are millimeters.
}

PatternView::PatternView(QGraphicsScene* scene, QWidget* parent):QGraphicsView(scene,parent) {
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
}
void PatternView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // Keep the grid visible when zoomed out while preserving its origin.
    const qreal scale = transform().m11();
    const qreal step = scale >= 0.8 ? gridStep : scale >= 0.16 ? 5 * gridStep : 25 * gridStep;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(215, 215, 215), 0));
    for (qreal x = std::ceil(rect.left() / step) * step; x <= rect.right(); x += step)
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = std::ceil(rect.top() / step) * step; y <= rect.bottom(); y += step)
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    painter->restore();
}

void PatternView::setSnapToGrid(bool enabled) {
    snapToGrid_ = enabled;
    updatePreview();
}

QPointF PatternView::drawingPoint(QPointF scenePoint) const {
    if (!snapToGrid_)
        return scenePoint;

    return QPointF(std::round(scenePoint.x() / gridStep) * gridStep,
                   std::round(scenePoint.y() / gridStep) * gridStep);
}

void PatternView::updateCursorPosition(const QPoint& viewportPoint) {
    cursorScenePosition_ = mapToScene(viewportPoint);
    updatePreview();
    emit cursorPositionChanged(cursorScenePosition_);
}

void PatternView::updatePreview() {
    if (preview_)
        preview_->setLine(QLineF(first_, drawingPoint(cursorScenePosition_)));
}

bool PatternView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave)
        emit cursorLeftCanvas();

    return QGraphicsView::viewportEvent(event);
}

void PatternView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    constexpr qreal minScale = 0.1;
    constexpr qreal maxScale = 10.0;
    const qreal currentScale = transform().m11();
    const qreal nextScale = std::clamp(
        currentScale * std::pow(1.2, delta / 120.0), minScale, maxScale);
    if (nextScale != currentScale) {
        const qreal factor = nextScale / currentScale;
        const QPoint cursor = event->position().toPoint();
        const QPointF scenePoint = mapToScene(cursor);
        scale(factor, factor);
        moveView(scenePoint, cursor);
    }
    updateCursorPosition(event->position().toPoint());
    event->accept();
}

void PatternView::setDocument(opc::PatternDocument* doc) { doc_=doc; refresh(); }
void PatternView::refresh() {
    pending_=false; preview_=nullptr;
    scene()->clear();
    QPen pen(QColor(30,100,190)); pen.setWidthF(0);
    for(const auto& s: doc_->segments()) scene()->addLine(QLineF(s.a,s.b),pen);
}
void PatternView::moveView(const QPointF& scenePoint, const QPoint& viewportPoint) {
    // QGraphicsView can only scroll within its view-specific scene rectangle.
    const QRect fullViewport(QPoint(0, 0), maximumViewportSize());
    const QRectF visible = mapToScene(fullViewport).boundingRect();
    const QRectF target = visible.translated(scenePoint - mapToScene(viewportPoint));
    const QRectF bounds = sceneRect();
    const qreal margin = 2 / transform().m11();
    const QRectF paddedTarget = target.adjusted(-margin, -margin, margin, margin);
    if (!bounds.contains(paddedTarget))
        setSceneRect(bounds.united(paddedTarget));

    const QPoint offset = mapFromScene(scenePoint) - viewportPoint;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + offset.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + offset.y());
}

void PatternView::mousePressEvent(QMouseEvent* event) {
    updateCursorPosition(event->pos());
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPanPosition_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::RightButton) {
        pending_ = false;
        delete preview_;
        preview_ = nullptr;
    } else if (event->button() == Qt::LeftButton && doc_) {
        if (!panning_) {
            const QPointF point = drawingPoint(cursorScenePosition_);
            if (!pending_) {
                first_ = point;
                pending_ = true;
                preview_ = scene()->addLine(QLineF(first_, point), QPen(Qt::gray, 0, Qt::DashLine));
            } else {
                doc_->addSegment(first_, point);
                refresh();
            }
        }
    } else {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    event->accept();
}

void PatternView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        if (event->pos() != lastPanPosition_) {
            moveView(mapToScene(lastPanPosition_), event->pos());
            lastPanPosition_ = event->pos();
        }
    }

    updateCursorPosition(event->pos());

    if (panning_)
        event->accept();
    else
        QGraphicsView::mouseMoveEvent(event);
}

void PatternView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}
MainWindow::MainWindow():view_(new PatternView(&scene_,this)) {
    setWindowTitle("SOFIT 0.3"); resize(1100,750);
    scene_.setSceneRect(-500,-350,1000,700);
    setCentralWidget(view_); view_->setDocument(&doc_);
    auto* file=menuBar()->addMenu(tr("&File"));
    auto add=[&](const QString& label, const QKeySequence& shortcut, auto callback) {
        auto* action=file->addAction(label); action->setShortcut(shortcut); connect(action,&QAction::triggered,this,callback);
    };
    add(tr("&New"),QKeySequence::New,[this]{newDocument();});
    add(tr("&Open"),QKeySequence::Open,[this]{openDocument();});
    add(tr("&Save"),QKeySequence::Save,[this]{saveDocument();});
    add(tr("Export &SVG"),QKeySequence(),[this]{exportSvg();});
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* snapAction = viewMenu->addAction(tr("Snap to Grid"));
    snapAction->setCheckable(true);
    connect(snapAction, &QAction::toggled, view_, &PatternView::setSnapToGrid);

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

    statusBar()->showMessage(tr("Left click: first/second endpoint | Right click: cancel | Mouse wheel: zoom | Middle drag: pan | Units: mm"));
}
void MainWindow::newDocument() { doc_.clear(); currentPath_.clear(); view_->refresh(); }
void MainWindow::openDocument() {
    auto path=QFileDialog::getOpenFileName(this,tr("Open pattern"),{},tr("SOFIT (*.sofit);;Legacy OpenPattern (*.opcad)"));
    if(path.isEmpty()) return;
    QString error;
    if(!doc_.load(path,&error)) { QMessageBox::warning(this,tr("Open failed"),error); return; }
    currentPath_=path; view_->refresh();
}
void MainWindow::saveDocument() {
    auto path=currentPath_.isEmpty()?QFileDialog::getSaveFileName(this,tr("Save pattern"),{},tr("SOFIT (*.sofit)")):currentPath_;
    if(path.isEmpty()) return;
    if(!path.endsWith(".sofit",Qt::CaseInsensitive)) path+=".sofit";
    QString error;
    if(!doc_.save(path,&error)) { QMessageBox::warning(this,tr("Save failed"),error); return; }
    currentPath_=path;
}
void MainWindow::exportSvg() {
    auto path=QFileDialog::getSaveFileName(this,tr("Export SVG"),{},tr("SVG (*.svg)"));
    if(path.isEmpty()) return;
    if(!path.endsWith(".svg",Qt::CaseInsensitive)) path+=".svg";
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)) { QMessageBox::warning(this,tr("Export failed"),file.errorString()); return; }
    QXmlStreamWriter xml(&file); xml.setAutoFormatting(true); xml.writeStartDocument();
    xml.writeStartElement("svg"); xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
    xml.writeAttribute("width","1000mm"); xml.writeAttribute("height","700mm");
    xml.writeAttribute("viewBox","-500 -350 1000 700");
    for(const auto& s:doc_.segments()) {
        xml.writeStartElement("line");
        xml.writeAttribute("x1",QString::number(s.a.x(),'g',16)); xml.writeAttribute("y1",QString::number(s.a.y(),'g',16));
        xml.writeAttribute("x2",QString::number(s.b.x(),'g',16)); xml.writeAttribute("y2",QString::number(s.b.y(),'g',16));
        xml.writeAttribute("stroke","#1e64be"); xml.writeAttribute("stroke-width","0.5"); xml.writeEndElement();
    }
    xml.writeEndElement(); xml.writeEndDocument();
}
