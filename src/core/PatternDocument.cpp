#include "PatternDocument.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace opc {
namespace {
QString shapeName(Shape shape) {
    switch (shape) {
    case Shape::Point: return QStringLiteral("point");
    case Shape::Line: return QStringLiteral("line");
    case Shape::Rectangle: return QStringLiteral("rectangle");
    case Shape::Circle: return QStringLiteral("circle");
    case Shape::Curve: return QStringLiteral("quadratic");
    }
    return {};
}

QJsonArray coordinates(const Entity& entity) {
    QJsonArray result{entity.a.x(), entity.a.y()};
    if (entity.shape != Shape::Point) {
        result.append(entity.b.x());
        result.append(entity.b.y());
    }
    if (entity.shape == Shape::Curve) {
        result.append(entity.control.x());
        result.append(entity.control.y());
    }
    return result;
}
}

bool PatternDocument::save(const QString& path, QString* error) const {
    QJsonArray objects;
    for (const auto& entity : entities_) {
        objects.append(QJsonObject{{"type", shapeName(entity.shape)},
                                   {"coordinates", coordinates(entity)},
                                   {"piece", entity.piece},
                                   {"layer", entity.layer == Layer::Main ? "main" : "construction"}});
    }
    QJsonArray pieces;
    for (const auto& piece : pieces_) {
        pieces.append(QJsonObject{{"name", piece.name}, {"allowanceMm", piece.allowanceMm}});
    }
    QJsonArray layerVisibility;
    for (bool visible : layerVisibility_)
        layerVisibility.append(visible);
    const QJsonObject root{{"format", "SOFIT"}, {"version", 4}, {"objects", objects},
                           {"pieces", pieces}, {"layerVisibility", layerVisibility}};
    const auto data = QJsonDocument(root).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool PatternDocument::load(const QString& path, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());

    const auto json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject())
        return fail(QStringLiteral("Invalid JSON document"));
    const auto root = json.object();
    const auto format = root.value("format").toString();
    const auto version = root.value("version").toDouble();
    const bool legacy = version == 1 && (format == "SOFIT" || format == "OpenPatternCAD");
    const bool current = (version == 2 || version == 3 || version == 4) && format == "SOFIT";
    const auto objects = root.value(legacy ? "segments" : "objects");
    if ((!legacy && !current) || !objects.isArray())
        return fail(QStringLiteral("Unsupported document format"));

    std::array<bool, 3> layerVisibility{true, true, true};
    QVector<Piece> pieces;
    if (version == 4) {
        const auto visibility = root.value("layerVisibility").toArray();
        if (visibility.size() != static_cast<qsizetype>(layerVisibility.size()))
            return fail(QStringLiteral("Invalid layer visibility"));
        for (qsizetype i = 0; i < visibility.size(); ++i) {
            if (!visibility[i].isBool())
                return fail(QStringLiteral("Invalid layer visibility"));
            layerVisibility[i] = visibility[i].toBool();
        }
        const auto list = root.value("pieces");
        if (!list.isArray())
            return fail(QStringLiteral("Invalid pieces list"));
        for (const auto& value : list.toArray()) {
            const auto piece = value.toObject();
            const auto name = piece.value("name").toString().trimmed();
            const auto allowance = piece.value("allowanceMm").toDouble(-1);
            if (name.isEmpty() || !std::isfinite(allowance) || allowance < 0)
                return fail(QStringLiteral("Invalid piece"));
            pieces.append({name, allowance});
        }
    }

    QVector<Entity> parsed;
    for (const auto& object : objects.toArray()) {
        const auto fields = object.toObject();
        const auto type = fields.value("type").toString();
        Shape shape = Shape::Line;
        if (!legacy) {
            if (type == "point") shape = Shape::Point;
            else if (type == "rectangle") shape = Shape::Rectangle;
            else if (version >= 3 && type == "circle") shape = Shape::Circle;
            else if (version >= 3 && type == "quadratic") shape = Shape::Curve;
            else if (type != "line") return fail(QStringLiteral("Unknown object type"));
        }

        const auto values = legacy ? object.toArray() : fields.value("coordinates").toArray();
        const int coordinateCount = shape == Shape::Point ? 2 : shape == Shape::Curve ? 6 : 4;
        if (values.size() != coordinateCount)
            return fail(QStringLiteral("Invalid object coordinates"));
        for (const auto& value : values) {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
                return fail(QStringLiteral("Invalid coordinate"));
        }
        const QPointF a(values[0].toDouble(), values[1].toDouble());
        const auto b = shape == Shape::Point ? a : QPointF(values[2].toDouble(), values[3].toDouble());
        QPointF control;
        if (shape == Shape::Curve)
            control = QPointF(values[4].toDouble(), values[5].toDouble());
        Entity entity{shape, a, b, control};
        if (version == 4) {
            const auto piece = fields.value("piece").toDouble(-2);
            const auto layer = fields.value("layer").toString();
            if (!std::isfinite(piece) || piece < -1 || piece >= pieces.size()
                || std::trunc(piece) != piece || (layer != "main" && layer != "construction"))
                return fail(QStringLiteral("Invalid object piece or layer"));
            entity.piece = static_cast<qsizetype>(piece);
            entity.layer = layer == "main" ? Layer::Main : Layer::Construction;
        }
        parsed.append(entity);
    }
    entities_ = std::move(parsed);
    pieces_ = std::move(pieces);
    layerVisibility_ = layerVisibility;
    return true;
}
}
