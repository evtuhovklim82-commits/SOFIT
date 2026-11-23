#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsLineItem>
#include "../core/PatternDocument.h"

class PatternView : public QGraphicsView {
    Q_OBJECT


public:
    explicit PatternView(QGraphicsScene* scene, QWidget* parent=nullptr);
    void setDocument(opc::PatternDocument* doc);
    void refresh();
    void setSnapToGrid(bool enabled);

signals:
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

    bool snapToGrid_ = false;
    QPointF cursorScenePosition_;
    bool panning_ = false;
    QPoint lastPanPosition_;
    opc::PatternDocument* doc_ = nullptr;
    bool pending_ = false;
    QPointF first_;
    QGraphicsLineItem* preview_ = nullptr;
};

class MainWindow : public QMainWindow {
public:
    MainWindow();
private:
    void newDocument();
    void openDocument();
    void saveDocument();
    void exportSvg();
    opc::PatternDocument doc_;
    QGraphicsScene scene_;
    PatternView* view_;
    QString currentPath_;
};
