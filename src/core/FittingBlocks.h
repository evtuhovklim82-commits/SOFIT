#pragma once

#include "PatternDocument.h"

namespace opc {

// Measurements and wearing ease are in millimetres. Knee and hem are per leg.
struct TrouserMeasurements {
    double waist, hip, waistEase, hipEase;
    double hipDepth, rise, kneeDepth, length;
    double knee, hem, dartLength;
};

struct DressSkirtMeasurements {
    double hip, hipEase, hipDepth, length, hemFlare, dartLength;
};

struct FittingSeam {
    QString label;
    double first, second, targetDifference;
};

struct FittingCheck {
    QStringList invalidPieces;
    QStringList missingSeams;
    QVector<FittingSeam> seams;
};

std::optional<PatternDocument> draftTrousers(const TrouserMeasurements& m, QString* error = nullptr);
std::optional<PatternDocument> draftTrousersWithSeamLinks(const TrouserMeasurements& m,
                                                         QString* error = nullptr);
// An empty size checks the base. Named sizes include any manual grade offsets.
FittingCheck checkFittingBlock(const PatternDocument& document, const QString& size = {},
                              std::optional<double> capEase = std::nullopt);

}
