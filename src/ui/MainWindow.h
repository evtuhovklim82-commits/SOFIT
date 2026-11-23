#pragma once
#include <QGraphicsScene>
#include <QMainWindow>
#include <QUndoStack>
#include "../core/PatternDocument.h"

class PatternView;

class MainWindow : public QMainWindow {
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool confirmDiscard();
    void replaceDocument(opc::PatternDocument document, const QString& path);
    void addEntity(const opc::Entity& entity);
    void deleteSelection();
    void exactInput();
    void exportSvg();

    opc::PatternDocument doc_;
    QGraphicsScene scene_;
    PatternView* view_;
    QUndoStack undo_;
    QString currentPath_;
};
