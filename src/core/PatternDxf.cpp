#include "PatternDxf.h"
#include "PieceMarkings.h"

#include <QLineF>
#include <QSaveFile>
#include <QTextStream>

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

    void entity(const char* type, const char* layer, const char* subclass) {
        group(0, type);
        group(100, "AcDbEntity");
        group(8, layer);
        group(100, subclass);
    }

    void polyline(const char* layer, const QPolygonF& vertices, bool closed) {
        if (vertices.size() < 2)
            return;
        entity("LWPOLYLINE", layer, "AcDbPolyline");
        group(90, vertices.size());
        group(70, closed ? 1 : 0);
        for (const auto& vertex : vertices)
            point(10, vertex, true);
    }

    void line(const char* layer, QPointF start, QPointF end) {
        entity("LINE", layer, "AcDbLine");
        point(10, start);
        point(11, end);
    }

    void lines(const char* layer, const QPainterPath& path) {
        for (qsizetype i = 1; i < path.elementCount(); ++i) {
            if (path.elementAt(i).isLineTo())
                line(layer, path.elementAt(i - 1), path.elementAt(i));
        }
    }

private:
    QTextStream& stream_;
};

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
    dxf.group(70, 7);
    for (const char* layer : {"0", "MAIN", "CONSTRUCTION", "ALLOWANCE", "NOTCH", "GRAINLINE", "LABEL"}) {
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

    dxf.section("ENTITIES");
    QVector<bool> visiblePieces(geometry.size(), false);
    for (qsizetype i = 0; i < visibility.size(); ++i) {
        const auto& item = document.entities()[i];
        if (!visibility[i] || !document.layerVisible(item.layer))
            continue;
        if (item.piece >= 0 && item.layer == Layer::Main)
            visiblePieces[item.piece] = true;
        const char* layer = item.layer == Layer::Main ? "MAIN" : "CONSTRUCTION";
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
}
