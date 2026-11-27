#include "ParametricDialog.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <utility>

namespace {
QString tr(const char* text) {
    return QCoreApplication::translate("ParametricDialog", text);
}

void appendRow(QTableWidget& table, const QStringList& values) {
    const auto row = table.rowCount();
    table.insertRow(row);
    for (int col = 0; col < table.columnCount(); ++col)
        table.setItem(row, col, new QTableWidgetItem(values.value(col)));
}

QTableWidget& addTable(QTabWidget& tabs, const QString& title, const QStringList& columns) {
    auto* page = new QWidget(&tabs);
    auto* layout = new QVBoxLayout(page);
    auto* table = new QTableWidget(0, static_cast<int>(columns.size()), page);
    table->setHorizontalHeaderLabels(columns);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setAlternatingRowColors(true);
    layout->addWidget(table);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add row"), page);
    auto* remove = new QPushButton(tr("Remove current row"), page);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    QObject::connect(add, &QPushButton::clicked, table, [table] {
        appendRow(*table, {});
        auto* first = table->item(table->rowCount() - 1, 0);
        table->setFocus();
        table->setCurrentItem(first);
        table->editItem(first);
    });
    QObject::connect(remove, &QPushButton::clicked, table, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    tabs.addTab(page, title);
    return *table;
}

void readRows(const QTableWidget& table, const std::function<void(const QStringList&)>& read) {
    for (int row = 0; row < table.rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < table.columnCount(); ++col)
            cells.append(table.item(row, col)->text().trimmed());
        read(cells);
    }
}
}

std::optional<opc::PatternDocument> editParametricDraft(QWidget* parent, const opc::PatternDocument& document,
                                                         QString* calculationError) {
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Parametric Draft"));
    dialog.resize(760, 480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* help = new QLabel(tr("Names are case-sensitive. Use A–Z, digits and underscores. Start with a letter or underscore. "
                               "Formulas support +, -, *, / and parentheses. Use a decimal point in formulas. "
                               "Example: chest / 4 + ease or A.x + 10. "
                               "Enter a control point for a quadratic curve, or leave it empty for a line. "
                               "Use tangentx and tangenty with endpoint coordinates and angles in degrees to calculate a control point. "
                               "Drag a draft point on the canvas to add an offset to its formulas."), &dialog);
    help->setTextFormat(Qt::PlainText);
    help->setWordWrap(true);
    layout->addWidget(help);

    auto* tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs, 1);
    auto& measurements = addTable(*tabs, tr("Measurements"), {tr("Name"), tr("Value (mm)")});
    auto& parameters = addTable(*tabs, tr("Parameters"), {tr("Name"), tr("Formula")});
    auto& points = addTable(*tabs, tr("Points"), {tr("Name"), tr("X formula (mm)"), tr("Y formula (mm)")});
    auto& edges = addTable(*tabs, tr("Lines and curves"),
                           {tr("Name"), tr("Start point"), tr("End point"), tr("Control point (optional)")});

    const auto& original = document.parametricDraft();
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator | QLocale::RejectGroupSeparator);
    auto decimalPointLocale = QLocale::c();
    decimalPointLocale.setNumberOptions(QLocale::RejectGroupSeparator);
    for (const auto& entry : original.measurements)
        appendRow(measurements, {entry.name, locale.toString(entry.mm, 'g', 17)});
    for (const auto& entry : original.parameters)
        appendRow(parameters, {entry.name, entry.formula});
    for (const auto& entry : original.points)
        appendRow(points, {entry.name, entry.x, entry.y});
    for (const auto& entry : original.edges)
        appendRow(edges, {entry.name, entry.start, entry.end, entry.control});

    auto* feedback = new QLabel(&dialog);
    feedback->setTextFormat(Qt::PlainText);
    feedback->setWordWrap(true);
    feedback->setVisible(false);
    layout->addWidget(feedback);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* validate = buttons->addButton(tr("Validate"), QDialogButtonBox::ActionRole);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Apply"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::optional<opc::PatternDocument> result;
    const auto evaluate = [&](bool apply) {
        // A keyboard-triggered action can arrive while a cell editor still owns focus.
        if (auto* focused = dialog.focusWidget())
            focused->clearFocus();
        opc::ParametricDraft draft;
        QString error;
        readRows(measurements, [&](const QStringList& row) {
            bool ok = false;
            auto mm = locale.toDouble(row[1], &ok);
            if (!ok)
                mm = decimalPointLocale.toDouble(row[1], &ok);
            if (!ok && error.isEmpty())
                error = tr("Enter a numeric value in mm for measurement row %1.").arg(draft.measurements.size() + 1);
            draft.measurements.append({row[0], mm});
        });
        readRows(parameters, [&](const QStringList& row) {
            draft.parameters.append({row[0], row[1]});
        });
        readRows(points, [&](const QStringList& row) {
            draft.points.append({row[0], row[1], row[2]});
        });
        readRows(edges, [&](const QStringList& row) {
            draft.edges.append({row[0], row[1], row[2], row[3]});
        });

        auto updated = document;
        const bool valid = error.isEmpty() && updated.setParametricDraft(std::move(draft), &error);
        if (calculationError)
            *calculationError = error;
        if (valid) {
            if (apply) {
                if (updated.parametricDraft() != original)
                    result = std::move(updated);
                dialog.accept();
                return;
            }
            feedback->setText(tr("All measurements, formulas and point references are valid."));
        } else {
            feedback->setText(error);
        }
        feedback->show();
    };
    QObject::connect(validate, &QPushButton::clicked, &dialog, [&] { evaluate(false); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { evaluate(true); });
    for (auto* table : {&measurements, &parameters, &points, &edges}) {
        QObject::connect(table, &QTableWidget::itemChanged, feedback, &QWidget::hide);
        QObject::connect(table->model(), &QAbstractItemModel::rowsRemoved, feedback, &QWidget::hide);
    }
    dialog.exec();
    return result;
}
