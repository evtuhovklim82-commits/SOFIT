#pragma once
#include <QPointF>
#include <QString>
#include <QVector>
#include <array>
#include <optional>
#include "ProductionData.h"
#include "ParametricDraft.h"

namespace opc {
enum class Shape { Point, Line, Rectangle, Circle, Curve };

enum class Layer { Main, Construction, Allowance };

struct Piece {
    QString name;
    double allowanceMm = 0;
    int cutCount = 1;
    QString size;
    bool showLabel = false;
    std::optional<double> grainAngle;
    QVector<double> notches;
    bool operator==(const Piece&) const = default;
};

struct Entity {
    Shape shape;
    QPointF a, b;
    QPointF control{};
    qsizetype piece = -1;
    Layer layer = Layer::Main;
    QString draftName{};
    std::optional<double> seamAllowanceMm;
    QVector<double> seamNotches; // Arc-length fractions measured from a.
};

class PatternDocument {
public:
    void insert(qsizetype index, const Entity& entity) { entities_.insert(index, entity); }
    void replace(qsizetype index, const Entity& entity) { entities_[index] = entity; }
    void remove(qsizetype index) { entities_.removeAt(index); }
    const QVector<Entity>& entities() const { return entities_; }
    QVector<Piece>& pieces() { return pieces_; }
    const QVector<Piece>& pieces() const { return pieces_; }
    ProductionData& production() { return production_; }
    const ProductionData& production() const { return production_; }
    bool layerVisible(Layer layer) const { return layerVisibility_[static_cast<int>(layer)]; }
    void setLayerVisible(Layer layer, bool visible) { layerVisibility_[static_cast<int>(layer)] = visible; }
    const ParametricDraft& parametricDraft() const { return draft_; }
    bool setParametricDraft(ParametricDraft draft, QString* error = nullptr);
    bool validateParametricSizes(const ProductionData& production, QString* error = nullptr) const;
    std::optional<QVector<Entity>> evaluateSizeDraft(const PatternSize& size) const;
    bool save(const QString& path, QString* error = nullptr) const;
    bool load(const QString& path, QString* error = nullptr);

private:
    ProductionData production_;
    ParametricDraft draft_;
    QVector<Entity> entities_;
    QVector<Piece> pieces_;
    std::array<bool, 3> layerVisibility_{true, true, true};
};
}
