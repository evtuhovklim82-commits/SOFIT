#pragma once
#include <QGraphicsScene>
#include <QAction>
#include "../core/PieceGeometry.h"
#include <QMainWindow>
#include <QUndoStack>
#include "../core/PatternDocument.h"

class PatternView;
class QTreeWidget;

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
    void refreshObjects();
    void moveEntity(qsizetype index, const QPointF& offset);
    void exportSvg();
    void createPiece();
    void assignPiece();
    void managePiece();
    void setSelectedLayer(opc::Layer layer);
    QVector<qsizetype> selectedObjects() const;
    qsizetype choosePiece(const QString& title);
    void applyPatternEdit(opc::PatternDocument document, const QString& text);

    opc::PatternDocument doc_;
    QGraphicsScene scene_;
    PatternView* view_;
    QUndoStack undo_;
    QTreeWidget* objects_;
    QString currentPath_;
    std::array<QAction*, 3> layerActions_{};
    QVector<opc::PieceGeometry> pieces_;
};
