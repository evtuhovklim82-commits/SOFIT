#pragma once

#include "DxfImport.h"
#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QGraphicsView;
class QLabel;
class QListWidget;
class QPushButton;

class DxfImportDialog final : public QDialog {
public:
    explicit DxfImportDialog(const opc::DxfDrawing& drawing, QWidget* parent = nullptr);
    QSet<QString> selectedLayers() const;
    double millimetersPerUnit() const;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePreview();
    const opc::DxfDrawing& drawing_;
    QListWidget* layers_;
    QDoubleSpinBox* units_;
    QDoubleSpinBox* scale_;
    QGraphicsView* preview_ = nullptr;
    QLabel* summary_;
    QCheckBox* confirmUnits_ = nullptr;
    QPushButton* import_;
};
