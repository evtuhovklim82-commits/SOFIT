#include "Modeling.h"
#include "EntityPath.h"
#include "PatternView.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsPathItem>
#include <QRubberBand>
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
const QColor constructionColor(161, 173, 200);
const QColor allowanceColor(255, 185, 104);
const QRectF initialBounds(-500, -350, 1000, 700);

QPointF closestCurvePoint(const opc::Entity& edge, QPointF target) {
    const auto at = [&edge](double t) {
        const auto u = 1 - t;
        return u * u * edge.a + 2 * u * t * edge.control + t * t * edge.b;
    };
    const auto distanceSquared = [&](double t) {
        const auto delta = at(t) - target;
        return QPointF::dotProduct(delta, delta);
    };
    constexpr int samples = 16;
    int best = 0;
    for (int i = 1; i <= samples; ++i) {
        if (distanceSquared(double(i) / samples) < distanceSquared(double(best) / samples))
            best = i;
    }
    double left = double(std::max(0, best - 1)) / samples;
    double right = double(std::min(samples, best + 1)) / samples;
    for (int i = 0; i < 24; ++i) {
        const auto a = std::lerp(left, right, 1.0 / 3);
        const auto b = std::lerp(left, right, 2.0 / 3);
        if (distanceSquared(a) < distanceSquared(b))
            right = b;
        else
            left = a;
    }
    return at((left + right) / 2);
}

QPainterPath viewEntityPath(opc::Entity entity) {
    if (entity.shape == opc::Shape::Point)
        entity.a = {};
    return opc::entityPath(entity);
}
}

PatternView::PatternView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent),
      selectionBand_(new QRubberBand(QRubberBand::Rectangle, viewport())) {
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
            if (entity.shape == opc::Shape::Curve) {
                const auto bounds = viewEntityPath(entity).controlPointRect();
                if (bounds.left() <= region.right() && bounds.right() >= region.left()
                    && bounds.top() <= region.bottom() && bounds.bottom() >= region.top())
                    consider(closestCurvePoint(entity, scenePoint));
            }
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

    if (selectionStart_ && (event->buttons() & Qt::LeftButton)) {
        if ((event->pos() - *selectionStart_).manhattanLength() >= QApplication::startDragDistance()) {
            selectionBand_->setGeometry(QRect(*selectionStart_, event->pos()).normalized());
            selectionBand_->show();
        }
        event->accept();
    } else if (dragStart_ && (event->buttons() & Qt::LeftButton)) {
        if ((event->pos() - dragPressPosition_).manhattanLength() < QApplication::startDragDistance())
            return;
        const auto offset = drawingPoint(cursorScenePosition_) - *dragStart_;
        const auto& entity = entities_[dragIndex_];
        items_[dragIndex_]->setPos(entity.shape == opc::Shape::Point ? entity.a + offset : offset);
        if (!entity.draftName.isEmpty())
            emit draftDragPreviewRequested(dragIndex_, offset);
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
    if (event->button() == Qt::LeftButton && selectionStart_) {
        const QPoint origin = *selectionStart_;
        cancelDrawing();
        if ((event->pos() - origin).manhattanLength() >= QApplication::startDragDistance()) {
            const auto mode = event->pos().x() < origin.x() ? Qt::IntersectsItemShape : Qt::ContainsItemShape;
            QPainterPath area;
            area.addPolygon(mapToScene(QRect(origin, event->pos()).normalized()));
            area.closeSubpath();
            scene()->setSelectionArea(area, Qt::AddToSelection, mode, viewportTransform());
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragStart_) {
        const bool dragged = (event->pos() - dragPressPosition_).manhattanLength()
            >= QApplication::startDragDistance();
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
    selectionStart_.reset();
    selectionBand_->hide();
    if (dragStart_) {
        const auto& entity = entities_[dragIndex_];
        items_[dragIndex_]->setPos(entity.shape == opc::Shape::Point ? entity.a : QPointF());
        dragStart_.reset();
        dragIndex_ = -1;
    }
    start_.reset();
    secondPoint_.reset();
    delete draftDragPreview_;
    draftDragPreview_ = nullptr;
    delete preview_;
    preview_ = nullptr;
}

void PatternView::updatePreview() {
    if (preview_) {
        const auto point = drawingPoint(cursorScenePosition_);
        if (tool_ == Tool::Dart && secondPoint_) {
            QPainterPath legs(*start_);
            legs.lineTo(*secondPoint_);
            legs.lineTo(point);
            preview_->setPath(legs);
        } else {
            preview_->setPath(viewEntityPath(draftEntity(point)));
        }
    }
}

opc::Entity PatternView::draftEntity(const QPointF& point) const {
    using opc::Shape;
    switch (tool_) {
    case Tool::Rectangle: return {Shape::Rectangle, *start_, point};
    case Tool::Circle: return {Shape::Circle, *start_, point};
    case Tool::Curve:
        if (secondPoint_)
            return {Shape::Curve, *start_, *secondPoint_, point};
        break;
    default: break;
    }
    return {Shape::Line, *start_, point};
}

void PatternView::previewDraftDrag(const QVector<opc::Entity>& entities) {
    if (!draftDragPreview_) {
        draftDragPreview_ = scene()->addPath({}, QPen(QColor("#ffcc80"), 0, Qt::DashLine));
        draftDragPreview_->setZValue(2);
    }
    QPainterPath changed;
    for (qsizetype i = 0; i < std::min(entities.size(), entities_.size()); ++i) {
        const auto& entity = entities[i];
        const auto& original = entities_[i];
        if (entity.shape != opc::Shape::Point && items_[i]->isVisible()
            && entity.draftName == original.draftName
            && (entity.a != original.a || entity.b != original.b || entity.control != original.control))
            changed.addPath(viewEntityPath(entity));
    }
    draftDragPreview_->setPath(changed);
}

void PatternView::setEntities(const QVector<opc::Entity>& entities) {
    cancelDrawing();
    items_.clear();
    entities_.clear();
    entityVisibility_.clear();
    allowances_.clear();
    sizeOverlays_.clear();
    scene()->clear();
    for (const auto& entity : entities)
        insertEntity(items_.size(), entity);
}

void PatternView::insertEntity(qsizetype index, const opc::Entity& entity) {
    auto* item = scene()->addPath(viewEntityPath(entity));
    item->setFlag(QGraphicsItem::ItemIsSelectable);
    if (entity.shape == opc::Shape::Point) {
        item->setPos(entity.a);
        item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    }
    if (index < items_.size())
        item->stackBefore(items_[index]);
    items_.insert(index, item);
    entities_.insert(index, entity);
    entityVisibility_.insert(index, true);
    updateEntityStyle(index);
}

void PatternView::removeEntity(qsizetype index) {
    auto* item = items_.takeAt(index);
    entities_.removeAt(index);
    entityVisibility_.removeAt(index);
    delete item;
}

void PatternView::replaceEntity(qsizetype index, const opc::Entity& entity) {
    const bool selected = items_[index]->isSelected();
    auto* item = items_[index];
    entities_[index] = entity;
    item->setPath(viewEntityPath(entity));
    item->setPos(entity.shape == opc::Shape::Point ? entity.a : QPointF());
    updateEntityStyle(index);
    item->setSelected(selected && item->isVisible());
}

void PatternView::selectEntities(const QVector<qsizetype>& indices) {
    for (qsizetype i = 0; i < items_.size(); ++i)
        items_[i]->setSelected(indices.contains(i) && items_[i]->isVisible());
    if (indices.size() == 1) {
        const auto index = indices.first();
        if (index >= 0 && index < items_.size() && items_[index]->isVisible())
            ensureVisible(items_[index]);
    }
}

bool PatternView::entityVisible(qsizetype index) const {
    return entityVisibility_[index];
}

void PatternView::setEntityVisible(qsizetype index, bool visible) {
    cancelDrawing();
    entityVisibility_[index] = visible;
    updateEntityStyle(index);
}

void PatternView::updateEntityStyle(qsizetype index) {
    const auto& entity = entities_[index];
    const bool construction = entity.layer == opc::Layer::Construction;
    const bool handle = entity.shape == opc::Shape::Point && !entity.draftName.isEmpty();
    const auto color = handle ? QColor("#ffcc80") : construction ? constructionColor : drawingColor;
    items_[index]->setToolTip(handle ? tr("Drag draft point %1").arg(entity.draftName) : QString());
    items_[index]->setPen(QPen(color, 0, construction ? Qt::DashLine : Qt::SolidLine));
    const bool point = entity.shape == opc::Shape::Point;
    items_[index]->setBrush(point ? QBrush(color) : QBrush(Qt::NoBrush));
    items_[index]->setZValue(point ? 1 : 0);
    items_[index]->setVisible(entityVisibility_[index] && (construction ? constructionVisible_ : mainVisible_));
}

void PatternView::applyLayers(const opc::PatternDocument& document, const QVector<QPolygonF>& allowances,
                              const QVector<QPainterPath>& markings) {
    mainVisible_ = document.layerVisible(opc::Layer::Main);
    constructionVisible_ = document.layerVisible(opc::Layer::Construction);
    for (qsizetype index = 0; index < entities_.size(); ++index) {
        entities_[index].layer = document.entities()[index].layer;
        updateEntityStyle(index);
    }
    qDeleteAll(allowances_);
    allowances_.clear();
    for (const auto& path : markings) {
        auto* item = scene()->addPath(path, QPen(QColor("#489e79"), 0));
        item->setZValue(-1);
        allowances_.append(item);
    }
    if (!document.layerVisible(opc::Layer::Allowance))
        return;

    for (const auto& polygon : allowances) {
        QPainterPath path;
        path.addPolygon(polygon);
        path.closeSubpath();
        auto* item = scene()->addPath(path, QPen(allowanceColor, 0, Qt::DashLine));
        item->setZValue(-1);
        allowances_.append(item);
    }
}

void PatternView::setSizeOverlays(const QVector<QPair<QPainterPath, QColor>>& overlays) {
    qDeleteAll(sizeOverlays_);
    sizeOverlays_.clear();
    for (const auto& [path, color] : overlays) {
        auto* item = scene()->addPath(path, QPen(color, 0, Qt::DashDotLine));
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setZValue(-0.5);
        sizeOverlays_.append(item);
    }
}

QVector<qsizetype> PatternView::selectedIndices() const {
    QVector<qsizetype> indices;
    for (auto* item : scene()->selectedItems()) {
        const auto index = items_.indexOf(static_cast<QGraphicsPathItem*>(item));
        if (index >= 0)
            indices.append(index);
    }
    return indices;
}

void PatternView::fitDrawing() {
    QRectF bounds;
    for (const auto* item : items_) {
        if (item->isVisible())
            bounds = bounds.united(item->sceneBoundingRect());
    }
    for (const auto* item : allowances_)
        bounds = bounds.united(item->sceneBoundingRect());
    for (const auto* item : sizeOverlays_)
        bounds = bounds.united(item->sceneBoundingRect());
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
    const int angle = event->angleDelta().y();
    const qreal steps = angle ? angle / 120.0 : event->pixelDelta().y() / 60.0;
    if (!steps) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    zoomAt(std::pow(1.2, steps), event->position().toPoint());
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
            const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
            auto* item = objectAt(event->pos());
            if (!toggle && (!item || !item->isSelected()))
                scene()->clearSelection();
            if (!item) {
                selectionStart_ = event->pos();
            } else {
                item->setSelected(!toggle || !item->isSelected());
                const auto index = items_.indexOf(static_cast<QGraphicsPathItem*>(item));
                if (!toggle && scene()->selectedItems().size() == 1 && index >= 0
                    && (entities_[index].draftName.isEmpty() || entities_[index].shape == opc::Shape::Point)) {
                    dragPressPosition_ = event->pos();
                    dragIndex_ = index;
                    dragStart_ = drawingPoint(cursorScenePosition_);
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
            if ((tool_ == Tool::Curve || tool_ == Tool::Dart) && !secondPoint_ && *start_ != point) {
                secondPoint_ = point;
                updatePreview();
            } else if (tool_ == Tool::Dart) {
                if (secondPoint_) {
                    if (const auto legs = opc::dartLegs(*start_, *secondPoint_, point)) {
                        cancelDrawing();
                        emit dartDrawn((*legs)[0], (*legs)[1]);
                    } else {
                        emit dartRejected();
                    }
                }
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

QGraphicsItem* PatternView::objectAt(const QPoint& position) const {
    const QRect hit(position - QPoint(5, 5), QSize(11, 11));
    for (auto* item : items(hit)) {
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            return item;
    }
    return nullptr;
}

void PatternView::contextMenuEvent(QContextMenuEvent* event) {
    if (tool_ == Tool::Select) {
        if (event->reason() == QContextMenuEvent::Mouse) {
            if (auto* item = objectAt(event->pos()); item && !item->isSelected()) {
                scene()->clearSelection();
                item->setSelected(true);
            }
        }
        emit selectionMenuRequested(event->globalPos());
    }
    event->accept();
}

void PatternView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (tool_ == Tool::Select && event->button() == Qt::LeftButton) {
        cancelDrawing();
        if (auto* item = objectAt(event->pos())) {
            scene()->clearSelection();
            item->setSelected(true);
            emit geometryEditRequested();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}
