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
    }
    return {};
}

QJsonArray coordinates(const Entity& entity) {
    QJsonArray result{entity.a.x(), entity.a.y()};
    if (entity.shape != Shape::Point) {
        result.append(entity.b.x());
        result.append(entity.b.y());
    }
    return result;
}
}

bool PatternDocument::save(const QString& path, QString* error) const {
    QJsonArray objects;
    for (const auto& entity : entities_)
        objects.append(QJsonObject{{"type", shapeName(entity.shape)},
                                   {"coordinates", coordinates(entity)}});
    const QJsonObject root{{"format", "SOFIT"}, {"version", 2}, {"objects", objects}};
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
    const bool current = version == 2 && format == "SOFIT";
    const auto objects = root.value(legacy ? "segments" : "objects");
    if ((!legacy && !current) || !objects.isArray())
        return fail(QStringLiteral("Unsupported document format"));

    QVector<Entity> parsed;
    for (const auto& object : objects.toArray()) {
        const auto fields = object.toObject();
        const auto type = fields.value("type").toString();
        Shape shape = Shape::Line;
        if (!legacy) {
            if (type == "point") shape = Shape::Point;
            else if (type == "rectangle") shape = Shape::Rectangle;
            else if (type != "line") return fail(QStringLiteral("Unknown object type"));
        }

        const auto values = legacy ? object.toArray() : fields.value("coordinates").toArray();
        if (values.size() != (shape == Shape::Point ? 2 : 4))
            return fail(QStringLiteral("Invalid object coordinates"));
        for (const auto& value : values) {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
                return fail(QStringLiteral("Invalid coordinate"));
        }
        const QPointF a(values[0].toDouble(), values[1].toDouble());
        const auto b = shape == Shape::Point ? a : QPointF(values[2].toDouble(), values[3].toDouble());
        parsed.append({shape, a, b});
    }
    entities_ = std::move(parsed);
    return true;
}
}
