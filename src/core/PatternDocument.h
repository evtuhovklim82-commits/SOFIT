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

enum class ContourOperation {
    Cut, Spread, TransferDart, TransferDartToOutlet, MoveOpenDart, Pleat, Gather, Relief, Yoke
};

QString contourOperationKey(ContourOperation operation);
std::optional<ContourOperation> contourOperationFromKey(const QString& key);

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

// A step addresses the full piece by its persistent formula object names.
struct ModelingStep {
    QVector<QString> objects;
    double axisFraction = 0;
    double topScale = 1;
    double bottomScale = 1;
    std::optional<ContourOperation> contour;
    QVector<QString> guides;
    double spreadMm = 0;
    QVector<QString> topology;
    qsizetype secondPiece = -1;
    bool operator==(const ModelingStep&) const = default;
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
    const QVector<ModelingStep>& modelingHistory() const { return history_; }
    bool appendSilhouette(qsizetype piece, double axisFraction, double topScale,
                          double bottomScale, QString* error = nullptr);
    bool appendContour(qsizetype piece, ContourOperation operation,
                       const QVector<qsizetype>& guides, double spreadMm, QString* error = nullptr);
    bool hasContourHistory() const { return !hiddenSources_.isEmpty(); }
    bool removeLastModelingStep(QString* error = nullptr);
    bool save(const QString& path, QString* error = nullptr) const;
    bool load(const QString& path, QString* error = nullptr);

private:
    bool captureContourTopology(ModelingStep& step, QString* error) const;
    QVector<Entity> bindDraftGeometry(QVector<Entity> generated) const;
    bool replayModeling(QVector<Entity>& entities, const ProductionData& production,
                        QString* error, ModelingStep* capture = nullptr) const;
    bool validateDraftSizes(const ParametricDraft& draft, const QVector<Entity>& base,
                            const ProductionData& production, QString* error) const;

    struct SourceBinding {
        QString name;
        qsizetype piece;
        Layer layer;
    };
    QVector<ModelingStep> history_;
    QVector<SourceBinding> hiddenSources_;
    ProductionData production_;
    ParametricDraft draft_;
    QVector<Entity> entities_;
    QVector<Piece> pieces_;
    std::array<bool, 3> layerVisibility_{true, true, true};
};
}
