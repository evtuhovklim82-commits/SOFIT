#pragma once

#include "BodiceDraft.h"

namespace opc {

// Appends pieces that share the fitting block's neckline measurements.
void addNeckFinish(ParametricDraft& draft, PatternDocument& document, const BodiceOptions& options);

bool addLinkedNeckStand(PatternDocument& document, const QVector<qsizetype>& neckline, double depthMm,
                        int notchPairs, QString* error = nullptr, double riseMm = 0);

}
