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
const QColor drawingColor(118, 208, 230);
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
    case opc::Shape::Circle: {
        const qreal radius = QLineF(entity.a, entity.b).length();
        path.addEllipse(entity.a, radius, radius);
        break;
    }
    case opc::Shape::Curve:
        path.moveTo(entity.a);
        path.quadTo(entity.control, entity.b);
        break;
    }
    return path;
}
}

PatternView::PatternView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setBackgroundBrush(QColor("#141a22"));
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
    painter->setPen(QPen(QColor("#263340"), 0));
    for (qreal x = std::ceil(rect.left() / step) * step; x <= rect.right() && x + step > x; x += step)
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = std::ceil(rect.top() / step) * step; y <= rect.bottom() && y + step > y; y += step)
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    painter->setPen(QPen(QColor("#536478"), 0));
    painter->drawLine(QLineF(0, rect.top(), 0, rect.bottom()));
    painter->drawLine(QLineF(rect.left(), 0, rect.right(), 0));
    painter->restore();
}

void PatternView::setSnapToGrid(bool enabled) {
    snapToGrid_ = enabled;
    updatePreview();
}

void PatternView::setSnapToObjects(bool enabled) {
    snapToObjects_ = enabled;
    updatePreview();
}

QPointF PatternView::drawingPoint(QPointF scenePoint) const {
    if (snapToObjects_) {
        QPointF best;
        qreal distance = 9 / transform().m11();
        const auto consider = [&](const QPointF& candidate) {
            const qreal candidateDistance = QLineF(scenePoint, candidate).length();
            if (candidateDistance < distance) {
                distance = candidateDistance;
                best = candidate;
            }
        };
        QVector<QLineF> nearbyEdges;
        const QRectF region(scenePoint - QPointF(distance, distance),
                            QSizeF(2 * distance, 2 * distance));
        const auto addEdge = [&](const QPointF& a, const QPointF& b) {
            const QRectF bounds = QRectF(a, b).normalized();
            if (bounds.left() <= region.right() && bounds.right() >= region.left()
                && bounds.top() <= region.bottom() && bounds.bottom() >= region.top())
                nearbyEdges.append(QLineF(a, b));
        };
        for (qsizetype i = 0; i < entities_.size(); ++i) {
            if (i == dragIndex_ || !items_[i]->isVisible())
                continue;
            const auto& entity = entities_[i];
            consider(entity.a);
            consider(entity.b);
            if (entity.shape == opc::Shape::Line)
                addEdge(entity.a, entity.b);
            if (entity.shape == opc::Shape::Rectangle) {
                const QPointF c(entity.a.x(), entity.b.y());
                const QPointF d(entity.b.x(), entity.a.y());
                consider(c);
                consider(d);
                addEdge(entity.a, c);
                addEdge(c, entity.b);
                addEdge(entity.b, d);
                addEdge(d, entity.a);
            }
        }
        for (qsizetype i = 0; i < nearbyEdges.size(); ++i) {
            for (qsizetype j = 0; j < i; ++j) {
                QPointF intersection;
                if (nearbyEdges[i].intersects(nearbyEdges[j], &intersection)
                    == QLineF::BoundedIntersection)
                    consider(intersection);
            }
        }
        if (distance < 9 / transform().m11())
            return best;
    }
    if (!snapToGrid_)
        return scenePoint;

    return QPointF(std::round(scenePoint.x() / gridStep) * gridStep,
                   std::round(scenePoint.y() / gridStep) * gridStep);
}

void PatternView::updateCursorPosition(const QPoint& viewportPoint) {
    cursorScenePosition_ = mapToScene(viewportPoint);
    updatePreview();
    emit cursorPositionChanged(drawingPoint(cursorScenePosition_));
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

    if (dragStart_ && (event->buttons() & Qt::LeftButton)) {
        const auto offset = drawingPoint(cursorScenePosition_) - *dragStart_;
        const auto& entity = entities_[dragIndex_];
        items_[dragIndex_]->setPos(entity.shape == opc::Shape::Point ? entity.a + offset : offset);
        event->accept();
    } else if (panning_)
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
    if (event->button() == Qt::LeftButton && dragStart_) {
        const bool dragged = (event->pos() - dragPressPosition_).manhattanLength() >= 3;
        const auto offset = dragged ? drawingPoint(mapToScene(event->pos())) - *dragStart_ : QPointF();
        const auto index = dragIndex_;
        cancelDrawing();
        if (!offset.isNull())
            emit entityMoved(index, offset);
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
    if (dragStart_) {
        const auto& entity = entities_[dragIndex_];
        items_[dragIndex_]->setPos(entity.shape == opc::Shape::Point ? entity.a : QPointF());
        dragStart_.reset();
        dragIndex_ = -1;
    }
    start_.reset();
    curveEnd_.reset();
    delete preview_;
    preview_ = nullptr;
}

void PatternView::updatePreview() {
    if (preview_) {
        preview_->setPath(entityPath(draftEntity(drawingPoint(cursorScenePosition_))));
    }
}

opc::Entity PatternView::draftEntity(const QPointF& point) const {
    using opc::Shape;
    switch (tool_) {
    case Tool::Rectangle: return {Shape::Rectangle, *start_, point};
    case Tool::Circle: return {Shape::Circle, *start_, point};
    case Tool::Curve:
        if (curveEnd_)
            return {Shape::Curve, *start_, *curveEnd_, point};
        break;
    default: break;
    }
    return {Shape::Line, *start_, point};
}

void PatternView::setEntities(const QVector<opc::Entity>& entities) {
    cancelDrawing();
    items_.clear();
    entities_.clear();
    scene()->clear();
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
    entities_.insert(index, entity);
}

void PatternView::removeEntity(qsizetype index) {
    auto* item = items_.takeAt(index);
    entities_.removeAt(index);
    delete item;
}

void PatternView::replaceEntity(qsizetype index, const opc::Entity& entity) {
    const bool selected = items_[index]->isSelected();
    auto* item = items_[index];
    entities_[index] = entity;
    item->setPath(entityPath(entity));
    item->setPos(entity.shape == opc::Shape::Point ? entity.a : QPointF());
    item->setSelected(selected);
}

void PatternView::selectEntity(qsizetype index) {
    scene()->clearSelection();
    if (index >= 0 && index < items_.size() && items_[index]->isVisible()) {
        items_[index]->setSelected(true);
        ensureVisible(items_[index]);
    }
}

bool PatternView::entityVisible(qsizetype index) const {
    return items_[index]->isVisible();
}

void PatternView::setEntityVisible(qsizetype index, bool visible) {
    cancelDrawing();
    items_[index]->setVisible(visible);
}

qsizetype PatternView::selectedIndex() const {
    const auto selection = scene()->selectedItems();
    if (selection.isEmpty())
        return -1;
    return items_.indexOf(static_cast<QGraphicsPathItem*>(selection.front()));
}

void PatternView::fitDrawing() {
    QRectF bounds;
    for (const auto* item : items_) {
        if (item->isVisible())
            bounds = bounds.united(item->sceneBoundingRect());
    }
    if (bounds.isNull()) {
        resetTransform();
        setSceneRect(initialBounds);
        centerOn(0, 0);
        emit zoomChanged(transform().m11());
        return;
    }

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
    emit zoomChanged(scale);
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
        emit zoomChanged(next);
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
                    dragPressPosition_ = event->pos();
                    dragIndex_ = selectedIndex();
                    dragStart_ = drawingPoint(cursorScenePosition_);
                    break;
                }
            }
        } else if (tool_ == Tool::Point) {
            emit entityDrawn({opc::Shape::Point, point, point});
        } else if (!start_) {
            start_ = point;
            preview_ = scene()->addPath({}, QPen(QColor("#ffc976"), 0, Qt::DashLine));
            updatePreview();
        } else {
            const auto entity = draftEntity(point);
            if (tool_ == Tool::Curve && !curveEnd_ && *start_ != point) {
                curveEnd_ = point;
                updatePreview();
            } else if (tool_ == Tool::Measure) {
                const qreal distance = QLineF(*start_, point).length();
                cancelDrawing();
                emit measured(distance);
            } else if (entity.a != entity.b
                       && (entity.shape != opc::Shape::Rectangle
                           || (entity.a.x() != entity.b.x() && entity.a.y() != entity.b.y()))) {
                cancelDrawing();
                emit entityDrawn(entity);
            }
        }
    } else {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    event->accept();
}
