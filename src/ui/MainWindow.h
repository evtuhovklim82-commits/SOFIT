#pragma once
#include <QMainWindow>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsLineItem>
#include "../core/PatternDocument.h"

class PatternView : public QGraphicsView {
public:
    explicit PatternView(QGraphicsScene* scene, QWidget* parent=nullptr);
    void setDocument(opc::PatternDocument* doc);
    void refresh();
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
private:
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
