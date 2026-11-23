#pragma once

#include "PatternDocument.h"

class QWidget;

std::optional<opc::PatternDocument> createPatternFromTemplate(QWidget* parent);
