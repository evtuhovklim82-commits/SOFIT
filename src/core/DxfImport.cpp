#include "DxfImport.h"

#include <QFile>
#include <QStringList>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <map>
#include <tuple>

namespace opc {
namespace {
struct Group {
    int code;
    QString value;
};
using Groups = QList<Group>;

double number(const QString& text) {
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok && std::isfinite(value) && std::abs(value) <= 1e9 ? value : NAN;
}

double value(const Groups& groups, int code, double fallback = NAN) {
    for (const auto& group : groups)
        if (group.code == code)
            return number(group.value);
    return fallback;
}

QString layer(const Groups& groups) {
    static const QRegularExpression escape(QStringLiteral(R"(\\U\+([a-fA-F0-9]{4}))"));
    static const QRegularExpression control(QStringLiteral(R"([\x00-\x1f])"));
    for (const auto& group : groups) {
        if (group.code != 8 || group.value.isEmpty())
            continue;
        QString decoded;
        qsizetype offset = 0;
        auto matches = escape.globalMatch(group.value);
        while (matches.hasNext()) {
            const auto match = matches.next();
            decoded += group.value.mid(offset, match.capturedStart() - offset);
            decoded += QChar(match.captured(1).toUShort(nullptr, 16));
            offset = match.capturedEnd();
        }
        decoded += group.value.mid(offset);
        return decoded.replace(control, QStringLiteral("_"));
    }
    return QStringLiteral("0");
}

bool planar(const Groups& groups) {
    for (const auto& group : groups) {
        if (((group.code >= 30 && group.code <= 38) || group.code == 39 ||
             group.code == 210 || group.code == 220) && number(group.value) != 0)
            return false;
        if (group.code == 230 && number(group.value) != 1)
            return false;
        if (group.code == 410 && group.value.compare(QStringLiteral("Model"), Qt::CaseInsensitive) != 0)
            return false;
    }
    return value(groups, 67, 0) == 0;
}

bool valid(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) &&
           std::abs(p.x()) <= 1e9 && std::abs(p.y()) <= 1e9;
}

int flags(const Groups& groups) {
    const double raw = value(groups, 70, 0);
    return raw >= 0 && raw <= 65535 && std::floor(raw) == raw ? static_cast<int>(raw) : -1;
}

struct Vertex {
    QPointF point;
    double bulge = 0;
};

Entity line(QPointF start, QPointF end) {
    return {Shape::Line, start, end};
}

constexpr double pi = std::numbers::pi_v<double>;

// Exact endpoints and quadratic spans of at most 15 degrees.
bool arc(DxfPath& path, QPointF center, QPointF start, QPointF end, double sweep) {
    if (!valid(center) || !valid(start) || !valid(end) || !std::isfinite(sweep) || sweep == 0)
        return false;
    const int spans = static_cast<int>(std::ceil(std::abs(sweep) / (pi / 12)));
    const QPointF radial = start - center;
    const auto rotated = [radial](double angle) {
        return QPointF(radial.x() * std::cos(angle) - radial.y() * std::sin(angle),
                       radial.x() * std::sin(angle) + radial.y() * std::cos(angle));
    };
    const double step = sweep / spans;
    QPointF previous = start;
    for (int i = 1; i <= spans; ++i) {
        const QPointF next = i == spans ? end : center + rotated(step * i);
        const QPointF control = center + rotated(step * (i - 0.5)) / std::cos(step * 0.5);
        if (!valid(next) || !valid(control) || next == previous)
            return false;
        path.entities.append(Entity{Shape::Curve, previous, next, control});
        previous = next;
    }
    return true;
}

bool polyline(DxfPath& path, const QVector<Vertex>& vertices, bool closed, bool& approximate) {
    if (vertices.size() < 2)
        return false;
    for (const auto& vertex : vertices)
        if (!valid(vertex.point) || !std::isfinite(vertex.bulge))
            return false;
    const auto edges = vertices.size() - (closed ? 0 : 1);
    for (qsizetype i = 0; i < edges; ++i) {
        if (path.entities.size() > 20000)
            return false;
        const auto& [start, bulge] = vertices[i];
        const auto end = vertices[(i + 1) % vertices.size()].point;
        if (start == end) {
            if (bulge != 0)
                return false;
            continue;
        }
        if (std::abs(bulge) < 1e-10) {
            path.entities.append(line(start, end));
            continue;
        }
        approximate = true;
        const QPointF delta = end - start;
        const QPointF center = (start + end) / 2 +
            QPointF(-delta.y(), delta.x()) * ((1 / bulge - bulge) / 4);
        if (!arc(path, center, start, end, 4 * std::atan(bulge)))
            return false;
    }
    path.closed = closed || vertices.front().point == vertices.back().point;
    return !path.entities.isEmpty();
}

bool decode(const QString& type, const Groups& groups, const QVector<Groups>& legacy,
            DxfPath& path, bool& approximate) {
    if (!planar(groups))
        return false;
    const QPointF start(value(groups, 10), value(groups, 20));
    if (type == "LINE" || type == "POINT") {
        const QPointF end = type == "POINT" ? start : QPointF(value(groups, 11), value(groups, 21));
        if (!valid(start) || !valid(end) || (type == "LINE" && start == end))
            return false;
        path.entities.append(Entity{type == "POINT" ? Shape::Point : Shape::Line, start, end});
        return true;
    }
    if (type == "ARC" || type == "CIRCLE") {
        const double radius = value(groups, 40);
        const double first = value(groups, 50);
        const double last = value(groups, 51);
        if (!valid(start) || !(radius > 0) ||
            !valid(start + QPointF(radius, radius)) || !valid(start - QPointF(radius, radius)))
            return false;
        if (type == "CIRCLE") {
            path.entities.append(Entity{Shape::Circle, start, start + QPointF(radius, 0)});
            path.closed = true;
            return true;
        }
        if (!std::isfinite(first) || !std::isfinite(last))
            return false;
        double sweep = std::fmod(last - first, 360);
        if (sweep < 0)
            sweep += 360;
        const auto pointAt = [start, radius](double degrees) {
            const double angle = std::fmod(degrees, 360) * pi / 180;
            return start + radius * QPointF(std::cos(angle), std::sin(angle));
        };
        approximate = true;
        return arc(path, start, pointAt(first), pointAt(last), sweep * pi / 180);
    }
    if (type == "POLYLINE" || type == "LWPOLYLINE") {
        const int bits = flags(groups);
        if (bits < 0 || (bits & ~(1 | 128)) || value(groups, 43, 0) != 0)
            return false;
        QVector<Vertex> vertices;
        if (type == "POLYLINE") {
            for (const auto& vertex : legacy) {
                if (!planar(vertex) || flags(vertex) != 0 ||
                    value(vertex, 40, 0) != 0 || value(vertex, 41, 0) != 0)
                    return false;
                vertices.append(Vertex{{value(vertex, 10), value(vertex, 20)}, value(vertex, 42, 0)});
            }
            if (value(groups, 40, 0) != 0 || value(groups, 41, 0) != 0)
                return false;
        } else {
            for (const auto& group : groups) {
                if (group.code == 10)
                    vertices.append(Vertex{{number(group.value), NAN}, 0});
                if (group.code == 20 || group.code == 42) {
                    if (vertices.isEmpty())
                        return false;
                    if (group.code == 20)
                        vertices.last().point.setY(number(group.value));
                    else
                        vertices.last().bulge = number(group.value);
                }
                if ((group.code == 40 || group.code == 41) && number(group.value) != 0)
                    return false;
            }
            if (value(groups, 90) != vertices.size())
                return false;
        }
        return polyline(path, vertices, bits & 1, approximate);
    }
    if (type == "SPLINE") {
        const int bits = flags(groups);
        if ((bits != 0 && bits != 8) || value(groups, 71) != 2 ||
            value(groups, 72) != 6 || value(groups, 73) != 3 || value(groups, 74, 0) != 0)
            return false;
        QVector<double> knots;
        QVector<QPointF> points;
        for (const auto& group : groups) {
            if (group.code == 40)
                knots.append(number(group.value));
            if (group.code == 41 && number(group.value) != 1)
                return false;
            if (group.code == 10)
                points.append(QPointF(number(group.value), NAN));
            if (group.code == 20) {
                if (points.isEmpty())
                    return false;
                points.last().setY(number(group.value));
            }
        }
        if (knots.size() != 6 || !(knots[0] < knots[5]) ||
            knots[0] != knots[1] || knots[1] != knots[2] ||
            knots[3] != knots[4] || knots[4] != knots[5] || points.size() != 3 ||
            !std::all_of(points.begin(), points.end(), valid))
            return false;
        path.entities.append(Entity{Shape::Curve, points[0], points[2], points[1]});
        return true;
    }
    return false;
}

void identifyContours(DxfDrawing& drawing) {
    using Node = std::tuple<QString, double, double>;
    using Links = QVector<qsizetype>;
    std::map<Node, Links> neighbors;
    QVector<std::pair<const Links*, const Links*>> connections(drawing.paths.size());
    const QStringList auxiliary{"CONSTRUCTION", "ALLOWANCE", "NOTCH", "GRAINLINE",
                                "LABEL", "FABRIC", "FOLD"};
    for (qsizetype i = 0; i < drawing.paths.size(); ++i) {
        auto& path = drawing.paths[i];
        if (auxiliary.contains(path.layer.toUpper()) ||
            path.entities.front().shape == Shape::Point || path.entities.front().shape == Shape::Circle)
            continue;
        if (path.closed) {
            path.contour = i;
            continue;
        }
        const auto node = [&path](QPointF endpoint) {
            return Node{path.layer, endpoint.x(), endpoint.y()};
        };
        auto& start = neighbors[node(path.entities.front().a)];
        auto& end = neighbors[node(path.entities.back().b)];
        start.append(i);
        end.append(i);
        connections[i] = {&start, &end};
    }

    QVector<bool> visited(drawing.paths.size(), false);
    for (qsizetype i = 0; i < drawing.paths.size(); ++i) {
        if (visited[i] || !connections[i].first)
            continue;
        QVector<qsizetype> component{i};
        visited[i] = true;
        bool ring = true;
        for (qsizetype j = 0; j < component.size(); ++j) {
            const auto& [start, end] = connections[component[j]];
            for (const auto* links : {start, end}) {
                ring &= links->size() == 2;
                // Traverse each junction once via its first incident path.
                if (links->front() != component[j]) {
                    const auto index = links->front();
                    if (!visited[index]) {
                        visited[index] = true;
                        component.append(index);
                    }
                    continue;
                }
                for (const auto index : *links) {
                    if (!visited[index]) {
                        visited[index] = true;
                        component.append(index);
                    }
                }
            }
        }
        if (ring && component.size() > 1) {
            for (const auto index : component)
                drawing.paths[index].contour = i;
        }
    }
}
}

std::optional<double> dxfUnitMillimeters(int units) {
    switch (units) {
    case 1: return 25.4;
    case 2: return 304.8;
    case 4: return 1;
    case 5: return 10;
    case 6: return 1000;
    default: return std::nullopt;
    }
}

std::optional<DxfDrawing> readDxf(const QString& path, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return std::nullopt;
    }
    const auto fail = [&error](const QString& message) -> std::optional<DxfDrawing> {
        error = message;
        return std::nullopt;
    };
    if (file.size() > 32 * 1024 * 1024)
        return fail(QStringLiteral("DXF exceeds the 32 MiB import limit."));
    if (file.peek(22).startsWith("AutoCAD Binary DXF"))
        return fail(QStringLiteral("Save the drawing as text (ASCII) DXF."));
    QTextStream input(&file);
    Groups groups;
    while (!input.atEnd()) {
        bool ok = false;
        const int code = input.readLine().trimmed().toInt(&ok);
        const QString raw = input.readLine();
        if (!ok || code < 0 || code > 1071 || raw.isNull() || raw.contains(QChar::Null))
            return fail(QStringLiteral("Invalid DXF group at line %1.").arg(groups.size() * 2 + 1));
        groups.append({code, raw.trimmed()});
        if (groups.size() > 500000)
            return fail(QStringLiteral("DXF exceeds the 500,000 group import limit."));
    }
    if (input.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
        return fail(QStringLiteral("Could not read the entire DXF."));
    DxfDrawing result;
    QString section;
    bool sawEntities = false;
    bool sawEof = false;
    qsizetype entityCount = 0;
    for (qsizetype i = 0; i < groups.size();) {
        const auto& group = groups[i++];
        if (group.code == 999)
            continue;
        if (group.code == 0 && group.value == "SECTION") {
            if (!section.isEmpty() || i == groups.size() || groups[i].code != 2 || groups[i].value.isEmpty())
                return fail(QStringLiteral("Invalid DXF section."));
            section = groups[i++].value;
            sawEntities |= section == "ENTITIES";
        } else if (group.code == 0 && group.value == "ENDSEC") {
            if (section.isEmpty())
                return fail(QStringLiteral("Unexpected DXF ENDSEC."));
            section.clear();
        } else if (group.code == 0 && group.value == "EOF") {
            if (!section.isEmpty() || i != groups.size())
                return fail(QStringLiteral("Unexpected DXF EOF."));
            sawEof = true;
        } else if (section == "HEADER" && group.code == 9 && group.value == "$INSUNITS" &&
                   i < groups.size() && groups[i].code == 70) {
            const double units = number(groups[i++].value);
            if (units >= 0 && units <= 65535 && std::floor(units) == units)
                result.insertionUnits = static_cast<int>(units);
        } else if (section == "ENTITIES" && group.code == 0) {
            const auto fields = [&]() {
                Groups values;
                while (i < groups.size() && groups[i].code != 0)
                    values.append(groups[i++]);
                return values;
            };
            const auto body = fields();
            QVector<Groups> vertices;
            if (group.value == "POLYLINE") {
                while (i < groups.size() && groups[i].code == 0 && groups[i].value == "VERTEX") {
                    ++i;
                    vertices.append(fields());
                }
                if (i == groups.size() || groups[i].code != 0 || groups[i].value != "SEQEND")
                    return fail(QStringLiteral("POLYLINE is missing SEQEND."));
                ++i;
                fields();
            }
            DxfPath decoded{layer(body), {}, false};
            bool approximate = false;
            if (decode(group.value, body, vertices, decoded, approximate)) {
                result.approximated += approximate;
                entityCount += decoded.entities.size();
                if (entityCount > 20000)
                    return fail(QStringLiteral("DXF expands to more than 20,000 editable objects."));
                result.paths.append(std::move(decoded));
            } else {
                ++result.skipped[group.value];
            }
        } else if (section.isEmpty() || section == "ENTITIES") {
            return fail(QStringLiteral("Unexpected group outside a DXF entity or section."));
        }
    }
    if (!sawEntities || !sawEof)
        return fail(QStringLiteral("DXF requires an ENTITIES section and EOF."));
    identifyContours(result);
    error.clear();
    return result;
}

PatternDocument DxfDrawing::appendTo(PatternDocument document, const QSet<QString>& layers,
                                     double millimetersPerUnit) const {
    if (!std::isfinite(millimetersPerUnit) || millimetersPerUnit <= 0)
        return document;
    const auto transform = [millimetersPerUnit](QPointF p) {
        return QPointF(p.x(), -p.y()) * millimetersPerUnit;
    };
    for (const auto& path : paths) {
        if (!layers.contains(path.layer))
            continue;
        for (const auto& entity : path.entities) {
            if (!valid(transform(entity.a)) || !valid(transform(entity.b)) || !valid(transform(entity.control)))
                return document;
            if (entity.shape == Shape::Circle) {
                const auto radius = (entity.b.x() - entity.a.x()) * millimetersPerUnit;
                if (!valid(transform(entity.a) + QPointF(radius, radius)) ||
                    !valid(transform(entity.a) - QPointF(radius, radius)))
                    return document;
            }
        }
    }
    QHash<qsizetype, qsizetype> pieces;
    for (const auto& path : paths) {
        if (!layers.contains(path.layer))
            continue;
        if (path.contour >= 0 && !pieces.contains(path.contour)) {
            const auto index = document.pieces().size();
            pieces.insert(path.contour, index);
            document.pieces().append(Piece{path.layer + QStringLiteral(" · %1").arg(index + 1)});
        }
        const auto piece = pieces.value(path.contour, -1);
        for (auto entity : path.entities) {
            entity.a = transform(entity.a);
            entity.b = transform(entity.b);
            entity.control = transform(entity.control);
            entity.piece = piece;
            entity.dxfLayer = path.layer;
            if (path.layer.compare(QStringLiteral("CONSTRUCTION"), Qt::CaseInsensitive) == 0)
                entity.layer = Layer::Construction;
            document.insert(document.entities().size(), entity);
        }
    }
    return document;
}
}
