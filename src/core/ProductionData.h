#pragma once

#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>

namespace opc {

struct PatternSize {
    QString name;
    QMap<QString, double> measurements;
};

struct GradeRule {
    QString size;
    qsizetype piece;
    QPointF point;
    QPointF offset;
};

struct FabricPlacement {
    qsizetype piece;
    QString size;
    QPointF position;
    bool reversed = false;
};

struct ProductionData {
    QVector<PatternSize> sizes;
    QVector<GradeRule> rules;
    double fabricWidth = 1500;
    QVector<FabricPlacement> placements;
    bool allowHalfTurns = true;

    void removePiece(qsizetype piece);
};

}
