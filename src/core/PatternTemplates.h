#pragma once

#include "PatternDocument.h"

namespace opc {
enum class SkirtStyle { Gathered, ALine };

struct SkirtMeasurements {
    double hipMm;
    double lengthMm;
    double hipEaseMm;
    double hemFlareMm;
    double casingMm;
};

std::optional<PatternDocument> draftPullOnSkirt(SkirtStyle style, const SkirtMeasurements& input,
                                              QString* error = nullptr);
}
