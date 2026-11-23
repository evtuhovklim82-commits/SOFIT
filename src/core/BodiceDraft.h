#pragma once

#include "FittingBlocks.h"

namespace opc {

// All lengths and circumferences are in millimetres.
struct BodiceMeasurements {
    double bust, waist, bodiceLength, armholeDepth;
    double neckWidth, frontNeckDepth, backNeckDepth, shoulderRun, shoulderDrop;
    double bustEase, waistEase, dartLength;
    double upperArm, upperArmEase, cuff, sleeveLength, capHeight;
};

struct BodiceSeams {
    double frontArmhole, backArmhole, frontCap, backCap;
};

std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& input,
                                                   QString* error = nullptr);
// Sleeve measurements are unused for the sleeveless dress.
std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt,
                                          QString* error = nullptr);
std::optional<BodiceSeams> measureBodiceSeams(const PatternDocument& document);

}
