#include "PatternPdf.h"
#include "EntityPath.h"
#include "PieceMarkings.h"
#include "Production.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace {
constexpr double marginMm = 10;
constexpr double footerMm = 18;
constexpr double overlapMm = 10;
constexpr double paddingMm = 2;
constexpr int dpi = 254;
constexpr double pixelsPerMm = dpi / 25.4;
constexpr int maxPages = 200;

QString tr(const char* text) {
    return QCoreApplication::translate("PatternPdf", text);
}

struct Stroke {
    QPainterPath path;
    QRectF bounds;
    opc::Layer layer;
    bool filled = false;
};

double tileCount(double length, double span) {
    return 1 + std::ceil(std::max(0.0, length - span) / (span - overlapMm));
}

void drawRegistration(QPainter& painter, const QRectF& frame, int row, int column,
                      int rows, int columns) {
    QPen pen(Qt::black, 0.18);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.drawRect(frame);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);
    const auto cross = [&painter](QPointF center) {
        const QPointF horizontal(2, 0);
        const QPointF vertical(0, 2);
        painter.drawLine(center - horizontal, center + horizontal);
        painter.drawLine(center - vertical, center + vertical);
    };
    const double halfOverlap = overlapMm / 2;
    if (column > 0)
        cross({frame.left() + halfOverlap, frame.center().y()});
    if (column + 1 < columns)
        cross({frame.right() - halfOverlap, frame.center().y()});
    if (row > 0)
        cross({frame.center().x(), frame.top() + halfOverlap});
    if (row + 1 < rows)
        cross({frame.center().x(), frame.bottom() - halfOverlap});
}

void drawFooter(QPainter& painter, const QRectF& frame, int row, int column,
                int rows, int columns) {
    const double left = frame.left();
    const double top = frame.bottom();
    QFont font = painter.font();
    font.setPixelSize(int(3 * pixelsPerMm));
    painter.save();
    // Use an integer font size in device pixels, then keep placement in mm.
    painter.scale(1 / pixelsPerMm, 1 / pixelsPerMm);
    painter.setFont(font);
    painter.drawText(QPointF(left + 60, top + 7) * pixelsPerMm,
                     tr("Row %1/%2, column %3/%4").arg(row + 1).arg(rows).arg(column + 1).arg(columns));
    painter.drawText(QPointF(left + 60, top + 12) * pixelsPerMm,
                     tr("Actual size (100%)  |  10 mm overlap"));
    painter.drawText(QPointF(left, top + 12) * pixelsPerMm, tr("50 mm"));
    painter.restore();
    painter.drawLine(QLineF(left, top + 6, left + 50, top + 6));
    painter.drawLine(QLineF(left, top + 4, left, top + 8));
    painter.drawLine(QLineF(left + 50, top + 4, left + 50, top + 8));
}
bool writePdf(const QString& path, const QVector<Stroke>& strokes, const QRectF& bounds,
              const opc::PatternPdfOptions& options, QString& error) {
    QPageLayout layout(QPageSize(options.paper), options.orientation, QMarginsF(), QPageLayout::Millimeter);
    layout.setMode(QPageLayout::FullPageMode);
    const QSizeF paper = layout.fullRect(QPageLayout::Millimeter).size();
    const QRectF frame(marginMm, marginMm,
                       paper.width() - 2 * marginMm, paper.height() - 2 * marginMm - footerMm);
    if (!layout.isValid() || frame.width() <= overlapMm || frame.height() <= overlapMm) {
        error = tr("The selected paper size cannot fit the print area.");
        return false;
    }

    const double columnCount = tileCount(bounds.width(), frame.width());
    const double rowCount = tileCount(bounds.height(), frame.height());
    if (!std::isfinite(bounds.left()) || !std::isfinite(bounds.top()) ||
        !std::isfinite(columnCount * rowCount) || columnCount * rowCount > maxPages) {
        error = tr("The drawing exceeds the PDF export limit of %1 pages.").arg(maxPages);
        return false;
    }
    const int columns = int(columnCount);
    const int rows = int(rowCount);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setCreator(QStringLiteral("SOFIT"));
        writer.setResolution(dpi);
        QPainter painter;
        if (!writer.setPageLayout(layout) || !painter.begin(&writer)) {
            error = tr("Could not start PDF export.");
            return false;
        }

        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                if ((row != 0 || column != 0) && !writer.newPage()) {
                    error = tr("Could not add a PDF page.");
                    return false;
                }
                const QPointF origin = bounds.topLeft() +
                    QPointF(column * (frame.width() - overlapMm), row * (frame.height() - overlapMm));
                painter.save();
                painter.scale(pixelsPerMm, pixelsPerMm);
                painter.save();
                painter.setClipRect(frame);
                painter.translate(frame.topLeft() - origin);
                const QRectF tile(origin, frame.size());
                for (const auto& stroke : strokes) {
                    if (!tile.intersects(stroke.bounds))
                        continue;
                    Qt::PenStyle style = Qt::SolidLine;
                    if (stroke.layer == opc::Layer::Construction)
                        style = Qt::DotLine;
                    if (stroke.layer == opc::Layer::Allowance)
                        style = Qt::DashLine;
                    painter.setPen(QPen(Qt::black, 0.28, style));
                    if (stroke.filled)
                        painter.setPen(Qt::NoPen);
                    painter.setBrush(stroke.filled ? QBrush(Qt::black) : QBrush(Qt::NoBrush));
                    painter.drawPath(stroke.path);
                }
                painter.restore();
                drawRegistration(painter, frame, row, column, rows, columns);
                drawFooter(painter, frame, row, column, rows, columns);
                painter.restore();
            }
        }
        if (!painter.end()) {
            error = tr("Could not finish the PDF.");
            return false;
        }
    }
    if (file.error() != QFileDevice::NoError || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
}

namespace opc {
bool exportPatternPdf(const QString& path, const PatternDocument& document,
                      const QVector<bool>& objectVisibility,
                      const QVector<QPolygonF>& allowances,
                      const PatternPdfOptions& options, QString& error) {
    error.clear();
    if (objectVisibility.size() != document.entities().size()) {
        error = tr("Object visibility does not match the drawing.");
        return false;
    }

    QVector<Stroke> strokes;
    QRectF bounds;
    const auto append = [&strokes, &bounds](QPainterPath outline, Layer layer, bool filled = false) {
        if (outline.isEmpty())
            return;
        const auto extent = outline.boundingRect().adjusted(-paddingMm, -paddingMm, paddingMm, paddingMm);
        bounds = bounds.united(extent);
        strokes.append({std::move(outline), extent, layer, filled});
    };
    QVector<bool> visiblePieces(document.pieces().size(), false);
    QVector<bool> edgeMarkedPieces(document.pieces().size(), false);
    const auto linkedNotches = document.inspectSeamLinks().notches;
    for (qsizetype i = 0; i < document.entities().size(); ++i) {
        const auto& entity = document.entities()[i];
        if (entity.piece >= 0 && entity.layer == Layer::Main
            && (!entity.seamNotches.isEmpty() || linkedNotches.contains(i)))
            edgeMarkedPieces[entity.piece] = true;
        if (objectVisibility[i] && document.layerVisible(entity.layer)) {
            append(entityPath(entity), entity.layer, entity.shape == Shape::Point);
            if (entity.piece >= 0 && entity.layer == Layer::Main)
                visiblePieces[entity.piece] = true;
        }
    }
    for (qsizetype i = 0; i < visiblePieces.size(); ++i) {
        if (!visiblePieces[i])
            continue;
        const auto& piece = document.pieces()[i];
        if (!piece.showLabel && !piece.grainAngle && piece.notches.isEmpty() && !edgeMarkedPieces[i])
            continue;
        const auto marks = buildPieceMarkings(piece, analyzePiece(document, i),
                                              document.layerVisible(Layer::Allowance));
        append(marks.notches, Layer::Main);
        append(marks.grainline, Layer::Main);
        append(marks.labelPath(), Layer::Main, true);
    }
    if (document.layerVisible(Layer::Allowance)) {
        for (const auto& polygon : allowances) {
            if (polygon.isEmpty())
                continue;
            QPainterPath outline;
            outline.addPolygon(polygon);
            outline.closeSubpath();
            append(std::move(outline), Layer::Allowance);
        }
    }
    if (strokes.isEmpty()) {
        error = tr("There is no visible geometry to export.");
        return false;
    }

    return writePdf(path, strokes, bounds, options, error);
}

bool exportFabricPdf(const QString& path, const PatternDocument& document,
                     const ProductionData& data, const PatternPdfOptions& options, QString& error) {
    error.clear();
    const auto report = cuttingReport(document, data);
    if (!report.canExport(data.fabricWidth)) {
        error = tr("The fabric layout must contain valid copies within the fabric and meet overlap, clearance, orientation and fold checks.");
        return false;
    }

    QVector<Stroke> strokes;
    const auto append = [&](QPainterPath stroke, Layer layer) {
        const auto extent = stroke.boundingRect().adjusted(-paddingMm, -paddingMm, paddingMm, paddingMm);
        strokes.append({std::move(stroke), extent, layer});
    };
    const QRectF fabric(0, 0, data.fabricWidth, report.usage.length);
    QPainterPath boundary;
    boundary.addRect(fabric);
    append(std::move(boundary), Layer::Construction);
    QMap<QString, PatternDocument> graded;
    for (const auto& placement : data.placements) {
        if (!graded.contains(placement.size))
            graded.insert(placement.size, gradedPattern(document, placement.size));
        const auto geometry = fabricPiece(graded.constFind(placement.size).value(), placement);
        const bool onFold = !geometry.fold.isNull();
        const auto outline = onFold ? openFoldContour(geometry.cut, geometry.fold) : geometry.cut;
        if (outline.size() < 3) {
            error = tr("Could not separate a cutting contour from its fold line.");
            return false;
        }
        QPainterPath cut;
        cut.addPolygon(outline.translated(placement.position));
        if (!onFold)
            cut.closeSubpath();
        append(std::move(cut), Layer::Main);
        if (onFold) {
            QPainterPath fold(geometry.fold.p1() + placement.position);
            fold.lineTo(geometry.fold.p2() + placement.position);
            append(std::move(fold), Layer::Allowance);
        }
        if (document.pieces()[placement.piece].grainAngle) {
            QPainterPath grain(geometry.grain.p1() + placement.position);
            grain.lineTo(geometry.grain.p2() + placement.position);
            append(std::move(grain), Layer::Construction);
        }
    }
    return writePdf(path, strokes, fabric.adjusted(-paddingMm, -paddingMm, paddingMm, paddingMm),
                    options, error);
}
}
