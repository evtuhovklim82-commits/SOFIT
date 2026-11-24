#include "ProductInformation.h"
#include "PatternDocument.h"
#include "Production.h"

#include <QCoreApplication>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>
#include <algorithm>

namespace opc {

std::optional<double> finishedMeasurementMm(const PatternDocument& graded, const FinishedMeasurement& measurement) {
    const auto& entities = graded.entities();
    const auto a = std::ranges::find(entities, measurement.first, &Entity::draftName);
    const auto b = std::ranges::find(entities, measurement.second, &Entity::draftName);
    if (a == entities.end() || b == entities.end() || a->shape != Shape::Point || b->shape != Shape::Point)
        return {};
    return QLineF(a->a, b->a).length();
}

bool exportProductInformationPdf(const QString& path, const PatternDocument& document, QString& error) {
    const auto tr = [](const char* text) { return QCoreApplication::translate("ProductInformation", text); };
    const auto& info = document.productInformation();
    QTextDocument report;
    QTextCursor cursor(&report);
    QTextCharFormat heading;
    heading.setFontPointSize(18);
    heading.setFontWeight(QFont::Bold);
    const auto reportTitle = info.reportTitle.trimmed().isEmpty() ? tr("Product Information") : info.reportTitle;
    const auto section = [&](const QString& label) {
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.insertText(label, heading);
    };
    cursor.insertText(reportTitle, heading);
    const auto field = [&](const QString& label, const QString& value) {
        if (!value.isEmpty()) {
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            cursor.insertText(label + QStringLiteral(" — ") + value);
        }
    };
    field(tr("Style code"), info.styleCode);
    field(tr("Title"), info.title);
    field(tr("Revision"), info.revision);
    field(tr("Material"), info.material);
    field(tr("Notes"), info.notes);

    const auto textSection = [&](const QString& label, const QString& text) {
        if (text.trimmed().isEmpty())
            return;
        section(label);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.insertText(text);
    };
    textSection(tr("Bill of materials"), info.billOfMaterials);
    textSection(tr("Assembly instructions"), info.assemblyInstructions);

    if (info.includeSizeMeasurements) {
        QMap<QString, double> base;
        for (const auto& measurement : document.parametricDraft().measurements)
            base.insert(measurement.name, measurement.mm);
        const auto& production = document.production();
        qsizetype rowCount = base.size();
        for (const auto& size : production.sizes) {
            for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
                rowCount += !base.contains(it.key());
            rowCount += base.size();
        }
        if (rowCount > 0) {
            section(tr("Draft input measurements (mm)"));
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            QTextTableFormat format;
            format.setHeaderRowCount(1);
            format.setCellPadding(3);
            auto* table = cursor.insertTable(rowCount + 1, 3, format);
            const auto cell = [table](int row, int column, const QString& value) {
                table->cellAt(row, column).firstCursorPosition().insertText(value);
            };
            cell(0, 0, tr("Size"));
            cell(0, 1, tr("Measurement"));
            cell(0, 2, tr("Millimetres"));
            int row = 1;
            const auto measurements = [&](const QString& size, const QMap<QString, double>& values) {
                for (auto it = values.cbegin(); it != values.cend(); ++it) {
                    cell(row, 0, size);
                    cell(row, 1, it.key());
                    cell(row++, 2, QString::number(it.value(), 'g', 12));
                }
            };
            measurements(tr("Base"), base);
            for (const auto& size : production.sizes) {
                auto values = base;
                for (auto it = size.measurements.cbegin(); it != size.measurements.cend(); ++it)
                    values.insert(it.key(), it.value());
                measurements(size.name, values);
            }
            cursor.setPosition(table->lastPosition() + 1);
        }
    }

    if (!info.finishedMeasurements.isEmpty()) {
        section(tr("Finished pattern measurements (mm)"));
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        QTextTableFormat format;
        format.setHeaderRowCount(1);
        format.setCellPadding(3);
        QVector<QString> sizes{QString{}};
        for (const auto& size : document.production().sizes)
            sizes.append(size.name);
        auto* table = cursor.insertTable(1 + sizes.size() * info.finishedMeasurements.size(), 3, format);
        const auto cell = [table](int row, int column, const QString& value) {
            table->cellAt(row, column).firstCursorPosition().insertText(value);
        };
        cell(0, 0, tr("Size"));
        cell(0, 1, tr("Measurement"));
        cell(0, 2, tr("Millimetres"));
        int row = 1;
        for (const auto& size : sizes) {
            const auto graded = gradedPattern(document, size);
            for (const auto& measurement : info.finishedMeasurements) {
                const auto value = finishedMeasurementMm(graded, measurement);
                cell(row, 0, size.isEmpty() ? tr("Base") : size);
                cell(row, 1, measurement.label);
                cell(row++, 2, value ? QString::number(*value, 'f', 2) : tr("Unavailable"));
            }
        }
        cursor.setPosition(table->lastPosition() + 1);
    }

    if (info.includeCuttingQuantities && !document.pieces().isEmpty()) {
        const auto rows = cuttingKitRows(document, cuttingReport(document, document.production()));
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.insertText(tr("Parts and marker quantities"), heading);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        QTextTableFormat format;
        format.setHeaderRowCount(1);
        format.setCellPadding(3);
        auto* table = cursor.insertTable(rows.size() + 1, 7, format);
        const QStringList labels{tr("Part"), tr("Size"), tr("Single"), tr("Left"), tr("Right"), tr("On fold"), tr("Total")};
        for (int column = 0; column < labels.size(); ++column)
            table->cellAt(0, column).firstCursorPosition().insertText(labels[column]);
        for (int i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const QStringList cells{document.pieces()[row.piece].name, row.size.isEmpty() ? tr("Base") : row.size,
                QString::number(row.single), QString::number(row.left), QString::number(row.right),
                QString::number(row.onFold), QString::number(row.copies())};
            for (int column = 0; column < cells.size(); ++column)
                table->cellAt(i + 1, column).firstCursorPosition().insertText(cells[column]);
        }
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    {
        QPdfWriter writer(&output);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(96);
        writer.setTitle(info.reportTitle.isEmpty() && !info.title.isEmpty() ? info.title : reportTitle);
        report.print(&writer);
    }
    if (output.error() != QFileDevice::NoError || output.size() == 0 || !output.commit()) {
        error = output.error() == QFileDevice::NoError ? tr("Could not write the product information PDF") : output.errorString();
        return false;
    }
    error.clear();
    return true;
}

}
