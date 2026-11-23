#pragma once
#include <QGraphicsScene>
#include <QAction>
#include <QSet>
#include "../core/PieceGeometry.h"
#include <QMainWindow>
#include <QUndoStack>
#include "../core/PatternDocument.h"

namespace opc { enum class ContourOperation; }

class PatternView;
class QTreeWidget;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;

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
    void splitSelectedEdge();
    void joinSelectedEdges();
    void modelPiece(opc::ContourOperation operation, const QString& title);
    bool canModelEdge(const opc::Entity& edge);
    void refreshObjects();
    void refreshPieceDisplay();
    opc::PatternDocument exportPattern(const QString& size, QVector<bool>& visibility,
                                       QVector<opc::PieceGeometry>& geometry) const;
    void moveEntity(qsizetype index, const QPointF& offset);
    void exportSvg();
    void exportPdf();
    void exportDxf();
    void createPiece();
    void assignPiece();
    void managePiece();
    void measureSelection();
    void matchSeams();
    void editEdgeAllowance();
    void clearEdgeNotches();
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
    QSet<QString> overlaySizes_;
};
