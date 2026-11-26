#pragma once

#include "PatternDocument.h"

class QWidget;

std::optional<opc::ProductionData> editGrading(QWidget* parent, const opc::PatternDocument& document);
std::optional<opc::ProductionData> editFabricLayout(QWidget* parent, const opc::PatternDocument& document,
                                                    const QString& material = {});
void reopenMaterialOrder(QWidget* parent);
void exportMaterialLay(QWidget* parent, const opc::PatternDocument& document);
void planMaterialOrder(QWidget* parent, const opc::PatternDocument& document);
