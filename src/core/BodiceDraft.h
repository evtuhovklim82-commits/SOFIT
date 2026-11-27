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

enum class NeckFinish { None, FlatCollar, Stand, Facing, PointedCollar };
enum class SleeveStyle { Fitted, Wide, Puff, TwoPiece };

struct BodiceOptions {
    NeckFinish neckFinish = NeckFinish::None;
    double finishDepth = 35;
    SleeveStyle sleeveStyle = SleeveStyle::Fitted;
    double extraWidth = 100;
    double crownRise = 30;
    double gatherPerCapSection = 35;
    double elbowShape = 25;
    double collarPointExtension = 30;
    double dartIntakeShare = 50;
    double dartPosition = 50;
    double elbowLevel = 50;
    double capEase = 4;
};

struct LinkedSleeveMeasurements {
    double lengthMm = 600;
    double capRiseMm = 120;
    double hemWidthMm = 240;
    double frontEaseMm = 4;
    double backEaseMm = 4;
    int notchPairs = 1;
    bool twoPiece = false;
};

bool addLinkedSleeve(PatternDocument& document, const QVector<qsizetype>& front,
                     const QVector<qsizetype>& back, const LinkedSleeveMeasurements& measurements,
                     bool reverseFront, bool reverseBack, QString* error = nullptr);

struct BodiceSeams {
    double frontArmhole, backArmhole, frontCap, backCap;
};

std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& input,
                                                   QString* error = nullptr);
std::optional<PatternDocument> draftBodiceAndSleeve(const BodiceMeasurements& input,
                                                   const BodiceOptions& options, QString* error = nullptr);
// Sleeve measurements are unused for the sleeveless dress.
std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt,
                                          QString* error = nullptr);
std::optional<PatternDocument> draftDress(const BodiceMeasurements& bodice,
                                          const DressSkirtMeasurements& skirt,
                                          const BodiceOptions& options, QString* error = nullptr);
std::optional<BodiceSeams> measureBodiceSeams(const PatternDocument& document);

}
