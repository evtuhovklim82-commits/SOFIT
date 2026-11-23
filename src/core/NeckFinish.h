#pragma once

#include "BodiceDraft.h"

namespace opc {

// Appends pieces that share the fitting block's neckline measurements.
void addNeckFinish(ParametricDraft& draft, PatternDocument& document, const BodiceOptions& options);

}
