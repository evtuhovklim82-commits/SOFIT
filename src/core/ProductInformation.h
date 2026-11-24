#pragma once

#include <QString>
#include <QVector>
#include <optional>

namespace opc {

class PatternDocument;

struct FinishedMeasurement {
    QString label, first, second;
    bool operator==(const FinishedMeasurement&) const = default;
};

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
    QVector<FinishedMeasurement> finishedMeasurements;

    bool usesTechPackOptions() const {
        return !reportTitle.isEmpty() || !billOfMaterials.isEmpty() || !assemblyInstructions.isEmpty()
            || !includeCuttingQuantities || includeSizeMeasurements;
    }

    bool operator==(const ProductInformation&) const = default;
};

std::optional<double> finishedMeasurementMm(const PatternDocument& graded, const FinishedMeasurement& measurement);
bool exportProductInformationPdf(const QString& path, const PatternDocument& document, QString& error);

}
