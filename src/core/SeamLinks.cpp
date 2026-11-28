#include "PatternDocument.h"

#include "PieceGeometry.h"
#include "Production.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace opc {
namespace {
bool validLink(const SeamLink& link) {
    if (link.first.isEmpty() || link.second.isEmpty()
        || !std::isfinite(link.targetEaseMm) || !std::isfinite(link.toleranceMm)
        || (link.allowanceMm && (!std::isfinite(*link.allowanceMm) || *link.allowanceMm < 0))
        || link.toleranceMm < 0 || link.notchPairs < 0 || link.notchPairs > 20)
        return false;
    QSet<QString> names;
    for (const auto* section : {&link.first, &link.second}) {
        for (const auto& name : *section) {
            if (name.isEmpty() || names.contains(name))
                return false;
            names.insert(name);
        }
    }
    return true;
}

std::optional<SeamSection> resolveSection(const PatternDocument& document,
                                          const QHash<QString, qsizetype>& indices,
                                          const QVector<QString>& names, bool reverseFirst) {
    QVector<qsizetype> ordered;
    for (const auto& name : names) {
        const auto index = indices.value(name, -1);
        if (index < 0)
            return {};
        ordered.append(index);
    }
    auto section = measureSeamSection(document, ordered);
    if (!section)
        return {};
    if (section->edges.front().index != ordered.front()
        || section->edges.front().reversed != reverseFirst) {
        std::ranges::reverse(section->edges);
        for (auto& edge : section->edges)
            edge.reversed = !edge.reversed;
    }
    for (qsizetype i = 0; i < ordered.size(); ++i) {
        if (section->edges[i].index != ordered[i])
            return {};
    }
    if (section->edges.front().reversed != reverseFirst)
        return {};
    const auto piece = document.entities()[ordered.front()].piece;
    if (piece < 0 || piece >= document.pieces().size())
        return {};
    return section;
}
}

SeamLinkReport PatternDocument::inspectSeamLinks() const {
    SeamLinkReport report;
    if (seamLinks_.isEmpty())
        return report;
    QHash<QString, qsizetype> indices;
    for (qsizetype i = 0; i < entities_.size(); ++i) {
        const auto& name = entities_[i].draftName;
        if (!name.isEmpty())
            indices.insert(name, indices.contains(name) ? -1 : i);
    }
    for (const auto& link : seamLinks_) {
        auto& check = report.checks.emplaceBack();
        const auto first = resolveSection(*this, indices, link.first, link.reverseFirst);
        const auto second = resolveSection(*this, indices, link.second, link.reverseSecond);
        if (!first || !second) {
            check.error = QStringLiteral("A linked seam edge is missing or the sewing chain changed.");
            continue;
        }
        check.firstMm = first->lengthMm;
        check.secondMm = second->lengthMm;
        check.withinTolerance = std::abs(check.secondMm - check.firstMm - link.targetEaseMm) <= link.toleranceMm;
        QVector<std::pair<qsizetype, double>> positions;
        for (int i = 1; i <= link.notchPairs; ++i) {
            const auto fraction = static_cast<double>(i) / (link.notchPairs + 1);
            const auto a = first->notchAt(fraction);
            const auto b = second->notchAt(fraction);
            if (!a || !b) {
                check.error = QStringLiteral("A linked notch falls on a seam junction. Adjust the notch count or the seam.");
                break;
            }
            positions.append(*a);
            positions.append(*b);
        }
        if (!check.error.isEmpty())
            continue;
        for (const auto* section : {&*first, &*second}) {
            if (link.allowanceMm && std::ranges::any_of(section->edges, [&](const SeamEdge& edge) {
                    return report.allowances.value(edge.index, *link.allowanceMm) != *link.allowanceMm;
                })) {
                check.error = QStringLiteral("Two linked seams specify different allowances on the same edge.");
                break;
            }
        }
        if (check.error.isEmpty()) {
            for (const auto* section : {&*first, &*second}) {
                report.pieces.insert(entities_[section->edges.front().index].piece);
                if (link.allowanceMm) {
                    for (const auto& edge : section->edges)
                        report.allowances.insert(edge.index, *link.allowanceMm);
                }
            }
            for (const auto& [index, fraction] : positions) {
                auto& notches = report.notches[index];
                if (std::ranges::none_of(notches, [fraction](double existing) {
                        return std::abs(existing - fraction) < 1e-9;
                    }))
                    notches.append(fraction);
            }
        }
    }
    return report;
}

bool PatternDocument::validateSeamLinks(QString* error) const {
    if (seamLinks_.isEmpty())
        return true;
    const auto validate = [&](const PatternDocument& pattern, const QString& size) {
        const auto report = pattern.inspectSeamLinks();
        if (report.checks.size() != seamLinks_.size()) {
            if (error)
                *error = QStringLiteral("Size '%1': linked seams could not be recalculated.").arg(size);
            return false;
        }
        for (qsizetype i = 0; i < report.checks.size(); ++i) {
            if (!report.checks[i].error.isEmpty()) {
                if (error)
                    *error = QStringLiteral("%1 · Seam link %2: %3").arg(size).arg(i + 1).arg(report.checks[i].error);
                return false;
            }
            if (!seamLinks_[i].gradedLengthMeasurement.isEmpty() && !report.checks[i].withinTolerance) {
                if (error)
                    *error = QStringLiteral("%1 · Seam link %2: the fitted curve no longer matches its driving chain.")
                        .arg(size).arg(i + 1);
                return false;
            }
        }
        // Validate against the actual cutting contour, including the shared widths.
        for (qsizetype piece = 0; piece < pattern.pieces().size(); ++piece) {
            if (!report.pieces.contains(piece))
                continue;
            const auto geometry = analyzePiece(pattern, piece);
            if (geometry.error != ContourError::None || geometry.allowanceFailed) {
                if (error)
                    *error = QStringLiteral("%1 · %2: a linked piece has an invalid contour or cut outline.")
                        .arg(size, pattern.pieces()[piece].name);
                return false;
            }
        }
        return true;
    };
    if (!validate(*this, QStringLiteral("Base pattern")))
        return false;
    for (const auto& size : production_.sizes) {
        if (!validate(gradedPattern(*this, size.name), size.name))
            return false;
    }
    return true;
}

bool PatternDocument::addSeamLink(SeamLink link, QString* error) {
    if (!validLink(link) || seamLinks_.contains(link)) {
        if (error)
            *error = QStringLiteral("A seam link needs distinct named edges and valid settings and must be unique.");
        return false;
    }
    if (!link.gradedLengthMeasurement.isEmpty()
        && (std::ranges::find(draft_.measurements, link.gradedLengthMeasurement, &Measurement::name) == draft_.measurements.end()
            || std::ranges::any_of(seamLinks_, [&](const auto& previous) {
                return previous.gradedLengthMeasurement == link.gradedLengthMeasurement;
            }))) {
        if (error)
            *error = QStringLiteral("A graded seam needs its own existing draft measurement.");
        return false;
    }
    auto candidate = *this;
    candidate.seamLinks_.append(std::move(link));
    if (!candidate.validateSeamLinks(error))
        return false;
    seamLinks_ = std::move(candidate.seamLinks_);
    if (error)
        error->clear();
    return true;
}

bool PatternDocument::editSeamLink(qsizetype index, SeamLink link, QString* error) {
    if (index < 0 || index >= seamLinks_.size()
        || !validLink(link) || (seamLinks_[index] != link && seamLinks_.contains(link))) {
        if (error)
            *error = QStringLiteral("Choose an existing seam link and unique valid settings.");
        return false;
    }
    auto candidate = *this;
    candidate.seamLinks_[index] = std::move(link);
    if (!candidate.validateSeamLinks(error))
        return false;
    seamLinks_ = std::move(candidate.seamLinks_);
    if (error)
        error->clear();
    return true;
}

bool PatternDocument::removeSeamLink(qsizetype index) {
    if (index < 0 || index >= seamLinks_.size())
        return false;
    seamLinks_.removeAt(index);
    return true;
}
}
