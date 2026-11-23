#pragma once

#include "PatternDocument.h"
#include <QPageLayout>
#include <QPolygonF>

namespace opc {

struct PatternPdfOptions {
    QPageSize::PageSizeId paper = QPageSize::A4;
    QPageLayout::Orientation orientation = QPageLayout::Portrait;
};

// The document's layer switches and the caller's session-only object visibility
// are both applied. Allowances follow their own layer switch.
bool exportPatternPdf(const QString& path, const PatternDocument& document,
                      const QVector<bool>& objectVisibility,
                      const QVector<QPolygonF>& allowances,
                      const PatternPdfOptions& options, QString& error);

}
