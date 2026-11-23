#include "PatternDocument.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace opc {
void PatternDocument::addSegment(QPointF a, QPointF b) { segments_.append({a,b}); }
void PatternDocument::clear() { segments_.clear(); }
const QVector<Segment>& PatternDocument::segments() const { return segments_; }
bool PatternDocument::save(const QString& path, QString* error) const {
    QJsonArray lines;
    for (const auto& s : segments_) {
        QJsonArray line;
        line.append(s.a.x()); line.append(s.a.y()); line.append(s.b.x()); line.append(s.b.y());
        lines.append(line);
    }
    QJsonObject root{{"format", "SOFIT"}, {"version", 1}, {"segments", lines}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if(error) *error=file.errorString(); return false; }
    if (file.write(QJsonDocument(root).toJson()) == -1 || !file.commit()) {
        if(error) *error=file.errorString(); return false;
    }
    return true;
}
bool PatternDocument::load(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if(error) *error=file.errorString(); return false; }
    QJsonParseError parseError;
    auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        if(error) *error="Invalid JSON document"; return false;
    }
    auto root=json.object();
    if((root.value("format").toString()!="SOFIT" && root.value("format").toString()!="OpenPatternCAD") || root.value("version").toInt()!=1 || !root.value("segments").isArray()) {
        if(error) *error="Unsupported document format"; return false;
    }
    QVector<Segment> parsed;
    for(const auto& value : root.value("segments").toArray()) {
        if(!value.isArray()) { if(error) *error="Invalid segment"; return false; }
        auto arr=value.toArray();
        if(arr.size()!=4) { if(error) *error="Invalid segment length"; return false; }
        for(const auto& n: arr) if(!n.isDouble() || !std::isfinite(n.toDouble())) { if(error) *error="Invalid coordinate"; return false; }
        parsed.append({{arr[0].toDouble(),arr[1].toDouble()},{arr[2].toDouble(),arr[3].toDouble()}});
    }
    segments_=std::move(parsed);
    return true;
}
}
