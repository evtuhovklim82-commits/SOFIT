#pragma once
#include <QGraphicsScene>
#include <QAction>
#include <QSet>
#include "../core/PieceGeometry.h"
#include <QMainWindow>
#include <QUndoStack>
#include <QTimer>
#include "ProjectRecovery.h"
#include "../core/PatternDocument.h"

namespace opc { enum class ContourOperation; }

class PatternView;
class QTreeWidget;
class QDockWidget;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
public:
    MainWindow();
    ~MainWindow() override;
    bool openProject(const QString& path, QString* error);
    void offerProjectRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void recoverProject();
    void autosaveProject();
    void clearRecovery();
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    bool saveProject(const QString& path);
    void refreshSelectionProperties();
    bool canEditGeometry(qsizetype index) const;
    bool confirmDiscard();
    void replaceDocument(opc::PatternDocument document, const QString& path);
    void addEntity(const opc::Entity& entity);
    void deleteSelection();
    void exactInput();
    void editSelectedGeometry();
    void geometryDialog(qsizetype index);
    void splitSelectedEdge();
    void joinSelectedEdges();
    void shapeSilhouette();
    void rotateConstruction();
    void showModelingHistory();
    void editSeamLink(qsizetype index);
    void editModelingStep(qsizetype index);
    void refreshModelingHistory();
    void setModelingError(const QString& error);
    void diagnoseGradedPieces();
    void modelPiece(opc::ContourOperation operation, const QString& title);
    bool canModelEdge(const opc::Entity& edge);
    void refreshObjects();
    void refreshPieceDisplay();
    opc::PatternDocument exportPattern(const QString& size, QVector<bool>& visibility,
                                       QVector<opc::PieceGeometry>& geometry) const;
    void moveEntity(qsizetype index, const QPointF& offset);
    void exportSvg();
    void exportPdf();
    void importDxf();
    void exportDxf();
    void createPiece();
    void assignPiece();
    void managePiece();
    void measureSelection();
    void matchSeams(const QVector<qsizetype>& indices, bool reverseSecond = false);
    void editEdgeAllowance();
    void clearEdgeNotches();
    void setSelectedLayer(opc::Layer layer);
    QVector<qsizetype> selectedObjects() const;
    qsizetype choosePiece(const QString& title);
    void applyPatternEdit(opc::PatternDocument document, const QString& text, bool redraw = false);

    opc::PatternDocument doc_;
    QGraphicsScene scene_;
    PatternView* view_;
    QUndoStack undo_;
    QTreeWidget* objects_;
    QLabel* selectionProperties_;
    QDockWidget* modelingDock_ = nullptr;
    QTreeWidget* modelingTree_ = nullptr;
    QLabel* modelingError_ = nullptr;
    QPushButton* removeModelingStep_ = nullptr;
    QString currentPath_;
    std::unique_ptr<opc::ProjectRecovery> recovery_;
    QTimer autosaveTimer_;
    bool recovered_ = false;
    bool autosavePending_ = false;
    std::array<QAction*, 3> layerActions_{};
    QVector<opc::PieceGeometry> pieces_;
    QSet<QString> overlaySizes_;
};
