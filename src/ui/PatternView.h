#pragma once
#include <QGraphicsView>
#include <optional>
#include "../core/PatternDocument.h"

class QGraphicsPathItem;

class PatternView : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Point, Line, Rectangle };
    explicit PatternView(QGraphicsScene* scene, QWidget* parent = nullptr);
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setSnapToGrid(bool enabled);
    void cancelDrawing();
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
    void moveView(const QPointF& scenePoint, const QPoint& viewportPoint);
    void zoomAt(qreal factor, const QPoint& anchor);

    Tool tool_ = Tool::Select;
    bool snapToGrid_ = false;
    bool panning_ = false;
    QPoint lastPanPosition_;
    QPointF cursorScenePosition_;
    std::optional<QPointF> start_;
    QGraphicsPathItem* preview_ = nullptr;
    QVector<QGraphicsPathItem*> items_;
};
