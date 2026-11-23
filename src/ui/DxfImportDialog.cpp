#include "DxfImportDialog.h"

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QListWidget>
#include <QPainterPath>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <cmath>

DxfImportDialog::DxfImportDialog(const opc::DxfDrawing& drawing, QWidget* parent)
    : QDialog(parent), drawing_(drawing) {
    setObjectName(QStringLiteral("dxfImportDialog"));
    setWindowTitle(tr("Import DXF"));
    resize(790, 690);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    units_ = new QDoubleSpinBox(this);
    units_->setObjectName(QStringLiteral("dxfUnits"));
    units_->setDecimals(9);
    units_->setRange(0.000000001, 1000000);
    const auto declaredUnits = opc::dxfUnitMillimeters(drawing.insertionUnits);
    units_->setValue(declaredUnits.value_or(1));
    form->addRow(tr("Millimeters per DXF unit"), units_);
    scale_ = new QDoubleSpinBox(this);
    scale_->setDecimals(6);
    scale_->setRange(0.000001, 10000);
    scale_->setValue(1);
    form->addRow(tr("Scale multiplier"), scale_);
    if (!declaredUnits) {
        confirmUnits_ = new QCheckBox(tr("DXF units are missing or unsupported. Use the conversion above."), this);
        layout->addWidget(confirmUnits_);
        connect(confirmUnits_, &QCheckBox::toggled, this, &DxfImportDialog::updatePreview);
    }

    auto* row = new QHBoxLayout;
    layout->addLayout(row, 1);
    auto* layerColumn = new QVBoxLayout;
    auto* layerLabel = new QLabel(tr("DXF layers"), this);
    layers_ = new QListWidget(this);
    layers_->setObjectName(QStringLiteral("dxfLayers"));
    layers_->setMaximumWidth(230);
    layerLabel->setBuddy(layers_);
    layerColumn->addWidget(layerLabel);
    layerColumn->addWidget(layers_);
    row->addLayout(layerColumn);
    QMap<QString, qsizetype> counts;
    for (const auto& path : drawing.paths)
        ++counts[path.layer];
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        auto* item = new QListWidgetItem(tr("%1 (%2)").arg(it.key()).arg(it.value()), layers_);
        item->setData(Qt::UserRole, it.key());
        item->setCheckState(Qt::Checked);
    }
    preview_ = new QGraphicsView(new QGraphicsScene(this), this);
    preview_->setRenderHint(QPainter::Antialiasing);
    preview_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    row->addWidget(preview_, 1);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    QStringList diagnostics{tr("DXF imports geometry. Parametric history, AAMA/ASTM grade rules, "
                               "and marking semantics do not transfer. Source layer names are kept for DXF export. "
                               "CONSTRUCTION uses the SOFIT construction layer; other geometry uses the main layer."),
                            tr("Closed polylines and loops with exactly matching endpoints become pieces. "
                               "Inspect pieces for gaps, crossings and overlaps before grading.")};
    if (drawing.approximated)
        diagnostics.append(tr("Entities with circular arcs approximated by editable curves: %1.").arg(drawing.approximated));
    for (auto it = drawing.skipped.cbegin(); it != drawing.skipped.cend(); ++it)
        diagnostics.append(tr("Skipped %1: %2 (unsupported or invalid geometry).").arg(it.key()).arg(it.value()));
    auto* details = new QTextEdit(this);
    details->setReadOnly(true);
    details->setPlainText(diagnostics.join(QLatin1Char('\n')));
    details->setMaximumHeight(125);
    layout->addWidget(details);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    import_ = buttons->button(QDialogButtonBox::Ok);
    import_->setText(tr("Import"));
    import_->setObjectName(QStringLiteral("confirmDxfImport"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(layers_, &QListWidget::itemChanged, this, &DxfImportDialog::updatePreview);
    connect(units_, &QDoubleSpinBox::valueChanged, this, &DxfImportDialog::updatePreview);
    connect(scale_, &QDoubleSpinBox::valueChanged, this, &DxfImportDialog::updatePreview);
    updatePreview();
}

QSet<QString> DxfImportDialog::selectedLayers() const {
    QSet<QString> result;
    for (int i = 0; i < layers_->count(); ++i) {
        const auto* item = layers_->item(i);
        if (item->checkState() == Qt::Checked)
            result.insert(item->data(Qt::UserRole).toString());
    }
    return result;
}

double DxfImportDialog::millimetersPerUnit() const {
    return units_->value() * scale_->value();
}

void DxfImportDialog::updatePreview() {
    const auto selected = selectedLayers();
    qsizetype open = 0;
    qsizetype count = 0;
    QPainterPath closedPaths, openPaths;
    QPainterPath points;
    for (const auto& path : drawing_.paths) {
        if (!selected.contains(path.layer))
            continue;
        count += path.entities.size();
        const bool isOpen = path.contour < 0 && !path.closed &&
                            path.entities.front().shape != opc::Shape::Point;
        open += isOpen;
        auto& geometry = isOpen ? openPaths : closedPaths;
        for (const auto& entity : path.entities) {
            const auto a = QPointF(entity.a.x(), -entity.a.y());
            const auto b = QPointF(entity.b.x(), -entity.b.y());
            if (entity.shape == opc::Shape::Circle) {
                geometry.addEllipse(a, b.x() - a.x(), b.x() - a.x());
            } else if (entity.shape == opc::Shape::Point) {
                points.moveTo(a);
                points.lineTo(a + QPointF(1e-5, 0));
            } else {
                geometry.moveTo(a);
                if (entity.shape == opc::Shape::Curve)
                    geometry.quadTo(QPointF(entity.control.x(), -entity.control.y()), b);
                else
                    geometry.lineTo(b);
            }
        }
    }
    auto* scene = preview_->scene();
    scene->clear();
    QPen pen(palette().color(QPalette::Text));
    pen.setCosmetic(true);
    scene->addPath(closedPaths, pen);
    pen.setColor(QColor(QStringLiteral("#c4782a")));
    scene->addPath(openPaths, pen);
    pen.setColor(palette().color(QPalette::Text));
    pen.setWidth(4);
    scene->addPath(points, pen);
    auto frame = scene->itemsBoundingRect();
    const double margin = qMax(qMax(frame.width(), frame.height()) * 0.04, 1e-9);
    scene->setSceneRect(frame.adjusted(-margin, -margin, margin, margin));
    preview_->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    QRectF bounds;
    bool hasBounds = false;
    const auto include = [&](QRectF rect) {
        if (!hasBounds)
            bounds = rect;
        else
            bounds.setCoords(qMin(bounds.left(), rect.left()), qMin(bounds.top(), rect.top()),
                             qMax(bounds.right(), rect.right()), qMax(bounds.bottom(), rect.bottom()));
        hasBounds = true;
    };
    for (const auto* geometry : {&closedPaths, &openPaths, &points}) {
        if (!geometry->isEmpty())
            include(geometry->boundingRect());
    }
    const double factor = millimetersPerUnit();
    const bool safeScale = qMax(qMax(std::abs(bounds.left()), std::abs(bounds.right())),
                               qMax(std::abs(bounds.top()), std::abs(bounds.bottom()))) * factor <= 1e9;
    import_->setEnabled(count > 0 && safeScale && (!confirmUnits_ || confirmUnits_->isChecked()));
    summary_->setText(tr("%1 editable objects · %2 open paths in orange · %3 × %4 mm")
        .arg(count).arg(open).arg(bounds.width() * factor, 0, 'g', 8).arg(bounds.height() * factor, 0, 'g', 8) +
        (safeScale ? QString{} : tr("\nScaled coordinates exceed the import limit.")));
}

void DxfImportDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (preview_)
        preview_->fitInView(preview_->sceneRect(), Qt::KeepAspectRatio);
}
