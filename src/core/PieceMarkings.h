#pragma once

#include "PieceGeometry.h"
#include <QPainterPath>

namespace opc {
enum class PieceSymbol {
    Cross, Circle, Buttonhole, WarpRepeat, WeftRepeat,
    NotchStroke, VNotch, UNotch, Arrow, ReverseArrow, GrainArrow, Tip,
    CenterFront, CenterBack, Scissors
};
inline const QString pieceSymbolLayer = QStringLiteral("SOFIT_SYMBOL");

QVector<Entity> buildPieceSymbol(PieceSymbol type, qsizetype piece, QPointF center,
                                 double lengthMm, double angleDegrees);
bool symbolFitsContour(const QPainterPath& symbol, const QPolygonF& contour);

struct PieceMarkings {
    QPainterPath notches;
    QPainterPath grainline;
    QString label;
    QPointF labelOrigin;
    qsizetype notchesMissingCutOutline = 0;
    QPainterPath labelPath() const;
};

PieceMarkings buildPieceMarkings(const Piece& piece, const PieceGeometry& geometry,
                                 bool useAllowance = false);
}
