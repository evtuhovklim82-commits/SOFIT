#include "PatternDocument.h"
#include "Production.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <utility>
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
QJsonObject saveDraft(const ParametricDraft& draft) {
    QJsonArray measurements, parameters, points, edges;
    for (const auto& entry : draft.measurements)
        measurements.append(QJsonObject{{"name", entry.name}, {"mm", entry.mm}});
    for (const auto& entry : draft.parameters)
        parameters.append(QJsonObject{{"name", entry.name}, {"formula", entry.formula}});
    for (const auto& entry : draft.points)
        points.append(QJsonObject{{"name", entry.name}, {"x", entry.x}, {"y", entry.y}});
    for (const auto& entry : draft.edges)
        edges.append(QJsonObject{{"name", entry.name}, {"start", entry.start}, {"end", entry.end},
                                 {"control", entry.control}});
    return {{"measurements", measurements}, {"parameters", parameters}, {"points", points}, {"edges", edges}};
}

bool loadDraft(const QJsonObject& data, ParametricDraft& draft) {
    const auto measurements = data.value("measurements");
    const auto parameters = data.value("parameters");
    const auto points = data.value("points");
    const auto edges = data.value("edges");
    if (!measurements.isArray() || !parameters.isArray() || !points.isArray() || !edges.isArray())
        return false;
    for (const auto& value : measurements.toArray()) {
        const auto row = value.toObject();
        if (!row.value("name").isString() || !row.value("mm").isDouble())
            return false;
        draft.measurements.append({row.value("name").toString(), row.value("mm").toDouble()});
    }
    for (const auto& value : parameters.toArray()) {
        const auto row = value.toObject();
        if (!row.value("name").isString() || !row.value("formula").isString())
            return false;
        draft.parameters.append({row.value("name").toString(), row.value("formula").toString()});
    }
    for (const auto& value : points.toArray()) {
        const auto row = value.toObject();
        if (!row.value("name").isString() || !row.value("x").isString() || !row.value("y").isString())
            return false;
        draft.points.append({row.value("name").toString(), row.value("x").toString(), row.value("y").toString()});
    }
    for (const auto& value : edges.toArray()) {
        const auto row = value.toObject();
        if (!row.value("name").isString() || !row.value("start").isString()
            || !row.value("end").isString() || !row.value("control").isString())
            return false;
        draft.edges.append({row.value("name").toString(), row.value("start").toString(),
                            row.value("end").toString(), row.value("control").toString()});
    }
    return true;
}

}

bool PatternDocument::save(const QString& path, QString* error) const {
    if (!validateParametricSizes(production_, error))
        return false;
    QJsonArray objects;
    bool hasSeamData = false;
    for (const auto& entity : entities_) {
        if ((entity.seamAllowanceMm || !entity.seamNotches.isEmpty())
            && entity.shape != Shape::Line && entity.shape != Shape::Curve) {
            if (error) *error = QStringLiteral("Edge metadata requires a line or curve");
            return false;
        }
        QJsonObject object{{"type", shapeName(entity.shape)}, {"coordinates", coordinates(entity)},
                           {"piece", entity.piece}, {"layer", entity.layer == Layer::Main ? "main" : "construction"}};
        if (!entity.draftName.isEmpty())
            object.insert("draftName", entity.draftName);
        if (entity.seamAllowanceMm) {
            if (!std::isfinite(*entity.seamAllowanceMm) || *entity.seamAllowanceMm < 0) {
                if (error) *error = QStringLiteral("Invalid edge allowance");
                return false;
            }
            object.insert("edgeAllowanceMm", *entity.seamAllowanceMm);
        }
        if (!entity.seamNotches.isEmpty()) {
            QJsonArray positions;
            for (const auto position : entity.seamNotches) {
                if (!(position > 0 && position < 1) || entity.seamNotches.size() > 100) {
                    if (error) *error = QStringLiteral("Invalid edge notch position");
                    return false;
                }
                positions.append(position);
            }
            object.insert("edgeNotches", positions);
        }
        hasSeamData |= entity.seamAllowanceMm.has_value() || !entity.seamNotches.isEmpty();
        objects.append(object);
    }
    QJsonArray pieces;
    for (const auto& piece : pieces_) {
        QJsonArray notches;
        for (double position : piece.notches)
            notches.append(position);
        QJsonObject record{{"name", piece.name}, {"allowanceMm", piece.allowanceMm},
                           {"cutCount", piece.cutCount}, {"size", piece.size},
                           {"showLabel", piece.showLabel}, {"notches", notches}};
        if (piece.grainAngle)
            record.insert("grainAngle", *piece.grainAngle);
        pieces.append(record);
    }
    QJsonArray layerVisibility;
    for (bool visible : layerVisibility_)
        layerVisibility.append(visible);
    const bool parametric = draft_ != ParametricDraft{};
    int version = 7;
    if (parametric)
        version = production_.useDraftMeasurements && !production_.sizes.isEmpty() ? 9 : 8;
    QJsonObject root{{"format", "SOFIT"}, {"version", hasSeamData ? 10 : version}, {"objects", objects},
                     {"pieces", pieces}, {"layerVisibility", layerVisibility},
                     {"production", saveProduction(production_)}};
    if (hasSeamData)
        root.insert("baseVersion", version);
    if (parametric)
        root.insert("parametricDraft", saveDraft(draft_));
    ProductionData validated;
    if (!loadProduction(root.value("production").toObject(), pieces_.size(), validated)) {
        if (error) *error = QStringLiteral("Invalid grading or fabric layout data");
        return false;
    }
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
    const bool seamFormat = root.value("version").toDouble() == 10;
    const auto version = root.value(seamFormat ? "baseVersion" : "version").toDouble();
    if (seamFormat && (version < 7 || version > 9))
        return fail(QStringLiteral("Invalid base document version"));
    const bool legacy = version == 1 && (format == "SOFIT" || format == "OpenPatternCAD");
    const bool current = format == "SOFIT" && version >= 2 && version <= 9 && std::trunc(version) == version;
    const auto objects = root.value(legacy ? "segments" : "objects");
    if ((!legacy && !current) || !objects.isArray())
        return fail(QStringLiteral("Unsupported document format"));

    std::array<bool, 3> layerVisibility{true, true, true};
    QVector<Piece> pieces;
    if (version >= 4) {
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
            Piece parsedPiece{name, allowance};
            if (version >= 5) {
                const auto count = piece.value("cutCount").toInt();
                const auto positions = piece.value("notches");
                if (count < 1 || count > 9999 || !piece.value("size").isString()
                    || !piece.value("showLabel").isBool() || !positions.isArray()
                    || positions.toArray().size() > 100)
                    return fail(QStringLiteral("Invalid piece markings"));
                parsedPiece.cutCount = count;
                parsedPiece.size = piece.value("size").toString();
                parsedPiece.showLabel = piece.value("showLabel").toBool();
                for (const auto& position : positions.toArray()) {
                    const double percent = position.toDouble(-1);
                    if (!(percent >= 0 && percent < 100))
                        return fail(QStringLiteral("Invalid notch position"));
                    parsedPiece.notches.append(percent);
                }
                if (piece.contains("grainAngle")) {
                    const double angle = piece.value("grainAngle").toDouble(-1);
                    if (!(angle >= 0 && angle < 360))
                        return fail(QStringLiteral("Invalid grainline angle"));
                    parsedPiece.grainAngle = angle;
                }
            }
            pieces.append(std::move(parsedPiece));
        }
    }

    QVector<Entity> parsed;
    QSet<QString> draftNames;
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
        if (version >= 4) {
            const auto piece = fields.value("piece").toDouble(-2);
            const auto layer = fields.value("layer").toString();
            if (!std::isfinite(piece) || piece < -1 || piece >= pieces.size()
                || std::trunc(piece) != piece || (layer != "main" && layer != "construction"))
                return fail(QStringLiteral("Invalid object piece or layer"));
            entity.piece = static_cast<qsizetype>(piece);
            entity.layer = layer == "main" ? Layer::Main : Layer::Construction;
        }
        if (version >= 8 && fields.contains("draftName")) {
            entity.draftName = fields.value("draftName").toString();
            if (entity.draftName.isEmpty() || draftNames.contains(entity.draftName))
                return fail(QStringLiteral("Invalid or duplicate parametric object name"));
            draftNames.insert(entity.draftName);
        }
        if (seamFormat && (fields.contains("edgeAllowanceMm") || fields.contains("edgeNotches"))) {
            if (shape != Shape::Line && shape != Shape::Curve)
                return fail(QStringLiteral("Edge metadata requires a line or curve"));
            if (fields.contains("edgeAllowanceMm")) {
                const auto width = fields.value("edgeAllowanceMm").toDouble(-1);
                if (!std::isfinite(width) || width < 0)
                    return fail(QStringLiteral("Invalid edge allowance"));
                entity.seamAllowanceMm = width;
            }
            if (fields.contains("edgeNotches")) {
                const auto positions = fields.value("edgeNotches");
                if (!positions.isArray() || positions.toArray().size() > 100)
                    return fail(QStringLiteral("Invalid edge notches"));
                for (const auto& value : positions.toArray()) {
                    const auto position = value.toDouble(-1);
                    if (!(position > 0 && position < 1))
                        return fail(QStringLiteral("Invalid edge notch position"));
                    entity.seamNotches.append(position);
                }
            }
        }
        parsed.append(entity);
    }
    ProductionData production;
    if (version >= 6 && !loadProduction(root.value("production").toObject(), pieces.size(), production))
        return fail(QStringLiteral("Invalid grading or fabric layout data"));

    PatternDocument loaded;
    loaded.production_ = std::move(production);
    loaded.entities_ = std::move(parsed);
    loaded.pieces_ = std::move(pieces);
    loaded.layerVisibility_ = layerVisibility;
    // Version 8 stored size measurements for reference only.
    loaded.production_.useDraftMeasurements = version != 8 || loaded.production_.sizes.isEmpty();
    if (version >= 8) {
        ParametricDraft draft;
        if (!loadDraft(root.value("parametricDraft").toObject(), draft))
            return fail(QStringLiteral("Invalid parametric draft"));
        QString draftError;
        const auto storedObjects = loaded.entities_;
        if (!loaded.setParametricDraft(std::move(draft), &draftError))
            return fail(draftError);
        if (loaded.entities_.size() != storedObjects.size())
            return fail(QStringLiteral("Parametric object list does not match the draft"));
        for (qsizetype i = 0; i < storedObjects.size(); ++i) {
            const auto& stored = storedObjects[i];
            const auto& generated = loaded.entities_[i];
            if (stored.draftName != generated.draftName || stored.shape != generated.shape)
                return fail(QStringLiteral("Parametric object list does not match the draft"));
        }
    }
    *this = std::move(loaded);
    return true;
}
}
