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

namespace opc {

bool exportProductInformationPdf(const QString& path, const PatternDocument& document, QString& error) {
    const auto tr = [](const char* text) { return QCoreApplication::translate("ProductInformation", text); };
    const auto& info = document.productInformation();
    QTextDocument report;
    QTextCursor cursor(&report);
    QTextCharFormat heading;
    heading.setFontPointSize(18);
    heading.setFontWeight(QFont::Bold);
    cursor.insertText(tr("Product Information"), heading);
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

    if (!document.pieces().isEmpty()) {
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
        writer.setTitle(info.title.isEmpty() ? tr("Product Information") : info.title);
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
