#pragma once

#include <QString>

namespace opc {

class PatternDocument;

struct ProductInformation {
    QString styleCode;
    QString title;
    QString revision;
    QString material;
    QString notes;
    QString reportTitle;
    QString billOfMaterials;
    QString assemblyInstructions;
    bool includeCuttingQuantities = true;
    bool includeSizeMeasurements = false;

    bool usesTechPackOptions() const {
        return !reportTitle.isEmpty() || !billOfMaterials.isEmpty() || !assemblyInstructions.isEmpty()
            || !includeCuttingQuantities || includeSizeMeasurements;
    }

    bool operator==(const ProductInformation&) const = default;
};

bool exportProductInformationPdf(const QString& path, const PatternDocument& document, QString& error);

}
