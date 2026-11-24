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

    bool operator==(const ProductInformation&) const = default;
};

bool exportProductInformationPdf(const QString& path, const PatternDocument& document, QString& error);

}
