#pragma once
#include <QGraphicsView>
#include <QPolygonF>
#include <optional>
#include "../core/PatternDocument.h"

class QGraphicsPathItem;

class PatternView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Point, Line, Rectangle, Circle, Curve, Measure };
    explicit PatternView(QGraphicsScene* scene, QWidget* parent = nullptr);
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setSnapToGrid(bool enabled);
    void setSnapToObjects(bool enabled);
    void cancelDrawing();
    void replaceEntity(qsizetype index, const opc::Entity& entity);
    void selectEntity(qsizetype index);
    bool entityVisible(qsizetype index) const;
    void setEntityVisible(qsizetype index, bool visible);
    void applyLayers(const opc::PatternDocument& document, const QVector<QPolygonF>& allowances,
                     const QVector<QPainterPath>& markings = {});
    void setEntities(const QVector<opc::Entity>& entities);
    void insertEntity(qsizetype index, const opc::Entity& entity);
    void removeEntity(qsizetype index);
    void fitDrawing();
    void zoomAtCenter(qreal factor);
    qsizetype selectedIndex() const;

signals:
    void entityDrawn(const opc::Entity& entity);
    void cursorPositionChanged(const QPointF& position);
    void cursorLeftCanvas();
    void zoomChanged(qreal pixelsPerMillimeter);
    void measured(qreal distance);
    void entityMoved(qsizetype index, const QPointF& offset);

protected:
    bool viewportEvent(QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPointF drawingPoint(QPointF scenePoint) const;
    void updateCursorPosition(const QPoint& viewportPoint);
    void updatePreview();
    opc::Entity draftEntity(const QPointF& point) const;
    void moveView(const QPointF& scenePoint, const QPoint& viewportPoint);
    void zoomAt(qreal factor, const QPoint& anchor);
    void updateEntityStyle(qsizetype index);

    Tool tool_ = Tool::Select;
    bool snapToGrid_ = false;
    bool snapToObjects_ = true;
    bool panning_ = false;
    QPoint lastPanPosition_;
    QPointF cursorScenePosition_;
    std::optional<QPointF> start_;
    std::optional<QPointF> curveEnd_;
    std::optional<QPointF> dragStart_;
    QPoint dragPressPosition_;
    qsizetype dragIndex_ = -1;
    QVector<opc::Entity> entities_;
    QGraphicsPathItem* preview_ = nullptr;
    QVector<QGraphicsPathItem*> items_;
    QVector<bool> entityVisibility_;
    QVector<QGraphicsPathItem*> allowances_;
    bool mainVisible_ = true;
    bool constructionVisible_ = true;
};
