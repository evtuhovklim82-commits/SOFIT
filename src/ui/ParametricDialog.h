#pragma once

#include "PatternDocument.h"
#include <optional>

class QWidget;

std::optional<opc::PatternDocument> editParametricDraft(QWidget* parent, const opc::PatternDocument& document);
