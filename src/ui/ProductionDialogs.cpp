#include "ProductionDialogs.h"
#include "PatternPdf.h"
#include "Production.h"
#include "PatternDxf.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineF>
#include <QMap>
#include <QPainter>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
QString tr(const char* text) { return QCoreApplication::translate("ProductionDialogs", text); }
QString number(double value) { return QString::number(value, 'g', 17); }

QTableWidget* makeTable(QVBoxLayout* layout, const QStringList& headers) {
    auto* table = new QTableWidget(0, headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table);
    return table;
}

QHBoxLayout* tableButtons(QVBoxLayout* layout, QTableWidget* table) {
    auto* row = new QHBoxLayout;
    layout->addLayout(row);
    auto* remove = new QPushButton(tr("Remove selected row"));
    row->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, table, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    return row;
}

QDialogButtonBox* dialogButtons(QVBoxLayout* layout, QDialog& dialog) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}

class PlacementItem : public QGraphicsPolygonItem {
public:
    PlacementItem(qsizetype index, const QPolygonF& cut, std::optional<double> lockedX,
                  std::function<void()> moved)
        : QGraphicsPolygonItem(cut), index(index), valid(!cut.isEmpty()), lockedX_(lockedX),
          moved_(std::move(moved)) {
        if (!valid)
            setPolygon(QPolygonF(QRectF(0, 0, 100, 40)));
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    }
    const qsizetype index;
    const bool valid;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemPositionChange && scene() && lockedX_) {
            auto position = value.toPointF();
            position.setX(*lockedX_);
            return position;
        }
        if (change == ItemPositionHasChanged && scene())
            moved_();
        return QGraphicsPolygonItem::itemChange(change, value);
    }

private:
    std::optional<double> lockedX_;
    std::function<void()> moved_;
};
}

std::optional<opc::ProductionData> editGrading(QWidget* parent, const opc::PatternDocument& document) {
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Sizes, Measurements and Grade Rules"));
    dialog.resize(820, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto* sizesPage = new QWidget;
    auto* rulesPage = new QWidget;
    auto* sizesLayout = new QVBoxLayout(sizesPage);
    auto* rulesLayout = new QVBoxLayout(rulesPage);
    tabs->addTab(sizesPage, tr("Measurements"));
    tabs->addTab(rulesPage, tr("Grade Rules"));
    auto* sizesHint = new QLabel(tr("Use one row per measurement. Repeat the size name for each measurement. Leave the last two cells empty to define a size without measurements. Matching draft measurement names replace base values for that size. Omitted measurements keep their base values."));
    sizesHint->setWordWrap(true);
    sizesLayout->addWidget(sizesHint);
    auto* useDraftMeasurements = new QCheckBox(tr("Use size measurements in draft formulas"));
    useDraftMeasurements->setChecked(document.production().useDraftMeasurements);
    sizesLayout->addWidget(useDraftMeasurements);
    auto* sizes = makeTable(sizesLayout, {tr("Size"), tr("Measurement"), tr("Value (mm)")});
    const auto addMeasurement = [sizes](const QString& size, const QString& name, const QString& value) {
        const int row = sizes->rowCount();
        sizes->insertRow(row);
        sizes->setItem(row, 0, new QTableWidgetItem(size));
        sizes->setItem(row, 1, new QTableWidgetItem(name));
        sizes->setItem(row, 2, new QTableWidgetItem(value));
    };
    for (const auto& size : document.production().sizes) {
        if (size.measurements.isEmpty())
            addMeasurement(size.name, {}, {});
        for (auto map = size.measurements.cbegin(); map != size.measurements.cend(); ++map)
            addMeasurement(size.name, map.key(), number(map.value()));
    }
    auto* addSize = new QPushButton(tr("Add measurement row"));
    auto* measurementButtons = tableButtons(sizesLayout, sizes);
    measurementButtons->insertWidget(0, addSize);
    auto* copyTable = new QPushButton(tr("Copy measurement table"));
    auto* pasteTable = new QPushButton(tr("Replace table from clipboard"));
    measurementButtons->addWidget(copyTable);
    measurementButtons->addWidget(pasteTable);
    const auto header = QStringLiteral("Size\tMeasurement\tValue (mm)");
    QObject::connect(copyTable, &QPushButton::clicked, &dialog, [=, &dialog] {
        QStringList lines{header};
        for (int row = 0; row < sizes->rowCount(); ++row) {
            QStringList cells;
            for (int column = 0; column < sizes->columnCount(); ++column) {
                const auto value = sizes->item(row, column)->text();
                if (value.contains(u'\t') || value.contains(u'\r') || value.contains(u'\n')) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                                         tr("Cells containing tabs or line breaks cannot be copied as a table."));
                    return;
                }
                cells.append(value);
            }
            lines.append(cells.join(u'\t'));
        }
        QApplication::clipboard()->setText(lines.join(u'\n'));
    });
    QObject::connect(pasteTable, &QPushButton::clicked, &dialog, [=, &dialog] {
        auto text = QApplication::clipboard()->text();
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        auto lines = text.split(u'\n');
        if (!lines.isEmpty() && lines.back().isEmpty())
            lines.removeLast();
        if (!lines.isEmpty() && lines.front() == header)
            lines.removeFirst();
        if (lines.isEmpty())
            return;
        QVector<QStringList> rows;
        for (const auto& line : lines) {
            auto cells = line.split(u'\t');
            if (cells.size() != 3 || cells[0].trimmed().isEmpty() || line.contains(u'\r')) {
                QMessageBox::warning(&dialog, dialog.windowTitle(),
                    tr("Clipboard row %1 needs three tab-separated cells: size, measurement and value in mm.")
                        .arg(rows.size() + 1));
                return;
            }
            rows.append(std::move(cells));
        }
        sizes->setRowCount(0);
        for (const auto& cells : rows)
            addMeasurement(cells[0], cells[1], cells[2]);
    });
    QObject::connect(addSize, &QPushButton::clicked, &dialog, [=] {
        addMeasurement({}, {}, {});
        sizes->editItem(sizes->item(sizes->rowCount() - 1, 0));
    });

    auto* rulesHint = new QLabel(tr("Enter the target size and choose a point on the base piece. Enter X and Y offsets in mm. Offsets are added after formulas are recalculated, using the selected base point as the reference. Positive Y moves down. Coincident base points on the same piece share the offset."));
    rulesHint->setWordWrap(true);
    rulesLayout->addWidget(rulesHint);
    auto* rules = makeTable(rulesLayout, {tr("Size"), tr("Piece"), tr("Base point (X, Y)"), tr("ΔX (mm)"), tr("ΔY (mm)")});
    QVector<QVector<QPointF>> points;
    for (qsizetype i = 0; i < document.pieces().size(); ++i)
        points.append(opc::gradingPoints(document, i));

    const auto addRule = [&](const opc::GradeRule& rule, bool existing) {
        const int row = rules->rowCount();
        rules->insertRow(row);
        rules->setItem(row, 0, new QTableWidgetItem(rule.size));
        auto* piece = new QComboBox;
        for (const auto& entry : document.pieces())
            piece->addItem(entry.name);
        piece->setCurrentIndex(rule.piece);
        rules->setCellWidget(row, 1, piece);
        auto* point = new QComboBox;
        rules->setCellWidget(row, 2, point);
        const auto fillPoints = [point, &points](int pieceIndex) {
            point->clear();
            if (pieceIndex < 0)
                return;
            for (const auto position : points[pieceIndex])
                point->addItem(QStringLiteral("%1, %2").arg(number(position.x()), number(position.y())), position);
        };
        fillPoints(piece->currentIndex());
        if (existing) {
            const auto& available = points[rule.piece];
            int selected = -1;
            for (int i = 0; i < available.size(); ++i) {
                if (available[i].x() == rule.point.x() && available[i].y() == rule.point.y()) {
                    selected = i;
                    break;
                }
            }
            if (selected < 0) {
                selected = point->count();
                point->addItem(tr("Missing point (%1, %2)").arg(number(rule.point.x()), number(rule.point.y())),
                               rule.point);
            }
            point->setCurrentIndex(selected);
        }
        QObject::connect(piece, &QComboBox::currentIndexChanged, point, fillPoints);
        rules->setItem(row, 3, new QTableWidgetItem(number(rule.offset.x())));
        rules->setItem(row, 4, new QTableWidgetItem(number(rule.offset.y())));
    };
    for (const auto& rule : document.production().rules)
        addRule(rule, true);
    auto* addGrade = new QPushButton(tr("Add grade rule"));
    addGrade->setEnabled(!points.isEmpty());
    tableButtons(rulesLayout, rules)->insertWidget(0, addGrade);
    QObject::connect(addGrade, &QPushButton::clicked, &dialog, [&] {
        addRule({{}, 0, {}, {}}, false);
        rules->editItem(rules->item(rules->rowCount() - 1, 0));
    });

    auto* buttons = dialogButtons(layout, dialog);
    auto result = document.production();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto warn = [&](const QString& message) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), message);
        };
        auto updated = document.production();
        updated.sizes.clear();
        updated.rules.clear();
        QMap<QString, qsizetype> sizeIndices;
        for (int row = 0; row < sizes->rowCount(); ++row) {
            const auto size = sizes->item(row, 0)->text().trimmed();
            const auto name = sizes->item(row, 1)->text().trimmed();
            const auto value = sizes->item(row, 2)->text().trimmed();
            if (size.isEmpty()) {
                warn(tr("Enter a size name in measurement row %1.").arg(row + 1));
                return;
            }
            if (!sizeIndices.contains(size)) {
                sizeIndices.insert(size, updated.sizes.size());
                updated.sizes.append({size, {}});
            }
            if (name.isEmpty() && value.isEmpty())
                continue;
            bool ok = false;
            const double mm = value.toDouble(&ok);
            auto& measurements = updated.sizes[sizeIndices[size]].measurements;
            if (name.isEmpty() || !ok || !std::isfinite(mm) || mm <= 0 || measurements.contains(name)) {
                warn(tr("Measurement row %1 needs a unique measurement name for its size and a positive value in mm.").arg(row + 1));
                return;
            }
            measurements.insert(name, mm);
        }
        for (int row = 0; row < rules->rowCount(); ++row) {
            const auto size = rules->item(row, 0)->text().trimmed();
            auto* piece = qobject_cast<QComboBox*>(rules->cellWidget(row, 1));
            auto* point = qobject_cast<QComboBox*>(rules->cellWidget(row, 2));
            bool xOk, yOk;
            const double x = rules->item(row, 3)->text().toDouble(&xOk);
            const double y = rules->item(row, 4)->text().toDouble(&yOk);
            if (!sizeIndices.contains(size) || point->currentIndex() < 0 || !xOk || !yOk) {
                warn(tr("Grade rule row %1 needs a defined size, a point and two numeric offsets.").arg(row + 1));
                return;
            }
            updated.rules.append({size, piece->currentIndex(), point->currentData().toPointF(), {x, y}});
        }
        for (const auto& placement : updated.placements) {
            if (!placement.size.isEmpty() && !sizeIndices.contains(placement.size)) {
                warn(tr("Size %1 is used in the fabric layout. Remove those placements before removing the size.").arg(placement.size));
                return;
            }
        }
        if (!opc::loadProduction(opc::saveProduction(updated), document.pieces().size(), result)) {
            warn(tr("Check for duplicate grade rules at the same point and size or invalid offsets."));
            return;
        }
        result.useDraftMeasurements = useDraftMeasurements->isChecked();
        QString error;
        if (!document.validateParametricSizes(result, &error)) {
            warn(error);
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted)
        return result;
    return std::nullopt;
}

std::optional<opc::ProductionData> editFabricLayout(QWidget* parent, const opc::PatternDocument& document) {
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Fabric Layout"));
    dialog.resize(1000, 660);
    auto data = document.production();
    auto* layout = new QVBoxLayout(&dialog);
    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);
    auto* width = new QDoubleSpinBox;
    width->setDecimals(3);
    width->setRange(0.001, 1000000);
    width->setValue(data.fabricWidth);
    width->setSuffix(tr(" mm"));
    controls->addWidget(new QLabel(tr("Fabric width")));
    controls->addWidget(width);
    auto* clearance = new QDoubleSpinBox;
    clearance->setObjectName(QStringLiteral("markerPieceClearance"));
    clearance->setDecimals(3);
    clearance->setRange(0, 1000);
    clearance->setValue(data.pieceClearanceMm);
    clearance->setSuffix(tr(" mm"));
    controls->addWidget(new QLabel(tr("Piece clearance")));
    controls->addWidget(clearance);
    auto* piece = new QComboBox;
    for (const auto& entry : document.pieces())
        piece->addItem(entry.name);
    controls->addWidget(piece, 1);
    auto* size = new QComboBox;
    size->addItem(tr("Base pattern"), QString{});
    for (const auto& entry : data.sizes)
        size->addItem(entry.name, entry.name);
    controls->addWidget(size);
    auto* quantity = new QSpinBox;
    quantity->setRange(1, int(opc::maxAutomaticCopies));
    quantity->setAccessibleName(tr("Number of copies"));
    controls->addWidget(new QLabel(tr("Quantity")));
    controls->addWidget(quantity);
    auto* add = new QPushButton(tr("Add copy"));
    add->setEnabled(piece->count() > 0);
    controls->addWidget(add);
    auto* turn = new QPushButton(tr("Turn 180°"));
    controls->addWidget(turn);
    auto* remove = new QPushButton(tr("Remove copy"));
    controls->addWidget(remove);
    auto* fit = new QPushButton(tr("Fit"));
    controls->addWidget(fit);

    auto* cutControls = new QHBoxLayout;
    layout->addLayout(cutControls);
    auto* cutMode = new QComboBox;
    cutMode->addItem(tr("Single copy"));
    cutMode->addItem(tr("Left + right pair"));
    cutMode->addItem(tr("Left copy"));
    cutMode->addItem(tr("Right copy (mirrored)"));
    cutMode->addItem(tr("On left fold"));
    cutMode->addItem(tr("On right fold"));
    cutControls->addWidget(new QLabel(tr("Cut mode")));
    cutControls->addWidget(cutMode);
    auto* fixedOrientation = new QCheckBox(tr("Fix orientation for new copies"));
    cutControls->addWidget(fixedOrientation);
    cutControls->addStretch();
    auto* reportButton = new QPushButton(tr("Cutting report…"));
    cutControls->addWidget(reportButton);
    auto* dxf = new QPushButton(tr("Export layout DXF…"));
    cutControls->addWidget(dxf);
    auto* pdf = new QPushButton(tr("Export layout A4 PDF…"));
    cutControls->addWidget(pdf);

    auto* automaticControls = new QHBoxLayout;
    layout->addLayout(automaticControls);
    auto* automatic = new QPushButton(tr("Auto arrange"));
    automaticControls->addWidget(automatic);
    auto* halfTurns = new QCheckBox(tr("Allow 180° turns in automatic layout"));
    halfTurns->setChecked(data.allowHalfTurns);
    automaticControls->addWidget(halfTurns);
    auto* directional = new QCheckBox(tr("One-way nap or print"));
    directional->setChecked(data.directionalFabric);
    automaticControls->addWidget(directional);
    halfTurns->setEnabled(!data.directionalFabric);
    automaticControls->addStretch();

    auto* repeatControls = new QHBoxLayout;
    layout->addLayout(repeatControls);
    const auto repeatInput = [&](const QString& label, double value, const QString& name) {
        auto* input = new QDoubleSpinBox;
        input->setObjectName(name);
        input->setDecimals(3);
        input->setRange(0, 10000);
        input->setSuffix(tr(" mm"));
        input->setSpecialValueText(tr("Off"));
        input->setValue(value);
        repeatControls->addWidget(new QLabel(label));
        repeatControls->addWidget(input);
        return input;
    };
    auto* crossRepeat = repeatInput(tr("Repeat across width"), data.fabricRepeatMm.x(), "markerCrossRepeat");
    auto* lengthRepeat = repeatInput(tr("Repeat along length"), data.fabricRepeatMm.y(), "markerLengthRepeat");
    repeatControls->addStretch();

    auto* hint = new QLabel(tr("Grain runs along the fabric length. Fold copies need a straight outer edge and a finished cutting contour with zero added allowance. Set the width of the folded fabric for fold layouts. Repeat matching uses the top-left of each cutting outline. Fold copies match along the length only."));
    if (document.pieces().isEmpty()) {
        hint->setText(tr("To begin a layout, close this window and use Pieces → Create Piece from Selection. "
                         "Select the objects that form a closed outline first."));
    }
    hint->setWordWrap(true);
    layout->addWidget(hint);
    QGraphicsScene scene;
    auto* view = new QGraphicsView(&scene);
    view->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(view, 1);
    auto* stats = new QLabel;
    stats->setWordWrap(true);
    layout->addWidget(stats);
    auto* buttons = dialogButtons(layout, dialog);
    QMap<QString, opc::PatternDocument> graded;
    graded.insert(QString{}, document);
    QVector<PlacementItem*> items;
    QGraphicsRectItem* fabric = nullptr;

    const auto cutFor = [&](const opc::FabricPlacement& placement) {
        if (!graded.contains(placement.size))
            graded.insert(placement.size, opc::gradedPattern(document, placement.size));
        return opc::fabricPiece(graded[placement.size], placement);
    };
    const auto updateUsage = [&] {
        QVector<QPolygonF> placed;
        qsizetype repeats = 0;
        for (auto* item : items) {
            auto& placement = data.placements[item->index];
            placement.position = item->pos();
            if (!opc::matchesFabricRepeat(data, placement))
                ++repeats;
            placed.append(item->valid ? item->polygon().translated(item->pos()) : QPolygonF{});
        }
        const auto usage = opc::fabricUsage(placed, data.fabricWidth, data.pieceClearanceMm);
        const double displayLength = std::max(1000.0, usage.length + 200);
        fabric->setRect(0, 0, data.fabricWidth, displayLength);
        automatic->setEnabled(!items.isEmpty());
        dxf->setEnabled(!items.isEmpty());
        auto summary = tr("Used length: %1 m · Copies: %2 · Outside fabric: %3 · Overlap or clearance conflicts: %4 · Invalid contours: %5")
                           .arg(usage.length / 1000, 0, 'f', 3).arg(items.size())
                           .arg(usage.outside).arg(usage.overlaps).arg(usage.invalid);
        if (!data.fabricRepeatMm.isNull())
            summary += tr(" · Off repeat: %1").arg(repeats);
        if (const auto utilization = usage.utilization(data.fabricWidth)) {
            const double fabricArea = data.fabricWidth * usage.length;
            summary += QLatin1Char('\n') + tr("Cut area: %1 m² · Unused area: %2 m² · Fabric utilization: %3%")
                .arg(usage.cutArea / 1e6, 0, 'f', 3)
                .arg(std::max(0.0, fabricArea - usage.cutArea) / 1e6, 0, 'f', 3)
                .arg(*utilization * 100, 0, 'f', 1);
        }
        stats->setText(summary);
    };
    const auto updateSceneBounds = [&] {
        auto bounds = scene.itemsBoundingRect();
        bounds.setBottom(std::max(bounds.bottom(), 2000.0));
        scene.setSceneRect(bounds.adjusted(-80, -80, 80, 80));
    };
    const auto addItem = [&](qsizetype index) {
        const auto& placement = data.placements[index];
        const auto cut = cutFor(placement);
        std::optional<double> lockedX;
        if (placement.fold != opc::FoldEdge::None && !cut.cut.isEmpty()) {
            lockedX = opc::foldPosition(placement, data.fabricWidth, cut.cut.boundingRect().width());
        }
        auto* item = new PlacementItem(index, cut.cut, lockedX, updateUsage);
        item->setPen(QPen(QColor("#173b5e"), 0));
        item->setBrush(placement.hand == opc::CutHand::Right ? QColor(208, 146, 89, 140)
                                                           : QColor(86, 167, 222, 125));
        item->setPos(lockedX.value_or(placement.position.x()), placement.position.y());
        const auto& entry = document.pieces()[placement.piece];
        auto* label = new QGraphicsSimpleTextItem(
            tr("%1 · %2 (cut ×%3)").arg(entry.name, placement.size.isEmpty() ? tr("Base") : placement.size)
                                     .arg(entry.cutCount), item);
        if (placement.hand == opc::CutHand::Left)
            label->setText(label->text() + tr(" · Left"));
        if (placement.hand == opc::CutHand::Right)
            label->setText(label->text() + tr(" · Right"));
        if (placement.fold != opc::FoldEdge::None)
            label->setText(label->text() + tr(" · Fold"));
        if (placement.reversed && !opc::canTurn(data, placement))
            label->setText(label->text() + tr(" · Rotation conflict"));
        label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        label->setPos(cut.cut.boundingRect().center());
        if (cut.cut.isEmpty()) {
            item->setBrush(QColor("#ffc2ae"));
            label->setText(tr("Invalid: %1").arg(label->text()));
        } else if (entry.grainAngle) {
            const auto tip = cut.grain.p2();
            const double head = std::min(8.0, cut.grain.length() / 4);
            const double behind = cut.grain.dy() > 0 ? -head : head;
            QPainterPath arrow(cut.grain.p1());
            arrow.lineTo(tip);
            arrow.lineTo(tip + QPointF(-head / 2, behind));
            arrow.moveTo(tip);
            arrow.lineTo(tip + QPointF(head / 2, behind));
            auto* line = new QGraphicsPathItem(arrow, item);
            line->setPen(QPen(QColor("#1b6645"), 0));
        }
        if (!cut.fold.isNull()) {
            auto* line = new QGraphicsLineItem(cut.fold, item);
            line->setPen(QPen(QColor("#654590"), 0, Qt::DashLine));
        }
        scene.addItem(item);
        items.append(item);
    };
    const auto rebuild = [&] {
        items.clear();
        scene.clear();
        fabric = scene.addRect(0, 0, data.fabricWidth, 1000, QPen(QColor("#71806d"), 0), QColor("#eef4e7"));
        fabric->setZValue(-1);
        for (qsizetype index = 0; index < data.placements.size(); ++index)
            addItem(index);
        updateUsage();
        updateSceneBounds();
    };
    const auto selected = [&]() -> PlacementItem* {
        const auto selection = scene.selectedItems();
        return selection.isEmpty() ? nullptr : static_cast<PlacementItem*>(selection.front());
    };
    QObject::connect(&scene, &QGraphicsScene::selectionChanged, &dialog, [&] {
        const auto* item = selected();
        turn->setEnabled(item && (data.placements[item->index].reversed
                                  || opc::canTurn(data, data.placements[item->index])));
        remove->setEnabled(selected());
    });
    turn->setEnabled(false);
    remove->setEnabled(false);
    QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        data.fabricWidth = value;
        rebuild();
    });
    QObject::connect(clearance, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        data.pieceClearanceMm = value;
        updateUsage();
    });
    const auto updateRepeat = [&] {
        data.fabricRepeatMm = {crossRepeat->value(), lengthRepeat->value()};
        updateUsage();
    };
    QObject::connect(crossRepeat, &QDoubleSpinBox::valueChanged, &dialog, updateRepeat);
    QObject::connect(lengthRepeat, &QDoubleSpinBox::valueChanged, &dialog, updateRepeat);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        opc::FabricPlacement placement{piece->currentIndex(), size->currentData().toString(), {}};
        placement.fixedOrientation = fixedOrientation->isChecked();
        const int mode = cutMode->currentIndex();
        const bool pair = mode == 1;
        if (pair || mode == 2)
            placement.hand = opc::CutHand::Left;
        else if (mode == 3)
            placement.hand = opc::CutHand::Right;
        else if (mode == 4)
            placement.fold = opc::FoldEdge::Left;
        else if (mode == 5)
            placement.fold = opc::FoldEdge::Right;
        const auto cut = cutFor(placement).cut;
        if (cut.isEmpty()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), placement.fold == opc::FoldEdge::None
                ? tr("This size needs a valid closed contour and allowance. "
                     "Use Grading → Diagnose Pieces by Size to find the problem.")
                : tr("Use a finished contour with zero added allowance and a straight outer edge on the selected side, parallel to the grain."));
            return;
        }
        for (auto* item : items) {
            if (item->valid)
                placement.position.setY(std::max(placement.position.y(),
                                                item->pos().y() + item->polygon().boundingRect().bottom() + 10));
        }
        const double step = cut.boundingRect().height() + 10;
        const auto append = [&] {
            data.placements.append(placement);
            addItem(data.placements.size() - 1);
            placement.position.ry() += step;
        };
        for (int i = 0; i < quantity->value(); ++i) {
            append();
            if (pair) {
                placement.hand = opc::CutHand::Right;
                append();
                placement.hand = opc::CutHand::Left;
            }
        }
        updateUsage();
        updateSceneBounds();
        scene.clearSelection();
        items.back()->setSelected(true);
        view->ensureVisible(items.back());
    });
    QObject::connect(turn, &QPushButton::clicked, &dialog, [&] {
        if (const auto* item = selected()) {
            const auto index = item->index;
            data.placements[index].reversed = !data.placements[index].reversed;
            rebuild();
            items[index]->setSelected(true);
        }
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (const auto* item = selected()) {
            data.placements.removeAt(item->index);
            rebuild();
        }
    });
    const auto fitFabric = [&] {
        updateSceneBounds();
        view->fitInView(scene.itemsBoundingRect().adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
    };
    QObject::connect(fit, &QPushButton::clicked, &dialog, fitFabric);
    QObject::connect(halfTurns, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        data.allowHalfTurns = enabled;
    });
    QObject::connect(directional, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        data.directionalFabric = enabled;
        halfTurns->setEnabled(!enabled);
        rebuild();
    });
    QObject::connect(dxf, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getSaveFileName(&dialog, tr("Export fabric layout"), {}, tr("DXF (*.dxf)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive))
            path += QStringLiteral(".dxf");
        QString error;
        if (!opc::exportFabricDxf(path, document, data, error))
            QMessageBox::warning(&dialog, dialog.windowTitle(), error);
    });
    QObject::connect(pdf, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getSaveFileName(&dialog, tr("Export 1:1 fabric layout"), {}, tr("PDF (*.pdf)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
            path += QStringLiteral(".pdf");
        QString error;
        if (!opc::exportFabricPdf(path, document, data, {}, error))
            QMessageBox::warning(&dialog, dialog.windowTitle(), error);
    });
    QObject::connect(reportButton, &QPushButton::clicked, &dialog, [&] {
        const auto report = opc::cuttingReport(document, data);
        const auto& usage = report.usage;
        QString text = tr("Working width: %1 mm · Used length: %2 m · Cut area: %3 m²")
            .arg(data.fabricWidth).arg(usage.length / 1000, 0, 'f', 3).arg(usage.cutArea / 1e6, 0, 'f', 3);
        if (!report.orientationViolations && !report.foldViolations) {
            if (const auto utilization = usage.utilization(data.fabricWidth))
                text += tr(" · Utilization: %1%").arg(*utilization * 100, 0, 'f', 1);
        }
        text += tr("\nPlotted copies: %1. On-fold entries each count as one unfolded piece. Other counts are per cutting layer.")
            .arg(data.placements.size());
        text += tr("\nOutside: %1 · Overlap or clearance conflicts: %2 · Invalid outlines: %3"
                   "\nRotation conflicts: %4 · Off fold: %5 · Off repeat: %6\n")
            .arg(usage.outside).arg(usage.overlaps).arg(usage.invalid)
            .arg(report.orientationViolations).arg(report.foldViolations).arg(report.repeatViolations);
        qsizetype countIssues = 0;
        qsizetype pairIssues = 0;
        QString details;
        QStringList checkedSizes;
        for (const auto& row : opc::cuttingKitRows(document, report)) {
            if (!checkedSizes.contains(row.size))
                checkedSizes.append(row.size);
            const auto& piece = document.pieces()[row.piece];
            details += tr("\n%1 · %2 | Expected: %3 | Plotted: %4 (single %5, left %6, right %7, on fold %8)")
                .arg(piece.name, row.size.isEmpty() ? tr("Base") : row.size)
                .arg(piece.cutCount).arg(row.copies()).arg(row.single).arg(row.left)
                .arg(row.right).arg(row.onFold);
            if (row.copies() < piece.cutCount) {
                details += tr(" | Missing: %1").arg(piece.cutCount - row.copies());
                ++countIssues;
            } else if (row.copies() > piece.cutCount) {
                details += tr(" | Extra: %1").arg(row.copies() - piece.cutCount);
                ++countIssues;
            }
            if (row.left != row.right) {
                details += tr(" | Unmatched left/right copies");
                ++pairIssues;
            }
            if (!piece.grainAngle)
                details += tr(" | Grain direction unspecified");
        }
        qsizetype seamIssues = 0;
        if (!document.seamLinks().isEmpty()) {
            details += tr("\n\nSeam link checks");
            for (const auto& size : checkedSizes) {
                const auto pattern = opc::gradedPattern(document, size);
                const auto checks = pattern.inspectSeamLinks().checks;
                const auto label = size.isEmpty() ? tr("Base") : size;
                if (checks.size() != document.seamLinks().size()) {
                    details += tr("\n%1 · Could not recalculate seam links").arg(label);
                    ++seamIssues;
                    continue;
                }
                for (qsizetype i = 0; i < checks.size(); ++i) {
                    const auto& check = checks[i];
                    const auto& link = document.seamLinks()[i];
                    const auto result = check.error.isEmpty()
                        ? tr("Ease %L1 mm · Target %L2 ± %L3 mm · %4")
                            .arg(check.secondMm - check.firstMm, 0, 'f', 2)
                            .arg(link.targetEaseMm, 0, 'f', 2).arg(link.toleranceMm, 0, 'f', 2)
                            .arg(check.withinTolerance ? tr("Within tolerance") : tr("Outside tolerance"))
                        : check.error;
                    details += tr("\n%1 · Seam link %2 · %3").arg(label).arg(i + 1).arg(result);
                    if (!check.error.isEmpty() || !check.withinTolerance)
                        ++seamIssues;
                }
            }
        }
        if (data.placements.isEmpty() || usage.invalid || usage.outside || usage.overlaps
            || report.orientationViolations || report.foldViolations || report.repeatViolations || countIssues || pairIssues || seamIssues) {
            text += tr("\nReview the issues below before cutting.\n");
        } else {
            text += tr("\nLayout geometry, copy counts and handed pairs pass the checks.\n");
        }
        text += tr("Counts compare one kit per plotted size. Check fabric, markings and fit with a sample before production.\n");
        text += details;
        QDialog reportDialog(&dialog);
        reportDialog.setWindowTitle(tr("Cutting report"));
        reportDialog.resize(700, 440);
        QVBoxLayout reportLayout(&reportDialog);
        auto* reportText = new QPlainTextEdit(text);
        reportText->setReadOnly(true);
        reportLayout.addWidget(reportText);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
        reportLayout.addWidget(close);
        QObject::connect(close, &QDialogButtonBox::rejected, &reportDialog, &QDialog::reject);
        reportDialog.exec();
    });
    QObject::connect(automatic, &QPushButton::clicked, &dialog, [&] {
        auto result = opc::arrangeFabric(document, data);
        QString error;
        switch (result.error) {
        case opc::FabricLayoutError::None:
            data.placements = std::move(result.placements);
            rebuild();
            fitFabric();
            return;
        case opc::FabricLayoutError::TooManyCopies:
            error = tr("Automatic layout supports up to %1 copies.").arg(opc::maxAutomaticCopies);
            break;
        case opc::FabricLayoutError::InvalidWidth:
            error = tr("Enter a positive fabric width.");
            break;
        case opc::FabricLayoutError::InvalidClearance:
            error = tr("Enter a piece clearance from 0 to 1000 mm.");
            break;
        case opc::FabricLayoutError::InvalidRepeat:
            error = tr("Enter fabric repeats from 0 to 10000 mm.");
            break;
        case opc::FabricLayoutError::InvalidContour:
            error = tr("The cutting outline, seam allowance, or required straight fold edge is invalid.");
            break;
        case opc::FabricLayoutError::TooWide:
            error = tr("The cutting outline is wider than the fabric with the grain aligned.");
            break;
        case opc::FabricLayoutError::VerificationFailed:
            error = tr("Automatic layout could not produce a verified arrangement.");
            break;
        }
        if (result.failedCopy >= 0) {
            const auto& copy = data.placements[result.failedCopy];
            error = tr("Copy %1 · %2 · %3\n%4").arg(result.failedCopy + 1)
                .arg(document.pieces()[copy.piece].name, copy.size.isEmpty() ? tr("Base") : copy.size, error);
        }
        QMessageBox::warning(&dialog, dialog.windowTitle(), error);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rebuild();
    QTimer::singleShot(0, &dialog, fitFabric);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    QObject::disconnect(&scene, nullptr, &dialog, nullptr);
    view->setScene(nullptr);
    return accepted ? std::optional(std::move(data)) : std::nullopt;
}
