#pragma once

#include <QString>
#include <QVector>

namespace opc {
struct Measurement {
    QString name;
    double mm;
    bool operator==(const Measurement&) const = default;
};

struct DraftParameter {
    QString name, formula;
    bool operator==(const DraftParameter&) const = default;
};

struct DraftPoint {
    QString name, x, y;
    bool operator==(const DraftPoint&) const = default;
};

struct DraftEdge {
    QString name, start, end, control;
    bool operator==(const DraftEdge&) const = default;
};

struct ParametricDraft {
    QVector<Measurement> measurements;
    QVector<DraftParameter> parameters;
    QVector<DraftPoint> points;
    QVector<DraftEdge> edges;
    bool operator==(const ParametricDraft&) const = default;
};
}
