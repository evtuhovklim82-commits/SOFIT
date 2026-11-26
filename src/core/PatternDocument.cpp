#include "PatternDocument.h"
#include "ContourModeling.h"
#include "Production.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <utility>
#include <cmath>

namespace opc {
namespace {
constexpr int legacyEnvelopeVersion = 14;
constexpr int productEnvelopeVersion = 15;
constexpr int repeatEnvelopeVersion = 16;
constexpr int flawEnvelopeVersion = 17;
constexpr int shrinkageEnvelopeVersion = 18;
constexpr int pieceShrinkageEnvelopeVersion = 19;
constexpr int bevelEnvelopeVersion = 20;
constexpr int projectVersion = 21;
constexpr qint64 maxProjectBytes = 64 * 1024 * 1024;

QString payloadDigest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString canonicalContourName(const QString& name) {
    // SOFIT 1.8 could save the numeric UTF-16 values in generated cut edge names.
    static const QRegularExpression legacySeam(QStringLiteral("\\A(@[1-9][0-9]*:seam-)(65|66)\\z"));
    const auto match = legacySeam.match(name);
    if (!match.hasMatch())
        return name;
    return match.captured(1) + (match.captured(2) == QStringLiteral("65") ? u'A' : u'B');
}

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
    if (error)
        error->clear();
    QJsonArray objects;
    bool hasSeamData = false;
    bool hasAnchoredEdgeData = false;
    for (const auto& entity : entities_) {
        if (entity.layer != Layer::Main && entity.layer != Layer::Construction) {
            if (error) *error = QStringLiteral("Invalid object layer");
            return false;
        }
        const bool cornerOverride = entity.bevelAtStart.has_value() || entity.bevelAtEnd.has_value();
        const bool anchoredEdgeData = cornerOverride || !entity.seamSymbols.isEmpty();
        if (anchoredEdgeData && (entity.layer != Layer::Main || entity.piece < 0)) {
            if (error) *error = QStringLiteral("Anchored edge metadata requires a piece boundary");
            return false;
        }
        if ((entity.seamAllowanceMm || !entity.seamNotches.isEmpty() || anchoredEdgeData)
            && entity.shape != Shape::Line && entity.shape != Shape::Curve) {
            if (error) *error = QStringLiteral("Edge metadata requires a line or curve");
            return false;
        }
        QJsonObject object{{"type", shapeName(entity.shape)}, {"coordinates", coordinates(entity)},
                           {"piece", entity.piece}, {"layer", entity.layer == Layer::Main ? "main" : "construction"}};
        if (!entity.dxfLayer.isEmpty())
            object.insert("dxfLayer", entity.dxfLayer);
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
        if (entity.bevelAtStart)
            object.insert("bevelAtStart", *entity.bevelAtStart);
        if (entity.bevelAtEnd)
            object.insert("bevelAtEnd", *entity.bevelAtEnd);
        if (!entity.seamSymbols.isEmpty()) {
            if (entity.seamSymbols.size() > 100 || !std::ranges::all_of(entity.seamSymbols, validEdgeSymbol)) {
                if (error) *error = QStringLiteral("Invalid edge symbols");
                return false;
            }
            QJsonArray symbols;
            for (const auto& mark : entity.seamSymbols)
                symbols.append(QJsonArray{int(mark.type), mark.fraction, mark.sizeMm, mark.insetMm, mark.fromCutOutline});
            object.insert("edgeSymbols", symbols);
        }
        hasAnchoredEdgeData |= anchoredEdgeData;
        hasSeamData |= entity.seamAllowanceMm.has_value() || !entity.seamNotches.isEmpty() || anchoredEdgeData;
        objects.append(object);
    }
    QJsonArray pieces;
    bool hasPieceShrinkage = false;
    for (const auto& piece : pieces_) {
        QJsonArray notches;
        for (double position : piece.notches)
            notches.append(position);
        QJsonObject record{{"name", piece.name}, {"allowanceMm", piece.allowanceMm},
                           {"cutCount", piece.cutCount}, {"size", piece.size},
                           {"showLabel", piece.showLabel}, {"notches", notches}};
        if (piece.grainAngle)
            record.insert("grainAngle", *piece.grainAngle);
        if (piece.bevelAllowanceCorners)
            record.insert("bevelAllowanceCorners", true);
        if (piece.fabricShrinkageOverride) {
            const auto percent = *piece.fabricShrinkageOverride;
            if (!validShrinkage(percent)) {
                if (error) *error = QStringLiteral("Invalid piece fabric compensation");
                return false;
            }
            record.insert("fabricShrinkageOverride", QJsonArray{percent.x(), percent.y()});
            hasPieceShrinkage = true;
        }
        pieces.append(record);
    }
    QJsonArray layerVisibility;
    for (bool visible : layerVisibility_)
        layerVisibility.append(visible);
    const bool parametric = draft_ != ParametricDraft{};
    int version = 7;
    if (parametric)
        version = production_.useDraftMeasurements && !production_.sizes.isEmpty() ? 9 : 8;
    QJsonObject root{{"format", "SOFIT"}, {"version", hasContourHistory() ? 12 : !history_.isEmpty() ? 11 : hasSeamData ? 10 : version}, {"objects", objects},
                     {"pieces", pieces}, {"layerVisibility", layerVisibility},
                     {"production", saveProduction(production_)}};
    if (hasSeamData || !history_.isEmpty() || !seamLinks_.isEmpty())
        root.insert("baseVersion", version);
    if (!seamLinks_.isEmpty()) {
        root.insert("linkedBaseVersion", hasContourHistory() ? 12 : !history_.isEmpty() ? 11 : 10);
        root.insert("version", 13);
        const auto saveNames = [](const QVector<QString>& names) {
            QJsonArray array;
            for (const auto& name : names)
                array.append(name);
            return array;
        };
        QJsonArray links;
        for (const auto& link : seamLinks_) {
            QJsonObject record{
                {"first", saveNames(link.first)}, {"second", saveNames(link.second)},
                {"reverseFirst", link.reverseFirst}, {"reverseSecond", link.reverseSecond},
                {"ease", link.targetEaseMm}, {"tolerance", link.toleranceMm}, {"notches", link.notchPairs}
            };
            if (link.allowanceMm)
                record.insert("allowance", *link.allowanceMm);
            links.append(record);
        }
        root.insert("seamLinks", links);
    }
    if (parametric)
        root.insert("parametricDraft", saveDraft(draft_));
    if (!history_.isEmpty()) {
        QJsonArray steps;
        for (const auto& step : history_) {
            const auto names = [](const QVector<QString>& strings) {
                QJsonArray array;
                for (const auto& string : strings)
                    array.append(string);
                return array;
            };
            QJsonObject record{{"objects", names(step.objects)}};
            if (!step.rotationPivot.isEmpty()) {
                record.insert("operation", "rotate-construction");
                record.insert("pivot", step.rotationPivot);
                record.insert("degrees", step.rotationDegrees);
            } else if (step.contour) {
                record.insert("operation", contourOperationKey(*step.contour));
                record.insert("guides", names(step.guides));
                if (step.spreadFormula.isEmpty())
                    record.insert("width", step.spreadMm);
                else
                    record.insert("width", step.spreadFormula);
                if (!step.outletWeights.isEmpty()) {
                    QJsonArray weights;
                    for (const double weight : step.outletWeights)
                        weights.append(weight);
                    record.insert("outletWeights", weights);
                }
                record.insert("topology", names(step.topology));
                if (step.followingGuides)
                    record.insert("followingGuides", names(*step.followingGuides));
                record.insert("secondPiece", step.secondPiece);
            } else {
                record.insert("operation", step.lengthScale == 1 ? "silhouette" : "silhouette-length");
                if (step.lengthScale != 1)
                    record.insert("length", step.lengthScale);
                record.insert("axis", step.axisFraction);
                record.insert("top", step.topScale);
                record.insert("bottom", step.bottomScale);
            }
            steps.append(record);
        }
        root.insert("modelingHistory", steps);
    }
    if (hasContourHistory()) {
        QJsonArray sources;
        for (const auto& source : hiddenSources_) {
            sources.append(QJsonObject{{"name", source.name}, {"piece", source.piece},
                {"layer", source.layer == Layer::Main ? "main" : "construction"}});
        }
        root.insert("modelingSources", sources);
    }
    if (!libraryInstances_.isEmpty()) {
        const auto mappedNames = [](const QMap<QString, QString>& map) {
            QJsonObject object;
            for (auto it = map.cbegin(); it != map.cend(); ++it)
                object.insert(it.key(), it.value());
            return object;
        };
        QJsonArray instances;
        for (const auto& instance : libraryInstances_) {
            instances.append(QJsonObject{{"sourceName", instance.sourceName}, {"revision", instance.revision},
                {"structure", instance.sourceStructure}, {"formulas", instance.installedFormulas},
                {"measurements", mappedNames(instance.measurements)}, {"parameters", mappedNames(instance.parameters)},
                {"points", mappedNames(instance.points)}, {"edges", mappedNames(instance.edges)}});
        }
        root.insert("libraryInstances", instances);
    }
    root.insert("useDraftMeasurements", production_.useDraftMeasurements);
    const auto& info = productInformation_;
    const bool hasProductInformation = info != ProductInformation{};
    if (hasProductInformation) {
        root.insert("productInformation", QJsonObject{{"styleCode", info.styleCode}, {"title", info.title},
            {"revision", info.revision}, {"material", info.material}, {"notes", info.notes}});
    }
    if (info.usesTechPackOptions()) {
        root.insert("techPack", QJsonObject{{"reportTitle", info.reportTitle}, {"billOfMaterials", info.billOfMaterials},
            {"assemblyInstructions", info.assemblyInstructions}, {"includeCuttingQuantities", info.includeCuttingQuantities},
            {"includeSizeMeasurements", info.includeSizeMeasurements}});
    }
    if (!info.finishedMeasurements.isEmpty()) {
        QJsonArray measurements;
        for (const auto& row : info.finishedMeasurements)
            measurements.append(QJsonObject{{"label", row.label}, {"first", row.first}, {"second", row.second}});
        root.insert("finishedMeasurements", measurements);
    }
    const bool hasSeamRepeatPoints = std::ranges::any_of(production_.placements,
        [](const auto& placement) { return !placement.repeatEdge.isEmpty(); });
    const bool transportsGuides = std::ranges::any_of(history_, [](const auto& step) { return step.followingGuides.has_value(); });
    const bool hasMultipleDartOutlets = std::ranges::any_of(history_, [](const auto& step) {
        return step.contour == ContourOperation::SplitDart && step.guides.size() > 4;
    });
    const bool hasDartWeights = std::ranges::any_of(history_, [](const auto& step) {
        return !step.outletWeights.isEmpty();
    });
    const bool hasBeveledCorners = std::ranges::any_of(pieces_, &Piece::bevelAllowanceCorners);
    const bool hasPleatTabs = std::ranges::any_of(history_, [](const auto& step) {
        return step.contour == ContourOperation::PleatWithTabs;
    });
    const int envelopeVersion = hasAnchoredEdgeData || hasPleatTabs || !libraryInstances_.isEmpty() ? projectVersion
        : hasDartWeights || hasBeveledCorners ? bevelEnvelopeVersion
        : hasPieceShrinkage || hasMultipleDartOutlets ? pieceShrinkageEnvelopeVersion
        : transportsGuides || !production_.fabricShrinkagePercent.isNull() ? shrinkageEnvelopeVersion
        : hasSeamRepeatPoints || !production_.fabricFlaws.isEmpty()
        || !info.finishedMeasurements.isEmpty() ? flawEnvelopeVersion
        : info.usesTechPackOptions() || !production_.fabricRepeatMm.isNull() ? repeatEnvelopeVersion
        : hasProductInformation || production_.pieceClearanceMm != 0 ? productEnvelopeVersion : legacyEnvelopeVersion;
    const auto payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const QJsonObject envelope{{"format", "SOFIT"}, {"version", envelopeVersion},
                               {"payload", QString::fromUtf8(payload)}, {"sha256", payloadDigest(payload)}};
    const auto data = QJsonDocument(envelope).toJson();
    if (data.size() > maxProjectBytes) {
        if (error) *error = QStringLiteral("Project exceeds the 64 MiB file limit");
        return false;
    }
    // Exercise the same structural validation and modeling replay used by Open.
    PatternDocument validated;
    if (!validated.loadPayload(QJsonDocument::fromJson(payload).object(), true, error))
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool PatternDocument::load(const QString& path, QString* error) {
    if (error)
        error->clear();
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QFile file(path);
    if (!QFileInfo(file).isFile())
        return fail(QStringLiteral("Project is missing or is not a regular file"));
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    if (file.size() > maxProjectBytes)
        return fail(QStringLiteral("Project exceeds the 64 MiB file limit"));
    const auto bytes = file.read(maxProjectBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(file.errorString());
    if (bytes.size() > maxProjectBytes)
        return fail(QStringLiteral("Project exceeds the 64 MiB file limit"));

    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(QStringLiteral("Invalid project JSON at byte %1: %2")
                    .arg(parseError.offset).arg(parseError.errorString()));
    if (!json.isObject())
        return fail(QStringLiteral("Project JSON must contain an object"));
    const auto root = json.object();
    if (root.value("format") == QStringLiteral("SOFIT") && root.value("version").toDouble() > projectVersion)
        return fail(QStringLiteral("This project requires a newer version of SOFIT"));
    const auto envelopeVersion = root.value("version").toDouble();
    if (root.value("format") != QStringLiteral("SOFIT")
        || (envelopeVersion != projectVersion && envelopeVersion != bevelEnvelopeVersion
            && envelopeVersion != pieceShrinkageEnvelopeVersion
            && envelopeVersion != shrinkageEnvelopeVersion
            && envelopeVersion != flawEnvelopeVersion
            && envelopeVersion != repeatEnvelopeVersion
            && envelopeVersion != productEnvelopeVersion
            && envelopeVersion != legacyEnvelopeVersion))
        return loadPayload(root, false, error);

    const auto payload = root.value("payload").toString().toUtf8();
    if (payload.isEmpty() || root.value("sha256").toString() != payloadDigest(payload))
        return fail(QStringLiteral("Project integrity check failed (SHA-256 mismatch)"));
    const auto decoded = QJsonDocument::fromJson(payload);
    if (!decoded.isObject())
        return fail(QStringLiteral("Invalid project payload"));
    return loadPayload(decoded.object(), true, error);
}

bool PatternDocument::loadPayload(const QJsonObject& root, bool native, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    const auto format = root.value("format").toString();
    const bool linkedFormat = root.value("version").toDouble() == 13;
    const auto fileVersion = root.value(linkedFormat ? "linkedBaseVersion" : "version").toDouble();
    if (linkedFormat && fileVersion != 10 && fileVersion != 11 && fileVersion != 12)
        return fail(QStringLiteral("Invalid linked document version"));
    const bool topologyFormat = fileVersion == 12;
    const bool historyFormat = fileVersion == 11 || topologyFormat;
    const bool seamFormat = fileVersion == 10 || historyFormat;
    const auto version = root.value(seamFormat ? "baseVersion" : "version").toDouble();
    if (seamFormat && (version < (historyFormat ? 8 : 7) || version > 9))
        return fail(QStringLiteral("Invalid base document version"));
    const bool legacy = version == 1 && (format == "SOFIT" || format == "OpenPatternCAD");
    const bool current = format == "SOFIT" && version >= 2 && version <= 9 && std::trunc(version) == version;
    const auto objects = root.value(legacy ? "segments" : "objects");
    if ((native && (!current || version < 7)) || (!legacy && !current) || !objects.isArray())
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
            if (piece.contains("bevelAllowanceCorners") && !piece.value("bevelAllowanceCorners").isBool())
                return fail(QStringLiteral("Invalid allowance corner setting"));
            parsedPiece.bevelAllowanceCorners = piece.value("bevelAllowanceCorners").toBool();
            if (piece.contains("fabricShrinkageOverride")) {
                const auto axes = piece.value("fabricShrinkageOverride").toArray();
                if (axes.size() != 2 || !axes[0].isDouble() || !axes[1].isDouble())
                    return fail(QStringLiteral("Invalid piece fabric compensation"));
                const QPointF percent(axes[0].toDouble(), axes[1].toDouble());
                if (!validShrinkage(percent))
                    return fail(QStringLiteral("Invalid piece fabric compensation"));
                parsedPiece.fabricShrinkageOverride = percent;
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
        if (fields.contains("dxfLayer")) {
            const auto sourceLayer = fields.value("dxfLayer");
            if (!sourceLayer.isString() || sourceLayer.toString().contains(QRegularExpression("[\\x00-\\x1f]")))
                return fail(QStringLiteral("Invalid DXF source layer"));
            entity.dxfLayer = sourceLayer.toString();
        }
        if ((version >= 8 || linkedFormat || native) && fields.contains("draftName")) {
            entity.draftName = fields.value("draftName").toString();
            if (topologyFormat && entity.draftName.startsWith(u'@'))
                entity.draftName = canonicalContourName(entity.draftName);
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
        if (fields.contains("bevelAtStart") || fields.contains("bevelAtEnd")) {
            if (!native || !seamFormat || entity.layer != Layer::Main
                || entity.piece < 0 || (shape != Shape::Line && shape != Shape::Curve)
                || (fields.contains("bevelAtStart") && !fields.value("bevelAtStart").isBool())
                || (fields.contains("bevelAtEnd") && !fields.value("bevelAtEnd").isBool()))
                return fail(QStringLiteral("Invalid corner override"));
            if (fields.contains("bevelAtStart"))
                entity.bevelAtStart = fields.value("bevelAtStart").toBool();
            if (fields.contains("bevelAtEnd"))
                entity.bevelAtEnd = fields.value("bevelAtEnd").toBool();
        }
        if (fields.contains("edgeSymbols")) {
            const auto symbols = fields.value("edgeSymbols");
            if (!native || !seamFormat || entity.layer != Layer::Main || entity.piece < 0
                || (shape != Shape::Line && shape != Shape::Curve) || !symbols.isArray() || symbols.toArray().size() > 100)
                return fail(QStringLiteral("Invalid edge symbols"));
            for (const auto& entry : symbols.toArray()) {
                const auto values = entry.toArray();
                if (values.size() != 5 || !values[4].isBool())
                    return fail(QStringLiteral("Invalid edge symbol record"));
                const EdgeSymbol symbol{PieceSymbol(values[0].toInt(-1)), values[1].toDouble(-1),
                    values[2].toDouble(-1), values[3].toDouble(-1), values[4].toBool()};
                if (!validEdgeSymbol(symbol))
                    return fail(QStringLiteral("Invalid edge symbol position or type"));
                entity.seamSymbols.append(symbol);
            }
        }
        parsed.append(entity);
    }
    ProductionData production;
    if (version >= 6 && !loadProduction(root.value("production").toObject(), pieces.size(), production))
        return fail(QStringLiteral("Invalid grading or fabric layout data"));

    PatternDocument loaded;
    if (native && root.contains("productInformation")) {
        const auto info = root.value("productInformation").toObject();
        for (const auto* field : {"styleCode", "title", "revision", "material", "notes"}) {
            if (!info.value(QLatin1String(field)).isString())
                return fail(QStringLiteral("Invalid product information"));
        }
        loaded.productInformation_ = {info.value("styleCode").toString(), info.value("title").toString(),
            info.value("revision").toString(), info.value("material").toString(), info.value("notes").toString()};
    }
    if (native && root.contains("techPack")) {
        const auto pack = root.value("techPack").toObject();
        if (!pack.value("reportTitle").isString() || !pack.value("billOfMaterials").isString()
            || !pack.value("assemblyInstructions").isString() || !pack.value("includeCuttingQuantities").isBool()
            || !pack.value("includeSizeMeasurements").isBool())
            return fail(QStringLiteral("Invalid tech pack options"));
        auto& info = loaded.productInformation_;
        info.reportTitle = pack.value("reportTitle").toString();
        info.billOfMaterials = pack.value("billOfMaterials").toString();
        info.assemblyInstructions = pack.value("assemblyInstructions").toString();
        info.includeCuttingQuantities = pack.value("includeCuttingQuantities").toBool();
        info.includeSizeMeasurements = pack.value("includeSizeMeasurements").toBool();
    }
    if (native && root.contains("finishedMeasurements")) {
        const auto measurements = root.value("finishedMeasurements").toArray();
        if (!root.value("finishedMeasurements").isArray() || measurements.size() > 100)
            return fail(QStringLiteral("Invalid finished measurement table"));
        QSet<QString> labels;
        for (const auto& value : measurements) {
            const auto row = value.toObject();
            FinishedMeasurement item{row.value("label").toString(), row.value("first").toString(),
                                     row.value("second").toString()};
            if (item.label.trimmed().isEmpty() || labels.contains(item.label)
                || item.first.isEmpty() || item.second.isEmpty() || item.first == item.second
                || item.first.size() > 64 || item.second.size() > 64)
                return fail(QStringLiteral("Invalid finished measurement reference"));
            labels.insert(item.label);
            loaded.productInformation_.finishedMeasurements.append(std::move(item));
        }
    }
    loaded.production_ = std::move(production);
    loaded.entities_ = std::move(parsed);
    loaded.pieces_ = std::move(pieces);
    loaded.layerVisibility_ = layerVisibility;
    // Version 8 stored size measurements for reference only.
    loaded.production_.useDraftMeasurements = version != 8 || loaded.production_.sizes.isEmpty();
    if (native) {
        const auto mode = root.value("useDraftMeasurements");
        if (!mode.isBool())
            return fail(QStringLiteral("Invalid size measurement mode"));
        loaded.production_.useDraftMeasurements = mode.toBool();
    }
    if (historyFormat) {
        const auto steps = root.value("modelingHistory").toArray();
        if (steps.isEmpty())
            return fail(QStringLiteral("Missing modeling history"));
        for (const auto& value : steps) {
            const auto entry = value.toObject();
            const auto operation = entry.value("operation").toString();
            ModelingStep step;
            const auto readNames = [](const QJsonValue& value, QVector<QString>& names) {
                if (!value.isArray() || value.toArray().isEmpty())
                    return false;
                for (const auto& name : value.toArray()) {
                    if (!name.isString() || name.toString().isEmpty())
                        return false;
                    names.append(name.toString());
                }
                return true;
            };
            if (!readNames(entry.value("objects"), step.objects))
                return fail(QStringLiteral("Invalid modeling object names"));
            if (operation == "rotate-construction") {
                step.rotationPivot = entry.value("pivot").toString();
                if (step.rotationPivot.isEmpty() || !entry.value("degrees").isDouble())
                    return fail(QStringLiteral("Invalid construction rotation step"));
                step.rotationDegrees = entry.value("degrees").toDouble();
            } else if (operation == "silhouette" || operation == "silhouette-length") {
                if (operation == "silhouette-length")
                    step.lengthScale = entry.value("length").toDouble(-1);
                step.axisFraction = entry.value("axis").toDouble(-1);
                step.topScale = entry.value("top").toDouble(-1);
                step.bottomScale = entry.value("bottom").toDouble(-1);
            } else {
                if (!topologyFormat || !(step.contour = contourOperationFromKey(operation)))
                    return fail(QStringLiteral("Unknown modeling operation"));
                const auto widthValue = entry.value("width");
                const auto width = widthValue.toDouble(-1);
                const auto formula = widthValue.toString().trimmed();
                const bool validWidth = widthValue.isDouble() ? std::isfinite(width) && width >= 0
                    : !formula.isEmpty() && usesSpreadWidth(*step.contour);
                const auto panel = entry.value("secondPiece").toDouble(-2);
                if (!readNames(entry.value("guides"), step.guides)
                    || !readNames(entry.value("topology"), step.topology)
                    || !validWidth
                    || panel < -1 || panel >= loaded.pieces_.size() || std::trunc(panel) != panel)
                    return fail(QStringLiteral("Invalid contour modeling step"));
                if (entry.contains("followingGuides")) {
                    const auto following = entry.value("followingGuides");
                    if (!usesSpreadWidth(*step.contour) || !following.isArray()
                        || (!following.toArray().isEmpty() && !readNames(following, step.followingGuides.emplace())))
                        return fail(QStringLiteral("Invalid following guides"));
                    if (!step.followingGuides)
                        step.followingGuides.emplace();
                }
                if (entry.contains("outletWeights")) {
                    const auto weights = entry.value("outletWeights").toArray();
                    if (*step.contour != ContourOperation::SplitDart
                        || !validDartOutletCount(step.guides.size()) || weights.size() != step.guides.size() - 2)
                        return fail(QStringLiteral("Invalid dart outlet weights"));
                    for (const auto& weight : weights) {
                        if (!weight.isDouble() || !std::isfinite(weight.toDouble()) || weight.toDouble() <= 0)
                            return fail(QStringLiteral("Invalid dart outlet weight"));
                        step.outletWeights.append(weight.toDouble());
                    }
                }
                step.spreadMm = widthValue.isDouble() ? width : 0;
                step.spreadFormula = formula;
                step.secondPiece = static_cast<qsizetype>(panel);
            }
            if (topologyFormat) {
                step.rotationPivot = canonicalContourName(step.rotationPivot);
                for (auto& name : step.objects)
                    name = canonicalContourName(name);
                for (auto& name : step.guides)
                    name = canonicalContourName(name);
            }
            loaded.history_.append(std::move(step));
        }
    }
    if (topologyFormat) {
        if (std::ranges::none_of(loaded.history_, [](const auto& step) { return step.contour.has_value(); }))
            return fail(QStringLiteral("Missing contour modeling steps"));
        const auto sources = root.value("modelingSources").toArray();
        if (sources.isEmpty())
            return fail(QStringLiteral("Missing modeling sources"));
        QSet<QString> names = draftNames;
        for (const auto& value : sources) {
            const auto entry = value.toObject();
            const auto name = entry.value("name").toString();
            const auto piece = entry.value("piece").toDouble(-2);
            const auto layer = entry.value("layer").toString();
            if (name.isEmpty() || name.startsWith(u'@') || names.contains(name)
                || piece < -1 || piece >= loaded.pieces_.size() || std::trunc(piece) != piece
                || (layer != "main" && layer != "construction"))
                return fail(QStringLiteral("Invalid modeling source binding"));
            names.insert(name);
            loaded.hiddenSources_.append({name, static_cast<qsizetype>(piece),
                                          layer == "main" ? Layer::Main : Layer::Construction});
        }
    }
    if (version >= 8) {
        ParametricDraft draft;
        if (!loadDraft(root.value("parametricDraft").toObject(), draft))
            return fail(QStringLiteral("Invalid parametric draft"));
        QString draftError;
        for (const auto& source : loaded.hiddenSources_) {
            if (std::ranges::find(draft.edges, source.name, &DraftEdge::name) == draft.edges.end())
                return fail(QStringLiteral("Unknown modeling source binding '%1'").arg(source.name));
        }
        const auto storedObjects = loaded.entities_;
        if (!loaded.setParametricDraft(std::move(draft), &draftError))
            return fail(draftError);
        if (loaded.entities_.size() != storedObjects.size())
            return fail(QStringLiteral("Parametric object list does not match the draft"));
        if (topologyFormat) {
            QHash<QString, Entity> replayed;
            for (const auto& entity : loaded.entities_) {
                if (!entity.draftName.isEmpty())
                    replayed.insert(entity.draftName, entity);
            }
            loaded.entities_ = storedObjects;
            for (auto& stored : loaded.entities_) {
                if (stored.draftName.isEmpty())
                    continue;
                const auto generated = replayed.constFind(stored.draftName);
                if (generated == replayed.cend() || stored.shape != generated->shape
                    || stored.piece != generated->piece || stored.layer != generated->layer)
                    return fail(QStringLiteral("Parametric object list does not match the replayed draft"));
                const auto sourceLayer = stored.dxfLayer;
                stored = generated.value();
                stored.dxfLayer = sourceLayer;
                replayed.remove(stored.draftName);
            }
            if (!replayed.isEmpty())
                return fail(QStringLiteral("Missing replayed parametric objects"));
        } else {
            for (qsizetype i = 0; i < storedObjects.size(); ++i) {
                const auto& stored = storedObjects[i];
                auto& generated = loaded.entities_[i];
                if (stored.draftName != generated.draftName || stored.shape != generated.shape)
                    return fail(QStringLiteral("Parametric object list does not match the draft"));
                generated.dxfLayer = stored.dxfLayer;
            }
        }
    }
    if (linkedFormat) {
        const auto links = root.value("seamLinks").toArray();
        if (links.isEmpty())
            return fail(QStringLiteral("Missing seam links"));
        const auto readNames = [](const QJsonValue& value, QVector<QString>& result) {
            for (const auto& item : value.toArray()) {
                if (!item.isString() || item.toString().isEmpty())
                    return false;
                result.append(item.toString());
            }
            return !result.isEmpty();
        };
        for (const auto& value : links) {
            const auto record = value.toObject();
            SeamLink link;
            const auto count = record.value("notches").toDouble(-1);
            if (!readNames(record.value("first"), link.first) || !readNames(record.value("second"), link.second)
                || !record.value("reverseFirst").isBool() || !record.value("reverseSecond").isBool()
                || !record.value("ease").isDouble() || !record.value("tolerance").isDouble()
                || count < 0 || count > 20 || std::trunc(count) != count)
                return fail(QStringLiteral("Invalid seam link"));
            link.reverseFirst = record.value("reverseFirst").toBool();
            link.reverseSecond = record.value("reverseSecond").toBool();
            link.targetEaseMm = record.value("ease").toDouble();
            link.toleranceMm = record.value("tolerance").toDouble();
            link.notchPairs = static_cast<int>(count);
            if (record.contains("allowance")) {
                if (!record.value("allowance").isDouble())
                    return fail(QStringLiteral("Invalid shared seam allowance"));
                link.allowanceMm = record.value("allowance").toDouble();
            }
            if (!loaded.addSeamLink(std::move(link), error))
                return false;
        }
    }
    if (root.contains("libraryInstances")) {
        const auto instances = root.value("libraryInstances");
        if (!native || version < 8 || !instances.isArray())
            return fail(QStringLiteral("Invalid library instance list"));
        const auto readBindings = [](const QJsonValue& value, QMap<QString, QString>& bindings) {
            static const QRegularExpression identifier(QStringLiteral("\\A[A-Za-z_][A-Za-z_0-9]{0,63}\\z"));
            if (!value.isObject())
                return false;
            QSet<QString> destinations;
            const auto object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                const auto target = it.value().toString();
                if (!identifier.match(it.key()).hasMatch() || !identifier.match(target).hasMatch()
                    || destinations.contains(target))
                    return false;
                destinations.insert(target);
                bindings.insert(it.key(), target);
            }
            return true;
        };
        static const QRegularExpression digestPattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
        for (const auto& value : instances.toArray()) {
            const auto record = value.toObject();
            LibraryInstance instance;
            instance.sourceName = record.value("sourceName").toString();
            instance.revision = record.value("revision").toString();
            instance.sourceStructure = record.value("structure").toString();
            instance.installedFormulas = record.value("formulas").toString();
            if (instance.sourceName.trimmed().isEmpty() || instance.sourceName.size() > 255
                || !digestPattern.match(instance.revision).hasMatch()
                || !digestPattern.match(instance.sourceStructure).hasMatch()
                || !digestPattern.match(instance.installedFormulas).hasMatch()
                || !readBindings(record.value("measurements"), instance.measurements)
                || !readBindings(record.value("parameters"), instance.parameters)
                || !readBindings(record.value("points"), instance.points) || instance.points.isEmpty()
                || !readBindings(record.value("edges"), instance.edges))
                return fail(QStringLiteral("Invalid library instance record"));
            loaded.libraryInstances_.append(std::move(instance));
        }
    }
    *this = std::move(loaded);
    return true;
}
}
