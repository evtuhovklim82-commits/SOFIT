#include "PatternDxf.h"
#include "PieceMarkings.h"
#include "Production.h"

#include <QLineF>
#include <QMap>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <QStringList>

namespace opc {
namespace {
class DxfWriter {
public:
    explicit DxfWriter(QTextStream& stream) : stream_(stream) {}

    template<class T>
    void group(int code, const T& value) { stream_ << code << '\n' << value << '\n'; }

    void section(const char* name) {
        group(0, "SECTION");
        group(2, name);
    }

    void point(int code, QPointF position, bool planar = false) {
        group(code, position.x());
        group(code + 10, -position.y());
        if (!planar)
            group(code + 20, 0);
    }

    void entity(const char* type, const QString& layer, const char* subclass) {
        group(0, type);
        group(100, "AcDbEntity");
        group(8, layer);
        group(100, subclass);
    }

    void polyline(const QString& layer, const QPolygonF& vertices, bool closed) {
        if (vertices.size() < 2)
            return;
        entity("LWPOLYLINE", layer, "AcDbPolyline");
        group(90, vertices.size());
        group(70, closed ? 1 : 0);
        for (const auto& vertex : vertices)
            point(10, vertex, true);
    }

    void line(const QString& layer, QPointF start, QPointF end) {
        entity("LINE", layer, "AcDbLine");
        point(10, start);
        point(11, end);
    }

    void lines(const QString& layer, const QPainterPath& path) {
        for (qsizetype i = 1; i < path.elementCount(); ++i) {
            if (path.elementAt(i).isLineTo())
                line(layer, path.elementAt(i - 1), path.elementAt(i));
        }
    }

private:
    QTextStream& stream_;
};

void writeHeader(DxfWriter& dxf, const QStringList& layers) {
    dxf.section("HEADER");
    dxf.group(9, "$ACADVER");
    dxf.group(1, "AC1015");
    dxf.group(9, "$INSUNITS");
    dxf.group(70, 4);
    dxf.group(9, "$MEASUREMENT");
    dxf.group(70, 1);
    dxf.group(0, "ENDSEC");

    dxf.section("TABLES");
    dxf.group(0, "TABLE");
    dxf.group(2, "LAYER");
    dxf.group(100, "AcDbSymbolTable");
    dxf.group(70, layers.size());
    for (const auto& layer : layers) {
        dxf.group(0, "LAYER");
        dxf.group(100, "AcDbSymbolTableRecord");
        dxf.group(100, "AcDbLayerTableRecord");
        dxf.group(2, layer);
        dxf.group(70, 0);
        dxf.group(62, 7);
        dxf.group(6, "CONTINUOUS");
    }
    dxf.group(0, "ENDTAB");
    dxf.group(0, "ENDSEC");
}

QString dxfText(const QString& text) {
    QString escaped;
    for (const QChar character : text) {
        const unsigned code = character.unicode();
        if (code < 32 || code > 126 || character == '\\' || character == '%')
            escaped += QStringLiteral("\\U+%1").arg(code, 4, 16, QLatin1Char('0')).toUpper();
        else
            escaped += character;
    }
    return escaped;
}
}

bool exportPatternDxf(const QString& path, const PatternDocument& document,
                      const QVector<bool>& visibility, const QVector<PieceGeometry>& geometry,
                      QString& error) {
    if (visibility.size() != document.entities().size() || geometry.size() != document.pieces().size()) {
        error = QStringLiteral("Export geometry does not match the document.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setRealNumberPrecision(16);
    DxfWriter dxf(stream);
    QStringList layers{"0", "MAIN", "CONSTRUCTION", "ALLOWANCE", "NOTCH", "GRAINLINE", "LABEL"};
    QSet<QString> layerKeys;
    for (const auto& layer : layers)
        layerKeys.insert(layer.toCaseFolded());
    for (qsizetype i = 0; i < visibility.size(); ++i) {
        const auto& entity = document.entities()[i];
        if (!visibility[i] || !document.layerVisible(entity.layer) || entity.dxfLayer.isEmpty())
            continue;
        const auto name = dxfText(entity.dxfLayer);
        const auto key = name.toCaseFolded();
        if (!layerKeys.contains(key)) {
            layers.append(name);
            layerKeys.insert(key);
        }
    }
    writeHeader(dxf, layers);

    dxf.section("ENTITIES");
    QVector<bool> visiblePieces(geometry.size(), false);
    for (qsizetype i = 0; i < visibility.size(); ++i) {
        const auto& item = document.entities()[i];
        if (!visibility[i] || !document.layerVisible(item.layer))
            continue;
        if (item.piece >= 0 && item.layer == Layer::Main)
            visiblePieces[item.piece] = true;
        const auto layer = item.dxfLayer.isEmpty()
            ? (item.layer == Layer::Main ? QStringLiteral("MAIN") : QStringLiteral("CONSTRUCTION"))
            : dxfText(item.dxfLayer);
        switch (item.shape) {
        case Shape::Point:
            dxf.entity("POINT", layer, "AcDbPoint");
            dxf.point(10, item.a);
            break;
        case Shape::Line:
            dxf.line(layer, item.a, item.b);
            break;
        case Shape::Circle:
            dxf.entity("CIRCLE", layer, "AcDbCircle");
            dxf.point(10, item.a);
            dxf.group(40, QLineF(item.a, item.b).length());
            break;
        case Shape::Rectangle: {
            const auto rect = QRectF(item.a, item.b).normalized();
            dxf.polyline(layer, {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()}, true);
            break;
        }
        case Shape::Curve:
            dxf.entity("SPLINE", layer, "AcDbSpline");
            dxf.group(70, 8);
            dxf.group(71, 2);
            dxf.group(72, 6);
            dxf.group(73, 3);
            dxf.group(74, 0);
            for (int knot : {0, 0, 0, 1, 1, 1})
                dxf.group(40, knot);
            for (const auto& control : {item.a, item.control, item.b})
                dxf.point(10, control);
            break;
        }
    }

    for (qsizetype i = 0; i < geometry.size(); ++i) {
        if (document.layerVisible(Layer::Allowance))
            dxf.polyline("ALLOWANCE", geometry[i].allowance, true);
        if (!visiblePieces[i])
            continue;
        const auto marks = buildPieceMarkings(document.pieces()[i], geometry[i],
                                              document.layerVisible(Layer::Allowance));
        dxf.lines("NOTCH", marks.notches);
        dxf.lines("GRAINLINE", marks.grainline);
        if (!marks.label.isEmpty()) {
            dxf.entity("TEXT", "LABEL", "AcDbText");
            dxf.point(10, marks.labelOrigin);
            dxf.group(40, 3);
            dxf.group(1, dxfText(marks.label));
            dxf.group(72, 1);
            dxf.point(11, marks.labelOrigin);
            dxf.group(100, "AcDbText");
            dxf.group(73, 0);
        }
    }
    dxf.group(0, "ENDSEC");
    dxf.group(0, "EOF");
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        error = file.errorString();
        return false;
    }
    error.clear();
    return true;
}
bool exportFabricDxf(const QString& path, const PatternDocument& document,
                     const ProductionData& data, QString& error) {
    const auto report = cuttingReport(document, data);
    const auto& usage = report.usage;
    if (!report.canExport(data.fabricWidth)) {
        error = QStringLiteral("The fabric layout must contain valid copies within the fabric and meet overlap, clearance, flaw, repeat, orientation and fold checks.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setRealNumberPrecision(16);
    DxfWriter dxf(stream);
    writeHeader(dxf, {"0", "CUT", "FOLD", "GRAINLINE", "FABRIC"});
    dxf.section("ENTITIES");
    dxf.polyline("FABRIC", QPolygonF(QRectF(0, 0, data.fabricWidth, usage.length)), true);
    QMap<QString, PatternDocument> graded;
    for (const auto& placement : data.placements) {
        if (!graded.contains(placement.size))
            graded.insert(placement.size, gradedPattern(document, placement.size));
        const auto geometry = fabricPiece(graded.constFind(placement.size).value(), placement);
        const bool onFold = !geometry.fold.isNull();
        auto outline = onFold ? openFoldContour(geometry.cut, geometry.fold) : geometry.cut;
        if (outline.size() < 3) {
            error = QStringLiteral("Could not separate a cutting contour from its fold line.");
            return false;
        }
        if (!onFold && outline.front() == outline.back())
            outline.removeLast();
        dxf.polyline("CUT", outline.translated(placement.position), !onFold);
        if (onFold)
            dxf.line("FOLD", geometry.fold.p1() + placement.position, geometry.fold.p2() + placement.position);
        if (document.pieces()[placement.piece].grainAngle)
            dxf.line("GRAINLINE", geometry.grain.p1() + placement.position, geometry.grain.p2() + placement.position);
    }
    dxf.group(0, "ENDSEC");
    dxf.group(0, "EOF");
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

}
