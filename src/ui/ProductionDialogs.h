#pragma once

#include "PatternDocument.h"

class QWidget;

std::optional<opc::ProductionData> editGrading(QWidget* parent, const opc::PatternDocument& document);
std::optional<opc::ProductionData> editFabricLayout(QWidget* parent, const opc::PatternDocument& document);
