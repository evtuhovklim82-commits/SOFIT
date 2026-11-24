#pragma once

#include <QMap>
#include <QPointF>
#include <QRectF>
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

enum class CutHand { Single, Left, Right };
enum class FoldEdge { None, Left, Right };

struct FabricPlacement {
    qsizetype piece;
    QString size;
    QPointF position;
    bool reversed = false;
    CutHand hand = CutHand::Single;
    FoldEdge fold = FoldEdge::None;
    bool fixedOrientation = false;
    QString repeatEdge;
    double repeatParameter = 0.5;
};

struct ProductionData {
    QVector<PatternSize> sizes;
    QVector<GradeRule> rules;
    double fabricWidth = 1500;
    QVector<FabricPlacement> placements;
    bool allowHalfTurns = true;
    bool directionalFabric = false;
    bool useDraftMeasurements = true;

    double pieceClearanceMm = 0;
    QPointF fabricRepeatMm;
    QVector<QRectF> fabricFlaws;
    QPointF fabricShrinkagePercent; // Across width (weft), along length (warp).

    void removePiece(qsizetype piece);
};

}
