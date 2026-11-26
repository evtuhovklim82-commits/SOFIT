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
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QSignalBlocker>
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
    auto* sizes = makeTable(sizesLayout, {tr("Size"), tr("Measurement"), tr("Value (mm)"), tr("Source / edition")});
    const auto addMeasurement = [sizes](const QString& size, const QString& name, const QString& value, const QString& source = {}) {
        const int row = sizes->rowCount();
        sizes->insertRow(row);
        sizes->setItem(row, 0, new QTableWidgetItem(size));
        sizes->setItem(row, 1, new QTableWidgetItem(name));
        sizes->setItem(row, 2, new QTableWidgetItem(value));
        sizes->setItem(row, 3, new QTableWidgetItem(source));
    };
    for (const auto& size : document.production().sizes) {
        if (size.measurements.isEmpty())
            addMeasurement(size.name, {}, {}, size.source);
        for (auto map = size.measurements.cbegin(); map != size.measurements.cend(); ++map)
            addMeasurement(size.name, map.key(), number(map.value()),
                           map == size.measurements.cbegin() ? size.source : QString{});
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
    auto* compareTable = new QPushButton(QStringLiteral("Сверить с TSV…"));
    compareTable->setObjectName(QStringLiteral("compareMeasurementTable"));
    exchangeButtons->addWidget(compareTable);
    exchangeButtons->addStretch();
    sizesLayout->addLayout(exchangeButtons);
    const auto header = QStringLiteral("Size\tMeasurement\tValue (mm)");
    const auto measurementTsv = [=, &dialog]() -> std::optional<QString> {
        const auto extended = [&] {
            for (int row = 0; row < sizes->rowCount(); ++row)
                if (!sizes->item(row, 3)->text().trimmed().isEmpty())
                    return true;
            return false;
        }();
        QStringList lines{extended ? header + QStringLiteral("\tSource / edition") : header};
        for (int row = 0; row < sizes->rowCount(); ++row) {
            QStringList cells;
            for (int column = 0; column < (extended ? 4 : 3); ++column) {
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
        if (!lines.isEmpty() && (lines.front() == header
                                 || lines.front() == header + QStringLiteral("\tSource / edition")))
            lines.removeFirst();
        if (lines.isEmpty())
            return {};
        QVector<QStringList> rows;
        for (const auto& line : lines) {
            auto cells = line.split(u'\t');
            if ((cells.size() != 3 && cells.size() != 4) || cells[0].trimmed().isEmpty() || line.contains(u'\r')) {
                QMessageBox::warning(&dialog, dialog.windowTitle(),
                    QStringLiteral("Строка %1 должна содержать размер, мерку, значение в мм и, при наличии, источник.")
                        .arg(rows.size() + 1));
                return {};
            }
            if (cells.size() == 3)
                cells.append(QString{});
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
            addMeasurement(cells[0], cells[1], cells[2], cells[3]);
    });
    QObject::connect(addSize, &QPushButton::clicked, &dialog, [=] {
        addMeasurement({}, {}, {});
        sizes->editItem(sizes->item(sizes->rowCount() - 1, 0));
    });

    const auto readMeasurements = [sizes](QString& error, const QVector<QStringList>& appended = {},
                                            bool includeCurrent = true) {
        QVector<opc::PatternSize> entries;
        QMap<QString, qsizetype> sizeIndices;
        const qsizetype currentRows = includeCurrent ? sizes->rowCount() : 0;
        const auto cell = [sizes, &appended, currentRows](qsizetype row, int column) {
            return (row < currentRows ? sizes->item(row, column)->text()
                                     : appended[row - currentRows][column]).trimmed();
        };
        for (qsizetype row = 0; row < currentRows + appended.size(); ++row) {
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
            auto& entry = entries[sizeIndices[size]];
            const auto source = cell(row, 3);
            if (source.size() > 512 || source.contains(u'\t') || source.contains(u'\r') || source.contains(u'\n')
                || (!source.isEmpty() && !entry.source.isEmpty() && entry.source != source)) {
                error = tr("Measurement row %1 has an invalid or conflicting size source.").arg(row + 1);
                return QVector<opc::PatternSize>{};
            }
            if (!source.isEmpty())
                entry.source = source;
            if (name.isEmpty() && value.isEmpty())
                continue;
            bool ok = false;
            const double mm = value.toDouble(&ok);
            auto& measurements = entry.measurements;
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
            addMeasurement(cells[0], cells[1], cells[2], cells[3]);
    };
    QObject::connect(appendTable, &QPushButton::clicked, &dialog, [=] {
        appendMeasurements(QApplication::clipboard()->text());
    });
    QObject::connect(standard, &QPushButton::clicked, &dialog, [=, &dialog] {
        QDialog reference(&dialog);
        reference.setWindowTitle(QStringLiteral("ГОСТ 31396-2009 — женские фигуры"));
        QFormLayout form(&reference);
        QLabel source(QStringLiteral("Таблица 3 · 356 фигур в шести полнотных группах.<br>"
                                    "<a href='https://protect.gost.ru/gost/details/7d9919ad-6695-41c5-93e1-8224eaf3117d'>"
                                    "Карточка Росстандарта</a> · "
                                    "<a href='https://ohranatruda.ru/upload/iblock/caf/4293808596.pdf#page=16'>"
                                    "Таблица 3, с. 14–17</a>"));
        source.setWordWrap(true);
        source.setOpenExternalLinks(true);
        form.addRow(&source);
        // Each row is one printed range of bust values, in 4 cm increments.
        struct FigureRange { int stature, firstBust, lastBust; };
        const QVector<QVector<FigureRange>> figures{
            {{152, 84, 112}, {158, 84, 124}, {164, 84, 124}, {170, 84, 124}, {176, 108, 124}},
            {{152, 84, 120}, {158, 80, 124}, {164, 80, 124}, {170, 80, 124}, {176, 84, 124},
             {182, 96, 124}},
            {{152, 80, 116}, {158, 80, 132}, {164, 80, 132}, {170, 80, 132}, {176, 80, 132},
             {182, 88, 124}},
            {{152, 80, 108}, {158, 80, 132}, {164, 80, 132}, {170, 80, 132}, {176, 80, 132},
             {182, 88, 128}},
            {{152, 84, 108}, {158, 84, 124}, {164, 84, 124}, {170, 84, 124}, {176, 84, 124},
             {182, 88, 124}},
            {{158, 96, 124}, {164, 88, 124}, {170, 88, 116}, {176, 88, 112}},
        };
        QComboBox group, height, bust;
        for (qsizetype i = 0; i < figures.size(); ++i)
            group.addItem(QString::number(i), i);
        const auto updateBust = [&] {
            const auto previous = bust.currentData();
            bust.clear();
            if (height.currentIndex() < 0)
                return;
            const auto& row = figures[group.currentIndex()][height.currentIndex()];
            for (int cm = row.firstBust; cm <= row.lastBust; cm += 4)
                bust.addItem(QString::number(cm), cm);
            if (const auto index = bust.findData(previous); index >= 0)
                bust.setCurrentIndex(index);
        };
        const auto updateHeight = [&] {
            const auto previous = height.currentData();
            const QSignalBlocker blocker(&height);
            height.clear();
            for (const auto& row : figures[group.currentIndex()])
                height.addItem(QString::number(row.stature), row.stature);
            if (const auto index = height.findData(previous); index >= 0)
                height.setCurrentIndex(index);
            updateBust();
        };
        QObject::connect(&group, &QComboBox::currentIndexChanged, &reference, updateHeight);
        QObject::connect(&height, &QComboBox::currentIndexChanged, &reference, updateBust);
        updateHeight();
        form.addRow(QStringLiteral("Полнотная группа"), &group);
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
        const int fullness = group.currentIndex();
        const int hipCm = bustCm - 2 + 4 * fullness;
        const auto size = QStringLiteral("ГОСТ 31396-2009 %1-%2-%3").arg(statureCm).arg(bustCm).arg(hipCm);
        const auto row = [&](const QString& measurement, int cm) {
            return QStringLiteral("%1\t%2\t%3").arg(size, measurement).arg(cm * 10);
        };
        const auto attribution = QStringLiteral("\tГОСТ 31396-2009, таблица 3, полнотная группа %1").arg(fullness);
        appendMeasurements(QStringList{row(QStringLiteral("height"), statureCm) + attribution,
                                       row(QStringLiteral("bust"), bustCm),
                                       row(QStringLiteral("hip"), hipCm)}.join(u'\n'));
    });
    const auto readMeasurementFile = [&dialog](const QString& title, const QString& directory)
        -> std::optional<QString> {
        const auto path = QFileDialog::getOpenFileName(QApplication::activeModalWidget(), title, directory, QStringLiteral("TSV (*.tsv)"));
        if (path.isEmpty())
            return std::nullopt;
        constexpr qint64 maxTableBytes = 4 * 1024 * 1024;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Не удалось открыть таблицу мерок.\n%1").arg(file.errorString()));
            return std::nullopt;
        }
        const auto bytes = file.read(maxTableBytes + 1);
        QStringDecoder decode(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString text = decode(bytes);
        if (file.error() != QFileDevice::NoError || bytes.size() > maxTableBytes || decode.hasError()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Не удалось прочитать таблицу. Требуется файл UTF-8 размером до 4 МБ."));
            return std::nullopt;
        }
        return text;
    };
    const auto importMeasurementTable = [=](const QString& directory) {
        if (const auto text = readMeasurementFile(importTable->text(), directory))
            appendMeasurements(*text);
    };
    QObject::connect(importTable, &QPushButton::clicked, &dialog, [=] { importMeasurementTable({}); });
    QObject::connect(compareTable, &QPushButton::clicked, &dialog, [=, &dialog] {
        const auto text = readMeasurementFile(compareTable->text(), {});
        if (!text)
            return;
        const auto rows = readTsv(*text);
        if (rows.isEmpty()) {
            QMessageBox::warning(&dialog, compareTable->text(), QStringLiteral("В эталоне нет строк мерок."));
            return;
        }
        QString error;
        const auto current = readMeasurements(error);
        const auto reference = error.isEmpty() ? readMeasurements(error, rows, false) : QVector<opc::PatternSize>{};
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, compareTable->text(), error);
            return;
        }
        using MeasurementKey = std::pair<QString, QString>;
        const auto index = [](const QVector<opc::PatternSize>& entries) {
            QMap<MeasurementKey, QString> values;
            for (const auto& size : entries) {
                values.insert({size.name, {}}, size.source);
                for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
                    values.insert({size.name, it.key()}, number(it.value()));
            }
            return values;
        };
        const auto expected = index(reference);
        auto actual = index(current);
        QStringList differences;
        const auto identify = [](const MeasurementKey& key) {
            return key.second.isEmpty() ? key.first : key.first + QStringLiteral(" / ") + key.second;
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            if (!actual.contains(it.key())) {
                differences.append(QStringLiteral("Отсутствует: %1").arg(identify(it.key())));
                continue;
            }
            const auto value = actual.take(it.key());
            if (value != it.value())
                differences.append(QStringLiteral("%1: проект «%2», эталон «%3»")
                    .arg(identify(it.key()), value, it.value()));
        }
        for (auto it = actual.cbegin(); it != actual.cend(); ++it)
            differences.append(QStringLiteral("Лишняя строка: %1").arg(identify(it.key())));
        QDialog result(&dialog);
        result.setWindowTitle(QStringLiteral("Сверка мерок"));
        QVBoxLayout resultLayout(&result);
        QLabel summary(differences.isEmpty() ? QStringLiteral("Размеры, мерки и источники совпадают с эталоном.")
            : QStringLiteral("Расхождений: %1").arg(differences.size()));
        resultLayout.addWidget(&summary);
        QPlainTextEdit details;
        details.setObjectName(QStringLiteral("measurementComparisonDetails"));
        details.setReadOnly(true);
        details.setPlainText(differences.join(u'\n'));
        resultLayout.addWidget(&details);
        QDialogButtonBox close(QDialogButtonBox::Close);
        resultLayout.addWidget(&close);
        QObject::connect(&close, &QDialogButtonBox::rejected, &result, &QDialog::reject);
        result.resize(620, 380);
        result.exec();
    });
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
    auto* ruleButtons = tableButtons(rulesLayout, rules);
    ruleButtons->insertWidget(0, addGrade);
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

    auto* groupGrade = new QPushButton(tr("Offset curve or piece…"));
    groupGrade->setEnabled(!points.isEmpty());
    ruleButtons->insertWidget(1, groupGrade);
    QObject::connect(groupGrade, &QPushButton::clicked, &dialog, [&] {
        QString error;
        const auto entries = readMeasurements(error);
        QSet<QString> names;
        for (const auto& entry : entries)
            names.insert(entry.name);
        const auto existing = error.isEmpty() ? readRules(names, error) : QVector<opc::GradeRule>{};
        if (!error.isEmpty() || entries.isEmpty()) {
            QMessageBox::warning(&dialog, groupGrade->text(),
                                 error.isEmpty() ? tr("Define at least one size first.") : error);
            return;
        }
        QDialog group(&dialog);
        group.setWindowTitle(groupGrade->text());
        QFormLayout form(&group);
        QComboBox size, piece, target;
        for (const auto& entry : entries)
            size.addItem(entry.name);
        for (const auto& entry : document.pieces())
            piece.addItem(entry.name);
        const auto fillTargets = [&] {
            target.clear();
            target.addItem(tr("Entire piece"));
            for (qsizetype i = 0; i < document.entities().size(); ++i) {
                const auto& entity = document.entities()[i];
                if (entity.piece != piece.currentIndex() || entity.shape != opc::Shape::Curve)
                    continue;
                const auto label = entity.draftName.isEmpty()
                    ? tr("Curve %1 (%2, %3 → %4, %5)").arg(i + 1)
                          .arg(number(entity.a.x()), number(entity.a.y()),
                               number(entity.b.x()), number(entity.b.y()))
                    : entity.draftName;
                target.addItem(label, qlonglong(i));
            }
        };
        QObject::connect(&piece, &QComboBox::currentIndexChanged, &group, fillTargets);
        fillTargets();
        QDoubleSpinBox x, y;
        for (auto* input : {&x, &y}) {
            input->setRange(-1000000, 1000000);
            input->setDecimals(3);
        }
        form.addRow(tr("Size"), &size);
        form.addRow(tr("Piece"), &piece);
        form.addRow(tr("Target"), &target);
        form.addRow(tr("ΔX (mm)"), &x);
        form.addRow(tr("ΔY (mm)"), &y);
        QLabel hint(tr("Creates point rules for this target. Shared endpoints move together."));
        hint.setWordWrap(true);
        form.addRow(&hint);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        QObject::connect(&buttons, &QDialogButtonBox::rejected, &group, &QDialog::reject);
        QObject::connect(&buttons, &QDialogButtonBox::accepted, &group, [&] {
            const auto pieceIndex = piece.currentIndex();
            const auto sizeName = size.currentText();
            auto selected = points[pieceIndex];
            if (target.currentIndex() > 0) {
                const auto& curve = document.entities()[target.currentData().toLongLong()];
                selected = {curve.a, curve.b, curve.control};
            }
            if (selected.isEmpty()) {
                QMessageBox::warning(&group, group.windowTitle(), tr("The selected piece has no grading points."));
                return;
            }
            const auto exactly = [](QPointF a, QPointF b) { return a.x() == b.x() && a.y() == b.y(); };
            QVector<QPointF> unique;
            for (const auto point : selected) {
                if (std::ranges::any_of(unique, [&](QPointF other) { return exactly(point, other); }))
                    continue;
                if (std::ranges::any_of(existing, [&](const auto& rule) {
                        return rule.size == sizeName && rule.piece == pieceIndex && exactly(rule.point, point);
                    })) {
                    QMessageBox::warning(&group, group.windowTitle(),
                                         tr("A point on this target already has a rule for this size."));
                    return;
                }
                unique.append(point);
            }
            for (const auto point : unique)
                addRule({sizeName, pieceIndex, point, {x.value(), y.value()}}, true);
            group.accept();
        });
        group.exec();
    });

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
        input->setToolTip(QStringLiteral("Положительное значение для усадки, отрицательное для растяжения."));
        input->setRange(-99, 99);
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
            error = QStringLiteral("Задайте компенсацию от −99 до 99 % по каждой оси.");
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

namespace {
using LayKey = std::pair<QString, int>;
constexpr qint64 materialOrderLimit = 16 * 1024 * 1024;

struct SavedLay {
    opc::PatternDocument project;
    int layers;
};

struct MaterialOrder {
    QString equipmentName;
    double tableLength = 0;
    int maximumLayers = 10000;
    QVector<LayKey> assignments;
    QMap<LayKey, SavedLay> lays;
    QMap<int, int> plannedLayers;
    bool fixedSizePlan = false;
};

bool validMaterial(const QString& name) {
    return !name.isEmpty() && name.size() <= 100 && name == name.trimmed()
        && !name.contains(u'\t') && !name.contains(u'\r') && !name.contains(u'\n');
}

using CutCopies = std::multiset<std::tuple<qsizetype, QString, opc::CutHand, opc::FoldEdge>>;

CutCopies cutCopies(const QVector<opc::FabricPlacement>& placements) {
    CutCopies keys;
    for (const auto& copy : placements)
        keys.emplace(copy.piece, copy.size, copy.hand, copy.fold);
    return keys;
}

void editMaterialOrder(QWidget* parent, const opc::PatternDocument& document, const MaterialOrder& saved) {
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

    if (!saved.assignments.isEmpty()
        && saved.assignments.size() != original.placements.size() - pairedLater.size()) {
        QMessageBox::warning(parent, title, QStringLiteral("Число назначений не соответствует исходному проекту."));
        return;
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
        lay->setEnabled(!saved.fixedSizePlan);
        table->setCellWidget(row, 3, lay);
        if (!saved.assignments.isEmpty()) {
            material->setText(saved.assignments.at(row).first);
            lay->setValue(saved.assignments.at(row).second);
        }
        QVector<qsizetype> copies{i};
        if (pair != pairs.cend())
            copies.append(*pair);
        assignments.append({std::move(copies), material, lay});
    }
    auto* limits = new QFormLayout;
    QDoubleSpinBox tableLength;
    tableLength.setObjectName(QStringLiteral("layTableLength"));
    tableLength.setRange(0, 1000000);
    tableLength.setDecimals(2);
    tableLength.setSpecialValueText(QStringLiteral("Без ограничения"));
    tableLength.setSuffix(QStringLiteral(" мм"));
    tableLength.setValue(saved.tableLength);
    limits->addRow(QStringLiteral("Предельная длина стола"), &tableLength);
    QSpinBox maximumLayers;
    maximumLayers.setObjectName(QStringLiteral("layMaximumLayers"));
    maximumLayers.setRange(1, 10000);
    maximumLayers.setValue(saved.maximumLayers);
    limits->addRow(QStringLiteral("Предельное число слоёв"), &maximumLayers);
    QSettings equipmentSettings;
    equipmentSettings.beginGroup(QStringLiteral("cuttingEquipment"));
    auto profiles = equipmentSettings.value(QStringLiteral("profiles")).toMap();
    auto* profile = new QComboBox(&assignment);
    profile->setObjectName(QStringLiteral("cuttingEquipmentProfile"));
    profile->setEditable(true);
    profile->setInsertPolicy(QComboBox::NoInsert);
    profile->lineEdit()->setMaxLength(100);
    profile->addItem(QString());
    profile->addItems(profiles.keys());
    profile->setEditText(saved.equipmentName);
    auto* saveProfile = new QPushButton(QStringLiteral("Сохранить профиль"), &assignment);
    saveProfile->setObjectName(QStringLiteral("saveCuttingEquipmentProfile"));
    auto* profileControls = new QHBoxLayout;
    profileControls->addWidget(profile, 1);
    profileControls->addWidget(saveProfile);
    limits->addRow(QStringLiteral("Профиль оборудования"), profileControls);
    QObject::connect(profile, &QComboBox::activated, &assignment, [&](int index) {
        const auto values = profiles.value(profile->itemText(index)).toMap();
        if (values.isEmpty())
            return;
        tableLength.setValue(values.value(QStringLiteral("tableLength")).toDouble());
        maximumLayers.setValue(values.value(QStringLiteral("maximumLayers")).toInt());
    });
    QObject::connect(saveProfile, &QPushButton::clicked, &assignment, [&] {
        const auto name = profile->currentText().trimmed();
        if (!validMaterial(name)) {
            QMessageBox::warning(&assignment, title, QStringLiteral("Укажите имя профиля оборудования."));
            return;
        }
        auto updated = profiles;
        updated.insert(name, QVariantMap{{QStringLiteral("tableLength"), tableLength.value()},
                                         {QStringLiteral("maximumLayers"), maximumLayers.value()}});
        equipmentSettings.setValue(QStringLiteral("profiles"), updated);
        equipmentSettings.sync();
        if (equipmentSettings.status() != QSettings::NoError) {
            QMessageBox::warning(&assignment, title, QStringLiteral("Не удалось сохранить профиль оборудования."));
            return;
        }
        profiles = std::move(updated);
        int index = profile->findText(name, Qt::MatchExactly);
        if (index < 0) {
            profile->addItem(name);
            index = profile->count() - 1;
        }
        profile->setCurrentIndex(index);
    });
    layout.addLayout(limits);
    QCheckBox saveOnly(QStringLiteral("Сохранить заказ без DXF и PDF"));
    saveOnly.setObjectName(QStringLiteral("saveMaterialOrderOnly"));
    layout.addWidget(&saveOnly);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &assignment, [&] {
        const auto name = profile->currentText().trimmed();
        if (!name.isEmpty() && !validMaterial(name)) {
            QMessageBox::warning(&assignment, title, QStringLiteral("Имя профиля не должно содержать переносы и табуляцию."));
            return;
        }
        if (saved.fixedSizePlan && std::ranges::any_of(saved.plannedLayers,
                [&](int layers) { return layers > maximumLayers.value(); })) {
            QMessageBox::warning(&assignment, title,
                QStringLiteral("Предел оборудования ниже числа слоёв в плане. Пересчитайте план с меньшим пределом."));
            return;
        }
        assignment.accept();
    });
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &assignment, &QDialog::reject);
    assignment.resize(650, 420);
    if (assignment.exec() != QDialog::Accepted)
        return;

    QVector<LayKey> savedAssignments(assignments.size());
    QMap<LayKey, QVector<qsizetype>> groups;
    int lastLayNumber = 0;
    for (qsizetype row = 0; row < assignments.size(); ++row) {
        const auto& assignment = assignments[row];
        const auto name = assignment.material->text().trimmed();
        if (!validMaterial(name)) {
            QMessageBox::warning(parent, title, QStringLiteral("Укажите имя каждого материала без переносов и табуляции."));
            return;
        }
        const auto lay = assignment.lay->value();
        groups[{name, lay}].append(row);
        lastLayNumber = std::max(lastLayNumber, lay);
    }

    struct PendingLay {
        LayKey key;
        QVector<qsizetype> rows;
        std::optional<opc::ProductionData> splitSettings;
        int layers = 0;
    };
    QList<PendingLay> pending;
    for (auto group = groups.cbegin(); group != groups.cend(); ++group)
        pending.append({group.key(), group.value(), std::nullopt});

    struct Material {
        QString name;
        int lay;
        opc::PatternDocument project;
        int layers;
        double length;
    };
    QVector<Material> materials;
    while (!pending.isEmpty()) {
        const auto current = pending.takeFirst();
        const auto& [name, lay] = current.key;
        QVector<opc::FabricPlacement> copies;
        for (const auto row : current.rows) {
            for (const auto index : assignments[row].copies)
                copies.append(original.placements[index]);
        }
        const auto label = QStringLiteral("%1 · настил %2").arg(name).arg(lay);
        auto project = document;
        auto& data = project.production();
        data.placements = copies;
        data.fabricFlaws.clear();
        const auto previous = saved.lays.constFind(current.key);
        bool reuseLayout = false;
        int layers = current.layers;
        if (current.splitSettings) {
            data = *current.splitSettings;
        } else if (previous != saved.lays.cend()) {
            data = previous->project.production();
            reuseLayout = cutCopies(data.placements) == cutCopies(copies);
            if (!reuseLayout)
                data.placements = copies;
            layers = previous->layers;
        } else {
            layers = saved.plannedLayers.value(lay, 1);
        }

        const double previousWidth = data.fabricWidth;
        if (!current.splitSettings) {
            bool accepted = false;
            data.fabricWidth = QInputDialog::getDouble(parent, title,
                QStringLiteral("Ширина материала «%1», мм").arg(label),
                data.fabricWidth, 1, 1000000, 2, &accepted);
            if (!accepted)
                return;
            if (!saved.fixedSizePlan) {
                layers = QInputDialog::getInt(parent, title, QStringLiteral("Слоёв материала «%1»").arg(label),
                    layers, 1, maximumLayers.value(), 1, &accepted);
                if (!accepted)
                    return;
            }
        }

        auto splitIfTooLong = [&](const opc::CuttingReport& report) {
            if (!saved.fixedSizePlan || tableLength.value() <= 0
                || report.usage.length <= tableLength.value() + 1e-6
                || current.rows.size() < 2 || lastLayNumber >= 9999)
                return false;

            // Every assignment goes to one replacement marker with the same
            // layer count. A left/right pair shares one assignment.
            const auto middle = current.rows.size() / 2;
            using CopyKey = CutCopies::value_type;
            QMap<CopyKey, QVector<opc::FabricPlacement>> copiesByKey;
            const auto key = [](const opc::FabricPlacement& copy) {
                return CopyKey{copy.piece, copy.size, copy.hand, copy.fold};
            };
            for (const auto& copy : data.placements)
                copiesByKey[key(copy)].append(copy);
            const auto partition = [&](qsizetype from, qsizetype to) {
                auto settings = data;
                settings.placements.clear();
                for (auto row = from; row < to; ++row) {
                    for (const auto index : assignments[current.rows[row]].copies)
                        settings.placements.append(copiesByKey[key(original.placements[index])].takeFirst());
                }
                return settings;
            };
            auto first = partition(0, middle);
            auto second = partition(middle, current.rows.size());
            pending.prepend({{name, ++lastLayNumber}, current.rows.sliced(middle), std::move(second), layers});
            pending.prepend({current.key, current.rows.first(middle), std::move(first), layers});
            return true;
        };
        const bool needsLayout = !reuseLayout || previousWidth != data.fabricWidth;
        if (needsLayout) {
            const auto arranged = opc::arrangeFabric(project, data);
            if (arranged.error == opc::FabricLayoutError::None)
                data.placements = arranged.placements;
        }
        if (needsLayout && saved.fixedSizePlan && tableLength.value() > 0) {
            const auto arranged = opc::cuttingReport(project, data);
            if (arranged.canExport(data.fabricWidth) && splitIfTooLong(arranged))
                continue;
        }
        const auto edited = editFabricLayout(parent, project, label);
        if (!edited)
            return;
        // The batch owns copy membership. The editor can refine fabric settings
        // and layout, but changing the order quantity here would change the batch.
        if (cutCopies(edited->placements) != cutCopies(copies)) {
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
        if (splitIfTooLong(report))
            continue;
        if (tableLength.value() > 0 && report.usage.length > tableLength.value() + 1e-6) {
            QMessageBox::warning(parent, title,
                QStringLiteral("Настил «%1» требует %2 мм при длине стола %3 мм.")
                    .arg(label).arg(report.usage.length, 0, 'f', 2).arg(tableLength.value(), 0, 'f', 2));
            return;
        }
        for (const auto row : current.rows)
            savedAssignments[row] = current.key;
        project.productInformation().material = name;
        materials.append({name, lay, std::move(project), layers, report.usage.length});
    }

    QJsonArray assignmentRecords;
    for (const auto& [name, lay] : savedAssignments)
        assignmentRecords.append(QJsonObject{{"material", name}, {"lay", lay}});
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
    if (!document.save(staging.filePath(QStringLiteral("source.sofit")), &error)) {
        QMessageBox::warning(parent, title, error);
        return;
    }
    QJsonArray savedLays;
    for (qsizetype i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        const auto stem = QStringLiteral("material-%1").arg(i + 1);
        const auto& data = material.project.production();
        if (!material.project.save(staging.filePath(stem + ".sofit"), &error)
            || (!saveOnly.isChecked()
                && (!opc::exportFabricDxf(staging.filePath(stem + ".dxf"), material.project, data, error)
                    || !opc::exportFabricPdf(staging.filePath(stem + ".pdf"), material.project, data, {}, error)))) {
            QMessageBox::warning(parent, title, error);
            return;
        }
        savedLays.append(QJsonObject{{"material", material.name}, {"lay", material.lay},
                                     {"layers", material.layers}});
        usage += QStringLiteral("%1\t%2.sofit\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\n").arg(material.name, stem)
            .arg(data.fabricWidth, 0, 'f', 2).arg(data.placements.size()).arg(material.length, 0, 'f', 2)
            .arg(material.layers).arg(material.length * material.layers / 1000., 0, 'f', 4)
            .arg(data.fabricShrinkagePercent.x(), 0, 'f', 2).arg(data.fabricShrinkagePercent.y(), 0, 'f', 2).arg(material.lay);
    }
    const QJsonObject manifest{{"format", "SOFIT material order"}, {"version", 1},
                               {"equipment", profile->currentText().trimmed()}, {"fixedSizePlan", saved.fixedSizePlan},
                               {"tableLength", tableLength.value()}, {"maximumLayers", maximumLayers.value()},
                               {"assignments", assignmentRecords}, {"lays", savedLays}};
    QSaveFile orderFile(staging.filePath(QStringLiteral("order.sofit-order")));
    const auto orderBytes = QJsonDocument(manifest).toJson();
    if (orderBytes.size() > materialOrderLimit) {
        QMessageBox::warning(parent, title, QStringLiteral("Файл заказа превышает 16 МБ."));
        return;
    }
    if (!orderFile.open(QIODevice::WriteOnly) || orderFile.write(orderBytes) != orderBytes.size()
        || !orderFile.commit()) {
        QMessageBox::warning(parent, title, orderFile.errorString());
        return;
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
    QMessageBox::information(parent, title,
        QStringLiteral("Производственный заказ и расход сохранены в %1").arg(output.filePath(folder)));
}
} // namespace

void exportMaterialLay(QWidget* parent, const opc::PatternDocument& document) {
    editMaterialOrder(parent, document, {});
}

void planMaterialOrder(QWidget* parent, const opc::PatternDocument& document) {
    const auto title = QStringLiteral("Заказ по размерам");
    qsizetype copiesPerKit = 0;
    for (const auto& piece : document.pieces())
        copiesPerKit += piece.cutCount;
    if (copiesPerKit < 1 || copiesPerKit > opc::maxAutomaticCopies) {
        QMessageBox::warning(parent, title, QStringLiteral("Нужны лекала с числом копий от 1 до 200 на комплект."));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QVBoxLayout layout(&dialog);
    auto* table = makeTable(&layout, {QStringLiteral("Размер"), QStringLiteral("Комплектов в заказе")});
    QMap<QString, QSpinBox*> inputs;
    const auto addSize = [&](const QString& size) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto* name = new QTableWidgetItem(size.isEmpty() ? QStringLiteral("База") : size);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, name);
        auto* quantity = new QSpinBox;
        quantity->setRange(0, 1000000);
        table->setCellWidget(row, 1, quantity);
        inputs.insert(size, quantity);
    };
    layout.addWidget(new QLabel(QStringLiteral("Копии создаются по норме раскроя каждой детали. "
        "При нескольких комплектах на слое план может объединять размеры.")));
    addSize({});
    for (const auto& size : document.production().sizes)
        addSize(size.name);
    QFormLayout limits;
    QSpinBox maximumLayers;
    maximumLayers.setRange(1, 10000);
    maximumLayers.setValue(10000);
    limits.addRow(QStringLiteral("Предельное число слоёв"), &maximumLayers);
    QSpinBox maximumKits;
    maximumKits.setRange(1, opc::maxAutomaticCopies / copiesPerKit);
    maximumKits.setValue(1);
    limits.addRow(QStringLiteral("Предельное число комплектов на слой"), &maximumKits);
    layout.addLayout(&limits);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVector<opc::KitLay> planned;
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QMap<QString, int> demand;
        for (auto input = inputs.cbegin(); input != inputs.cend(); ++input)
            demand.insert(input.key(), input.value()->value());
        planned = opc::planKitLays(demand, maximumLayers.value(), maximumKits.value());
        qsizetype assignments = 0;
        for (const auto& lay : planned) {
            for (const auto kits : lay.kitsPerSize)
                assignments += kits * copiesPerKit;
        }
        if (planned.isEmpty() || assignments > 5000) {
            QMessageBox::warning(&dialog, title,
                QStringLiteral("Укажите тираж. План должен содержать не более 5000 назначений деталей."));
            return;
        }
        dialog.accept();
    });
    dialog.resize(460, 380);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto source = document;
    auto& placements = source.production().placements;
    placements.clear();
    MaterialOrder order;
    order.maximumLayers = maximumLayers.value();
    order.fixedSizePlan = true;
    for (qsizetype index = 0; index < planned.size(); ++index) {
        const auto& lay = planned[index];
        const int number = int(index + 1);
        order.plannedLayers.insert(number, lay.layers);
        for (auto size = lay.kitsPerSize.cbegin(); size != lay.kitsPerSize.cend(); ++size) {
            for (qsizetype piece = 0; piece < source.pieces().size(); ++piece) {
                for (int copy = 0; copy < source.pieces()[piece].cutCount * size.value(); ++copy) {
                    placements.append({piece, size.key(), {}});
                    order.assignments.append({QStringLiteral("Основной"), number});
                }
            }
        }
    }
    editMaterialOrder(parent, source, order);
}

void reopenMaterialOrder(QWidget* parent) {
    const auto title = QStringLiteral("Открыть производственный заказ");
    const auto path = QFileDialog::getOpenFileName(parent, title, {}, QStringLiteral("Заказ SOFIT (*.sofit-order)"));
    if (path.isEmpty())
        return;
    auto invalid = [&] {
        QMessageBox::warning(parent, title, QStringLiteral("Файл заказа повреждён или имеет неизвестный формат."));
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > materialOrderLimit) {
        invalid();
        return;
    }
    const auto bytes = file.read(materialOrderLimit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > materialOrderLimit) {
        invalid();
        return;
    }
    const auto root = QJsonDocument::fromJson(bytes).object();
    MaterialOrder order;
    order.equipmentName = root.value("equipment").toString();
    order.fixedSizePlan = root.value("fixedSizePlan").toBool();
    order.tableLength = root.value("tableLength").toDouble(-1);
    order.maximumLayers = root.value("maximumLayers").toInt();
    if (root.value("format") != QStringLiteral("SOFIT material order") || root.value("version").toInt() != 1
        || (root.contains("fixedSizePlan") && !root.value("fixedSizePlan").isBool())
        || (root.contains("equipment") && !root.value("equipment").isString())
        || (!order.equipmentName.isEmpty() && !validMaterial(order.equipmentName))
        || order.tableLength < 0 || order.tableLength > 1000000
        || order.maximumLayers < 1 || order.maximumLayers > 10000
        || !root.value("assignments").isArray() || !root.value("lays").isArray()) {
        invalid();
        return;
    }
    const auto directory = QFileInfo(path).absoluteDir();
    opc::PatternDocument source;
    QString error;
    if (!source.load(directory.filePath(QStringLiteral("source.sofit")), &error)) {
        QMessageBox::warning(parent, title, error);
        return;
    }
    const auto assignments = root.value("assignments").toArray();
    const auto lays = root.value("lays").toArray();
    if (assignments.isEmpty() || lays.isEmpty() || assignments.size() > source.production().placements.size()
        || lays.size() > assignments.size()) {
        invalid();
        return;
    }
    std::set<LayKey> keys;
    for (const auto& entry : assignments) {
        const auto assignment = entry.toObject();
        const auto name = assignment.value("material").toString();
        const int lay = assignment.value("lay").toInt();
        if (!validMaterial(name) || lay < 1 || lay > 9999) {
            invalid();
            return;
        }
        order.assignments.append(LayKey{name, lay});
        keys.insert({name, lay});
    }
    for (qsizetype i = 0; i < lays.size(); ++i) {
        const auto entry = lays.at(i).toObject();
        const LayKey key{entry.value("material").toString(), entry.value("lay").toInt()};
        const int layers = entry.value("layers").toInt();
        if (order.fixedSizePlan) {
            if (order.plannedLayers.value(key.second, layers) != layers) {
                invalid();
                return;
            }
            order.plannedLayers.insert(key.second, layers);
        }
        if (!keys.erase(key) || layers < 1 || layers > order.maximumLayers) {
            invalid();
            return;
        }
        SavedLay saved{{}, layers};
        if (!saved.project.load(directory.filePath(QStringLiteral("material-%1.sofit").arg(i + 1)), &error)) {
            QMessageBox::warning(parent, title, error);
            return;
        }
        order.lays.insert(key, std::move(saved));
    }
    if (!keys.empty()) {
        invalid();
        return;
    }
    editMaterialOrder(parent, source, order);
}
