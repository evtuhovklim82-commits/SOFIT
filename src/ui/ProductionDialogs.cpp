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
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMap>
#include <QPainter>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStringDecoder>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <set>
#include <tuple>
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

void addContextHelp(QDialog& dialog, QDialogButtonBox& buttons, const QString& title, const QString& text) {
    const auto showHelp = [&dialog, title, text] {
        QMessageBox::information(&dialog, title, text);
    };
    auto* help = buttons.addButton(QStringLiteral("Справка"), QDialogButtonBox::HelpRole);
    QObject::connect(help, &QPushButton::clicked, &dialog, showHelp);
    auto* shortcut = new QShortcut(QKeySequence(Qt::Key_F1), &dialog);
    QObject::connect(shortcut, &QShortcut::activated, &dialog, showHelp);
}

std::optional<QVector<QRectF>> editFabricFlaws(QWidget* parent, const QVector<QRectF>& flaws) {
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Fabric Flaws"));
    dialog.resize(550, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(tr("Enter the position and size of each rectangular area to avoid, in millimetres. "
                               "The origin is the top-left corner of the fabric."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* table = makeTable(layout, {tr("X across"), tr("Y along"), tr("Width"), tr("Length")});
    const auto add = [table](const QRectF& flaw) {
        const auto row = table->rowCount();
        table->insertRow(row);
        const std::array values{flaw.x(), flaw.y(), flaw.width(), flaw.height()};
        for (int column = 0; column < int(values.size()); ++column)
            table->setItem(row, column, new QTableWidgetItem(number(values[column])));
    };
    for (const auto& flaw : flaws)
        add(flaw);
    auto* actions = tableButtons(layout, table);
    auto* addRow = new QPushButton(tr("Add flaw"));
    actions->insertWidget(0, addRow);
    QObject::connect(addRow, &QPushButton::clicked, &dialog, [&] {
        if (table->rowCount() < 100)
            add(QRectF(0, 0, 10, 10));
    });
    auto* buttons = dialogButtons(layout, dialog);
    QVector<QRectF> result;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        result.clear();
        for (int row = 0; row < table->rowCount(); ++row) {
            std::array<double, 4> values;
            for (int column = 0; column < int(values.size()); ++column) {
                bool valid;
                values[column] = table->item(row, column)->text().toDouble(&valid);
                if (!valid) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(), tr("Row %1 needs four numbers.").arg(row + 1));
                    return;
                }
            }
            result.append(QRectF(values[0], values[1], values[2], values[3]));
            if (!opc::validFabricFlaws(result)) {
                QMessageBox::warning(&dialog, dialog.windowTitle(),
                    tr("Row %1 needs positive dimensions and coordinates within 0–1,000,000 mm.").arg(row + 1));
                return;
            }
        }
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted ? std::optional(std::move(result)) : std::nullopt;
}

class PlacementItem : public QGraphicsPolygonItem {
public:
    PlacementItem(qsizetype index, const opc::LayoutPiece& layout, std::optional<double> lockedX,
                  std::function<void()> moved)
        : QGraphicsPolygonItem(layout.cut), index(index), valid(!layout.cut.isEmpty()),
          repeatPoint(layout.repeatPoint), lockedX_(lockedX),
          moved_(std::move(moved)) {
        if (!valid)
            setPolygon(QPolygonF(QRectF(0, 0, 100, 40)));
        setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    }
    const qsizetype index;
    const bool valid;
    const std::optional<QPointF> repeatPoint;

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
    auto* appendTable = new QPushButton(QStringLiteral("Добавить мерки из буфера"));
    auto* importTable = new QPushButton(QStringLiteral("Загрузить мерки из TSV…"));
    auto* exportTable = new QPushButton(QStringLiteral("Сохранить мерки в TSV…"));
    auto* exchangeButtons = new QHBoxLayout;
    exchangeButtons->addWidget(appendTable);
    exchangeButtons->addWidget(importTable);
    exchangeButtons->addWidget(exportTable);
    auto* library = new QPushButton(QStringLiteral("Библиотека мерок…"));
    exchangeButtons->addWidget(library);
    auto* standard = new QPushButton(QStringLiteral("Типовая фигура ГОСТ…"));
    exchangeButtons->addWidget(standard);
    exchangeButtons->addStretch();
    sizesLayout->addLayout(exchangeButtons);
    const auto header = QStringLiteral("Size\tMeasurement\tValue (mm)");
    const auto measurementTsv = [=, &dialog]() -> std::optional<QString> {
        QStringList lines{header};
        for (int row = 0; row < sizes->rowCount(); ++row) {
            QStringList cells;
            for (int column = 0; column < sizes->columnCount(); ++column) {
                const auto value = sizes->item(row, column)->text();
                if (value.contains(u'\t') || value.contains(u'\r') || value.contains(u'\n')) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                                         tr("Cells containing tabs or line breaks cannot be copied as a table."));
                    return std::nullopt;
                }
                cells.append(value);
            }
            lines.append(cells.join(u'\t'));
        }
        return lines.join(u'\n');
    };
    QObject::connect(copyTable, &QPushButton::clicked, &dialog, [=] {
        if (const auto text = measurementTsv())
            QApplication::clipboard()->setText(*text);
    });
    const auto saveMeasurementTable = [=, &dialog](const QString& directory) {
        const auto text = measurementTsv();
        if (!text)
            return;
        const auto path = QFileDialog::getSaveFileName(QApplication::activeModalWidget(), exportTable->text(), directory, QStringLiteral("TSV (*.tsv)"));
        if (path.isEmpty())
            return;
        QSaveFile file(path);
        const auto bytes = text->toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Не удалось сохранить таблицу мерок.\n%1").arg(file.errorString()));
    };
    QObject::connect(exportTable, &QPushButton::clicked, &dialog, [=] { saveMeasurementTable({}); });
    const auto readTsv = [=, &dialog](QString text) -> QVector<QStringList> {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        auto lines = text.split(u'\n');
        if (!lines.isEmpty() && lines.back().isEmpty())
            lines.removeLast();
        if (!lines.isEmpty() && lines.front() == header)
            lines.removeFirst();
        if (lines.isEmpty())
            return {};
        QVector<QStringList> rows;
        for (const auto& line : lines) {
            auto cells = line.split(u'\t');
            if (cells.size() != 3 || cells[0].trimmed().isEmpty() || line.contains(u'\r')) {
                QMessageBox::warning(&dialog, dialog.windowTitle(),
                    QStringLiteral("Строка %1 должна содержать три колонки: размер, мерка и значение в мм.")
                        .arg(rows.size() + 1));
                return {};
            }
            rows.append(std::move(cells));
        }
        return rows;
    };
    QObject::connect(pasteTable, &QPushButton::clicked, &dialog, [=] {
        const auto rows = readTsv(QApplication::clipboard()->text());
        if (rows.isEmpty())
            return;
        sizes->setRowCount(0);
        for (const auto& cells : rows)
            addMeasurement(cells[0], cells[1], cells[2]);
    });
    QObject::connect(addSize, &QPushButton::clicked, &dialog, [=] {
        addMeasurement({}, {}, {});
        sizes->editItem(sizes->item(sizes->rowCount() - 1, 0));
    });

    const auto readMeasurements = [sizes](QString& error, const QVector<QStringList>& appended = {}) {
        QVector<opc::PatternSize> entries;
        QMap<QString, qsizetype> sizeIndices;
        const auto cell = [sizes, &appended](qsizetype row, int column) {
            return (row < sizes->rowCount() ? sizes->item(row, column)->text()
                                            : appended[row - sizes->rowCount()][column]).trimmed();
        };
        for (qsizetype row = 0; row < sizes->rowCount() + appended.size(); ++row) {
            const auto size = cell(row, 0);
            const auto name = cell(row, 1);
            const auto value = cell(row, 2);
            if (size.isEmpty()) {
                error = tr("Enter a size name in measurement row %1.").arg(row + 1);
                return QVector<opc::PatternSize>{};
            }
            if (!sizeIndices.contains(size)) {
                sizeIndices.insert(size, entries.size());
                entries.append({size, {}});
            }
            if (name.isEmpty() && value.isEmpty())
                continue;
            bool ok = false;
            const double mm = value.toDouble(&ok);
            auto& measurements = entries[sizeIndices[size]].measurements;
            if (name.isEmpty() || !ok || !std::isfinite(mm) || mm <= 0 || measurements.contains(name)) {
                error = tr("Measurement row %1 needs a unique measurement name for its size and a positive value in mm.").arg(row + 1);
                return QVector<opc::PatternSize>{};
            }
            measurements.insert(name, mm);
        }
        return entries;
    };
    const auto appendMeasurements = [=, &dialog](const QString& text) {
        const auto rows = readTsv(text);
        if (rows.isEmpty())
            return;
        QString error;
        readMeasurements(error, rows);
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Мерки не добавлены.\n%1").arg(error));
            return;
        }
        for (const auto& cells : rows)
            addMeasurement(cells[0], cells[1], cells[2]);
    };
    QObject::connect(appendTable, &QPushButton::clicked, &dialog, [=] {
        appendMeasurements(QApplication::clipboard()->text());
    });
    QObject::connect(standard, &QPushButton::clicked, &dialog, [=, &dialog] {
        QDialog reference(&dialog);
        reference.setWindowTitle(QStringLiteral("ГОСТ 31396-2009 — женские фигуры"));
        QFormLayout form(&reference);
        QLabel source(QStringLiteral("Таблица 3, полнотная группа 0. Доступен фрагмент ряда "
                                    "для обхватов груди 84–104 см и ростов 152–170 см.<br>"
                                    "<a href='https://protect.gost.ru/gost/details/7d9919ad-6695-41c5-93e1-8224eaf3117d'>"
                                    "Карточка Росстандарта</a>"));
        source.setWordWrap(true);
        source.setOpenExternalLinks(true);
        form.addRow(&source);
        QComboBox height, bust;
        for (int cm : {152, 158, 164, 170})
            height.addItem(QString::number(cm), cm);
        // This block of table 3 lists a hip circumference 2 cm below the bust.
        for (int cm : {84, 88, 92, 96, 100, 104})
            bust.addItem(QString::number(cm), cm);
        form.addRow(QStringLiteral("Рост, см"), &height);
        form.addRow(QStringLiteral("Обхват груди, см"), &bust);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        QObject::connect(&buttons, &QDialogButtonBox::accepted, &reference, &QDialog::accept);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &reference, &QDialog::reject);
        if (reference.exec() != QDialog::Accepted)
            return;
        const int statureCm = height.currentData().toInt();
        const int bustCm = bust.currentData().toInt();
        const int hipCm = bustCm - 2;
        const auto size = QStringLiteral("ГОСТ 31396-2009 %1-%2-%3").arg(statureCm).arg(bustCm).arg(hipCm);
        const auto row = [&](const QString& measurement, int cm) {
            return QStringLiteral("%1\t%2\t%3").arg(size, measurement).arg(cm * 10);
        };
        appendMeasurements(QStringList{row(QStringLiteral("height"), statureCm),
                                       row(QStringLiteral("bust"), bustCm),
                                       row(QStringLiteral("hip"), hipCm)}.join(u'\n'));
    });
    const auto importMeasurementTable = [=, &dialog](const QString& directory) {
        const auto path = QFileDialog::getOpenFileName(QApplication::activeModalWidget(), importTable->text(), directory, QStringLiteral("TSV (*.tsv)"));
        if (path.isEmpty())
            return;
        constexpr qint64 maxTableBytes = 4 * 1024 * 1024;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Не удалось открыть таблицу мерок.\n%1").arg(file.errorString()));
            return;
        }
        const auto bytes = file.read(maxTableBytes + 1);
        QStringDecoder decode(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString text = decode(bytes);
        if (file.error() != QFileDevice::NoError || bytes.size() > maxTableBytes || decode.hasError()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Не удалось прочитать таблицу. Требуется файл UTF-8 размером до 4 МБ."));
            return;
        }
        appendMeasurements(text);
    };
    QObject::connect(importTable, &QPushButton::clicked, &dialog, [=] { importMeasurementTable({}); });
    QObject::connect(library, &QPushButton::clicked, &dialog, [=, &dialog] {
        QSettings settings;
        const auto key = QStringLiteral("grading/measurementLibraryDirectory");
        auto directory = settings.value(key).toString();
        QDialog catalog(&dialog);
        catalog.setWindowTitle(library->text());
        QFormLayout form(&catalog);
        QLabel location;
        auto* choose = new QPushButton(QStringLiteral("Выбрать папку…"));
        auto* load = new QPushButton(QStringLiteral("Добавить систему в проект…"));
        auto* save = new QPushButton(QStringLiteral("Сохранить текущую таблицу…"));
        form.addRow(QStringLiteral("Папка"), &location);
        form.addRow(choose);
        form.addRow(load);
        form.addRow(save);
        QDialogButtonBox close(QDialogButtonBox::Close);
        form.addRow(&close);
        const auto refresh = [&] {
            const bool available = !directory.isEmpty() && QDir(directory).exists();
            location.setText(available ? QDir::toNativeSeparators(directory) : QStringLiteral("Выберите папку с таблицами TSV"));
            load->setEnabled(available);
            save->setEnabled(available);
        };
        QObject::connect(choose, &QPushButton::clicked, &catalog, [&] {
            const auto path = QFileDialog::getExistingDirectory(&catalog, choose->text(), directory);
            if (!path.isEmpty()) {
                directory = path;
                settings.setValue(key, directory);
                refresh();
            }
        });
        QObject::connect(load, &QPushButton::clicked, &catalog, [&] { importMeasurementTable(directory); });
        QObject::connect(save, &QPushButton::clicked, &catalog, [&] {
            QString error;
            const auto entries = readMeasurements(error);
            if (!error.isEmpty() || entries.isEmpty()) {
                QMessageBox::warning(&catalog, catalog.windowTitle(), error.isEmpty()
                    ? QStringLiteral("Добавьте хотя бы одну строку мерок.") : error);
                return;
            }
            saveMeasurementTable(directory);
        });
        QObject::connect(&close, &QDialogButtonBox::rejected, &catalog, &QDialog::reject);
        refresh();
        catalog.exec();
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

    const auto readRules = [rules](const QSet<QString>& sizeNames, QString& error) {
        QVector<opc::GradeRule> result;
        for (int row = 0; row < rules->rowCount(); ++row) {
            const auto size = rules->item(row, 0)->text().trimmed();
            auto* piece = qobject_cast<QComboBox*>(rules->cellWidget(row, 1));
            auto* point = qobject_cast<QComboBox*>(rules->cellWidget(row, 2));
            bool xOk, yOk;
            const double x = rules->item(row, 3)->text().toDouble(&xOk);
            const double y = rules->item(row, 4)->text().toDouble(&yOk);
            if (!sizeNames.contains(size) || point->currentIndex() < 0 || !xOk || !yOk || !std::isfinite(x) || !std::isfinite(y)) {
                error = tr("Grade rule row %1 needs a defined size, a point and two numeric offsets.").arg(row + 1);
                return QVector<opc::GradeRule>{};
            }
            result.append({size, piece->currentIndex(), point->currentData().toPointF(), {x, y}});
        }
        return result;
    };

    auto* interpolate = new QPushButton(QStringLiteral("Добавить промежуточный размер…"));
    auto* interpolationButtons = new QHBoxLayout;
    sizesLayout->addLayout(interpolationButtons);
    interpolationButtons->addWidget(interpolate);
    interpolationButtons->addStretch();
    auto* grid = new QPushButton(QStringLiteral("Добавить по размеру и росту…"));
    interpolationButtons->insertWidget(1, grid);
    auto* batchGrid = new QPushButton(QStringLiteral("Группа размеров и ростов…"));
    interpolationButtons->insertWidget(2, batchGrid);
    const auto interpolateSize = [&](QPushButton* command, bool twoAxes, bool batch = false) {
        QString error;
        const auto entries = readMeasurements(error);
        const int count = twoAxes ? 4 : 2;
        if (!error.isEmpty() || entries.size() < count) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), error.isEmpty()
                ? QStringLiteral("Для этой операции нужно %1 разных опорных строк.").arg(count) : error);
            return;
        }

        QDialog interpolation(&dialog);
        interpolation.setWindowTitle(command->text());
        auto* layout = new QVBoxLayout(&interpolation);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        const QStringList gridLabels{
            QStringLiteral("Меньший размер, меньший рост"),
            QStringLiteral("Больший размер, меньший рост"),
            QStringLiteral("Меньший размер, больший рост"),
            QStringLiteral("Больший размер, больший рост")
        };
        QVector<QComboBox*> references;
        for (int i = 0; i < count; ++i) {
            auto* reference = new QComboBox;
            for (const auto& entry : entries)
                reference->addItem(entry.name);
            reference->setCurrentIndex(i);
            references.append(reference);
            form->addRow(twoAxes ? gridLabels[i] : i == 0 ? QStringLiteral("Начальный размер")
                                                         : QStringLiteral("Конечный размер"), reference);
        }
        const auto addPercentage = [&](const QString& label) {
            auto* field = new QDoubleSpinBox;
            field->setRange(twoAxes ? 0 : 0.01, twoAxes ? 100 : 99.99);
            field->setValue(50);
            field->setSuffix(QStringLiteral(" %"));
            form->addRow(label, field);
            return field;
        };
        auto* sizeShare = batch ? nullptr : addPercentage(twoAxes ? QStringLiteral("Доля по размеру")
                                                : QStringLiteral("Доля от начального к конечному"));
        auto* heightShare = twoAxes && !batch ? addPercentage(QStringLiteral("Доля по росту")) : nullptr;
        auto* name = batch ? nullptr : new QLineEdit;
        auto* targets = batch ? new QPlainTextEdit : nullptr;
        if (batch) {
            targets->setPlaceholderText(QStringLiteral("49/168\t25\t25\n50/172\t50\t50"));
            form->addRow(QStringLiteral("Размер, доля размера и доля роста (%), через табуляцию"), targets);
            QSpinBox* limits[2][3];
            for (int axis = 0; axis < 2; ++axis) {
                auto* range = new QHBoxLayout;
                const int defaults[2][3] = {{44, 52, 2}, {164, 180, 4}};
                const QStringList labels{QStringLiteral("От"), QStringLiteral("До"), QStringLiteral("Шаг")};
                for (int column = 0; column < 3; ++column) {
                    limits[axis][column] = new QSpinBox;
                    limits[axis][column]->setRange(1, 999);
                    limits[axis][column]->setValue(defaults[axis][column]);
                    range->addWidget(new QLabel(labels[column]));
                    range->addWidget(limits[axis][column]);
                }
                form->addRow(axis == 0 ? QStringLiteral("Ряд размеров") : QStringLiteral("Ряд ростов (см)"), range);
            }
            auto* generate = new QPushButton(QStringLiteral("Заполнить размерно-ростовой ряд"));
            form->addRow(generate);
            QObject::connect(generate, &QPushButton::clicked, &interpolation, [=, &interpolation] {
                const int sizeMin = limits[0][0]->value(), sizeMax = limits[0][1]->value();
                const int sizeStep = limits[0][2]->value();
                const int heightMin = limits[1][0]->value(), heightMax = limits[1][1]->value();
                const int heightStep = limits[1][2]->value();
                if (sizeMin >= sizeMax || heightMin >= heightMax
                    || (sizeMax - sizeMin) % sizeStep || (heightMax - heightMin) % heightStep) {
                    QMessageBox::warning(&interpolation, interpolation.windowTitle(),
                        QStringLiteral("Укажите возрастающие границы и шаг, который точно приводит к верхней границе."));
                    return;
                }
                const int count = ((sizeMax - sizeMin) / sizeStep + 1)
                    * ((heightMax - heightMin) / heightStep + 1) - 4;
                if (count < 1 || count > 500) {
                    QMessageBox::warning(&interpolation, interpolation.windowTitle(),
                        QStringLiteral("Ряд должен содержать от 1 до 500 новых размеров и ростов."));
                    return;
                }
                QStringList lines;
                for (int size = sizeMin; size <= sizeMax; size += sizeStep) {
                    for (int height = heightMin; height <= heightMax; height += heightStep) {
                        if ((size == sizeMin || size == sizeMax) && (height == heightMin || height == heightMax))
                            continue;
                        lines.append(QStringLiteral("%1/%2\t%3\t%4").arg(size).arg(height)
                            .arg(number(100.0 * (size - sizeMin) / (sizeMax - sizeMin)))
                            .arg(number(100.0 * (height - heightMin) / (heightMax - heightMin))));
                    }
                }
                targets->setPlainText(lines.join(u'\n'));
            });

        } else {
            form->addRow(QStringLiteral("Новый размер"), name);
        }
        auto* includeRules = new QCheckBox(QStringLiteral("Рассчитать смещения точек"));
        form->addRow(includeRules);

        auto* buttons = dialogButtons(layout, interpolation);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &interpolation, [&] {
            const auto warn = [&](const QString& message) {
                QMessageBox::warning(&interpolation, interpolation.windowTitle(), message);
            };
            struct Target {
                QString name;
                double sizeShare;
                double heightShare;
            };
            QVector<Target> requested;
            QSet<QString> names;
            for (const auto& entry : entries)
                names.insert(entry.name);
            const auto addTarget = [&](const QString& name, double size, double height) {
                if (name.isEmpty() || name.contains(u'\t') || name.contains(u'\r') || name.contains(u'\n')
                    || names.contains(name) || !(size >= 0 && size <= 100 && height >= 0 && height <= 100))
                    return false;
                names.insert(name);
                requested.append({name, size / 100, height / 100});
                return true;
            };
            if (batch) {
                const auto lines = targets->toPlainText().split(u'\n');
                for (qsizetype row = 0; row < lines.size(); ++row) {
                    if (lines[row].trimmed().isEmpty())
                        continue;
                    const auto cells = lines[row].split(u'\t');
                    bool sizeOk = false, heightOk = false;
                    const double size = cells.value(1).trimmed().toDouble(&sizeOk);
                    const double height = cells.value(2).trimmed().toDouble(&heightOk);
                    if (cells.size() != 3 || !sizeOk || !heightOk
                        || !addTarget(cells.front().trimmed(), size, height)) {
                        warn(QStringLiteral("Строка %1: укажите уникальное название и две доли от 0 до 100 %.").arg(row + 1));
                        return;
                    }
                }
                if (requested.isEmpty()) {
                    warn(QStringLiteral("Введите хотя бы одну строку для расчёта."));
                    return;
                }
            } else if (!addTarget(name->text().trimmed(), sizeShare->value(), twoAxes ? heightShare->value() : 0)) {
                warn(QStringLiteral("Введите новое уникальное название размера без табуляции и переносов строки."));
                return;
            }
            const auto& first = entries[references.front()->currentIndex()].measurements;
            const auto keys = first.keys();
            QMap<QString, int> referenceIndices;
            for (int i = 0; i < count; ++i) {
                const auto& entry = entries[references[i]->currentIndex()];
                referenceIndices.insert(entry.name, i);
                if (keys.isEmpty() || entry.measurements.keys() != keys) {
                    warn(QStringLiteral("У всех опорных строк должен быть одинаковый непустой набор мерок."));
                    return;
                }
            }
            if (referenceIndices.size() != count) {
                warn(QStringLiteral("Выберите разные опорные строки."));
                return;
            }

            const auto blend = [&](const std::array<double, 4>& values, const Target& target) {
                const double low = std::lerp(values[0], values[1], target.sizeShare);
                return twoAxes ? std::lerp(low, std::lerp(values[2], values[3], target.sizeShare),
                                          target.heightShare) : low;
            };
            using PointKey = std::tuple<qsizetype, double, double>;
            std::array<QMap<PointKey, QPointF>, 4> offsets;
            if (includeRules->isChecked()) {
                QSet<QString> sizeNames;
                for (const auto& entry : entries)
                    sizeNames.insert(entry.name);
                QString ruleError;
                const auto existingRules = readRules(sizeNames, ruleError);
                if (!ruleError.isEmpty()) {
                    warn(ruleError);
                    return;
                }
                for (const auto& rule : existingRules) {
                    const auto reference = referenceIndices.constFind(rule.size);
                    if (reference == referenceIndices.cend())
                        continue;
                    auto& rulesAtReference = offsets[reference.value()];
                    const auto key = PointKey(rule.piece, rule.point.x(), rule.point.y());
                    if (rulesAtReference.contains(key)) {
                        warn(QStringLiteral("Уберите повторяющиеся правила для одной точки опорного размера."));
                        return;
                    }
                    rulesAtReference.insert(key, rule.offset);
                }
                const auto points = offsets.front().keys();
                for (int i = 1; i < count; ++i) {
                    if (offsets[i].keys() != points) {
                        warn(QStringLiteral("Задайте правила для одних и тех же точек во всех опорных размерах. "
                                            "Укажите нулевое смещение явно, если точка не смещается."));
                        return;
                    }
                }
            }

            for (const auto& target : requested) {
                for (const auto& key : keys) {
                    std::array<double, 4> values{};
                    for (int i = 0; i < count; ++i)
                        values[i] = entries[references[i]->currentIndex()].measurements.value(key);
                    addMeasurement(target.name, key, number(blend(values, target)));
                }
                for (auto point = offsets.front().cbegin(); point != offsets.front().cend(); ++point) {
                    const auto [piece, x, y] = point.key();
                    std::array<double, 4> dx{}, dy{};
                    for (int i = 0; i < count; ++i) {
                        const auto offset = offsets[i].value(point.key());
                        dx[i] = offset.x();
                        dy[i] = offset.y();
                    }
                    addRule({target.name, piece, {x, y}, {blend(dx, target), blend(dy, target)}}, true);
                }
            }
            interpolation.accept();
        });
        interpolation.exec();
    };
    QObject::connect(interpolate, &QPushButton::clicked, &dialog, [&] { interpolateSize(interpolate, false); });
    QObject::connect(grid, &QPushButton::clicked, &dialog, [&] { interpolateSize(grid, true); });
    QObject::connect(batchGrid, &QPushButton::clicked, &dialog, [&] { interpolateSize(batchGrid, true, true); });

    auto* buttons = dialogButtons(layout, dialog);
    addContextHelp(dialog, *buttons, QStringLiteral("Градация лекал"), QStringLiteral(
        "Добавьте отдельную строку для каждого сочетания размера и роста, например 48/164 и 48/176. "
        "В таблице мерок задайте значения в миллиметрах. Пропущенные мерки берутся из базовой конструкции.\n\n"
        "Для переноса мерок между моделями скопируйте таблицу в исходном проекте и нажмите «Добавить мерки из буфера» в целевом. "
        "Команды сохранения и загрузки TSV позволяют повторно использовать таблицу мерок из файла UTF-8. "
        "Совпадающие мерки одного размера вызывают отказ всей вставки.\n\n"
        "Чтобы получить мерки промежуточного размера, выберите два опорных размера с одинаковым набором мерок. "
        "Задайте долю от начального к конечному размеру. Для 48/170 между 48/164 и 48/176 укажите 50 %. "
        "Чтобы также рассчитать смещения, включите «Рассчитать смещения точек». Правила обоих опорных размеров должны ссылаться на одинаковые точки.\n\n"
        "Для расчёта группы выберите четыре опорные строки. Для каждого нового размера введите его название, долю по размеру и долю по росту через табуляцию.\n\n"
        "Для параметрических лекал включите пересчёт по меркам размера. Правила градации задают дополнительные "
        "смещения точек. Сравните контуры наложением размеров и проверьте сопрягаемые срезы перед раскладкой."));
    auto result = document.production();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto warn = [&](const QString& message) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), message);
        };
        auto updated = document.production();
        updated.rules.clear();
        QString measurementError;
        updated.sizes = readMeasurements(measurementError);
        if (!measurementError.isEmpty()) {
            warn(measurementError);
            return;
        }
        QSet<QString> sizeNames;
        for (const auto& size : updated.sizes)
            sizeNames.insert(size.name);
        QString ruleError;
        updated.rules = readRules(sizeNames, ruleError);
        if (!ruleError.isEmpty()) {
            warn(ruleError);
            return;
        }
        for (const auto& placement : updated.placements) {
            if (!placement.size.isEmpty() && !sizeNames.contains(placement.size)) {
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

std::optional<opc::ProductionData> editFabricLayout(QWidget* parent, const opc::PatternDocument& document, const QString& material) {
    QDialog dialog(parent);
    dialog.setWindowTitle(material.isEmpty() ? tr("Fabric Layout") : tr("Fabric Layout · %1").arg(material));
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
    auto* flawsButton = new QPushButton(tr("Fabric flaws…"));
    automaticControls->addWidget(flawsButton);
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

    auto* shrinkageControls = new QHBoxLayout;
    layout->addLayout(shrinkageControls);
    const auto shrinkageInput = [&](const QString& label, double value, const QString& name) {
        auto* input = new QDoubleSpinBox;
        input->setObjectName(name);
        input->setRange(0, 99);
        input->setDecimals(2);
        input->setSuffix(QStringLiteral(" %"));
        input->setValue(value);
        auto* caption = new QLabel(label);
        caption->setBuddy(input);
        shrinkageControls->addWidget(caption);
        shrinkageControls->addWidget(input);
        return input;
    };
    auto* weft = shrinkageInput(QStringLiteral("Усадка по утку"), data.fabricShrinkagePercent.x(), "markerWeftShrinkage");
    auto* warp = shrinkageInput(QStringLiteral("Усадка по основе"), data.fabricShrinkagePercent.y(), "markerWarpShrinkage");
    shrinkageControls->addStretch();

    auto* registrationControls = new QHBoxLayout;
    layout->addLayout(registrationControls);
    auto* registration = new QComboBox;
    registration->setObjectName(QStringLiteral("markerRepeatEdge"));
    registrationControls->addWidget(new QLabel(tr("Match motif at")));
    registrationControls->addWidget(registration, 1);
    auto* registrationParameter = new QDoubleSpinBox;
    registrationParameter->setObjectName(QStringLiteral("markerRepeatParameter"));
    registrationParameter->setRange(0, 100);
    registrationParameter->setValue(50);
    registrationParameter->setDecimals(2);
    registrationParameter->setSuffix(tr("%"));
    registrationParameter->setAccessibleName(tr("Parameter from the start of the seam edge"));
    registrationControls->addWidget(registrationParameter);
    auto* setRegistration = new QPushButton(tr("Apply to selected copy"));
    setRegistration->setEnabled(false);
    registrationControls->addWidget(setRegistration);
    const auto fillRegistration = [&] {
        registration->clear();
        registration->addItem(tr("Cutting outline corner"), QString{});
        for (const auto& edge : document.entities()) {
            if (edge.piece == piece->currentIndex() && edge.layer == opc::Layer::Main
                && !edge.draftName.isEmpty() && (edge.shape == opc::Shape::Line || edge.shape == opc::Shape::Curve))
                registration->addItem(edge.draftName, edge.draftName);
        }
    };
    QObject::connect(registration, &QComboBox::currentIndexChanged, &dialog, [&](int) {
        registrationParameter->setEnabled(registration->currentIndex() > 0);
    });
    fillRegistration();

    auto* hint = new QLabel(tr("Grain runs along the fabric length. Fold copies need a straight outer edge and a finished cutting contour with zero added allowance. Set the width of the folded fabric for fold layouts. Each copy matches at its cutting outline corner or a named seam point. Fold copies match along the length only."));
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
    auto* optimization = new QLabel;
    optimization->setWordWrap(true);
    optimization->hide();
    layout->addWidget(optimization);
    auto* buttons = dialogButtons(layout, dialog);
    addContextHelp(dialog, *buttons, QStringLiteral("Раскладка лекал"), QStringLiteral(
        "Укажите ширину ткани и минимальный зазор в миллиметрах. Добавьте копии деталей по размерам "
        "и проверьте количество деталей кроя. Для направленной ткани запретите разворот.\n\n"
        "Для ткани с рисунком задайте раппорт. Для совмещения рисунка на шве укажите точку на именованном срезе. "
        "После авторасстановки сравните длину с исходным алгоритмом и проверьте отчёт раскроя. Печатайте PDF в масштабе 100 %."));

    QMap<QString, opc::PatternDocument> graded;
    graded.insert(QString{}, document);
    QVector<PlacementItem*> items;
    QGraphicsRectItem* fabric = nullptr;

    const auto cutFor = [&](const opc::FabricPlacement& placement) {
        if (!graded.contains(placement.size))
            graded.insert(placement.size, opc::gradedPattern(document, placement.size, nullptr, opc::GradeEdgeNames::Preserve));
        return opc::fabricPiece(graded[placement.size], placement, false, data.fabricShrinkagePercent);
    };
    const auto updateUsage = [&] {
        optimization->hide();
        QVector<QPolygonF> placed;
        qsizetype repeats = 0;
        for (auto* item : items) {
            auto& placement = data.placements[item->index];
            placement.position = item->pos();
            if (!opc::matchesFabricRepeat(data, placement, item->repeatPoint))
                ++repeats;
            placed.append(item->valid ? item->polygon().translated(item->pos()) : QPolygonF{});
        }
        const auto usage = opc::fabricUsage(placed, data.fabricWidth, data.pieceClearanceMm, data.fabricFlaws);
        const double displayLength = std::max(1000.0, usage.length + 200);
        fabric->setRect(0, 0, data.fabricWidth, displayLength);
        automatic->setEnabled(!items.isEmpty());
        dxf->setEnabled(!items.isEmpty());
        auto summary = tr("Used length: %1 m · Copies: %2 · Outside fabric: %3 · Overlap or clearance conflicts: %4 · Invalid contours: %5")
                           .arg(usage.length / 1000, 0, 'f', 3).arg(items.size())
                           .arg(usage.outside).arg(usage.overlaps).arg(usage.invalid);
        if (!data.fabricFlaws.isEmpty())
            summary += tr(" · Copies over flaws: %1").arg(usage.flawConflicts);
        if (!data.fabricRepeatMm.isNull() || repeats > 0)
            summary += tr(" · Off repeat or missing seam point: %1").arg(repeats);
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
        auto* item = new PlacementItem(index, cut, lockedX, updateUsage);
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
        if (cut.repeatPoint) {
            const auto point = *cut.repeatPoint;
            auto* marker = new QGraphicsEllipseItem(-3, -3, 6, 6, item);
            marker->setPos(point);
            marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            marker->setBrush(QColor("#b04c26"));
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
        fabric->setZValue(-2);
        for (const auto& flaw : data.fabricFlaws) {
            auto* area = scene.addRect(flaw, QPen(QColor("#972a26"), 0), QColor(197, 66, 61, 125));
            area->setZValue(-1);
        }
        for (qsizetype index = 0; index < data.placements.size(); ++index)
            addItem(index);
        updateUsage();
        updateSceneBounds();
    };
    QObject::connect(flawsButton, &QPushButton::clicked, &dialog, [&] {
        if (auto updated = editFabricFlaws(&dialog, data.fabricFlaws)) {
            data.fabricFlaws = std::move(*updated);
            rebuild();
        }
    });
    const auto selected = [&]() -> PlacementItem* {
        const auto selection = scene.selectedItems();
        return selection.isEmpty() ? nullptr : static_cast<PlacementItem*>(selection.front());
    };
    const auto updateRegistrationAction = [&] {
        const auto* item = selected();
        setRegistration->setEnabled(item && data.placements[item->index].piece == piece->currentIndex());
    };
    QObject::connect(piece, &QComboBox::currentIndexChanged, &dialog, [&](int) {
        fillRegistration();
        updateRegistrationAction();
    });
    QObject::connect(&scene, &QGraphicsScene::selectionChanged, &dialog, [&] {
        const auto* item = selected();
        turn->setEnabled(item && (data.placements[item->index].reversed
                                  || opc::canTurn(data, data.placements[item->index])));
        remove->setEnabled(selected());
        updateRegistrationAction();
    });
    const auto assignRegistration = [&](opc::FabricPlacement& placement) {
        placement.repeatEdge = registration->currentData().toString();
        placement.repeatParameter = registrationParameter->value() / 100;
    };
    QObject::connect(setRegistration, &QPushButton::clicked, &dialog, [&] {
        if (const auto* item = selected()) {
            const auto index = item->index;
            assignRegistration(data.placements[index]);
            rebuild();
            items[index]->setSelected(true);
        }
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
    const auto updateShrinkage = [&] {
        data.fabricShrinkagePercent = {weft->value(), warp->value()};
        rebuild();
    };
    QObject::connect(weft, &QDoubleSpinBox::valueChanged, &dialog, updateShrinkage);
    QObject::connect(warp, &QDoubleSpinBox::valueChanged, &dialog, updateShrinkage);
    const auto updateRepeat = [&] {
        data.fabricRepeatMm = {crossRepeat->value(), lengthRepeat->value()};
        updateUsage();
    };
    QObject::connect(crossRepeat, &QDoubleSpinBox::valueChanged, &dialog, updateRepeat);
    QObject::connect(lengthRepeat, &QDoubleSpinBox::valueChanged, &dialog, updateRepeat);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        opc::FabricPlacement placement{piece->currentIndex(), size->currentData().toString(), {}};
        placement.fixedOrientation = fixedOrientation->isChecked();
        assignRegistration(placement);
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
                   "\nRotation conflicts: %4 · Off fold: %5 · Off repeat: %6 · Copies over flaws: %7\n")
            .arg(usage.outside).arg(usage.overlaps).arg(usage.invalid)
            .arg(report.orientationViolations).arg(report.foldViolations).arg(report.repeatViolations)
            .arg(usage.flawConflicts);
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
        if (data.placements.isEmpty() || usage.invalid || usage.outside || usage.overlaps || usage.flawConflicts
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
        QElapsedTimer timer;
        timer.start();
        auto result = opc::arrangeFabric(document, data);
        const auto elapsed = timer.elapsed();
        QString error;
        switch (result.error) {
        case opc::FabricLayoutError::None:
            data.placements = std::move(result.placements);
            rebuild();
            fitFabric();
            if (result.baselineLengthMm && result.optimizedLengthMm) {
                optimization->setText(QStringLiteral("Автоматическая раскладка: %1 → %2 мм. Экономия: %3 мм. Время поиска: %4 с.")
                    .arg(number(*result.baselineLengthMm), number(*result.optimizedLengthMm),
                         number(*result.baselineLengthMm - *result.optimizedLengthMm),
                         number(elapsed / 1000.0)));
                optimization->show();
            }
            return;
        case opc::FabricLayoutError::TooManyCopies:
            error = tr("Automatic layout supports up to %1 copies.").arg(opc::maxAutomaticCopies);
            break;
        case opc::FabricLayoutError::InvalidShrinkage:
            error = QStringLiteral("Задайте усадку от 0 до 99 % по каждой оси.");
            break;
        case opc::FabricLayoutError::InvalidWidth:
            error = tr("Enter a positive fabric width.");
            break;
        case opc::FabricLayoutError::InvalidClearance:
            error = tr("Enter a piece clearance from 0 to 1000 mm.");
            break;
        case opc::FabricLayoutError::InvalidRepeat:
            error = tr("Check the repeat dimensions and make sure the named seam point exists in the selected size.");
            break;
        case opc::FabricLayoutError::InvalidFlaws:
            error = tr("Check the dimensions and coordinates of the fabric flaws.");
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

void exportMaterialLay(QWidget* parent, const opc::PatternDocument& document) {
    const auto title = QStringLiteral("Многоматериальный настил");
    const auto& original = document.production();
    if (original.placements.isEmpty()) {
        QMessageBox::information(parent, title, QStringLiteral("Добавьте комплекты в раскладку."));
        return;
    }

    // Group matching left and right copies in source order so an assignment
    // cannot separate a pair. An unmatched hand can still be cut on its own.
    using HandKey = std::tuple<qsizetype, QString, opc::CutHand>;
    QMap<HandKey, QVector<qsizetype>> handed;
    for (qsizetype i = 0; i < original.placements.size(); ++i) {
        const auto& copy = original.placements[i];
        if (copy.hand != opc::CutHand::Single)
            handed[{copy.piece, copy.size, copy.hand}].append(i);
    }
    QMap<qsizetype, qsizetype> pairs;
    QSet<qsizetype> pairedLater;
    for (auto it = handed.cbegin(); it != handed.cend(); ++it) {
        const auto& [piece, size, hand] = it.key();
        if (hand != opc::CutHand::Left)
            continue;
        const auto right = handed.constFind({piece, size, opc::CutHand::Right});
        if (right == handed.cend())
            continue;
        for (qsizetype i = 0; i < std::min(it->size(), right->size()); ++i) {
            const auto first = std::min(it->at(i), right->at(i));
            const auto last = std::max(it->at(i), right->at(i));
            pairs.insert(first, last);
            pairedLater.insert(last);
        }
    }

    QDialog assignment(parent);
    assignment.setWindowTitle(title);
    QVBoxLayout layout(&assignment);
    auto* table = makeTable(&layout, {QStringLiteral("Деталь"), QStringLiteral("Копии"),
                                     QStringLiteral("Материал"), QStringLiteral("№ настила")});
    struct Assignment {
        QVector<qsizetype> copies;
        QLineEdit* material;
        QSpinBox* lay;
    };
    QVector<Assignment> assignments;
    for (qsizetype i = 0; i < original.placements.size(); ++i) {
        if (pairedLater.contains(i))
            continue;
        const auto& copy = original.placements[i];
        if (copy.piece < 0 || copy.piece >= document.pieces().size()) {
            QMessageBox::warning(parent, title, QStringLiteral("Одна из копий ссылается на отсутствующую деталь."));
            return;
        }
        const int row = table->rowCount();
        table->insertRow(row);
        const auto size = copy.size.isEmpty() ? QStringLiteral("База") : copy.size;
        auto* label = new QTableWidgetItem(QStringLiteral("%1 · %2").arg(document.pieces()[copy.piece].name, size));
        const auto pair = pairs.constFind(i);
        auto* count = new QTableWidgetItem(pair == pairs.cend() ? QString::number(i + 1)
            : QStringLiteral("%1 и %2").arg(i + 1).arg(*pair + 1));
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        count->setFlags(count->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, label);
        table->setItem(row, 1, count);
        auto* material = new QLineEdit(QStringLiteral("Основной"));
        material->setMaxLength(100);
        table->setCellWidget(row, 2, material);
        auto* lay = new QSpinBox;
        lay->setRange(1, 9999);
        table->setCellWidget(row, 3, lay);
        QVector<qsizetype> copies{i};
        if (pair != pairs.cend())
            copies.append(*pair);
        assignments.append({std::move(copies), material, lay});
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &assignment, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &assignment, &QDialog::reject);
    assignment.resize(650, 420);
    if (assignment.exec() != QDialog::Accepted)
        return;

    using LayKey = std::pair<QString, int>;
    QMap<LayKey, QVector<opc::FabricPlacement>> groups;
    for (const auto& assignment : assignments) {
        const auto name = assignment.material->text().trimmed();
        if (name.isEmpty() || name.contains(u'\t') || name.contains(u'\r') || name.contains(u'\n')) {
            QMessageBox::warning(parent, title, QStringLiteral("Укажите имя каждого материала без переносов и табуляции."));
            return;
        }
        auto& group = groups[{name, assignment.lay->value()}];
        for (const auto index : assignment.copies)
            group.append(original.placements[index]);
    }

    struct Material {
        QString name;
        int lay;
        opc::PatternDocument project;
        int layers;
        double length;
    };
    QVector<Material> materials;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const auto& [name, lay] = it.key();
        const auto label = QStringLiteral("%1 · настил %2").arg(name).arg(lay);
        auto project = document;
        auto& data = project.production();
        data.placements = it.value();
        data.fabricFlaws.clear();

        bool accepted = false;
        data.fabricWidth = QInputDialog::getDouble(parent, title, QStringLiteral("Ширина материала «%1», мм").arg(label),
                                                  data.fabricWidth, 1, 1000000, 2, &accepted);
        if (!accepted)
            return;
        const int layers = QInputDialog::getInt(parent, title, QStringLiteral("Слоёв материала «%1»").arg(label),
                                                1, 1, 10000, 1, &accepted);
        if (!accepted)
            return;
        const auto arranged = opc::arrangeFabric(project, data);
        if (arranged.error == opc::FabricLayoutError::None)
            data.placements = arranged.placements;
        const auto edited = editFabricLayout(parent, project, label);
        if (!edited)
            return;
        // The batch owns copy membership. The editor can refine fabric settings
        // and layout, but changing the order quantity here would change the batch.
        auto order = [](const auto& placements) {
            std::multiset<std::tuple<qsizetype, QString, opc::CutHand, opc::FoldEdge>> keys;
            for (const auto& copy : placements)
                keys.emplace(copy.piece, copy.size, copy.hand, copy.fold);
            return keys;
        };
        if (order(edited->placements) != order(it.value())) {
            QMessageBox::warning(parent, title,
                QStringLiteral("Состав, парность и сгибы копий материала «%1» изменились.").arg(label));
            return;
        }
        data = *edited;
        const auto report = opc::cuttingReport(project, data);
        if (!report.canExport(data.fabricWidth)) {
            QMessageBox::warning(parent, title, QStringLiteral("Проверьте раскладку материала «%1».").arg(label));
            return;
        }
        project.productInformation().material = name;
        materials.append({name, lay, std::move(project), layers, report.usage.length});
    }

    const auto destination = QFileDialog::getExistingDirectory(parent, title);
    if (destination.isEmpty())
        return;
    QDir output(destination);
    QTemporaryDir staging(output.filePath(QStringLiteral(".sofit-lay-XXXXXX")));
    if (!staging.isValid()) {
        QMessageBox::warning(parent, title, staging.errorString());
        return;
    }

    QString usage = QStringLiteral("Материал\tПроект\tШирина, мм\tКопий на слой\tДлина слоя, мм\tСлоёв\tРасход, м\tУсадка по утку, %\tУсадка по основе, %\t№ настила\n");
    QString error;
    for (qsizetype i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        const auto stem = QStringLiteral("material-%1").arg(i + 1);
        const auto& data = material.project.production();
        if (!material.project.save(staging.filePath(stem + ".sofit"), &error)
            || !opc::exportFabricDxf(staging.filePath(stem + ".dxf"), material.project, data, error)
            || !opc::exportFabricPdf(staging.filePath(stem + ".pdf"), material.project, data, {}, error)) {
            QMessageBox::warning(parent, title, error);
            return;
        }
        usage += QStringLiteral("%1\t%2.sofit\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\n").arg(material.name, stem)
            .arg(data.fabricWidth, 0, 'f', 2).arg(data.placements.size()).arg(material.length, 0, 'f', 2)
            .arg(material.layers).arg(material.length * material.layers / 1000., 0, 'f', 4)
            .arg(data.fabricShrinkagePercent.x(), 0, 'f', 2).arg(data.fabricShrinkagePercent.y(), 0, 'f', 2).arg(material.lay);
    }
    QSaveFile report(staging.filePath(QStringLiteral("usage.tsv")));
    const auto bytes = usage.toUtf8();
    if (!report.open(QIODevice::WriteOnly) || report.write(bytes) != bytes.size() || !report.commit()) {
        QMessageBox::warning(parent, title, report.errorString());
        return;
    }
    const auto folder = QStringLiteral("SOFIT-lay-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!output.rename(QDir(staging.path()).dirName(), folder)) {
        QMessageBox::warning(parent, title, QStringLiteral("Не удалось сохранить комплект настила."));
        return;
    }
    staging.setAutoRemove(false);
    QMessageBox::information(parent, title, QStringLiteral("Комплект и расход сохранены в %1").arg(output.filePath(folder)));
}
