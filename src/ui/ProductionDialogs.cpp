#include "ProductionDialogs.h"
#include "Production.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainterPath>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
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
    PlacementItem(qsizetype index, const QPolygonF& cut, std::function<void()> moved)
        : QGraphicsPolygonItem(cut), index(index), valid(!cut.isEmpty()), moved_(std::move(moved)) {
        if (!valid)
            setPolygon(QPolygonF(QRectF(0, 0, 100, 40)));
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    }
    const qsizetype index;
    const bool valid;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == ItemPositionHasChanged && scene())
            moved_();
        return QGraphicsPolygonItem::itemChange(change, value);
    }

private:
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
    auto* sizesHint = new QLabel(tr("Use one row per measurement. Repeat the size name for each measurement. Leave the last two cells empty to define a size without measurements."));
    sizesHint->setWordWrap(true);
    sizesLayout->addWidget(sizesHint);
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
    tableButtons(sizesLayout, sizes)->insertWidget(0, addSize);
    QObject::connect(addSize, &QPushButton::clicked, &dialog, [=] {
        addMeasurement({}, {}, {});
        sizes->editItem(sizes->item(sizes->rowCount() - 1, 0));
    });

    auto* rulesHint = new QLabel(tr("Enter the target size and choose a point on the base piece. Enter X and Y offsets in mm. Positive Y moves down. Coincident points on the same piece move together."));
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
    auto* piece = new QComboBox;
    for (const auto& entry : document.pieces())
        piece->addItem(entry.name);
    controls->addWidget(piece, 1);
    auto* size = new QComboBox;
    size->addItem(tr("Base pattern"), QString{});
    for (const auto& entry : data.sizes)
        size->addItem(entry.name, entry.name);
    controls->addWidget(size);
    auto* add = new QPushButton(tr("Add copy"));
    add->setEnabled(piece->count() > 0);
    controls->addWidget(add);
    auto* turn = new QPushButton(tr("Turn 180°"));
    controls->addWidget(turn);
    auto* remove = new QPushButton(tr("Remove copy"));
    controls->addWidget(remove);
    auto* fit = new QPushButton(tr("Fit"));
    controls->addWidget(fit);

    auto* hint = new QLabel(tr("Drag copies to position them. Grain runs along the fabric length. Pieces without a grainline keep their base orientation."));
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
        for (auto* item : items) {
            auto& placement = data.placements[item->index];
            placement.position = item->pos();
            placed.append(item->valid ? item->polygon().translated(item->pos()) : QPolygonF{});
        }
        const auto usage = opc::fabricUsage(placed, data.fabricWidth);
        const double displayLength = std::max(1000.0, usage.length + 200);
        fabric->setRect(0, 0, data.fabricWidth, displayLength);
        stats->setText(tr("Used length: %1 m · Copies: %2 · Outside fabric: %3 · Overlapping pairs: %4 · Invalid contours: %5")
                           .arg(usage.length / 1000, 0, 'f', 3).arg(items.size())
                           .arg(usage.outside).arg(usage.overlaps).arg(usage.invalid));
    };
    const auto updateSceneBounds = [&] {
        auto bounds = scene.itemsBoundingRect();
        bounds.setBottom(std::max(bounds.bottom(), 2000.0));
        scene.setSceneRect(bounds.adjusted(-80, -80, 80, 80));
    };
    const auto addItem = [&](qsizetype index) {
        const auto& placement = data.placements[index];
        const auto cut = cutFor(placement);
        auto* item = new PlacementItem(index, cut.cut, updateUsage);
        item->setPen(QPen(QColor("#173b5e"), 0));
        item->setBrush(QColor(86, 167, 222, 125));
        item->setPos(placement.position);
        const auto& entry = document.pieces()[placement.piece];
        auto* label = new QGraphicsSimpleTextItem(
            tr("%1 · %2 (cut ×%3)").arg(entry.name, placement.size.isEmpty() ? tr("Base") : placement.size)
                                     .arg(entry.cutCount), item);
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
        turn->setEnabled(selected());
        remove->setEnabled(selected());
    });
    turn->setEnabled(false);
    remove->setEnabled(false);
    QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
        data.fabricWidth = value;
        updateUsage();
        updateSceneBounds();
    });
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        opc::FabricPlacement placement{piece->currentIndex(), size->currentData().toString(), {}};
        if (cutFor(placement).cut.isEmpty()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), tr("This size needs a valid closed contour and a valid allowance."));
            return;
        }
        for (auto* item : items) {
            if (item->valid)
                placement.position.setY(std::max(placement.position.y(),
                                                item->pos().y() + item->polygon().boundingRect().bottom() + 10));
        }
        data.placements.append(placement);
        addItem(data.placements.size() - 1);
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
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rebuild();
    QTimer::singleShot(0, &dialog, fitFabric);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    QObject::disconnect(&scene, nullptr, &dialog, nullptr);
    view->setScene(nullptr);
    return accepted ? std::optional(std::move(data)) : std::nullopt;
}
