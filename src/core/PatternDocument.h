#pragma once

#include "ProductInformation.h"
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>
#include <array>
#include <optional>
#include "ProductionData.h"
#include "ParametricDraft.h"

class QDir;
class QJsonArray;
class QJsonObject;

namespace opc {

enum class GradeEdgeNames { ForSeamLinks, Preserve };
enum class Shape { Point, Line, Rectangle, Circle, Curve };

enum class Layer { Main, Construction, Allowance };

enum class ContourOperation {
    Cut, Spread, TransferDart, TransferDartToOutlet, MoveOpenDart, Pleat, Gather, Relief, Yoke, SplitDart, MergeDarts, PleatWithTabs
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
    std::optional<QPointF> fabricShrinkageOverride;
    bool bevelAllowanceCorners = false;
    bool operator==(const Piece&) const = default;
};

enum class PieceSymbol {
    Cross, Circle, Buttonhole, WarpRepeat, WeftRepeat,
    NotchStroke, VNotch, UNotch, Arrow, ReverseArrow, GrainArrow, Tip,
    CenterFront, CenterBack, Scissors
};
struct EdgeSymbol {
    PieceSymbol type = PieceSymbol::Cross;
    double fraction = 0.5;
    double sizeMm = 10;
    double insetMm = 10;
    bool fromCutOutline = false;
    bool operator==(const EdgeSymbol&) const = default;
};

bool validEdgeSymbol(const EdgeSymbol& symbol);

struct EdgeMarks {
    QVector<double> notches;
    QVector<EdgeSymbol> symbols;
    bool operator==(const EdgeMarks&) const = default;
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
    QString dxfLayer;
    std::optional<bool> bevelAtStart;
    std::optional<bool> bevelAtEnd;
    QVector<EdgeSymbol> seamSymbols;
    QMap<QString, EdgeMarks> gradedMarks;
};

// A step addresses its inputs by persistent formula object names.
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
    double lengthScale = 1;
    QString rotationPivot;
    double rotationDegrees = 0;
    QString spreadFormula;
    QVector<double> outletWeights;
    std::optional<QVector<QString>> followingGuides;
    bool operator==(const ModelingStep&) const = default;
};

struct SeamLink {
    QVector<QString> first, second; // Ordered from the selected sewing starts.
    bool reverseFirst = false;
    bool reverseSecond = false;
    double targetEaseMm = 0;
    double toleranceMm = 1;
    int notchPairs = 0;
    std::optional<double> allowanceMm;
    bool operator==(const SeamLink&) const = default;
};

struct PanelSilhouette {
    double axisFraction;
    double topScale;
    double bottomScale;
    double lengthScale;
    bool operator==(const PanelSilhouette&) const = default;
};

struct SizeCutStep {
    qsizetype piece = -1;
    QVector<QString> guides;
    QString countFormula;
    QMap<QString, qsizetype> panels;
    QMap<qsizetype, QVector<PanelSilhouette>> silhouettes;
    bool operator==(const SizeCutStep&) const = default;
};

struct SeamLinkCheck {
    double firstMm = 0, secondMm = 0;
    bool withinTolerance = false;
    QString error;
};

struct SeamLinkReport {
    QVector<SeamLinkCheck> checks;
    QHash<qsizetype, QVector<double>> notches;
    QHash<qsizetype, double> allowances;
    QSet<qsizetype> pieces;
};

struct LibraryInstance {
    QString sourceName;
    QString sourcePath;
    QString revision;
    QString sourceStructure;
    QString installedFormulas;
    QMap<QString, QString> measurements, parameters, points, edges;
    bool operator==(const LibraryInstance&) const = default;
};

class PatternDocument {
public:
    void insert(qsizetype index, const Entity& entity) { entities_.insert(index, entity); }
    void replace(qsizetype index, Entity entity);
    void remove(qsizetype index) { entities_.removeAt(index); }
    const QVector<Entity>& entities() const { return entities_; }
    QVector<Piece>& pieces() { return pieces_; }
    const QVector<Piece>& pieces() const { return pieces_; }
    ProductInformation& productInformation() { return productInformation_; }
    const ProductInformation& productInformation() const { return productInformation_; }
    ProductionData& production() { return production_; }
    const ProductionData& production() const { return production_; }
    bool layerVisible(Layer layer) const { return layerVisibility_[static_cast<int>(layer)]; }
    void setLayerVisible(Layer layer, bool visible) { layerVisibility_[static_cast<int>(layer)] = visible; }
    const ParametricDraft& parametricDraft() const { return draft_; }
    // Create a production contour that shares the source's draft points.
    bool deriveParametricPiece(qsizetype source, const QString& name, QString* error = nullptr,
                               bool includeConstruction = false);
    bool insertConstruction(const PatternDocument& source, QString* error = nullptr,
                            const QMap<QString, QString>& measurementBindings = {},
                            const QString& libraryName = {}, const QString& sourcePath = {});
    const QVector<LibraryInstance>& libraryInstances() const { return libraryInstances_; }
    bool updateLibraryConstruction(qsizetype instance, const PatternDocument& source, QString* error = nullptr,
                                   const QMap<QString, QString>& replacements = {},
                                   const QMap<QString, QStringList>& edgeSplits = {},
                                   const QMap<QString, QStringList>& edgeMerges = {});
    bool refreshLibraryDependencies(const QString& projectPath, QString* error = nullptr,
                                    QSet<QString>* sourceFiles = nullptr);
    bool smoothCurveChain(const QVector<qsizetype>& edges, double startDegrees, double endDegrees,
                          QString* error = nullptr);
    bool moveDraftPoint(const QString& name, QPointF offset, QString* error = nullptr);
    bool setParametricDraft(ParametricDraft draft, QString* error = nullptr);
    bool validateParametricSizes(const ProductionData& production, QString* error = nullptr) const;
    std::optional<QVector<Entity>> evaluateSizeDraft(const PatternSize& size) const;
    std::optional<double> evaluateSizeFormula(const QString& size, const QString& formula,
                                              QString* error = nullptr) const;
    const QVector<ModelingStep>& modelingHistory() const { return history_; }
    const QVector<SizeCutStep>& sizeCutHistory() const { return sizeCuts_; }
    bool appendSizeCut(qsizetype piece, QVector<QString> guides, QString formula, QString* error = nullptr);
    bool editSizeCut(qsizetype step, QString formula, QString* error = nullptr);
    bool refreshSizeCutPanels(QString* error = nullptr);
    bool isSizeCutPiece(qsizetype piece) const;
    bool appendPanelSilhouette(qsizetype piece, PanelSilhouette silhouette, QString* error = nullptr);
    bool appendSilhouette(qsizetype piece, double axisFraction, double topScale,
                          double bottomScale, QString* error = nullptr, double lengthScale = 1);
    bool appendRotation(const QVector<qsizetype>& selection, qsizetype pivot,
                        double degrees, QString* error = nullptr);
    // Change numeric settings while keeping the operation and all references intact.
    bool editModelingStep(qsizetype index, const ModelingStep& settings, QString* error = nullptr);
    bool appendContour(qsizetype piece, ContourOperation operation,
                       const QVector<qsizetype>& guides, double spreadMm, QString* error = nullptr, const QString& spreadFormula = {},
                       const std::optional<QVector<QString>>& followingGuides = {},
                       const QVector<double>& outletWeights = {});
    // Apply consecutive named guides atomically.
    bool appendDrape(qsizetype piece, const QVector<QString>& guides, const QVector<double>& widths,
                     ContourOperation finish = ContourOperation::Spread, QString* error = nullptr);
    bool appendDrapeFormulas(qsizetype piece, const QVector<QString>& guides, const QVector<QString>& widths,
                             ContourOperation finish, QString* error = nullptr);
    bool appendDrapeFormulas(qsizetype piece, const QVector<QString>& guides, const QVector<QString>& widths,
                             const QVector<ContourOperation>& operations, QString* error = nullptr);
    bool hasContourHistory() const { return !hiddenSources_.isEmpty(); }
    bool applyModelingSequence(const PatternDocument& source, QString* error = nullptr);
    bool removeLastModelingStep(QString* error = nullptr);
    const QVector<SeamLink>& seamLinks() const { return seamLinks_; }
    bool addSeamLink(SeamLink link, QString* error = nullptr);
    bool fitFormulaPieceToSeam(SeamLink link, QString* error = nullptr);
    bool editSeamLink(qsizetype index, SeamLink link, QString* error = nullptr);
    bool removeSeamLink(qsizetype index);
    SeamLinkReport inspectSeamLinks() const;
    bool validateSeamLinks(QString* error = nullptr) const;
    bool save(const QString& path, QString* error = nullptr) const;
    bool load(const QString& path, QString* error = nullptr);

private:
    static QJsonArray saveSeamLinks(const QVector<SeamLink>& seamLinks);
    static QJsonArray saveModelingHistory(const QVector<ModelingStep>& history);
    QString libraryStructureDigest() const;
    bool loadPayload(const QJsonObject& root, bool native, const QDir& directory, QString* error);
    friend PatternDocument gradedPattern(const PatternDocument&, const QString&, QVector<qsizetype>*, GradeEdgeNames);
    friend PatternDocument uncutGradedPattern(const PatternDocument&, const QString&, QVector<qsizetype>*, GradeEdgeNames);
    QVector<SeamLink> seamLinks_;
    QVector<LibraryInstance> libraryInstances_;
    bool captureContourTopology(ModelingStep& step, QString* error) const;
    QVector<Entity> bindDraftGeometry(QVector<Entity> generated) const;
    static std::optional<double> evaluateModelingWidth(const ParametricDraft& draft,
                                                        const QString& formula, QString* error);
    bool replayModeling(QVector<Entity>& entities, const ProductionData& production, const ParametricDraft& draft,
                        QString* error, ModelingStep* capture = nullptr) const;
    bool validateDraftSizes(const ParametricDraft& draft, const QVector<Entity>& base,
                            const ProductionData& production, QString* error) const;

    struct SourceBinding {
        QString name;
        qsizetype piece;
        Layer layer;
    };
    QVector<ModelingStep> history_;
    QVector<SizeCutStep> sizeCuts_;
    QVector<SourceBinding> hiddenSources_;
    ProductionData production_;
    ProductInformation productInformation_;
    ParametricDraft draft_;
    QVector<Entity> entities_;
    QVector<Piece> pieces_;
    std::array<bool, 3> layerVisibility_{true, true, true};
};
}
