#include "PatternView.h"
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal gridStep = 10.0;
const QColor drawingColor(30, 100, 190);
const QRectF initialBounds(-500, -350, 1000, 700);

QPainterPath entityPath(const opc::Entity& entity) {
    QPainterPath path;
    switch (entity.shape) {
    case opc::Shape::Point:
        path.addEllipse(QPointF(), 3, 3);
        break;
    case opc::Shape::Line:
        if (entity.a.x() == entity.b.x() && entity.a.y() == entity.b.y())
            path.addEllipse(entity.a, 0.25, 0.25);
        else {
            path.moveTo(entity.a);
            path.lineTo(entity.b);
        }
        break;
    case opc::Shape::Rectangle:
        path.addRect(QRectF(entity.a, entity.b).normalized());
        break;
    }
    return path;
}
}

PatternView::PatternView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setSceneRect(initialBounds);
}

void PatternView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // Keep the grid visible when zoomed out while preserving its origin.
    const qreal scale = transform().m11();
    qreal step = gridStep;
    while (step * scale < 8)
        step *= 5;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(QColor(215, 215, 215), 0));
    for (qreal x = std::ceil(rect.left() / step) * step; x <= rect.right() && x + step > x; x += step)
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = std::ceil(rect.top() / step) * step; y <= rect.bottom() && y + step > y; y += step)
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

bool PatternView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave)
        emit cursorLeftCanvas();

    return QGraphicsView::viewportEvent(event);
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

void PatternView::setTool(Tool tool) {
    cancelDrawing();
    scene()->clearSelection();
    tool_ = tool;
}

void PatternView::cancelDrawing() {
    start_.reset();
    delete preview_;
    preview_ = nullptr;
}

void PatternView::updatePreview() {
    if (preview_) {
        const auto shape = tool_ == Tool::Line ? opc::Shape::Line : opc::Shape::Rectangle;
        preview_->setPath(entityPath({shape, *start_, drawingPoint(cursorScenePosition_)}));
    }
}

void PatternView::setEntities(const QVector<opc::Entity>& entities) {
    cancelDrawing();
    scene()->clear();
    items_.clear();
    for (const auto& entity : entities)
        insertEntity(items_.size(), entity);
    fitDrawing();
}

void PatternView::insertEntity(qsizetype index, const opc::Entity& entity) {
    QPen pen(drawingColor, 0);
    QBrush brush = Qt::NoBrush;
    if (entity.shape == opc::Shape::Point)
        brush = drawingColor;
    auto* item = scene()->addPath(entityPath(entity), pen, brush);
    item->setFlag(QGraphicsItem::ItemIsSelectable);
    if (entity.shape == opc::Shape::Point) {
        item->setPos(entity.a);
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    }
    if (index < items_.size())
        item->stackBefore(items_[index]);
    items_.insert(index, item);
}

void PatternView::removeEntity(qsizetype index) {
    delete items_.takeAt(index);
}

qsizetype PatternView::selectedIndex() const {
    const auto selection = scene()->selectedItems();
    if (selection.isEmpty())
        return -1;
    return items_.indexOf(static_cast<QGraphicsPathItem*>(selection.front()));
}

void PatternView::fitDrawing() {
    if (items_.isEmpty()) {
        resetTransform();
        setSceneRect(initialBounds);
        centerOn(0, 0);
        return;
    }

    QRectF bounds = items_.front()->sceneBoundingRect();
    for (const auto* item : items_)
        bounds = bounds.united(item->sceneBoundingRect());
    bounds = bounds.adjusted(-15, -15, 15, 15);
    const auto viewportSize = maximumViewportSize() - QSize(12, 12);
    const qreal fitScale = std::min(viewportSize.width() / bounds.width(),
                                    viewportSize.height() / bounds.height());
    const qreal scale = std::clamp(fitScale, 0.000001, 10.0);
    setTransform(QTransform::fromScale(scale, scale));
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const QSizeF size = visible.size() * 1.1;
    setSceneRect(bounds.united(QRectF(bounds.center() - QPointF(size.width() / 2, size.height() / 2), size)));
    centerOn(bounds.center());
}

void PatternView::zoomAtCenter(qreal factor) {
    zoomAt(factor, viewport()->rect().center());
}

void PatternView::zoomAt(qreal factor, const QPoint& anchor) {
    const qreal current = transform().m11();
    const qreal next = std::clamp(current * factor, 0.000001, 10.0);
    if (current != next) {
        const auto sceneAnchor = mapToScene(anchor);
        scale(next / current, next / current);
        moveView(sceneAnchor, anchor);
    }
    updateCursorPosition(anchor);
}

void PatternView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (!delta) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    zoomAt(std::pow(1.2, delta / 120.0), event->position().toPoint());
    event->accept();
}

void PatternView::mousePressEvent(QMouseEvent* event) {
    updateCursorPosition(event->pos());
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastPanPosition_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::RightButton) {
        cancelDrawing();
    } else if (event->button() == Qt::LeftButton && !panning_) {
        const QPointF point = drawingPoint(cursorScenePosition_);
        if (tool_ == Tool::Select) {
            scene()->clearSelection();
            const QRect hit(event->pos() - QPoint(5, 5), QSize(11, 11));
            for (auto* item : items(hit)) {
                if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
                    item->setSelected(true);
                    break;
                }
            }
        } else if (tool_ == Tool::Point) {
            emit entityDrawn({opc::Shape::Point, point, point});
        } else if (!start_) {
            start_ = point;
            preview_ = scene()->addPath({}, QPen(Qt::gray, 0, Qt::DashLine));
            updatePreview();
        } else {
            const bool line = tool_ == Tool::Line;
            const bool differentX = start_->x() != point.x();
            const bool differentY = start_->y() != point.y();
            if (line ? differentX || differentY : differentX && differentY) {
                const auto a = *start_;
                cancelDrawing();
                emit entityDrawn({line ? opc::Shape::Line : opc::Shape::Rectangle, a, point});
            }
        }
    } else {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    event->accept();
}
