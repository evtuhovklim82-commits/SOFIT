#include "MainWindow.h"
#include <QApplication>
#include "Workflow.h"
#include "PatternView.h"
#include "ParametricDialog.h"
#include "PatternPdf.h"
#include "PatternDxf.h"
#include "DxfImportDialog.h"
#include "PieceMarkings.h"
#include "Production.h"
#include "ProductionDialogs.h"
#include "TemplateWizard.h"
#include "PatternTemplates.h"
#include "FittingBlocks.h"
#include "NeckFinish.h"
#include "EntityPath.h"
#include "Modeling.h"
#include "ContourModeling.h"
#include <QPixmap>
#include <QGraphicsView>
#include <QPainter>
#include <QPen>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHeaderView>
#include <QHash>
#include <QInputDialog>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QWizard>

#include <functional>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QSaveFile>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace {
QString recoveryDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("recovery"));
}

void fillPieceSymbolTypes(QComboBox& type) {
    type.addItem(QStringLiteral("Крест"), int(opc::PieceSymbol::Cross));
    type.addItem(QStringLiteral("Окружность"), int(opc::PieceSymbol::Circle));
    type.addItem(QStringLiteral("Петля"), int(opc::PieceSymbol::Buttonhole));
    type.addItem(QStringLiteral("Раппорт по основе"), int(opc::PieceSymbol::WarpRepeat));
    type.addItem(QStringLiteral("Раппорт по утку"), int(opc::PieceSymbol::WeftRepeat));
    type.addItem(MainWindow::tr("Straight notch mark"), int(opc::PieceSymbol::NotchStroke));
    type.addItem(MainWindow::tr("V-notch mark"), int(opc::PieceSymbol::VNotch));
    type.addItem(MainWindow::tr("U-notch mark"), int(opc::PieceSymbol::UNotch));
    type.addItem(MainWindow::tr("Arrow"), int(opc::PieceSymbol::Arrow));
    type.addItem(MainWindow::tr("Reverse arrow"), int(opc::PieceSymbol::ReverseArrow));
    type.addItem(MainWindow::tr("Grain arrow mark"), int(opc::PieceSymbol::GrainArrow));
    type.addItem(MainWindow::tr("Tip mark"), int(opc::PieceSymbol::Tip));
    type.addItem(MainWindow::tr("Center front mark"), int(opc::PieceSymbol::CenterFront));
    type.addItem(MainWindow::tr("Center back mark"), int(opc::PieceSymbol::CenterBack));
    type.addItem(MainWindow::tr("Scissors mark"), int(opc::PieceSymbol::Scissors));
}

QString describeContour(opc::ContourError error) {
    using opc::ContourError;
    switch (error) {
    case ContourError::None: return QCoreApplication::translate("MainWindow", "One closed contour");
    case ContourError::Empty: return QCoreApplication::translate("MainWindow", "No objects on the main layer");
    case ContourError::UnsupportedShape: return QCoreApplication::translate("MainWindow", "Use lines, rectangles or quadratic curves on the main layer");
    case ContourError::Degenerate: return QCoreApplication::translate("MainWindow", "A zero-length or folded edge");
    case ContourError::Open: return QCoreApplication::translate("MainWindow", "Open ends");
    case ContourError::Branched: return QCoreApplication::translate("MainWindow", "Multiple edges meet at an endpoint");
    case ContourError::Disconnected: return QCoreApplication::translate("MainWindow", "More than one closed contour");
    case ContourError::SelfIntersecting: return QCoreApplication::translate("MainWindow", "Contour intersects, overlaps, or folds onto itself");
    case ContourError::UncertainCurves: return QCoreApplication::translate("MainWindow", "Curves are too close to verify a simple contour");
    case ContourError::TooComplex: return QCoreApplication::translate("MainWindow", "Contour exceeds the geometry limits");
    }
    return {};
}

QString describeSeamLink(const opc::SeamLinkCheck& check, double target) {
    const auto text = [](const char* label) { return QCoreApplication::translate("MainWindow", label); };
    if (!check.error.isEmpty())
        return check.error;
    return text("A %L1 mm · B %L2 mm · Ease %L3 mm (target %L4 mm) · %5")
        .arg(check.firstMm, 0, 'f', 2).arg(check.secondMm, 0, 'f', 2)
        .arg(check.secondMm - check.firstMm, 0, 'f', 2).arg(target, 0, 'f', 2)
        .arg(check.withinTolerance ? text("Within tolerance") : text("Outside tolerance"));
}

QString modelingStepName(const opc::ModelingStep& step) {
    using C = opc::ContourOperation;
    const auto text = [](const char* label) { return QCoreApplication::translate("MainWindow", label); };
    if (!step.rotationPivot.isEmpty())
        return text("Rotate Construction");
    if (!step.contour)
        return text("Shape Silhouette");
    switch (*step.contour) {
    case C::Cut: return text("Cut Piece");
    case C::Relief: return text("Relief Seam");
    case C::Yoke: return text("Yoke");
    case C::Spread: return text("Slash and Spread");
    case C::Pleat: return text("Box Pleat");
    case C::PleatWithTabs: return text("Pleat with Cut Extensions");
    case C::Gather: return text("Gathering Fullness");
    case C::TransferDart:
    case C::TransferDartToOutlet: return text("Close and Transfer Dart");
    case C::MoveOpenDart: return text("Move Open Contour Dart");
    case C::SplitDart: return QStringLiteral("Разделить вытачку");
    case C::MergeDarts: return QStringLiteral("Объединить вытачки");
    }
    return {};
}

QString dartWeightsText(const QVector<double>& weights) {
    QStringList values;
    for (const auto weight : weights)
        values.append(QString::number(weight, 'g', 17));
    return values.join(u' ');
}

QVector<double> readDartWeights(const QString& text) {
    QVector<double> weights;
    for (const auto& word : text.simplified().split(u' ', Qt::SkipEmptyParts)) {
        bool ok;
        const auto value = word.toDouble(&ok);
        weights.append(ok ? value : 0);
    }
    return weights;
}

class PieceSetupWizard : public QWizard {
public:
    using QWizard::QWizard;
    std::function<bool()> validateSetup;

    bool validateCurrentPage() override {
        return QWizard::validateCurrentPage()
            && (currentId() != pageIds().last() || validateSetup());
    }
};

QColor sizeColor(qsizetype index) {
    return QColor::fromHsv((265 + 137 * index) % 360, 205, 188);
}

void fillExportSizes(QComboBox& box, const opc::PatternDocument& document) {
    box.addItem(QCoreApplication::translate("MainWindow", "Base pattern"), QString{});
    for (const auto& size : document.production().sizes)
        box.addItem(size.name, size.name);
}

class PatternCommand : public QUndoCommand {
public:
    PatternCommand(opc::PatternDocument& document, opc::PatternDocument after, const QString& text,
                   PatternView* redraw = nullptr)
        : QUndoCommand(text), document_(document), before_(document), after_(std::move(after)), redraw_(redraw) {}

    PatternCommand(opc::PatternDocument& document, opc::ContourEdit edit, const QString& text,
                   PatternView& redraw)
        : PatternCommand(document, std::move(edit.document), text, &redraw) {
        for (qsizetype i = 0; i < before_.entities().size(); ++i) {
            if (!std::binary_search(edit.removedEntities.cbegin(), edit.removedEntities.cend(), i))
                survivingIndices_.append(i);
        }
        beforeVisibility_.resize(before_.entities().size(), true);
        afterVisibility_.resize(after_.entities().size(), true);
    }

    void undo() override { apply(false); }
    void redo() override { apply(true); }

private:
    void apply(bool forward) {
        const auto& updated = forward ? after_ : before_;
        if (!redraw_) {
            document_ = updated;
            return;
        }

        if (!afterVisibility_.isEmpty()) {
            applyContour(updated, forward);
            return;
        }

        // A draft edit leaves the order of manually drawn objects intact.
        QVector<bool> manualVisibility;
        QHash<QString, bool> draftVisibility;
        for (qsizetype i = 0; i < document_.entities().size(); ++i) {
            const auto& name = document_.entities()[i].draftName;
            const auto visible = redraw_->entityVisible(i);
            if (name.isEmpty())
                manualVisibility.append(visible);
            else
                draftVisibility.insert(name, visible);
        }
        document_ = updated;
        redraw_->setEntities(document_.entities());
        qsizetype manualIndex = 0;
        for (qsizetype i = 0; i < document_.entities().size(); ++i) {
            const auto& name = document_.entities()[i].draftName;
            const bool visible = name.isEmpty()
                ? manualVisibility.value(manualIndex++, true) : draftVisibility.value(name, true);
            if (!visible)
                redraw_->setEntityVisible(i, false);
        }
    }

    void applyContour(const opc::PatternDocument& updated, bool forward) {
        auto& current = forward ? beforeVisibility_ : afterVisibility_;
        auto& next = forward ? afterVisibility_ : beforeVisibility_;
        for (qsizetype i = 0; i < current.size(); ++i)
            current[i] = redraw_->entityVisible(i);
        for (qsizetype i = 0; i < survivingIndices_.size(); ++i) {
            const auto original = survivingIndices_[i];
            if (forward)
                next[i] = current[original];
            else
                next[original] = current[i];
        }
        redraw_->cancelDrawing();
        document_ = updated;
        redraw_->setEntities(document_.entities());
        for (qsizetype i = 0; i < next.size(); ++i) {
            if (!next[i])
                redraw_->setEntityVisible(i, false);
        }
    }

    opc::PatternDocument& document_;
    opc::PatternDocument before_, after_;
    PatternView* redraw_;
    QVector<qsizetype> survivingIndices_;
    QVector<bool> beforeVisibility_, afterVisibility_;
};

class ReplaceEntityCommand : public QUndoCommand {
public:
    ReplaceEntityCommand(opc::PatternDocument& document, PatternView& view, qsizetype index,
                         const opc::Entity& replacement, const QString& text)
        : QUndoCommand(text), document_(document), view_(view), index_(index),
          before_(document.entities()[index]), after_(replacement) {}

    void undo() override { apply(before_); }
    void redo() override { apply(after_); }

private:
    void apply(const opc::Entity& entity) {
        view_.cancelDrawing();
        document_.replace(index_, entity);
        view_.replaceEntity(index_, entity);
    }

    opc::PatternDocument& document_;
    PatternView& view_;
    qsizetype index_;
    opc::Entity before_, after_;
};

class EntityCommand : public QUndoCommand {
public:
    EntityCommand(opc::PatternDocument& document, PatternView& view, qsizetype index,
                  const opc::Entity& entity, bool adding, const QString& text)
        : QUndoCommand(text), document_(document), view_(view), index_(index),
          entity_(entity), adding_(adding), visible_(adding || view.entityVisible(index)) {}

    void redo() override { apply(adding_); }
    void undo() override { apply(!adding_); }

private:
    void apply(bool insert) {
        view_.cancelDrawing();
        if (insert) {
            document_.insert(index_, entity_);
            view_.insertEntity(index_, entity_);
            view_.setEntityVisible(index_, visible_);
        } else {
            visible_ = view_.entityVisible(index_);
            document_.remove(index_);
            view_.removeEntity(index_);
        }
    }

    opc::PatternDocument& document_;
    PatternView& view_;
    qsizetype index_;
    opc::Entity entity_;
    bool adding_;
    bool visible_;
};
}

MainWindow::MainWindow() : view_(new PatternView(&scene_, this)) {
    const auto version = QCoreApplication::applicationVersion();
    setWindowTitle(version.isEmpty() ? QStringLiteral("SOFIT[*]")
                                     : QStringLiteral("SOFIT %1[*]").arg(version));
    resize(1100, 750);
    setCentralWidget(view_);
    setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 5px; padding: 5px; border: 0; }"
        "QToolButton { padding: 6px 9px; border: 1px solid transparent; border-radius: 4px; }"
        "QToolButton:hover { background: #344457; }"
        "QToolButton:checked { background: #244755; border-color: #4f9cb1; }"
        "QTreeView { border: 1px solid #344253; }"
        "QTreeView::item { min-height: 24px; }"
        "QStatusBar { background: #1b2430; }"));

    auto* dock = new QDockWidget(tr("Drawing Objects"), this);
    dock->setObjectName(QStringLiteral("drawingObjects"));
    dock->setMinimumWidth(330);
    objects_ = new QTreeWidget(dock);
    objects_->setHeaderLabels({tr("Object"), tr("Visible"), tr("Piece"), tr("Layer")});
    objects_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    objects_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    objects_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    objects_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    objects_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    objects_->setRootIsDecorated(false);
    objects_->setAlternatingRowColors(true);
    auto* objectPanel = new QWidget(dock);
    auto* objectLayout = new QVBoxLayout(objectPanel);
    objectLayout->addWidget(objects_);
    selectionProperties_ = new QLabel(objectPanel);
    selectionProperties_->setTextFormat(Qt::PlainText);
    selectionProperties_->setWordWrap(true);
    selectionProperties_->setObjectName(QStringLiteral("selectionProperties"));
    objectLayout->addWidget(selectionProperties_);
    dock->setWidget(objectPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(objects_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QSignalBlocker blocker(objects_);
        view_->selectEntities(selectedObjects());
        refreshSelectionProperties();
    });
    connect(objects_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 1) {
            view_->setEntityVisible(objects_->indexOfTopLevelItem(item), item->checkState(1) == Qt::Checked);
            refreshPieceDisplay();
        }
    });
    connect(&scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (objects_->signalsBlocked())
            return;
        const QSignalBlocker blocker(objects_);
        const auto selected = view_->selectedIndices();
        objects_->clearSelection();
        for (const auto index : selected) {
            if (auto* item = objects_->topLevelItem(index))
                item->setSelected(true);
        }
        if (selected.size() == 1 && selected.first() < objects_->topLevelItemCount())
            objects_->scrollToItem(objects_->topLevelItem(selected.first()));
        refreshSelectionProperties();
    });
    connect(&undo_, &QUndoStack::indexChanged, this, &MainWindow::refreshObjects);

    connect(&undo_, &QUndoStack::indexChanged, this, [this] {
        autosavePending_ = recovered_ || !undo_.isClean();
        if (!autosavePending_)
            clearRecovery();
    });
    autosaveTimer_.setParent(this);
    autosaveTimer_.setObjectName(QStringLiteral("projectAutosaveTimer"));
    autosaveTimer_.setInterval(30000);
    connect(&autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosaveProject);
    autosaveTimer_.start();
    librarySyncTimer_.setSingleShot(true);
    librarySyncTimer_.setInterval(300);
    connect(&librarySyncTimer_, &QTimer::timeout, this, &MainWindow::syncLibraryDependencies);
    connect(&libraryWatcher_, &QFileSystemWatcher::fileChanged, this, [this] { librarySyncTimer_.start(); });
    connect(&libraryWatcher_, &QFileSystemWatcher::directoryChanged, this, [this] { librarySyncTimer_.start(); });

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newAction = file->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newDocument);
    auto* openAction = file->addAction(tr("&Open"), QKeySequence::Open, this, &MainWindow::openDocument);
    auto* saveAction = file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveDocument);
    file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveDocumentAs);
    file->addAction(tr("Recover Unsaved Project…"), this, &MainWindow::recoverProject);
    file->addSeparator();
    file->addAction(tr("Export &SVG"), this, &MainWindow::exportSvg);
    file->addAction(tr("Import DXF…"), this, &MainWindow::importDxf);
    file->addAction(tr("Export &DXF…"), this, &MainWindow::exportDxf);
    file->addAction(tr("Export &PDF for Printing…"), this, &MainWindow::exportPdf);

    auto* product = menuBar()->addMenu(tr("P&roduct"));
    product->addAction(tr("Product Information…"), this, [this] {
        const auto& current = doc_.productInformation();
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Product Information"));
        QFormLayout form(&dialog);
        QLineEdit code(current.styleCode), title(current.title), revision(current.revision), material(current.material);
        QPlainTextEdit notes(current.notes);
        code.setObjectName(QStringLiteral("productStyleCode"));
        notes.setObjectName(QStringLiteral("productNotes"));
        form.addRow(tr("Style code"), &code);
        form.addRow(tr("Title"), &title);
        form.addRow(tr("Revision"), &revision);
        form.addRow(tr("Material"), &material);
        form.addRow(tr("Notes"), &notes);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        auto info = current;
        info.styleCode = code.text();
        info.title = title.text();
        info.revision = revision.text();
        info.material = material.text();
        info.notes = notes.toPlainText();
        if (info != current) {
            auto updated = doc_;
            updated.productInformation() = std::move(info);
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Edit Product Information"), nullptr));
        }
    });
    auto* techPackOptions = product->addAction(tr("Tech Pack Options…"), this, [this] {
        auto updated = doc_;
        auto& info = updated.productInformation();
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Tech Pack Options"));
        QFormLayout form(&dialog);
        QLineEdit title(info.reportTitle);
        QPlainTextEdit materials(info.billOfMaterials), assembly(info.assemblyInstructions);
        QCheckBox quantities(tr("Include cutting quantities by size"));
        QCheckBox measurements(tr("Include draft input measurements by size"));
        quantities.setChecked(info.includeCuttingQuantities);
        measurements.setChecked(info.includeSizeMeasurements);
        form.addRow(tr("Report title"), &title);
        form.addRow(tr("Bill of materials"), &materials);
        form.addRow(tr("Assembly instructions"), &assembly);
        form.addRow(&quantities);
        form.addRow(&measurements);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        info.reportTitle = title.text().trimmed();
        info.billOfMaterials = materials.toPlainText();
        info.assemblyInstructions = assembly.toPlainText();
        info.includeCuttingQuantities = quantities.isChecked();
        info.includeSizeMeasurements = measurements.isChecked();
        if (info != doc_.productInformation())
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Edit Tech Pack Options"), nullptr));
    });
    auto* finishedMeasurements = product->addAction(tr("Finished Measurements…"), this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Finished Measurements"));
        dialog.resize(650, 420);
        QVBoxLayout layout(&dialog);
        QLabel hint(tr("Choose two named pattern points for each straight-line measurement. "
                       "The tech pack recalculates the distances for every size."));
        hint.setWordWrap(true);
        layout.addWidget(&hint);
        QTableWidget table(0, 3);
        table.setHorizontalHeaderLabels({tr("Measurement"), tr("First point"), tr("Second point")});
        table.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table.setSelectionBehavior(QAbstractItemView::SelectRows);
        layout.addWidget(&table);
        QStringList points;
        for (const auto& point : doc_.parametricDraft().points)
            points.append(point.name);
        const auto add = [&](const opc::FinishedMeasurement& measurement) {
            const auto row = table.rowCount();
            table.insertRow(row);
            table.setItem(row, 0, new QTableWidgetItem(measurement.label));
            const auto addPoint = [&](int column, const QString& name) {
                auto* box = new QComboBox;
                box->addItems(points);
                if (!name.isEmpty() && !points.contains(name))
                    box->addItem(name);
                box->setCurrentText(name);
                table.setCellWidget(row, column, box);
            };
            addPoint(1, measurement.first);
            addPoint(2, measurement.second);
        };
        for (const auto& measurement : doc_.productInformation().finishedMeasurements)
            add(measurement);
        QHBoxLayout actions;
        QPushButton insert(tr("Add measurement")), remove(tr("Remove selected measurement"));
        insert.setEnabled(points.size() >= 2);
        actions.addWidget(&insert);
        actions.addWidget(&remove);
        layout.addLayout(&actions);
        connect(&insert, &QPushButton::clicked, &dialog, [&] {
            if (table.rowCount() < 100)
                add({{}, points[0], points[1]});
        });
        connect(&remove, &QPushButton::clicked, &dialog, [&] {
            if (table.currentRow() >= 0)
                table.removeRow(table.currentRow());
        });
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QVector<opc::FinishedMeasurement> measurements;
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            measurements.clear();
            QSet<QString> labels;
            for (int row = 0; row < table.rowCount(); ++row) {
                const auto label = table.item(row, 0)->text().trimmed();
                const auto first = qobject_cast<QComboBox*>(table.cellWidget(row, 1))->currentText();
                const auto second = qobject_cast<QComboBox*>(table.cellWidget(row, 2))->currentText();
                if (label.isEmpty() || labels.contains(label) || first.isEmpty() || second.isEmpty() || first == second) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                        tr("Row %1 needs a unique name and two different points.").arg(row + 1));
                    return;
                }
                labels.insert(label);
                measurements.append({label, first, second});
            }
            dialog.accept();
        });
        if (dialog.exec() == QDialog::Accepted && measurements != doc_.productInformation().finishedMeasurements) {
            auto updated = doc_;
            updated.productInformation().finishedMeasurements = std::move(measurements);
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Edit Finished Measurements"), nullptr));
        }
    });
    auto* exportTechPack = product->addAction(tr("Export Product Information PDF…"), this, [this] {
        auto path = QFileDialog::getSaveFileName(this, tr("Export Product Information PDF"), {}, tr("PDF (*.pdf)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
            path += QStringLiteral(".pdf");
        QString error;
        if (!opc::exportProductInformationPdf(path, doc_, error))
            QMessageBox::warning(this, tr("Export Product Information PDF"), error);
    });

    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = undo_.createUndoAction(this, tr("Undo"));
    undoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    edit->addAction(undoAction);
    auto* redoAction = undo_.createRedoAction(this, tr("Redo"));
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    edit->addAction(redoAction);
    auto* editGeometry = edit->addAction(tr("Edit Geometry…"), this, &MainWindow::editSelectedGeometry);
    editGeometry->setShortcut(QKeySequence(Qt::Key_F2));
    connect(view_, &PatternView::geometryEditRequested, this, &MainWindow::editSelectedGeometry);
    auto* deleteAction = edit->addAction(tr("Delete Selection"), QKeySequence(Qt::Key_Delete),
                                         this, &MainWindow::deleteSelection);
    deleteAction->setEnabled(false);
    const auto updateDelete = [this, deleteAction, editGeometry] {
        const auto selected = selectedObjects();
        deleteAction->setEnabled(!selected.isEmpty());
        editGeometry->setEnabled(selected.size() == 1 && canEditGeometry(selected.front()));
    };
    updateDelete();
    connect(objects_, &QTreeWidget::itemSelectionChanged, this, updateDelete);
    connect(&scene_, &QGraphicsScene::selectionChanged, this, updateDelete);
    connect(&undo_, &QUndoStack::indexChanged, this, updateDelete);
    connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) { setWindowModified(recovered_ || !clean); });

    auto* construction = menuBar()->addMenu(tr("&Construction"));
    auto* modeling = menuBar()->addMenu(tr("&Modeling"));
    auto* grading = menuBar()->addMenu(tr("&Grading"));
    auto* pieces = menuBar()->addMenu(tr("&Pieces"));
    auto* layout = menuBar()->addMenu(tr("&Layout"));
    auto* split = modeling->addAction(tr("Split Line or Curve…"), this, &MainWindow::splitSelectedEdge);
    auto* tangent = modeling->addAction(QStringLiteral("Сопрячь кривую по касательной…"),
                                       this, &MainWindow::alignSelectedCurve);
    auto* join = modeling->addAction(tr("Join Free Endpoints"), this, &MainWindow::joinSelectedEdges);
    join->setToolTip(tr("Select two edges on the same layer and piece. Closes a gap of up to 5 mm."));
    auto* copyCurve = modeling->addAction(tr("Copy Curve as Guide…"), this, [this] {
        const auto selected = selectedObjects();
        if (selected.size() != 1 || doc_.entities()[selected.front()].shape != opc::Shape::Curve)
            return;
        const auto source = doc_.entities()[selected.front()];
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Copy Curve as Guide"));
        QFormLayout form(&dialog);
        QDoubleSpinBox x, y, controlX, controlY;
        for (auto* value : {&x, &y, &controlX, &controlY}) {
            value->setRange(-1000000, 1000000);
            value->setDecimals(3);
            value->setSuffix(tr(" mm"));
        }
        x.setObjectName(QStringLiteral("curveCopyX"));
        controlY.setObjectName(QStringLiteral("curveCopyControlY"));
        QCheckBox reverse(tr("Reverse direction"));
        form.addRow(tr("Horizontal offset"), &x);
        form.addRow(tr("Vertical offset"), &y);
        form.addRow(tr("Additional control point X offset"), &controlX);
        form.addRow(tr("Additional control point Y offset"), &controlY);
        form.addRow(&reverse);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QPointF offset(x.value(), y.value());
        const auto start = (reverse.isChecked() ? source.b : source.a) + offset;
        const auto end = (reverse.isChecked() ? source.a : source.b) + offset;
        const auto control = source.control + offset + QPointF(controlX.value(), controlY.value());
        const opc::Entity guide{opc::Shape::Curve, start, end, control, -1, opc::Layer::Construction};
        undo_.push(new EntityCommand(doc_, *view_, doc_.entities().size(), guide, true, tr("Copy Curve as Guide")));
    });
    connect(modeling, &QMenu::aboutToShow, this, [this, split, join, copyCurve, tangent] {
        const auto selected = selectedObjects();
        const bool edges = std::ranges::all_of(selected, [this](qsizetype index) {
            const auto shape = doc_.entities()[index].shape;
            return shape == opc::Shape::Line || shape == opc::Shape::Curve;
        });
        split->setEnabled(edges && selected.size() == 1);
        join->setEnabled(edges && selected.size() == 2);
        tangent->setEnabled(edges && selected.size() == 2 && std::ranges::any_of(selected, [this](qsizetype i) {
            const auto& edge = doc_.entities()[i];
            return edge.shape == opc::Shape::Curve && edge.draftName.isEmpty();
        }));
        copyCurve->setEnabled(selected.size() == 1 && doc_.entities()[selected.front()].shape == opc::Shape::Curve);
    });

    auto* rotate = modeling->addAction(tr("Rotate Construction…"), this, &MainWindow::rotateConstruction);
    rotate->setToolTip(tr("Select formula points, lines or curves on the Construction layer. Choose a pivot and angle."));
    modeling->setToolTipsVisible(true);
    modeling->addSeparator();
    modeling->addAction(tr("Cut Piece…"), this, [this] {
        modelPiece(opc::ContourOperation::Cut, tr("Cut Piece"));
    });
    modeling->addAction(tr("Cut Along Crossing Guides…"), this, [this] {
        const auto title = tr("Cut Along Crossing Guides");
        const auto guides = selectedObjects();
        if (guides.size() < 2 || guides.size() > opc::maxCrossingGuides) {
            QMessageBox::information(this, title, tr("Select two to eight crossing construction lines or quadratic curves."));
            return;
        }
        const auto piece = choosePiece(title);
        if (piece < 0)
            return;
        auto result = opc::cutCrossingGuides(doc_, piece, guides);
        if (!result.edit) {
            QMessageBox::warning(this, title, result.error);
            return;
        }
        undo_.push(new PatternCommand(doc_, std::move(*result.edit), title, *view_));
    });
    modeling->addAction(tr("Cut by Size Formula…"), this, [this] {
        const auto title = tr("Cut by Size Formula");
        auto selected = selectedObjects();
        std::ranges::sort(selected);
        QVector<QString> guides;
        for (const auto index : selected)
            guides.append(doc_.entities()[index].draftName);
        if (guides.isEmpty() || guides.size() > opc::maxCrossingGuides || guides.contains(QString{})) {
            QMessageBox::information(this, title, tr("Select one to eight named construction guides."));
            return;
        }
        const auto piece = choosePiece(title);
        if (piece < 0)
            return;
        bool accepted = false;
        const auto formula = QInputDialog::getText(this, title,
            tr("Number of active guides, in object-list order: %1").arg(QStringList(guides).join(QStringLiteral(", "))),
            QLineEdit::Normal, QString::number(guides.size()), &accepted);
        if (!accepted)
            return;
        auto candidate = doc_;
        QString error;
        if (!candidate.appendSizeCut(piece, std::move(guides), formula.trimmed(), &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
        applyPatternEdit(std::move(candidate), title, true);
        showModelingHistory();
    });
    modeling->addAction(tr("Relief Seam…"), this, [this] {
        modelPiece(opc::ContourOperation::Relief, tr("Relief Seam"));
    });
    modeling->addAction(tr("Yoke…"), this, [this] {
        modelPiece(opc::ContourOperation::Yoke, tr("Yoke"));
    });
    auto* spreadAction = modeling->addAction(tr("Slash and Spread…"), this, [this] {
        modelPiece(opc::ContourOperation::Spread, tr("Slash and Spread"));
    });
    modeling->addAction(QStringLiteral("Раздвижка по нескольким направляющим…"), this, &MainWindow::drapePiece);
    modeling->addAction(tr("Close and Transfer Dart…"), this, [this] {
        modelPiece(opc::ContourOperation::TransferDartToOutlet, tr("Close and Transfer Dart"));
    });
    modeling->addAction(tr("Move Open Contour Dart…"), this, [this] {
        modelPiece(opc::ContourOperation::MoveOpenDart, tr("Move Open Contour Dart"));
    });
    modeling->addAction(QStringLiteral("Разделить вытачку…"), this, [this] {
        modelPiece(opc::ContourOperation::SplitDart, QStringLiteral("Разделить вытачку"));
    });
    modeling->addAction(QStringLiteral("Объединить вытачки…"), this, [this] {
        modelPiece(opc::ContourOperation::MergeDarts, QStringLiteral("Объединить вытачки"));
    });
    modeling->addSeparator();
    modeling->addAction(tr("Smooth Formula Curve Chain…"), this, [this] {
        const auto title = tr("Smooth Formula Curve Chain");
        const auto edges = selectedObjects();
        const auto chain = opc::measureSeamSection(doc_, edges, true);
        if (!chain || edges.size() < 2 || edges.size() > 32
            || std::ranges::none_of(edges, [this](qsizetype index) { return doc_.entities()[index].shape == opc::Shape::Curve; })) {
            QMessageBox::information(this, title, tr("Select two to 32 contiguous seam edges including a quadratic curve."));
            return;
        }
        if (chain->closed) {
            auto candidate = doc_;
            QString error;
            if (candidate.smoothCurveChain(edges, 0, 0, &error))
                applyPatternEdit(std::move(candidate), title, true);
            else
                QMessageBox::warning(this, title, error);
            return;
        }
        const auto start = chain->edges.front();
        const auto end = chain->edges.back();
        const auto& first = doc_.entities()[start.index];
        const auto& last = doc_.entities()[end.index];
        const auto a = start.reversed ? first.b : first.a;
        const auto b = end.reversed ? last.a : last.b;
        const auto direction = [](QPointF vector) {
            return std::atan2(vector.y(), vector.x()) * (180 / std::numbers::pi);
        };
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        QFormLayout fields(&dialog);
        QDoubleSpinBox from(&dialog), to(&dialog);
        for (auto* field : {&from, &to}) {
            field->setRange(-360, 360);
            field->setDecimals(3);
            field->setSuffix(QStringLiteral("°"));
        }
        from.setValue(direction((first.shape == opc::Shape::Curve ? first.control : start.reversed ? first.a : first.b) - a));
        to.setValue(direction(b - (last.shape == opc::Shape::Curve ? last.control : end.reversed ? last.b : last.a)));
        from.setEnabled(first.shape == opc::Shape::Curve);
        to.setEnabled(last.shape == opc::Shape::Curve);
        fields.addRow(tr("Start at %1").arg(first.draftName), &from);
        fields.addRow(tr("End at %1").arg(last.draftName), &to);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        fields.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            auto candidate = doc_;
            QString error;
            if (!candidate.smoothCurveChain(edges, from.value(), to.value(), &error)) {
                QMessageBox::warning(&dialog, title, error);
                return;
            }
            applyPatternEdit(std::move(candidate), title, true);
            dialog.accept();
        });
        dialog.exec();
    });
    modeling->addAction(tr("Draft Stand from Neckline…"), this, [this] {
        const auto title = tr("Draft Stand from Neckline");
        const auto edges = selectedObjects();
        if (edges.isEmpty()) {
            QMessageBox::information(this, title, tr("Select the edges of a formula neckline."));
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        QFormLayout fields(&dialog);
        QDoubleSpinBox depth(&dialog);
        depth.setRange(1, 300);
        depth.setValue(30);
        depth.setSuffix(tr(" mm"));
        fields.addRow(tr("Stand depth"), &depth);
        QDoubleSpinBox rise(&dialog);
        rise.setRange(0, 100);
        rise.setSuffix(tr(" mm"));
        rise.setSpecialValueText(tr("Straight"));
        fields.addRow(tr("Centre rise"), &rise);
        QSpinBox notches(&dialog);
        notches.setRange(0, 20);
        fields.addRow(tr("Paired notches"), &notches);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        fields.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            auto candidate = doc_;
            QString error;
            if (!opc::addLinkedNeckStand(candidate, edges, depth.value(), notches.value(), &error, rise.value())) {
                QMessageBox::warning(&dialog, title, error);
                return;
            }
            applyPatternEdit(std::move(candidate), title, true);
            view_->fitDrawing();
            dialog.accept();
        });
        dialog.exec();
    });
    modeling->addAction(tr("Draft Sleeve from Armholes…"), this, [this] {
        const auto title = tr("Draft Sleeve from Armholes");
        QMap<qsizetype, QVector<qsizetype>> byPiece;
        for (const auto index : selectedObjects())
            byPiece[doc_.entities()[index].piece].append(index);
        if (byPiece.size() != 2 || byPiece.firstKey() < 0) {
            QMessageBox::information(this, title, tr("Select a continuous chain on each of two armhole pieces."));
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        QFormLayout fields(&dialog);
        QComboBox frontPiece(&dialog);
        QVector<QVector<qsizetype>> chains;
        QVector<QComboBox*> starts;
        for (auto it = byPiece.cbegin(); it != byPiece.cend(); ++it) {
            const auto chain = opc::measureSeamSection(doc_, it.value());
            if (!chain) {
                QMessageBox::warning(this, title, tr("Each armhole selection must be one open chain."));
                return;
            }
            const auto name = doc_.pieces()[it.key()].name;
            frontPiece.addItem(name);
            chains.append(it.value());
            auto* start = new QComboBox(&dialog);
            const auto pointLabel = [this](const opc::SeamEdge& edge, bool isStart) {
                const auto& entity = doc_.entities()[edge.index];
                const auto point = (isStart == edge.reversed) ? entity.b : entity.a;
                return tr("%1 (%2, %3 mm)").arg(entity.draftName)
                    .arg(point.x(), 0, 'f', 1).arg(point.y(), 0, 'f', 1);
            };
            start->addItem(pointLabel(chain->edges.front(), true));
            start->addItem(pointLabel(chain->edges.back(), false));
            starts.append(start);
            fields.addRow(tr("Shoulder end · %1").arg(name), start);
        }
        fields.addRow(tr("Front armhole piece"), &frontPiece);
        const auto dimension = [&](const QString& label, double minimum, double maximum, double value) {
            auto* field = new QDoubleSpinBox(&dialog);
            field->setRange(minimum, maximum);
            field->setValue(value);
            field->setSuffix(tr(" mm"));
            fields.addRow(label, field);
            return field;
        };
        const auto* length = dimension(tr("Sleeve length"), 1, 3000, 600);
        const auto* rise = dimension(tr("Cap height"), 1, 2999, 120);
        const auto* hem = dimension(tr("Hem width"), 1, 1000, 240);
        const auto* frontEase = dimension(tr("Front cap ease"), 0, 200, 4);
        const auto* backEase = dimension(tr("Back cap ease"), 0, 200, 4);
        QSpinBox notches(&dialog);
        notches.setRange(0, 20);
        notches.setValue(1);
        fields.addRow(tr("Notch pairs per cap section"), &notches);
        QCheckBox twoPiece(tr("Two-piece sleeve"), &dialog);
        fields.addRow(&twoPiece);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        fields.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            auto candidate = doc_;
            QString error;
            const int front = frontPiece.currentIndex();
            const int back = 1 - front;
            const opc::LinkedSleeveMeasurements measurements{length->value(), rise->value(), hem->value(),
                frontEase->value(), backEase->value(), notches.value(), twoPiece.isChecked()};
            if (!opc::addLinkedSleeve(candidate, chains[front], chains[back], measurements,
                    starts[front]->currentIndex() == 1, starts[back]->currentIndex() == 1, &error)) {
                QMessageBox::warning(&dialog, title, error);
                return;
            }
            applyPatternEdit(std::move(candidate), title, true);
            view_->fitDrawing();
            dialog.accept();
        });
        dialog.exec();
    });
    modeling->addAction(tr("Draft Pocket Bag from Seam…"), this, [this] {
        const auto title = tr("Draft Pocket Bag from Seam");
        const auto edges = selectedObjects();
        if (edges.size() != 1) {
            QMessageBox::information(this, title, tr("Select one straight formula edge that defines the pocket opening."));
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        QFormLayout fields(&dialog);
        QDoubleSpinBox depth(&dialog);
        depth.setRange(30, 500);
        depth.setValue(180);
        depth.setSuffix(tr(" mm"));
        fields.addRow(tr("Pocket depth"), &depth);
        QSpinBox notches(&dialog);
        notches.setRange(0, 20);
        notches.setValue(1);
        fields.addRow(tr("Paired notches"), &notches);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        fields.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            auto candidate = doc_;
            QString error;
            if (!opc::addLinkedSeamPocket(candidate, edges.front(), depth.value(), notches.value(), &error)) {
                QMessageBox::warning(&dialog, title, error);
                return;
            }
            applyPatternEdit(std::move(candidate), title, true);
            view_->fitDrawing();
            dialog.accept();
        });
        dialog.exec();
    });
    modeling->addAction(tr("Shape Silhouette…"), this, &MainWindow::shapeSilhouette);
    auto* constructionTreeAction = modeling->addAction(tr("Construction Tree…"), this, &MainWindow::showModelingHistory);
    constructionTreeAction->setShortcut(QKeySequence(Qt::Key_F12));
    modeling->addAction(tr("Add Box Pleat…"), this, [this] {
        modelPiece(opc::ContourOperation::Pleat, tr("Add Box Pleat"));
    });
    modeling->addAction(tr("Add Pleat with Cut Extensions…"), this, [this] {
        modelPiece(opc::ContourOperation::PleatWithTabs, tr("Add Pleat with Cut Extensions"));
    });
    modeling->addAction(tr("Add Gathering Fullness…"), this, [this] {
        modelPiece(opc::ContourOperation::Gather, tr("Add Gathering Fullness"));
    });

    auto* createPieceAction = pieces->addAction(tr("Create Piece from Selection…"), this, &MainWindow::createPiece);
    pieces->addAction(tr("Assign Selection to Piece…"), this, &MainWindow::assignPiece);
    pieces->addAction(tr("Remove Selection from Piece"), this, [this] {
        auto updated = doc_;
        bool changed = false;
        for (const auto index : selectedObjects()) {
            auto entity = updated.entities()[index];
            changed |= entity.piece >= 0;
            entity.piece = -1;
            updated.replace(index, entity);
        }
        if (changed)
            applyPatternEdit(std::move(updated), tr("Remove Objects from Piece"));
    });
    auto* allowanceAction = pieces->addAction(tr("Piece, Contour and Allowance…"), this, &MainWindow::managePiece);
    auto* templateAction = construction->addAction(tr("New Pattern from Template…"), this, [this] {
        if (auto generated = createPatternFromTemplate(this); generated && confirmDiscard()) {
            replaceDocument({}, {});
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(*generated), tr("Create Pattern from Template"), view_));
            view_->fitDrawing();
        }
    });
    auto* library = construction->addMenu(tr("Construction Library"));
    library->addAction(tr("Save Construction to Library…"), this, [this] {
        if (doc_.parametricDraft().points.isEmpty()) {
            QMessageBox::information(this, tr("Construction Library"), tr("Create a parametric draft first."));
            return;
        }
        QSettings settings;
        auto path = QFileDialog::getSaveFileName(this, tr("Save Construction to Library"),
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".sofit"), Qt::CaseInsensitive))
            path += QStringLiteral(".sofit");
        QString error;
        if (doc_.save(path, &error))
            settings.setValue(QStringLiteral("construction/libraryFolder"), QFileInfo(path).absolutePath());
        else
            QMessageBox::warning(this, tr("Save Construction to Library"), error);
    });
    library->addAction(tr("Preview Library Construction…"), this, [this] {
        QSettings settings;
        const auto title = tr("Preview Library Construction");
        const auto path = QFileDialog::getOpenFileName(this, title,
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument source;
        QString error;
        if (!source.load(path, &error) || source.parametricDraft().points.isEmpty()) {
            QMessageBox::warning(this, title, error.isEmpty()
                ? tr("The selected project has no parametric draft.") : error);
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);
        QVBoxLayout layout(&dialog);
        QLabel name(QFileInfo(path).completeBaseName(), &dialog);
        name.setTextFormat(Qt::PlainText);
        layout.addWidget(&name);
        QComboBox sizes(&dialog);
        fillExportSizes(sizes, source);
        layout.addWidget(&sizes);
        QGraphicsScene scene;
        QGraphicsView drawing(&scene, &dialog);
        drawing.setRenderHint(QPainter::Antialiasing);
        drawing.setDragMode(QGraphicsView::ScrollHandDrag);
        layout.addWidget(&drawing, 1);
        QLabel details(&dialog);
        layout.addWidget(&details);
        QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
        layout.addWidget(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const auto showSize = [&] {
            const auto graded = opc::gradedPattern(source, sizes.currentData().toString());
            scene.clear();
            QPen outline(drawing.palette().color(QPalette::Text));
            QPen guide(drawing.palette().color(QPalette::Text), 0, Qt::DashLine);
            outline.setCosmetic(true);
            for (const auto& entity : graded.entities()) {
                if (entity.shape != opc::Shape::Point)
                    scene.addPath(opc::entityPath(entity), entity.layer == opc::Layer::Main ? outline : guide);
            }
            const auto bounds = scene.itemsBoundingRect();
            scene.setSceneRect(bounds.adjusted(-5, -5, 5, 5));
            drawing.fitInView(scene.sceneRect(), Qt::KeepAspectRatio);
            details.setText(tr("Pieces: %1 · Measurements: %2")
                .arg(graded.pieces().size()).arg(source.parametricDraft().measurements.size()));
        };
        connect(&sizes, &QComboBox::currentIndexChanged, &dialog, showSize);
        dialog.resize(680, 550);
        QMetaObject::invokeMethod(&dialog, showSize, Qt::QueuedConnection);
        dialog.exec();
    });
    library->addAction(tr("New Pattern from Library…"), this, [this] {
        QSettings settings;
        const auto path = QFileDialog::getOpenFileName(this, tr("New Pattern from Library"),
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument construction;
        QString error;
        if (!construction.load(path, &error) || construction.parametricDraft().points.isEmpty()) {
            QMessageBox::warning(this, tr("Construction Library"), error.isEmpty()
                ? tr("The selected project has no parametric draft.") : error);
            return;
        }
        if (!confirmDiscard())
            return;
        settings.setValue(QStringLiteral("construction/libraryFolder"), QFileInfo(path).absolutePath());
        replaceDocument({}, {});
        undo_.push(new PatternCommand(doc_, std::move(construction), tr("Create Pattern from Library"), view_));
        view_->fitDrawing();
    });
    library->addAction(tr("Insert Construction into Current Pattern…"), this, [this] {
        QSettings settings;
        const auto path = QFileDialog::getOpenFileName(this, tr("Insert Construction into Current Pattern"),
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument source;
        auto updated = doc_;
        QString error;
        if (!source.load(path, &error) || !updated.insertConstruction(source, &error)) {
            QMessageBox::warning(this, tr("Construction Library"), error);
            return;
        }
        settings.setValue(QStringLiteral("construction/libraryFolder"), QFileInfo(path).absolutePath());
        applyPatternEdit(std::move(updated), tr("Insert Library Construction"));
        view_->fitDrawing();
    });
    library->addAction(QStringLiteral("Вставить конструкцию с привязкой мерок…"), this, [this] {
        const auto title = QStringLiteral("Привязка мерок конструкции");
        QSettings settings;
        const auto path = QFileDialog::getOpenFileName(this, title,
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument source;
        QString error;
        if (!source.load(path, &error) || source.parametricDraft().measurements.isEmpty()) {
            QMessageBox::warning(this, title, error.isEmpty() ? QStringLiteral("У конструкции нет мерок.") : error);
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(source.parametricDraft().measurements.size(), 2);
        layout->addWidget(table);
        table->setHorizontalHeaderLabels({QStringLiteral("Мерка конструкции"), QStringLiteral("Мерка проекта")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        QVector<QComboBox*> fields;
        for (qsizetype i = 0; i < source.parametricDraft().measurements.size(); ++i) {
            const auto& measurement = source.parametricDraft().measurements[i];
            auto* label = new QTableWidgetItem(QStringLiteral("%1 · %2 мм").arg(measurement.name).arg(measurement.mm));
            label->setFlags(label->flags() & ~Qt::ItemIsEditable);
            table->setItem(i, 0, label);
            auto* field = new QComboBox;
            field->addItem(QStringLiteral("Автоматически"), QString{});
            for (const auto& target : doc_.parametricDraft().measurements)
                field->addItem(target.name, target.name);
            fields.append(field);
            table->setCellWidget(i, 1, field);
        }
        auto* track = new QCheckBox(QStringLiteral("Отслеживать редакции этой конструкции"), &dialog);
        layout->addWidget(track);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            QMap<QString, QString> bindings;
            for (qsizetype i = 0; i < fields.size(); ++i) {
                const auto target = fields[i]->currentData().toString();
                if (!target.isEmpty())
                    bindings.insert(source.parametricDraft().measurements[i].name, target);
            }
            auto updated = doc_;
            const QFileInfo sourceFile(path);
            const auto libraryName = track->isChecked() ? sourceFile.fileName() : QString{};
            const auto sourcePath = track->isChecked() ? sourceFile.absoluteFilePath() : QString{};
            if (!updated.insertConstruction(source, &error, bindings, libraryName, sourcePath)) {
                QMessageBox::warning(&dialog, title, error);
                return;
            }
            settings.setValue(QStringLiteral("construction/libraryFolder"), QFileInfo(path).absolutePath());
            applyPatternEdit(std::move(updated), title, true);
            view_->fitDrawing();
            dialog.accept();
        });
        dialog.resize(580, 420);
        dialog.exec();
    });
    library->addAction(QStringLiteral("Обновить экземпляр конструкции…"), this, [this] {
        const auto title = QStringLiteral("Обновление экземпляра конструкции");
        const auto& instances = doc_.libraryInstances();
        if (instances.isEmpty()) {
            QMessageBox::information(this, title,
                QStringLiteral("В проекте нет отслеживаемых экземпляров. Включите отслеживание при вставке с привязкой мерок."));
            return;
        }
        QStringList choices;
        for (qsizetype i = 0; i < instances.size(); ++i)
            choices.append(QStringLiteral("%1 · %2 · %3").arg(i + 1)
                .arg(instances[i].sourceName, instances[i].revision.left(12)));
        bool accepted = false;
        const auto selected = QInputDialog::getItem(this, title, QStringLiteral("Экземпляр проекта"),
                                                   choices, 0, false, &accepted);
        if (!accepted)
            return;
        QSettings settings;
        const auto path = QFileDialog::getOpenFileName(this, title,
            settings.value(QStringLiteral("construction/libraryFolder")).toString(), tr("SOFIT constructions (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument source;
        QString error;
        auto candidate = doc_;
        if (!source.load(path, &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
        const auto index = choices.indexOf(selected);
        if (!candidate.updateLibraryConstruction(index, source, &error)) {
            const auto& installed = instances[index];
            const auto& draft = source.parametricDraft();
            QStringList missing;
            const auto collect = [&](const auto& bindings, const auto& definitions) {
                for (const auto& name : bindings.keys()) {
                    if (std::ranges::none_of(definitions, [&](const auto& item) { return item.name == name; }))
                        missing.append(name);
                }
            };
            collect(installed.parameters, draft.parameters);
            collect(installed.points, draft.points);
            collect(installed.edges, draft.edges);
            const bool newOutline = std::ranges::any_of(source.entities(), [&](const auto& entity) {
                return entity.piece >= 0 && entity.layer == opc::Layer::Main
                    && !entity.draftName.startsWith(u'@') && !installed.edges.contains(entity.draftName);
            });
            if (missing.isEmpty() && !newOutline) {
                QMessageBox::warning(this, title, error);
                return;
            }
            const auto mapping = QInputDialog::getMultiLineText(this, title,
                QStringLiteral("%1\nЗаменяемые имена: %2. Укажите «старое = новое». Для разделения или слияния перечислите рёбра по порядку через «+».")
                    .arg(error, missing.join(QStringLiteral(", "))), {}, &accepted);
            if (!accepted)
                return;
            QMap<QString, QString> replacements;
            QMap<QString, QStringList> splits;
            QMap<QString, QStringList> merges;
            bool valid = true;
            for (const auto& line : mapping.split(u'\n', Qt::SkipEmptyParts)) {
                const auto fields = line.split(u'=');
                if (fields.size() != 2 || fields[0].trimmed().isEmpty() || fields[1].trimmed().isEmpty()
                    || replacements.contains(fields[0].trimmed()) || splits.contains(fields[0].trimmed())) {
                    valid = false;
                    break;
                }
                auto origins = fields[0].split(u'+');
                for (auto& origin : origins)
                    origin = origin.trimmed();
                auto targets = fields[1].split(u'+');
                for (auto& target : targets)
                    target = target.trimmed();
                if (targets.contains(QString{}) || origins.contains(QString{})
                    || (origins.size() > 1 && (targets.size() != 1 || merges.contains(targets.front())))) {
                    valid = false;
                    break;
                }
                if (origins.size() > 1)
                    merges.insert(targets.front(), origins);
                else if (targets.size() == 1)
                    replacements.insert(fields[0].trimmed(), targets.front());
                else
                    splits.insert(fields[0].trimmed(), targets);
            }
            if (!valid || (replacements.isEmpty() && splits.isEmpty() && merges.isEmpty())) {
                QMessageBox::warning(this, title, QStringLiteral("Укажите каждую замену один раз в виде «старое = новое»."));
                return;
            }
            if (!candidate.updateLibraryConstruction(index, source, &error, replacements, splits, merges)) {
                QMessageBox::warning(this, title, error);
                return;
            }
        }
        settings.setValue(QStringLiteral("construction/libraryFolder"), QFileInfo(path).absolutePath());
        applyPatternEdit(std::move(candidate), title, true);
        view_->fitDrawing();
    });
    library->addAction(QStringLiteral("Обновить цепочку конструкций"), this, [this] {
        const auto title = QStringLiteral("Обновить цепочку конструкций");
        if (doc_.libraryInstances().isEmpty()) {
            QMessageBox::information(this, title, QStringLiteral("В проекте нет отслеживаемых конструкций."));
            return;
        }
        auto candidate = doc_;
        QString error;
        if (!candidate.refreshLibraryDependencies(currentPath_, &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
        if (candidate.libraryInstances() != doc_.libraryInstances()) {
            applyPatternEdit(std::move(candidate), title, true);
            view_->fitDrawing();
        }
    });
    autoSyncLibrary_ = library->addAction(QStringLiteral("Автоматически обновлять связанные конструкции"));
    autoSyncLibrary_->setCheckable(true);
    connect(autoSyncLibrary_, &QAction::toggled, this, [this] { syncLibraryDependencies(); });
    library->addAction(QStringLiteral("Применить операции из проекта…"), this, [this] {
        const auto title = QStringLiteral("Применение последовательности операций");
        const auto path = QFileDialog::getOpenFileName(this, title, {}, tr("SOFIT (*.sofit)"));
        if (path.isEmpty())
            return;
        opc::PatternDocument source;
        auto updated = doc_;
        QString error;
        if (!source.load(path, &error) || !updated.applyModelingSequence(source, &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
        applyPatternEdit(std::move(updated), title);
        view_->fitDrawing();
    });
    library->addAction(QStringLiteral("Справка по библиотеке конструкций"), this, [this] {
        QMessageBox::information(this, QStringLiteral("Библиотека конструкций"),
            QStringLiteral("Для повторного применения сохраните конструкцию с нужными операциями в .sofit. "
                           "Откройте совместимую конструкцию и выберите «Применить операции из проекта». "
                           "Имена исходных объектов и связи направляющих должны совпадать. Если в текущем "
                           "проекте есть операции, они должны совпадать с началом исходной последовательности. "
                           "Применённая последовательность отменяется и повторяется целиком."));
    });
    auto* draftAction = construction->addAction(tr("Parametric Draft…"), this, [this] {
        QString calculationError;
        auto updated = editParametricDraft(this, doc_, &calculationError);
        setModelingError(calculationError);
        if (updated) {
            view_->cancelDrawing();
            const bool firstDrawing = doc_.entities().isEmpty();
            undo_.push(new PatternCommand(doc_, std::move(*updated), tr("Edit Parametric Draft"), view_));
            if (firstDrawing && !doc_.entities().isEmpty())
                view_->fitDrawing();
        }
    });
    auto* sizesAction = grading->addAction(tr("Sizes, Measurements and Grade Rules…"), this, [this] {
        if (auto production = editGrading(this, doc_)) {
            if (production->useDraftMeasurements == doc_.production().useDraftMeasurements
                && opc::saveProduction(*production) == opc::saveProduction(doc_.production()))
                return;
            auto updated = doc_;
            updated.production() = std::move(*production);
            applyPatternEdit(std::move(updated), tr("Edit Sizes and Grade Rules"));
        }
    });
    auto* layoutAction = layout->addAction(tr("Fabric Layout…"), this, [this] {
        if (auto production = editFabricLayout(this, doc_)) {
            if (opc::saveProduction(*production) == opc::saveProduction(doc_.production()))
                return;
            auto updated = doc_;
            updated.production() = std::move(*production);
            applyPatternEdit(std::move(updated), tr("Edit Fabric Layout"));
        }
    });
    layout->addAction(QStringLiteral("Многоматериальный настил…"), this,
                      [this] { exportMaterialLay(this, doc_); });
    layout->addAction(QStringLiteral("Спланировать заказ по размерам…"), this,
                      [this] { planMaterialOrder(this, doc_); });
    layout->addAction(QStringLiteral("Открыть производственный заказ…"), this, [this] { reopenMaterialOrder(this); });
    const auto importMarkerModels = QStringLiteral("Добавить модели в раскладку…");
    layout->addAction(importMarkerModels, this, [this, importMarkerModels] {
        const auto paths = QFileDialog::getOpenFileNames(this, importMarkerModels, {},
                                                        QStringLiteral("SOFIT (*.sofit)"));
        if (paths.isEmpty())
            return;
        auto updated = doc_;
        for (const auto& path : paths) {
            opc::PatternDocument source;
            QString error;
            if (!source.load(path, &error)) {
                QMessageBox::warning(this, importMarkerModels, error);
                return;
            }
            const auto model = QFileInfo(path).completeBaseName();
            QStringList labels{QStringLiteral("Базовый вариант")};
            for (const auto& entry : source.production().sizes)
                labels.append(QStringLiteral("%1. %2").arg(labels.size()).arg(entry.name));
            bool accepted = false;
            const auto label = QInputDialog::getItem(this, importMarkerModels,
                QStringLiteral("Размер модели «%1»").arg(model), labels, 0, false, &accepted);
            if (!accepted)
                return;
            const auto index = labels.indexOf(label);
            const auto size = index > 0 ? source.production().sizes[index - 1].name : QString{};
            const int kits = QInputDialog::getInt(this, importMarkerModels,
                QStringLiteral("Комплектов модели «%1»").arg(model), 1, 1,
                int(opc::maxAutomaticCopies), 1, &accepted);
            if (!accepted)
                return;
            if (!opc::appendMarkerModel(updated, source, size, model, kits, error)) {
                QMessageBox::warning(this, importMarkerModels, error);
                return;
            }
        }
        auto production = editFabricLayout(this, updated);
        if (!production)
            return;
        updated.production() = std::move(*production);
        applyPatternEdit(std::move(updated), importMarkerModels, true);
    });
    pieces->addAction(QStringLiteral("Part Assistant…"), this, [=, this] {
        diagnoseGradedPieces({finishedMeasurements, techPackOptions, exportTechPack});
    });
    auto* diagnoseAction = grading->addAction(tr("Diagnose Pieces by Size…"), this, [this] {
        diagnoseGradedPieces();
    });
    diagnoseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    auto* seams = pieces->addMenu(tr("Seams"));
    seams->addAction(tr("Measure Selected Lines and Curves"), this, &MainWindow::measureSelection);
    seams->addAction(tr("Check Fitting Block by Size…"), this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Check Fitting Block by Size"));
        auto* form = new QFormLayout(&dialog);
        auto* size = new QComboBox(&dialog);
        fillExportSizes(*size, doc_);
        form->addRow(tr("Size"), size);
        auto* tolerance = new QDoubleSpinBox(&dialog);
        tolerance->setSuffix(tr(" mm"));
        tolerance->setValue(3);
        form->addRow(tr("Tolerance"), tolerance);
        auto* capEase = new QDoubleSpinBox(&dialog);
        capEase->setSuffix(tr(" mm"));
        capEase->setValue(4);
        auto* overrideEase = new QCheckBox(tr("Override cap ease for this check"), &dialog);
        capEase->setEnabled(false);
        form->addRow(overrideEase, capEase);
        connect(overrideEase, &QCheckBox::toggled, capEase, &QWidget::setEnabled);
        auto* report = new QPlainTextEdit(&dialog);
        report->setReadOnly(true);
        form->addRow(report);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);

        const auto refresh = [&] {
            const auto ease = overrideEase->isChecked() ? std::optional{capEase->value()} : std::nullopt;
            const auto check = opc::checkFittingBlock(doc_, size->currentData().toString(), ease);
            QStringList lines;
            lines.append(check.invalidPieces.isEmpty()
                ? (doc_.pieces().isEmpty() ? tr("No pieces to check.") : tr("All piece contours are closed and simple."))
                : tr("Invalid contours: %1").arg(check.invalidPieces.join(", ")));
            if (check.seams.isEmpty() && check.missingSeams.isEmpty())
                lines.append(tr("No fitting block seam pairs found."));
            for (const auto& missing : check.missingSeams)
                lines.append(tr("%1: could not measure both seams.").arg(missing));
            for (const auto& seam : check.seams) {
                const double difference = seam.second - seam.first;
                const auto mm = [](double value) { return QLocale().toString(value, 'f', 2); };
                lines.append(tr("%1: %2 / %3 mm, difference %4 mm, target %5 mm — %6")
                    .arg(seam.label, mm(seam.first), mm(seam.second), mm(difference), mm(seam.targetDifference),
                         std::abs(difference - seam.targetDifference) <= tolerance->value()
                             ? tr("within tolerance") : tr("outside tolerance")));
            }
            report->setPlainText(lines.join("\n"));
        };
        connect(size, &QComboBox::currentIndexChanged, &dialog, refresh);
        connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        connect(overrideEase, &QCheckBox::toggled, &dialog, refresh);
        connect(capEase, &QDoubleSpinBox::valueChanged, &dialog, refresh);
        refresh();
        dialog.resize(680, 340);
        dialog.exec();
    });

    seams->addAction(tr("Match Selected Seam Sections…"), this, [this] { matchSeams(selectedObjects()); });
    seams->addAction(tr("Allowance for Selected Edges…"), this, [this] {
        editEdgeAllowance(selectedObjects(), this);
    });
    seams->addAction(tr("Clear Automatic Notches on Selected Edges"), this, &MainWindow::clearEdgeNotches);
    auto* overlay = grading->addMenu(tr("Size Overlays"));
    connect(overlay, &QMenu::aboutToShow, this, [this, overlay] {
        overlay->clear();
        const auto& sizes = doc_.production().sizes;
        if (sizes.isEmpty())
            overlay->addAction(tr("No sizes defined"))->setEnabled(false);
        for (qsizetype i = 0; i < sizes.size(); ++i) {
            const auto name = sizes[i].name;
            QPixmap swatch(14, 14);
            swatch.fill(sizeColor(i));
            auto* action = overlay->addAction(QIcon(swatch), name);
            action->setCheckable(true);
            action->setChecked(overlaySizes_.contains(name));
            connect(action, &QAction::toggled, this, [this, name](bool visible) {
                if (visible)
                    overlaySizes_.insert(name);
                else
                    overlaySizes_.remove(name);
                refreshPieceDisplay();
            });
        }
    });
    construction->addSeparator();
    construction->addAction(tr("Move Selection to Main Layer"), this,
                       [this] { setSelectedLayer(opc::Layer::Main); });
    construction->addAction(tr("Move Selection to Construction Layer"), this,
                       [this] { setSelectedLayer(opc::Layer::Construction); });

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(dock->toggleViewAction());
    auto* layers = viewMenu->addMenu(tr("Layers"));
    const std::array layerNames{tr("Main Lines"), tr("Construction Lines"), tr("Seam Allowances")};
    for (int index = 0; index < static_cast<int>(layerNames.size()); ++index) {
        auto* action = layers->addAction(layerNames[index]);
        layerActions_[index] = action;
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::triggered, this, [this, index](bool visible) {
            auto updated = doc_;
            updated.setLayerVisible(static_cast<opc::Layer>(index), visible);
            applyPatternEdit(std::move(updated), tr("Change Layer Visibility"));
        });
    }
    auto* objectSnap = viewMenu->addAction(tr("Snap to Objects"));
    objectSnap->setCheckable(true);
    objectSnap->setChecked(true);
    objectSnap->setToolTip(tr("Snap to points, corners and straight-edge intersections"));
    connect(objectSnap, &QAction::toggled, view_, &PatternView::setSnapToObjects);
    auto* snapAction = viewMenu->addAction(tr("Snap to Grid (10 mm)"));
    snapAction->setCheckable(true);
    connect(snapAction, &QAction::toggled, view_, &PatternView::setSnapToGrid);
    auto* fitAction = viewMenu->addAction(tr("Fit Drawing"), QKeySequence(Qt::Key_F),
                                           view_, &PatternView::fitDrawing);
    viewMenu->addAction(tr("Zoom In"), QKeySequence::ZoomIn, view_, [this] { view_->zoomAtCenter(1.2); });
    viewMenu->addAction(tr("Zoom Out"), QKeySequence::ZoomOut, view_, [this] { view_->zoomAtCenter(1 / 1.2); });
    auto* cancelAction = new QAction(this);
    cancelAction->setText(tr("Cancel Drawing"));
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelAction, &QAction::triggered, this, [this] {
        view_->cancelDrawing();
        scene_.clearSelection();
        objects_->clearSelection();
    });
    addAction(cancelAction);

    auto* toolbar = new QToolBar(tr("Drawing Tools"), this);
    addToolBar(Qt::LeftToolBarArea, toolbar);
    toolbar->setObjectName(QStringLiteral("drawingTools"));
    viewMenu->addAction(toolbar->toggleViewAction());
    toolbar->setAllowedAreas(Qt::LeftToolBarArea | Qt::RightToolBarArea);
    auto* tools = new QActionGroup(this);
    const auto addTool = [this, toolbar, tools](const QString& text, PatternView::Tool tool, const QString& hint) {
        auto* action = toolbar->addAction(text);
        action->setCheckable(true);
        tools->addAction(action);
        action->setToolTip(hint);
        connect(action, &QAction::triggered, view_, [this, tool, hint] {
            view_->setTool(tool);
            statusBar()->showMessage(hint);
        });
        return action;
    };
    addTool(tr("Select"), PatternView::Tool::Select, tr("Select or drag an object"))->setChecked(true);
    addTool(tr("Point"), PatternView::Tool::Point, tr("Click to place a point"));
    addTool(tr("Line"), PatternView::Tool::Line, tr("Pick the two endpoints"));
    addTool(tr("Rectangle"), PatternView::Tool::Rectangle, tr("Pick opposite corners"));
    addTool(tr("Circle"), PatternView::Tool::Circle, tr("Pick the center, then a point on the circle"));
    addTool(tr("Curve"), PatternView::Tool::Curve, tr("Pick both endpoints, then the Bézier control point"));
    addTool(tr("Dart"), PatternView::Tool::Dart, tr("Pick the first leg end, apex, then the second leg end"));
    auto* measureAction = addTool(tr("Measure"), PatternView::Tool::Measure,
                                  tr("Pick two points to measure the distance"));
    measureAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));

    auto* drawing = construction->addMenu(tr("Drawing Tools"));
    drawing->addActions(toolbar->actions());

    auto* projectToolbar = addToolBar(tr("Project and View"));
    projectToolbar->setObjectName(QStringLiteral("projectAndView"));
    viewMenu->addAction(projectToolbar->toggleViewAction());
    projectToolbar->addActions({newAction, openAction, saveAction});
    projectToolbar->addSeparator();
    projectToolbar->addAction(undoAction);
    projectToolbar->addAction(redoAction);
    projectToolbar->addAction(deleteAction);
    auto* exactAction = construction->addAction(tr("Exact Input…"), QKeySequence(Qt::Key_I),
                                                this, &MainWindow::exactInput);
    projectToolbar->addAction(exactAction);
    projectToolbar->addSeparator();
    projectToolbar->addAction(snapAction);
    projectToolbar->addAction(objectSnap);
    projectToolbar->addAction(fitAction);
    installWorkflow(*this, {construction, modeling, grading, pieces, layout}, *viewMenu, {
        {"template", templateAction}, {"draft", draftAction}, {"spread", spreadAction},
        {"sizes", sizesAction}, {"createPiece", createPieceAction}, {"allowance", allowanceAction},
        {"layout", layoutAction}, {"exactInput", exactAction}, {"fit", fitAction},
        {"editGeometry", editGeometry}, {"measure", measureAction},
        {"constructionTree", constructionTreeAction}, {"diagnosePieces", diagnoseAction}
    });
    connect(view_, &PatternView::selectionMenuRequested, this,
            [this, editGeometry, deleteAction, fitAction](const QPoint& position) {
        QMenu context(this);
        context.addActions({editGeometry, deleteAction, fitAction});
        context.exec(position);
    });
    const auto defaultWorkspace = saveState(1);
    viewMenu->addAction(tr("Reset Panel Layout"), this, [this, defaultWorkspace] {
        restoreState(defaultWorkspace, 1);
        if (auto* workflow = findChild<QToolBar*>(QStringLiteral("workflowToolbar")))
            workflow->show();
        QSettings().setValue(QStringLiteral("workflow/showToolbar"), true);
        if (modelingDock_) {
            modelingDock_->setFloating(false);
            addDockWidget(Qt::RightDockWidgetArea, modelingDock_);
            modelingDock_->hide();
        }
    });
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("workspace/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("workspace/layout")).toByteArray(), 1);
    if (settings.value(QStringLiteral("workspace/showConstructionTree"), false).toBool())
        showModelingHistory();
    refreshSelectionProperties();
    connect(view_, &PatternView::entityDrawn, this, &MainWindow::addEntity);
    connect(view_, &PatternView::dartDrawn, this, [this](const opc::Entity& first, const opc::Entity& second) {
        undo_.beginMacro(tr("Draw Dart"));
        addEntity(first);
        addEntity(second);
        undo_.endMacro();
        if (!doc_.layerVisible(opc::Layer::Construction))
            statusBar()->showMessage(tr("Dart added. Show the Construction layer to see it."), 5000);
    });
    connect(view_, &PatternView::dartRejected, this, [this] {
        statusBar()->showMessage(tr("Choose a distinct leg end so the three points form a triangle."), 5000);
    });

    connect(view_, &PatternView::draftDragPreviewRequested, this, [this](qsizetype index, const QPointF& offset) {
        auto candidate = doc_;
        if (candidate.moveDraftPoint(doc_.entities()[index].draftName, offset))
            view_->previewDraftDrag(candidate.entities());
        else
            view_->previewDraftDrag({});
    });
    connect(view_, &PatternView::entityMoved, this, &MainWindow::moveEntity);
    connect(view_, &PatternView::measured, this, [this](qreal distance) {
        statusBar()->showMessage(tr("Distance %1 mm").arg(QLocale().toString(distance, 'f', 3)));
    });
    auto* zoom = new QLabel(tr("1.00 px/mm"), statusBar());
    statusBar()->addPermanentWidget(zoom);
    connect(view_, &PatternView::zoomChanged, zoom, [zoom](qreal value) {
        zoom->setText(tr("%1 px/mm").arg(QLocale().toString(value, 'g', 4)));
    });

    const QString emptyCoordinates = tr("X: — mm  Y: — mm");
    auto* coordinates = new QLabel(emptyCoordinates, statusBar());
    statusBar()->addPermanentWidget(coordinates);
    connect(view_, &PatternView::cursorPositionChanged, coordinates,
            [coordinates](const QPointF& position) {
        const QLocale locale;
        coordinates->setText(tr("X: %1 mm  Y: %2 mm")
                                 .arg(locale.toString(position.x(), 'f', 2),
                                      locale.toString(position.y(), 'f', 2)));
    });
    connect(view_, &PatternView::cursorLeftCanvas, coordinates,
            [coordinates, emptyCoordinates] { coordinates->setText(emptyCoordinates); });
    statusBar()->showMessage(tr("Wheel: zoom | Middle drag: pan | Drag empty space: select area | Ctrl: extend selection"));
}

void MainWindow::refreshPieceDisplay() {
    QVector<QPolygonF> allowances;
    for (const auto& geometry : pieces_) {
        if (!geometry.allowance.isEmpty())
            allowances.append(geometry.allowance);
    }
    QVector<QPainterPath> markings;
    if (doc_.layerVisible(opc::Layer::Main)) {
        QVector<bool> visible(pieces_.size(), false);
        for (qsizetype i = 0; i < doc_.entities().size(); ++i) {
            const auto& entity = doc_.entities()[i];
            if (entity.piece >= 0 && entity.layer == opc::Layer::Main && view_->entityVisible(i))
                visible[entity.piece] = true;
        }
        for (qsizetype i = 0; i < pieces_.size(); ++i) {
            if (!visible[i])
                continue;
            const auto marks = opc::buildPieceMarkings(doc_.pieces()[i], pieces_[i],
                                                       doc_.layerVisible(opc::Layer::Allowance));
            QPainterPath path = marks.notches;
            for (const auto& symbol : marks.anchoredSymbols)
                path.addPath(symbol);
            path.addPath(marks.grainline);
            path.addPath(marks.labelPath());
            if (!path.isEmpty())
                markings.append(std::move(path));
        }
    }
    view_->applyLayers(doc_, allowances, markings);

    QVector<QPair<QPainterPath, QColor>> overlays;
    const auto addOverlay = [&](const QString& size, QColor color, bool cutPiecesOnly) {
        QVector<qsizetype> sources;
        const auto graded = opc::gradedPattern(doc_, size, &sources);
        QPainterPath path;
        for (qsizetype j = 0; j < graded.entities().size(); ++j) {
            const auto& entity = graded.entities()[j];
            if (entity.piece >= 0 && (!cutPiecesOnly || sources[j] < 0) && doc_.layerVisible(entity.layer)
                && (sources[j] < 0 || view_->entityVisible(sources[j])))
                path.addPath(opc::entityPath(entity));
        }
        if (!path.isEmpty())
            overlays.append({std::move(path), color});
    };
    if (!doc_.sizeCutHistory().isEmpty())
        addOverlay({}, QColor(20, 122, 110), true);
    for (qsizetype i = 0; i < doc_.production().sizes.size(); ++i) {
        const auto& size = doc_.production().sizes[i].name;
        if (overlaySizes_.contains(size))
            addOverlay(size, sizeColor(i), false);
    }
    view_->setSizeOverlays(overlays);
}

void MainWindow::refreshObjects() {
    setModelingError({});
    refreshModelingHistory();
    const QSignalBlocker blocker(objects_);
    pieces_.clear();
    for (qsizetype index = 0; index < doc_.pieces().size(); ++index)
        pieces_.append(opc::analyzePiece(doc_, index));
    refreshPieceDisplay();
    for (int index = 0; index < static_cast<int>(layerActions_.size()); ++index)
        layerActions_[index]->setChecked(doc_.layerVisible(static_cast<opc::Layer>(index)));

    const auto selected = view_->selectedIndices();
    objects_->clear();
    for (qsizetype index = 0; index < doc_.entities().size(); ++index) {
        QString name;
        const auto& entity = doc_.entities()[index];
        switch (entity.shape) {
        case opc::Shape::Point: name = tr("Point"); break;
        case opc::Shape::Line: name = tr("Line"); break;
        case opc::Shape::Rectangle: name = tr("Rectangle"); break;
        case opc::Shape::Circle: name = tr("Circle"); break;
        case opc::Shape::Curve: name = tr("Curve"); break;
        }
        if (!entity.draftName.isEmpty())
            name = tr("%1 (%2)").arg(name, entity.draftName);
        auto* item = new QTreeWidgetItem(objects_, {tr("%1 · %2").arg(index + 1).arg(name)});
        if (entity.piece >= 0)
            item->setText(2, doc_.pieces()[entity.piece].name);
        item->setText(3, entity.layer == opc::Layer::Main ? tr("Main") : tr("Construction"));
        if (!entity.dxfLayer.isEmpty())
            item->setToolTip(3, tr("DXF source layer: %1").arg(entity.dxfLayer).toHtmlEscaped());
        item->setCheckState(1, view_->entityVisible(index) ? Qt::Checked : Qt::Unchecked);
        if (selected.contains(index))
            item->setSelected(true);
    }
    refreshSelectionProperties();
}

void MainWindow::refreshSelectionProperties() {
    const auto selected = selectedObjects();
    if (selected.size() != 1) {
        selectionProperties_->setText(selected.isEmpty() ? tr("No objects selected")
                                                         : tr("%1 objects selected").arg(selected.size()));
        return;
    }
    const auto index = selected.front();
    if (index >= doc_.entities().size())
        return;
    const auto& entity = doc_.entities()[index];
    const auto* row = objects_->topLevelItem(index);
    const QLocale locale;
    const auto coordinates = [&locale](const QPointF& point) {
        return tr("X %1 mm, Y %2 mm").arg(locale.toString(point.x(), 'f', 2),
                                         locale.toString(point.y(), 'f', 2));
    };
    QStringList lines{row->text(0), tr("Layer: %1").arg(row->text(3))};
    if (!row->text(2).isEmpty())
        lines.append(tr("Piece: %1").arg(row->text(2)));
    lines.append(tr("Start: %1").arg(coordinates(entity.a)));
    if (entity.shape == opc::Shape::Circle)
        lines.append(tr("Radius: %1 mm").arg(locale.toString(QLineF(entity.a, entity.b).length(), 'f', 2)));
    else if (entity.shape != opc::Shape::Point)
        lines.append(tr("End: %1").arg(coordinates(entity.b)));
    if (entity.shape == opc::Shape::Curve)
        lines.append(tr("Control: %1").arg(coordinates(entity.control)));
    if (canEditGeometry(index))
        lines.append(tr("Double-click to edit geometry"));
    selectionProperties_->setText(lines.join(QLatin1Char('\n')));
}

bool MainWindow::canEditGeometry(qsizetype index) const {
    if (index < 0 || index >= doc_.entities().size())
        return false;
    const auto& entity = doc_.entities()[index];
    return entity.draftName.isEmpty()
        && (entity.shape == opc::Shape::Point || entity.shape == opc::Shape::Line
            || entity.shape == opc::Shape::Curve)
        && (entity.piece < 0 || std::ranges::none_of(doc_.production().rules, [&entity](const auto& rule) {
            return rule.piece == entity.piece;
        }));
}

QVector<qsizetype> MainWindow::selectedObjects() const {
    QVector<qsizetype> indices;
    for (auto* item : objects_->selectedItems())
        indices.append(objects_->indexOfTopLevelItem(item));
    return indices;
}

qsizetype MainWindow::choosePiece(const QString& title) {
    if (doc_.pieces().isEmpty()) {
        QMessageBox::information(this, title, tr("Create a piece from drawing objects first."));
        return -1;
    }

    QStringList choices;
    for (const auto& piece : doc_.pieces())
        choices.append(tr("%1. %2").arg(choices.size() + 1).arg(piece.name));
    const auto selected = selectedObjects();
    const auto current = selected.isEmpty() ? -1 : doc_.entities()[selected.front()].piece;
    if (choices.size() == 1)
        return 0;
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, title, tr("Piece"), choices,
                                             current < 0 ? 0 : current, false, &accepted);
    return accepted ? choices.indexOf(choice) : -1;
}

void MainWindow::applyPatternEdit(opc::PatternDocument document, const QString& text, bool redraw) {
    QString error;
    if (((!document.modelingHistory().isEmpty() || !document.seamLinks().isEmpty())
        && !document.validateParametricSizes(document.production(), &error))
        || !document.refreshSizeCutPanels(&error)) {
        setModelingError(error);
        QMessageBox::warning(this, text, error);
        return;
    }
    const bool changedLibraryMembership = document.libraryInstances().size() != doc_.libraryInstances().size();
    const auto selected = selectedObjects();
    view_->cancelDrawing();
    undo_.push(new PatternCommand(doc_, std::move(document), text, redraw ? view_ : nullptr));
    if (changedLibraryMembership && autoSyncLibrary_->isChecked())
        librarySyncTimer_.start();
    for (const auto index : selected) {
        if (auto* item = objects_->topLevelItem(index))
            item->setSelected(true);
    }
}

void MainWindow::createPiece() {
    const auto selected = selectedObjects();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Create Piece"),
                                 tr("Select objects in the list. Use Ctrl or Shift to select several."));
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("Create Piece"), tr("Name"), QLineEdit::Normal,
                                            {}, &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;

    auto updated = doc_;
    const auto piece = updated.pieces().size();
    updated.pieces().append({name});
    for (const auto index : selected) {
        auto entity = updated.entities()[index];
        entity.piece = piece;
        updated.replace(index, entity);
    }
    applyPatternEdit(std::move(updated), tr("Create Piece"));
}

void MainWindow::assignPiece() {
    const auto selected = selectedObjects();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("Assign Selection"), tr("Select objects in the list first."));
        return;
    }
    const auto piece = choosePiece(tr("Assign Selection"));
    if (piece < 0)
        return;

    auto updated = doc_;
    bool changed = false;
    for (const auto index : selected) {
        auto entity = updated.entities()[index];
        changed |= entity.piece != piece;
        entity.piece = piece;
        updated.replace(index, entity);
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Assign Objects to Piece"));
}

void MainWindow::setSelectedLayer(opc::Layer layer) {
    auto updated = doc_;
    bool changed = false;
    for (const auto index : selectedObjects()) {
        auto entity = updated.entities()[index];
        changed |= entity.layer != layer;
        entity.layer = layer;
        updated.replace(index, entity);
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Change Object Layer"));
}

void MainWindow::measureSelection() {
    QStringList rows;
    double total = 0;
    const QLocale locale;
    for (const auto index : selectedObjects()) {
        if (const auto value = opc::seamLength(doc_.entities()[index])) {
            rows.append(tr("Object %1    %2 mm").arg(index + 1).arg(locale.toString(*value, 'f', 3)));
            total += *value;
        }
    }
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("Measure Selection"), tr("Select lines or quadratic curves."));
        return;
    }
    rows.prepend(tr("Total of measurable objects    %1 mm").arg(locale.toString(total, 'f', 3)));
    QMessageBox::information(this, tr("Measure Selection"), rows.join('\n'));
}

void MainWindow::matchSeams(const QVector<qsizetype>& indices, bool reverseSecond) {
    const auto title = tr("Match Seam Sections");
    if (indices.size() < 2 || !std::ranges::all_of(indices, [this](auto index) {
            const auto& edge = doc_.entities()[index];
            return edge.layer == opc::Layer::Main && opc::seamLength(edge).has_value();
        })) {
        QMessageBox::information(this, title, tr("Select the lines and quadratic curves of both seams on the main layer."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* table = new QTableWidget(indices.size(), 2, &dialog);
    table->setHorizontalHeaderLabels({tr("Object"), tr("Section")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QVector<QComboBox*> sides;
    const auto firstPiece = doc_.entities()[indices.front()].piece;
    for (qsizetype i = 0; i < indices.size(); ++i) {
        const auto& edge = doc_.entities()[indices[i]];
        auto* item = new QTableWidgetItem(tr("%1 · %2").arg(indices[i] + 1)
            .arg(edge.piece < 0 ? tr("Unassigned") : doc_.pieces()[edge.piece].name));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, item);
        auto* box = new QComboBox(table);
        box->addItems({tr("A"), tr("B")});
        box->setCurrentIndex(i != 0 && (indices.size() == 2 || edge.piece != firstPiece));
        table->setCellWidget(i, 1, box);
        sides.append(box);
    }
    form->addRow(table);

    auto* target = new QDoubleSpinBox(&dialog);
    target->setRange(-100000, 100000);
    target->setSuffix(tr(" mm"));
    form->addRow(tr("Target ease (B − A)"), target);
    auto* tolerance = new QDoubleSpinBox(&dialog);
    tolerance->setRange(0, 100000);
    tolerance->setValue(1);
    tolerance->setSuffix(tr(" mm"));
    form->addRow(tr("Tolerance (±)"), tolerance);
    auto* count = new QSpinBox(&dialog);
    count->setRange(0, 20);
    count->setValue(3);
    form->addRow(tr("Notch pairs"), count);
    auto* shareAllowance = new QCheckBox(tr("Share allowance on linked edges"), &dialog);
    auto* linkedWidth = new QDoubleSpinBox(&dialog);
    linkedWidth->setRange(0, 1000);
    linkedWidth->setValue(10);
    linkedWidth->setSuffix(tr(" mm"));
    linkedWidth->setEnabled(false);
    form->addRow(shareAllowance, linkedWidth);
    connect(shareAllowance, &QCheckBox::toggled, linkedWidth, &QWidget::setEnabled);
    auto* reverse = new QCheckBox(tr("Reverse correspondence on section B"), &dialog);
    reverse->setObjectName("reverseSeamCorrespondence");
    reverse->setChecked(reverseSecond);
    form->addRow(reverse);
    auto* fit = new QComboBox(&dialog);
    fit->addItems({tr("Use existing shapes"), tr("Scale formula piece B"), tr("Reshape curves on B")});
    fit->setToolTip(tr("Fit the receiving pattern in every size. Reshaping preserves the curve endpoints and the rest of the contour."));
    form->addRow(tr("Fit to seam A"), fit);
    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    form->addRow(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* add = buttons->addButton(tr("Place Paired Notches"), QDialogButtonBox::ActionRole);
    auto* link = buttons->addButton(tr("Save Seam Link"), QDialogButtonBox::ActionRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* size = new QComboBox(&dialog);
    fillExportSizes(*size, doc_);
    form->insertRow(form->rowCount() - 2, tr("Compare graded size"), size);
    std::optional<opc::SeamSection> a, b;
    const auto named = [this](const opc::SeamSection& section) {
        return std::ranges::all_of(section.edges, [this](const auto& edge) {
            return !doc_.entities()[edge.index].draftName.isEmpty();
        });
    };
    const auto refresh = [&] {
        QVector<qsizetype> aIndices, bIndices;
        for (qsizetype i = 0; i < indices.size(); ++i)
            (sides[i]->currentIndex() == 0 ? aIndices : bIndices).append(indices[i]);
        a = opc::measureSeamSection(doc_, aIndices);
        b = opc::measureSeamSection(doc_, bIndices);
        if (!a || !b) {
            summary->setText(tr("Assign one connected, open chain to each section."));
            add->setEnabled(false);
            link->setEnabled(false);
            return;
        }
        const auto difference = b->lengthMm - a->lengthMm;
        const bool within = std::abs(difference - target->value()) <= tolerance->value();
        const auto auditable = [this](const opc::SeamSection& section) {
            const auto piece = doc_.entities()[section.edges.front().index].piece;
            return piece >= 0 && pieces_[piece].error == opc::ContourError::None && !pieces_[piece].allowanceFailed;
        };
        const auto number = [](double value) { return QLocale().toString(value, 'f', 3); };
        auto message = tr("A %1 mm, B %2 mm. Ease (B − A) %3 mm. %4")
            .arg(number(a->lengthMm), number(b->lengthMm), number(difference),
                 within ? tr("Within tolerance.") : tr("Outside tolerance."));
        const auto describeStart = [this](const opc::SeamSection& section, bool reversed) {
            const auto& edge = reversed ? section.edges.back() : section.edges.front();
            const auto& entity = doc_.entities()[edge.index];
            const auto point = edge.reversed != reversed ? entity.b : entity.a;
            return tr("object %1 at (%2, %3)").arg(edge.index + 1)
                .arg(QLocale().toString(point.x(), 'f', 2), QLocale().toString(point.y(), 'f', 2));
        };
        message += tr("\nA starts on %1. B starts on %2.")
            .arg(describeStart(*a, false), describeStart(*b, reverse->isChecked()));
        const bool valid = auditable(*a) && auditable(*b);
        if (!valid)
            message += tr("\nNotches require closed pieces with valid cut outlines.");
        const auto sizeName = size->currentData().toString();
        if (!sizeName.isEmpty()) {
            QVector<qsizetype> sourceIndices;
            const auto graded = opc::gradedPattern(doc_, sizeName, &sourceIndices);
            QVector<qsizetype> gradedA, gradedB;
            const QSet<qsizetype> sourceA(aIndices.begin(), aIndices.end());
            const QSet<qsizetype> sourceB(bIndices.begin(), bIndices.end());
            for (qsizetype i = 0; i < sourceIndices.size(); ++i) {
                if (sourceA.contains(sourceIndices[i]))
                    gradedA.append(i);
                if (sourceB.contains(sourceIndices[i]))
                    gradedB.append(i);
            }
            const auto sizeA = opc::measureSeamSection(graded, gradedA);
            const auto sizeB = opc::measureSeamSection(graded, gradedB);
            if (sizeA && sizeB) {
                const auto ease = sizeB->lengthMm - sizeA->lengthMm;
                message += tr("\n%1 · A %2 mm, B %3 mm. Ease %4 mm. %5")
                    .arg(sizeName, number(sizeA->lengthMm), number(sizeB->lengthMm), number(ease),
                         std::abs(ease - target->value()) <= tolerance->value()
                             ? tr("Within tolerance.") : tr("Outside tolerance."));
            } else {
                message += tr("\n%1 · The graded seams could not be measured.").arg(sizeName);
            }
        }
        summary->setText(message);
        add->setEnabled(valid && count->value() > 0);
        link->setEnabled(valid && named(*a) && named(*b));
        link->setToolTip(tr("Save the seam sections and recalculate the notch pairs for every size. Requires named formula or modeling edges."));
    };
    for (auto* side : sides)
        connect(side, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(target, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(tolerance, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    connect(size, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(reverse, &QCheckBox::toggled, &dialog, refresh);
    connect(count, &QSpinBox::valueChanged, &dialog, refresh);
    connect(add, &QPushButton::clicked, &dialog, [&] {
        auto updated = doc_;
        bool changed = false;
        for (int i = 1; i <= count->value(); ++i) {
            const auto fraction = static_cast<double>(i) / (count->value() + 1);
            const auto first = a->notchAt(fraction);
            const auto second = b->notchAt(reverse->isChecked() ? 1 - fraction : fraction);
            if (!first || !second) {
                QMessageBox::warning(&dialog, title, tr("A notch falls on a junction. Choose a different number of pairs."));
                return;
            }
            for (const auto& [index, position] : {*first, *second}) {
                auto edge = updated.entities()[index];
                if (std::ranges::any_of(edge.seamNotches, [position](double old) {
                        return std::abs(old - position) < 1e-9;
                    }))
                    continue;
                if (edge.seamNotches.size() >= 100) {
                    QMessageBox::warning(&dialog, title, tr("An edge can hold at most 100 automatic notches."));
                    return;
                }
                edge.seamNotches.append(position);
                updated.replace(index, edge);
                changed = true;
            }
        }
        if (changed)
            applyPatternEdit(std::move(updated), tr("Place Paired Notches"));
        dialog.accept();
    });
    connect(link, &QPushButton::clicked, &dialog, [&] {
        opc::SeamLink relation;
        const auto capture = [this](const opc::SeamSection& section, bool reversed,
                                    QVector<QString>& names, bool& reverseFirst) {
            for (const auto& edge : section.edges)
                names.append(doc_.entities()[edge.index].draftName);
            if (reversed)
                std::ranges::reverse(names);
            reverseFirst = reversed ? !section.edges.back().reversed : section.edges.front().reversed;
        };
        capture(*a, false, relation.first, relation.reverseFirst);
        capture(*b, reverse->isChecked(), relation.second, relation.reverseSecond);
        relation.targetEaseMm = target->value();
        relation.toleranceMm = tolerance->value();
        relation.notchPairs = count->value();
        if (shareAllowance->isChecked())
            relation.allowanceMm = linkedWidth->value();
        auto updated = doc_;
        QString error;
        const bool fitted = fit->currentIndex() != 0;
        const bool saved = fitted ? updated.fitFormulaPieceToSeam(std::move(relation), &error, fit->currentIndex() == 2)
                                  : updated.addSeamLink(std::move(relation), &error);
        if (!saved) {
            QMessageBox::warning(&dialog, title, error);
            return;
        }
        applyPatternEdit(std::move(updated), fitted ? tr("Fit Formula Piece to Seam") : tr("Save Seam Link"), fitted);
        dialog.accept();
        showModelingHistory();
    });
    refresh();
    dialog.exec();
}

void MainWindow::editEdgeAllowance(const QVector<qsizetype>& indices, QWidget* parent) {
    const auto title = tr("Allowance for Selected Edges");
    if (indices.isEmpty() || !std::ranges::all_of(indices, [this](auto index) {
            const auto& edge = doc_.entities()[index];
            return edge.piece >= 0 && edge.layer == opc::Layer::Main && opc::seamLength(edge).has_value();
        })) {
        QMessageBox::information(parent, title, tr("Select lines or quadratic curves assigned to pieces on the main layer."));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    const auto& first = doc_.entities()[indices.front()];
    auto* inherit = new QCheckBox(tr("Use the default allowance of each piece"), &dialog);
    const auto original = first.seamAllowanceMm.value_or(doc_.pieces()[first.piece].allowanceMm);
    auto* width = new QDoubleSpinBox(&dialog);
    width->setDecimals(3);
    width->setRange(0, std::max(100000.0, original));
    width->setValue(original);
    width->setSuffix(tr(" mm"));
    form->addRow(inherit);
    form->addRow(tr("Allowance"), width);
    connect(inherit, &QCheckBox::toggled, width, &QDoubleSpinBox::setDisabled);
    inherit->setChecked(!first.seamAllowanceMm.has_value());
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto updated = doc_;
        QSet<qsizetype> affected;
        const auto value = inherit->isChecked() ? std::nullopt : std::optional<double>(width->value());
        for (const auto index : indices) {
            auto edge = updated.entities()[index];
            if (edge.seamAllowanceMm == value)
                continue;
            edge.seamAllowanceMm = value;
            affected.insert(edge.piece);
            updated.replace(index, edge);
        }
        for (const auto piece : affected) {
            const auto check = opc::analyzePiece(updated, piece);
            if (value && (check.error != opc::ContourError::None || check.allowanceFailed)) {
                QMessageBox::warning(&dialog, title, tr("The new widths cannot produce a valid cut outline for %1.")
                    .arg(updated.pieces()[piece].name));
                return;
            }
        }
        if (!affected.isEmpty())
            applyPatternEdit(std::move(updated), tr("Edit Edge Allowances"));
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::clearEdgeNotches() {
    auto updated = doc_;
    bool changed = false;
    for (const auto index : selectedObjects()) {
        auto edge = updated.entities()[index];
        if (edge.seamNotches.isEmpty())
            continue;
        edge.seamNotches.clear();
        updated.replace(index, edge);
        changed = true;
    }
    if (changed)
        applyPatternEdit(std::move(updated), tr("Clear Automatic Notches"));
}

void MainWindow::diagnoseGradedPieces(const QVector<QAction*>& productionActions) {
    QDialog dialog(this);
    dialog.setWindowTitle(productionActions.isEmpty() ? tr("Diagnose Pieces by Size") : QStringLiteral("Part Assistant"));
    QFormLayout form(&dialog);
    QComboBox sizes(&dialog);
    fillExportSizes(sizes, doc_);
    sizes.insertItem(0, tr("All sizes"), QVariant{});
    sizes.setCurrentIndex(0);
    form.addRow(tr("Size"), &sizes);
    QTreeWidget report(&dialog);
    report.setHeaderLabels({tr("Piece"), tr("Contour, allowance and notches")});
    form.addRow(&report);
    QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* editPiece = buttons.addButton(QStringLiteral("Свойства детали…"), QDialogButtonBox::ActionRole);
    editPiece->setEnabled(false);
    QPushButton* editEdge = nullptr;
    QPushButton* addDrill = nullptr;
    QPushButton* derivePiece = nullptr;
    QPushButton* addSymbol = nullptr;
    QPushButton* setup = nullptr;
    if (!productionActions.isEmpty()) {
        setup = buttons.addButton(tr("Prepare for Cutting…"), QDialogButtonBox::ActionRole);
        setup->setEnabled(false);
        derivePiece = buttons.addButton(QStringLiteral("Унаследовать контур…"), QDialogButtonBox::ActionRole);
        derivePiece->setEnabled(false);
        addSymbol = buttons.addButton(QStringLiteral("Знак…"), QDialogButtonBox::ActionRole);
        addSymbol->setEnabled(false);
        addDrill = buttons.addButton(QStringLiteral("Точка сверления…"), QDialogButtonBox::ActionRole);
        addDrill->setEnabled(false);
        editEdge = buttons.addButton(tr("Edge Allowance…"), QDialogButtonBox::ActionRole);
        editEdge->setEnabled(false);
    }
    for (auto* action : productionActions) {
        auto* shortcut = buttons.addButton(action->text(), QDialogButtonBox::ActionRole);
        connect(shortcut, &QPushButton::clicked, &dialog, [&, action] {
            dialog.accept();
            action->trigger();
        });
    }
    QMap<QString, QVector<qsizetype>> copyCounts;
    const auto addSize = [&](QTreeWidgetItem* group, const QString& size) {
        QVector<qsizetype> sourceIndices;
        const auto graded = opc::gradedPattern(doc_, size, editEdge ? &sourceIndices : nullptr);
        if (graded.pieces().isEmpty()) {
            group->setText(1, doc_.pieces().isEmpty() ? tr("No pieces") : tr("Size evaluation failed"));
            return;
        }
        if (!productionActions.isEmpty()) {
            for (const auto& measurement : doc_.productInformation().finishedMeasurements) {
                const auto length = opc::finishedMeasurementMm(graded, measurement);
                new QTreeWidgetItem(group, {measurement.label, length
                    ? tr("%1 mm").arg(*length, 0, 'f', 2) : tr("Measurement points unavailable")});
            }
        }
        const auto links = graded.inspectSeamLinks();
        for (qsizetype i = 0; i < links.checks.size(); ++i) {
            new QTreeWidgetItem(group, {tr("Seam link %1").arg(i + 1),
                describeSeamLink(links.checks[i], doc_.seamLinks()[i].targetEaseMm)});
        }
        const auto counts = copyCounts.constFind(size);
        if (counts == copyCounts.cend() && !copyCounts.isEmpty())
            group->setText(1, tr("Size is absent from the layout"));
        for (qsizetype i = 0; i < graded.pieces().size(); ++i) {
            const auto& piece = graded.pieces()[i];
            const auto geometry = opc::analyzePiece(graded, i);
            if (doc_.isSizeCutPiece(i) && geometry.error == opc::ContourError::Empty)
                continue;
            auto* row = new QTreeWidgetItem(group, {piece.name, describeContour(geometry.error)});
            row->setData(0, Qt::UserRole, QVariant::fromValue(i));
            const auto copies = counts == copyCounts.cend() ? 0 : counts->at(i);
            QString count = tr("%1 per kit").arg(piece.cutCount);
            if (counts != copyCounts.cend()) {
                if (copies < piece.cutCount)
                    count = tr("%1 of %2 in layout · missing %3").arg(copies).arg(piece.cutCount).arg(piece.cutCount - copies);
                else if (copies > piece.cutCount)
                    count = tr("%1 of %2 in layout · extra %3").arg(copies).arg(piece.cutCount).arg(copies - piece.cutCount);
                else
                    count = tr("%1 of %2 in layout").arg(copies).arg(piece.cutCount);
            }
            new QTreeWidgetItem(row, {tr("Cut quantity"), count});
            if (geometry.error == opc::ContourError::None) {
                new QTreeWidgetItem(row, {tr("Seam dimensions"),
                    tr("%1 × %2 mm · Perimeter %3 mm")
                        .arg(geometry.bounds.width(), 0, 'f', 2)
                        .arg(geometry.bounds.height(), 0, 'f', 2)
                        .arg(geometry.perimeterMm, 0, 'f', 2)});
            }
            if (geometry.allowanceFailed) {
                new QTreeWidgetItem(row, {tr("Allowance"), tr("Cut outline could not be generated")});
            } else if (!geometry.allowance.isEmpty()) {
                const auto bounds = geometry.allowance.boundingRect();
                new QTreeWidgetItem(row, {tr("Cut dimensions"), tr("%1 × %2 mm")
                    .arg(bounds.width(), 0, 'f', 2).arg(bounds.height(), 0, 'f', 2)});
            }
            if (!piece.grainAngle)
                new QTreeWidgetItem(row, {tr("Grainline"), tr("Grain direction is unspecified")});
            if (!productionActions.isEmpty()) {
                new QTreeWidgetItem(row, {tr("Default seam allowance"), tr("%1 mm").arg(piece.allowanceMm)});
                if (piece.grainAngle)
                    new QTreeWidgetItem(row, {tr("Grainline"), tr("%1°").arg(*piece.grainAngle)});
            }
            if (!piece.showLabel)
                new QTreeWidgetItem(row, {tr("Label"), tr("Printed label is disabled")});
            qsizetype expected = piece.notches.size();
            for (qsizetype j = 0; j < graded.entities().size(); ++j) {
                const auto& edge = graded.entities()[j];
                if (edge.piece != i)
                    continue;
                if (addDrill && opc::isDrillPoint(edge)) {
                    const bool inside = geometry.error == opc::ContourError::None
                        && geometry.contour.containsPoint(edge.a, Qt::OddEvenFill);
                    auto position = tr("%1, %2 mm").arg(edge.a.x(), 0, 'f', 2).arg(edge.a.y(), 0, 'f', 2);
                    if (!inside)
                        position += QStringLiteral(" · вне контура");
                    new QTreeWidgetItem(row, {QStringLiteral("Точка сверления"), position});
                }
                if (edge.layer != opc::Layer::Main)
                    continue;
                if (editEdge && j < sourceIndices.size() && sourceIndices[j] >= 0) {
                    const auto sourceIndex = sourceIndices[j];
                    const auto& source = doc_.entities()[sourceIndex];
                    const auto length = opc::seamLength(edge);
                    if (length && source.piece == i && opc::seamLength(source)) {
                        const bool linked = links.allowances.contains(j);
                        const auto width = links.allowances.value(j, edge.seamAllowanceMm.value_or(piece.allowanceMm));
                        const auto description = linked ? tr("Length %1 mm · Linked allowance %2 mm")
                                                        : tr("Length %1 mm · Allowance %2 mm");
                        auto* edgeRow = new QTreeWidgetItem(row, {
                            source.draftName.isEmpty() ? tr("Edge %1").arg(sourceIndex + 1) : source.draftName,
                            description.arg(*length, 0, 'f', 2).arg(width, 0, 'f', 2)
                        });
                        if (!linked)
                            edgeRow->setData(0, Qt::UserRole + 1, QVariant::fromValue(sourceIndex));
                    }
                }
                expected += edge.seamNotches.size();
                for (const auto fraction : links.notches.value(j)) {
                    if (std::ranges::none_of(edge.seamNotches, [fraction](double existing) {
                            return std::abs(existing - fraction) < 1e-9;
                        }))
                        ++expected;
                }
            }
            if (expected) {
                const auto marks = opc::buildPieceMarkings(piece, geometry, !geometry.allowance.isEmpty());
                const auto rendered = marks.notches.elementCount() / 2;
                new QTreeWidgetItem(row, {tr("Notches"), tr("%1 of %2 marks drawn").arg(rendered).arg(expected)});
                if (marks.notchesMissingCutOutline)
                    new QTreeWidgetItem(row, {tr("Notches"), tr("%1 marks do not reach the cut outline")
                        .arg(marks.notchesMissingCutOutline)});
            }
            row->setExpanded(true);
        }
    };
    const auto refresh = [&] {
        report.clear();
        copyCounts.clear();
        for (const auto& placement : doc_.production().placements) {
            if (placement.piece < 0 || placement.piece >= doc_.pieces().size())
                continue;
            auto& counts = copyCounts[placement.size];
            if (counts.isEmpty())
                counts.resize(doc_.pieces().size());
            ++counts[placement.piece];
        }
        for (int i = 1; i < sizes.count(); ++i) {
            if (sizes.currentIndex() != 0 && sizes.currentIndex() != i)
                continue;
            auto* group = new QTreeWidgetItem(&report, {sizes.itemText(i)});
            addSize(group, sizes.itemData(i).toString());
            group->setExpanded(true);
        }
        report.resizeColumnToContents(0);
    };
    const auto selectedPiece = [&] {
        for (auto* item = report.currentItem(); item; item = item->parent()) {
            const auto index = item->data(0, Qt::UserRole);
            if (index.isValid())
                return static_cast<qsizetype>(index.toLongLong());
        }
        return qsizetype{-1};
    };
    const auto selectedEdge = [&] {
        const auto* item = report.currentItem();
        return item ? item->data(0, Qt::UserRole + 1) : QVariant{};
    };
    connect(&report, &QTreeWidget::currentItemChanged, &dialog, [&] {
        editPiece->setEnabled(selectedPiece() >= 0);
        if (editEdge)
            editEdge->setEnabled(selectedEdge().isValid());
        if (setup)
            setup->setEnabled(selectedPiece() >= 0);
        if (addDrill)
            addDrill->setEnabled(selectedPiece() >= 0);
    });
    if (setup) {
        connect(setup, &QPushButton::clicked, &dialog, [&] {
            const auto piece = selectedPiece();
            if (piece >= 0 && preparePieceForCutting(piece, &dialog))
                refresh();
        });
    }
    if (addSymbol) {
        connect(&report, &QTreeWidget::currentItemChanged, &dialog, [&] {
            addSymbol->setEnabled(selectedPiece() >= 0);
        });
        connect(addSymbol, &QPushButton::clicked, &dialog, [&] {
            const auto piece = selectedPiece();
            if (piece < 0)
                return;
            QDialog symbols(&dialog);
            symbols.setWindowTitle(QStringLiteral("Технологический знак"));
            QFormLayout fields(&symbols);
            QComboBox type;
            fillPieceSymbolTypes(type);
            fields.addRow(QStringLiteral("Вид знака"), &type);
            QDoubleSpinBox x, y, length, angle;
            const auto center = opc::analyzePiece(doc_, piece).bounds.center();
            for (auto* field : {&x, &y}) {
                field->setDecimals(3);
                field->setRange(-1000000, 1000000);
                field->setSuffix(tr(" mm"));
            }
            x.setValue(center.x());
            y.setValue(center.y());
            length.setRange(0.1, 1000);
            length.setValue(10);
            length.setSuffix(tr(" mm"));
            angle.setRange(-360, 360);
            angle.setSuffix(QStringLiteral("°"));
            fields.addRow(QStringLiteral("Координата X"), &x);
            fields.addRow(QStringLiteral("Координата Y"), &y);
            fields.addRow(QStringLiteral("Длина"), &length);
            fields.addRow(QStringLiteral("Поворот"), &angle);
            QDialogButtonBox accept(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            fields.addRow(&accept);
            connect(&accept, &QDialogButtonBox::rejected, &symbols, &QDialog::reject);
            connect(&accept, &QDialogButtonBox::accepted, &symbols, [&] {
                const auto glyph = opc::buildPieceSymbol(opc::PieceSymbol(type.currentData().toInt()),
                    piece, {x.value(), y.value()}, length.value(), angle.value());
                for (int i = 1; i < sizes.count(); ++i) {
                    const auto graded = opc::gradedPattern(doc_, sizes.itemData(i).toString());
                    const auto geometry = opc::analyzePiece(graded, piece);
                    if (geometry.error != opc::ContourError::None
                        || !std::ranges::all_of(glyph, [&geometry](const auto& stroke) {
                            return opc::symbolFitsContour(opc::entityPath(stroke), geometry.contour);
                        })) {
                        QMessageBox::warning(&symbols, symbols.windowTitle(),
                            QStringLiteral("Знак должен лежать внутри контура в размере «%1».").arg(sizes.itemText(i)));
                        return;
                    }
                }
                auto updated = doc_;
                for (const auto& stroke : glyph)
                    updated.insert(updated.entities().size(), stroke);
                applyPatternEdit(std::move(updated), symbols.windowTitle(), true);
                symbols.accept();
            });
            if (symbols.exec() == QDialog::Accepted)
                refresh();
        });
    }
    if (derivePiece) {
        connect(&report, &QTreeWidget::currentItemChanged, &dialog, [&] {
            derivePiece->setEnabled(selectedPiece() >= 0);
        });
        connect(derivePiece, &QPushButton::clicked, &dialog, [&] {
            const auto source = selectedPiece();
            if (source < 0)
                return;
            QDialog inherit(&dialog);
            inherit.setWindowTitle(derivePiece->text());
            QFormLayout fields(&inherit);
            QLineEdit name(doc_.pieces()[source].name + QStringLiteral(" · Крой"), &inherit);
            fields.addRow(tr("Piece name"), &name);
            QCheckBox include(tr("Include attached formula construction objects"), &inherit);
            fields.addRow(&include);
            QDialogButtonBox actions(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inherit);
            fields.addRow(&actions);
            connect(&actions, &QDialogButtonBox::accepted, &inherit, &QDialog::accept);
            connect(&actions, &QDialogButtonBox::rejected, &inherit, &QDialog::reject);
            if (inherit.exec() != QDialog::Accepted)
                return;
            auto updated = doc_;
            QString error;
            if (!updated.deriveParametricPiece(source, name.text(), &error, include.isChecked())) {
                QMessageBox::warning(&dialog, derivePiece->text(), error);
                return;
            }
            applyPatternEdit(std::move(updated), derivePiece->text(), true);
            refresh();
        });
    }
    if (addDrill) {
        connect(addDrill, &QPushButton::clicked, &dialog, [&] {
            const auto piece = selectedPiece();
            if (piece < 0)
                return;
            const auto base = opc::analyzePiece(doc_, piece);
            if (base.error != opc::ContourError::None) {
                QMessageBox::warning(&dialog, addDrill->text(),
                                     QStringLiteral("Для сверления нужен замкнутый контур детали."));
                return;
            }
            QDialog drill(&dialog);
            drill.setWindowTitle(addDrill->text());
            QFormLayout fields(&drill);
            QDoubleSpinBox x, y;
            for (auto* field : {&x, &y}) {
                field->setDecimals(3);
                field->setRange(-1000000, 1000000);
                field->setSuffix(tr(" mm"));
            }
            x.setValue(base.bounds.center().x());
            y.setValue(base.bounds.center().y());
            fields.addRow(QStringLiteral("Координата X"), &x);
            fields.addRow(QStringLiteral("Координата Y"), &y);
            QDialogButtonBox accept(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            fields.addRow(&accept);
            connect(&accept, &QDialogButtonBox::rejected, &drill, &QDialog::reject);
            connect(&accept, &QDialogButtonBox::accepted, &drill, [&] {
                const QPointF point(x.value(), y.value());
                for (int i = 1; i < sizes.count(); ++i) {
                    const auto graded = opc::gradedPattern(doc_, sizes.itemData(i).toString());
                    const auto contour = opc::analyzePiece(graded, piece);
                    if (contour.error != opc::ContourError::None
                        || !contour.contour.containsPoint(point, Qt::OddEvenFill)) {
                        QMessageBox::warning(&drill, drill.windowTitle(),
                            QStringLiteral("Точка должна лежать внутри детали для размера «%1».").arg(sizes.itemText(i)));
                        return;
                    }
                }
                auto updated = doc_;
                opc::Entity mark{opc::Shape::Point, point, point, {}, piece, opc::Layer::Construction};
                mark.dxfLayer = opc::drillLayer;
                updated.insert(updated.entities().size(), mark);
                applyPatternEdit(std::move(updated), QStringLiteral("Добавить точку сверления"), true);
                drill.accept();
            });
            if (drill.exec() == QDialog::Accepted)
                refresh();
        });
    }
    if (editEdge) {
        connect(editEdge, &QPushButton::clicked, &dialog, [&] {
            const auto index = selectedEdge();
            if (!index.isValid())
                return;
            editEdgeAllowance({static_cast<qsizetype>(index.toLongLong())}, &dialog);
            refresh();
        });
    }
    connect(editPiece, &QPushButton::clicked, &dialog, [&] {
        const auto piece = selectedPiece();
        if (piece < 0)
            return;
        managePieceAt(piece, &dialog);
        refresh();
    });
    connect(&sizes, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();
    dialog.resize(680, 420);
    dialog.exec();
}

bool MainWindow::preparePieceForCutting(qsizetype piece, QWidget* parent) {
    PieceSetupWizard wizard(parent);
    wizard.setWindowTitle(tr("Prepare %1 for Cutting").arg(doc_.pieces()[piece].name));
    auto* allowancePage = new QWizardPage(&wizard);
    allowancePage->setTitle(tr("Seam allowance"));
    auto* allowanceForm = new QFormLayout(allowancePage);
    auto* width = new QDoubleSpinBox(allowancePage);
    const auto& original = doc_.pieces()[piece];
    width->setDecimals(3);
    width->setRange(0, std::max(100000.0, original.allowanceMm));
    width->setSuffix(tr(" mm"));
    width->setValue(original.allowanceMm);
    const double displayedWidth = width->value();
    allowanceForm->addRow(tr("Default width"), width);
    auto* corners = new QComboBox(allowancePage);
    corners->addItems({tr("Mitered"), tr("Beveled outer corners")});
    corners->setCurrentIndex(original.bevelAllowanceCorners ? 1 : 0);
    allowanceForm->addRow(tr("Allowance corners"), corners);
    struct CornerEndpoint {
        qsizetype index;
        bool end;
    };
    struct CornerSetting {
        QPointF point;
        QVector<CornerEndpoint> endpoints;
        QComboBox* choice = nullptr;
    };
    QVector<CornerSetting> cornerSettings;
    for (qsizetype index = 0; index < doc_.entities().size(); ++index) {
        const auto& edge = doc_.entities()[index];
        if (edge.piece != piece || edge.layer != opc::Layer::Main || !opc::seamLength(edge))
            continue;
        for (bool end : {false, true}) {
            const auto point = end ? edge.b : edge.a;
            auto existing = std::ranges::find_if(cornerSettings, [&](const CornerSetting& setting) {
                return QLineF(point, setting.point).length() <= 1e-6;
            });
            if (existing == cornerSettings.end())
                cornerSettings.append({point, {{index, end}}});
            else
                existing->endpoints.append({index, end});
        }
    }
    if (!cornerSettings.isEmpty()) {
        auto* table = new QTableWidget(cornerSettings.size(), 2, allowancePage);
        table->setHorizontalHeaderLabels({tr("Corner"), tr("Treatment")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (qsizetype row = 0; row < cornerSettings.size(); ++row) {
            auto& setting = cornerSettings[row];
            const auto& endpoint = setting.endpoints.front();
            const auto& edge = doc_.entities()[endpoint.index];
            const auto edgeName = edge.draftName.isEmpty() ? tr("Edge %1").arg(endpoint.index + 1) : edge.draftName;
            table->setItem(row, 0, new QTableWidgetItem(endpoint.end ? tr("%1 · end").arg(edgeName)
                                                                  : tr("%1 · start").arg(edgeName)));
            setting.choice = new QComboBox(table);
            setting.choice->setObjectName(QStringLiteral("cuttingCornerTreatment"));
            setting.choice->addItems({tr("Use piece setting"), tr("Mitered"), tr("Beveled if convex")});
            for (const auto& endpoint : setting.endpoints) {
                const auto& source = doc_.entities()[endpoint.index];
                const auto value = endpoint.end ? source.bevelAtEnd : source.bevelAtStart;
                if (value) {
                    setting.choice->setCurrentIndex(*value ? 2 : 1);
                    break;
                }
            }
            table->setCellWidget(row, 1, setting.choice);
        }
        allowanceForm->addRow(table);
    }
    wizard.addPage(allowancePage);

    auto* marksPage = new QWizardPage(&wizard);
    marksPage->setTitle(tr("Edge markings"));
    marksPage->setSubTitle(tr("Choose an edge to add a notch or a symbol. Finish checks the contour and marks in every size."));
    auto* marksForm = new QFormLayout(marksPage);
    auto* edges = new QComboBox(marksPage);
    edges->addItem(tr("No edge selected"), -1);
    for (qsizetype i = 0; i < doc_.entities().size(); ++i) {
        const auto& edge = doc_.entities()[i];
        if (edge.piece == piece && edge.layer == opc::Layer::Main && opc::seamLength(edge))
            edges->addItem(edge.draftName.isEmpty() ? tr("Edge %1").arg(i + 1) : edge.draftName,
                           QVariant::fromValue(i));
    }
    marksForm->addRow(tr("Seam edge"), edges);
    auto* position = new QDoubleSpinBox(marksPage);
    position->setDecimals(2);
    position->setRange(0.01, 99.99);
    position->setValue(50);
    position->setSuffix(tr(" %"));
    position->setEnabled(false);
    marksForm->addRow(tr("Distance from edge start"), position);
    auto* kind = new QComboBox(marksPage);
    kind->addItem(tr("Notch"), -1);
    fillPieceSymbolTypes(*kind);
    kind->addItem(tr("Clear attached symbols"), -2);
    marksForm->addRow(tr("Mark type"), kind);
    auto* reference = new QComboBox(marksPage);
    reference->addItems({tr("Seam line"), tr("Cut outline")});
    marksForm->addRow(tr("Measure from"), reference);
    auto* symbolSize = new QDoubleSpinBox(marksPage);
    auto* inset = new QDoubleSpinBox(marksPage);
    for (auto* dimension : {symbolSize, inset}) {
        dimension->setRange(0.1, 1000);
        dimension->setValue(10);
        dimension->setSuffix(tr(" mm"));
    }
    marksForm->addRow(tr("Symbol size"), symbolSize);
    marksForm->addRow(tr("Inset"), inset);
    const auto updateMarks = [=] {
        const auto available = edges->currentIndex() > 0;
        const auto type = kind->currentData().toInt();
        kind->setEnabled(available);
        position->setEnabled(available && type >= -1);
        reference->setEnabled(available && type >= 0);
        symbolSize->setEnabled(available && type >= 0);
        inset->setEnabled(available && type >= 0);
    };
    connect(edges, &QComboBox::currentIndexChanged, marksPage, updateMarks);
    connect(kind, &QComboBox::currentIndexChanged, marksPage, updateMarks);
    updateMarks();
    wizard.addPage(marksPage);

    std::optional<opc::PatternDocument> result;
    wizard.validateSetup = [&] {
        auto candidate = doc_;
        candidate.pieces()[piece].allowanceMm = width->value() == displayedWidth
            ? original.allowanceMm : width->value();
        candidate.pieces()[piece].bevelAllowanceCorners = corners->currentIndex() == 1;
        for (const auto& setting : cornerSettings) {
            const auto choice = setting.choice->currentIndex();
            for (qsizetype i = 0; i < setting.endpoints.size(); ++i) {
                const auto& endpoint = setting.endpoints[i];
                auto edge = candidate.entities()[endpoint.index];
                auto& override = endpoint.end ? edge.bevelAtEnd : edge.bevelAtStart;
                const std::optional<bool> value = i == 0 && choice > 0 ? std::optional(choice == 2) : std::nullopt;
                if (override != value) {
                    override = value;
                    candidate.replace(endpoint.index, edge);
                }
            }
        }
        const auto index = edges->currentData().toLongLong();
        const auto fraction = position->value() / 100;
        const auto type = kind->currentData().toInt();
        if (index >= 0) {
            auto edge = candidate.entities()[index];
            if (type == -2) {
                edge.seamSymbols.clear();
            } else if (type == -1) {
                if (std::ranges::none_of(edge.seamNotches, [fraction](double prior) {
                        return std::abs(prior - fraction) < 1e-9;
                    })) {
                    if (edge.seamNotches.size() >= 100) {
                        QMessageBox::warning(&wizard, wizard.windowTitle(), tr("This edge already has 100 notches."));
                        return false;
                    }
                    edge.seamNotches.append(fraction);
                }
            } else {
                const opc::EdgeSymbol symbol{opc::PieceSymbol(type), fraction, symbolSize->value(),
                    inset->value(), reference->currentIndex() == 1};
                if (!edge.seamSymbols.contains(symbol)) {
                    if (edge.seamSymbols.size() >= 100) {
                        QMessageBox::warning(&wizard, wizard.windowTitle(), tr("This edge already has 100 symbols."));
                        return false;
                    }
                    edge.seamSymbols.append(symbol);
                }
            }
            candidate.replace(index, edge);
        }
        QStringList names{QString{}};
        for (const auto& size : candidate.production().sizes)
            names.append(size.name);
        for (const auto& name : names) {
            QVector<qsizetype> sources;
            const auto graded = opc::gradedPattern(candidate, name, &sources);
            const auto geometry = opc::analyzePiece(graded, piece);
            const auto gradedIndex = sources.indexOf(index);
            const auto marks = opc::buildPieceMarkings(candidate.pieces()[piece], geometry, !geometry.allowance.isEmpty());
            if (geometry.error != opc::ContourError::None || geometry.allowanceFailed
                || (index >= 0 && gradedIndex < 0) || marks.notchesMissingCutOutline || marks.invalidAnchoredSymbols) {
                QMessageBox::warning(&wizard, wizard.windowTitle(),
                    tr("Check the contour, allowance and edge marks in %1.")
                        .arg(name.isEmpty() ? tr("Base pattern") : name));
                return false;
            }
        }
        result = std::move(candidate);
        return true;
    };
    if (wizard.exec() != QDialog::Accepted || !result)
        return false;
    applyPatternEdit(std::move(*result), tr("Prepare Piece for Cutting"));
    return true;
}

void MainWindow::managePiece() {
    const auto piece = choosePiece(tr("Piece, Contour and Allowance"));
    if (piece >= 0)
        managePieceAt(piece, this);
}

void MainWindow::managePieceAt(qsizetype piece, QWidget* parent) {
    const auto& original = doc_.pieces()[piece];
    const auto& geometry = pieces_[piece];
    using opc::ContourError;
    const auto contour = describeContour(geometry.error);

    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Piece, Contour and Allowance"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(original.name, &dialog);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Contour check"), new QLabel(contour, &dialog));
    QStringList shortEdges;
    for (qsizetype i = 0; i < doc_.entities().size(); ++i) {
        const auto& edge = doc_.entities()[i];
        if (edge.piece != piece || edge.layer != opc::Layer::Main)
            continue;
        const auto measured = opc::seamLength(edge);
        const bool narrowRectangle = edge.shape == opc::Shape::Rectangle
            && std::min(std::abs(edge.a.x() - edge.b.x()), std::abs(edge.a.y() - edge.b.y())) < 1;
        if ((measured && *measured < 1) || narrowRectangle)
            shortEdges.append(QString::number(i + 1));
    }
    if (!shortEdges.isEmpty()) {
        auto* warning = new QLabel(shortEdges.join(", "), &dialog);
        warning->setWordWrap(true);
        form->addRow(tr("Objects with segments shorter than 1 mm"), warning);
    }
    if (geometry.error == ContourError::None) {
        const QLocale locale;
        form->addRow(tr("Perimeter"), new QLabel(tr("%1 mm").arg(locale.toString(geometry.perimeterMm, 'f', 2)), &dialog));
        form->addRow(tr("Width × height"), new QLabel(tr("%1 × %2 mm").arg(
            locale.toString(geometry.bounds.width(), 'f', 2),
            locale.toString(geometry.bounds.height(), 'f', 2)), &dialog));
    }
    if (geometry.allowanceFailed || (original.allowanceMm > 0 && geometry.error != ContourError::None))
        form->addRow(new QLabel(tr("The stored allowance cannot be drawn on this contour."), &dialog));

    auto* width = new QDoubleSpinBox(&dialog);
    width->setDecimals(3);
    width->setRange(0, std::max(100000.0, original.allowanceMm));
    width->setSpecialValueText(tr("None"));
    width->setSuffix(tr(" mm"));
    width->setValue(original.allowanceMm);
    form->addRow(tr("Seam allowance"), width);
    auto* corners = new QComboBox(&dialog);
    corners->addItems({tr("Mitered"), tr("Beveled outer corners")});
    corners->setCurrentIndex(original.bevelAllowanceCorners ? 1 : 0);
    form->addRow(tr("Allowance corners"), corners);
    auto* label = new QCheckBox(tr("Show on piece"), &dialog);
    label->setChecked(original.showLabel);
    form->addRow(tr("Label"), label);
    auto* count = new QSpinBox(&dialog);
    count->setRange(1, 9999);
    count->setValue(original.cutCount);
    form->addRow(tr("Cut quantity"), count);
    auto* size = new QLineEdit(original.size, &dialog);
    form->addRow(tr("Size"), size);

    auto* grain = new QCheckBox(tr("Show arrow"), &dialog);
    grain->setChecked(original.grainAngle.has_value());
    form->addRow(tr("Grainline"), grain);
    auto* angle = new QDoubleSpinBox(&dialog);
    angle->setDecimals(2);
    angle->setRange(0, 359.99);
    angle->setValue(original.grainAngle.value_or(90));
    angle->setEnabled(grain->isChecked());
    connect(grain, &QCheckBox::toggled, angle, &QWidget::setEnabled);
    form->addRow(tr("Angle clockwise from right (°)"), angle);

    auto* usePieceCompensation = new QCheckBox(QStringLiteral("Задать для этой детали"), &dialog);
    usePieceCompensation->setObjectName(QStringLiteral("pieceFabricOverride"));
    usePieceCompensation->setChecked(original.fabricShrinkageOverride.has_value());
    form->addRow(QStringLiteral("Компенсация ткани"), usePieceCompensation);
    const auto compensation = original.fabricShrinkageOverride.value_or(QPointF{});
    const auto addCompensationAxis = [&](const QString& labelText, double value, const QString& objectName) {
        auto* input = new QDoubleSpinBox(&dialog);
        input->setObjectName(objectName);
        input->setRange(-99, 99);
        input->setDecimals(2);
        input->setSuffix(QStringLiteral(" %"));
        input->setValue(value);
        input->setEnabled(usePieceCompensation->isChecked());
        connect(usePieceCompensation, &QCheckBox::toggled, input, &QWidget::setEnabled);
        form->addRow(labelText, input);
        return input;
    };
    const auto* weft = addCompensationAxis(QStringLiteral("По утку"), compensation.x(), QStringLiteral("pieceWeft"));
    const auto* warp = addCompensationAxis(QStringLiteral("По основе"), compensation.y(), QStringLiteral("pieceWarp"));

    auto* notches = new QLineEdit(&dialog);
    QStringList positions;
    for (double position : original.notches)
        positions.append(QString::number(position, 'g', 17));
    notches->setText(positions.join(' '));
    notches->setPlaceholderText(tr("Example: 10 35.5 80"));
    notches->setToolTip(tr("Enter up to 100 positions along the contour, in percent from 0 to below 100. "
                           "Separate positions with spaces and use a decimal point."));
    form->addRow(tr("Notches (% of perimeter)"), notches);
    auto edited = original;

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* remove = buttons->addButton(tr("Delete Piece…"), QDialogButtonBox::DestructiveRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (QMessageBox::question(&dialog, tr("Delete Piece"),
                                  tr("Delete the piece and keep its drawing objects?")) == QMessageBox::Yes)
            dialog.done(2);
    });
    const auto displayedWidth = width->value();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty()) {
            name->setFocus();
            return;
        }
        if (width->value() > 0 && width->value() != displayedWidth) {
            auto updated = doc_;
            updated.pieces()[piece].allowanceMm = width->value();
            const auto check = opc::analyzePiece(updated, piece);
            if (check.error != ContourError::None || check.allowanceFailed) {
                QMessageBox::warning(&dialog, tr("Allowance unavailable"),
                    tr("The contour cannot produce an outward allowance at this width. "
                       "Check the contour or try a smaller width."));
                return;
            }
        }
        const auto fields = notches->text().simplified().split(' ', Qt::SkipEmptyParts);
        QVector<double> values;
        for (const auto& field : fields) {
            bool ok = false;
            const double position = field.toDouble(&ok);
            if (!ok || !(position >= 0 && position < 100) || values.size() >= 100) {
                QMessageBox::warning(&dialog, tr("Invalid notches"), notches->toolTip());
                return;
            }
            values.append(position);
        }
        edited.name = name->text().trimmed();
        edited.allowanceMm = width->value() == displayedWidth ? original.allowanceMm : width->value();
        edited.cutCount = count->value();
        edited.size = size->text().trimmed();
        edited.showLabel = label->isChecked();
        edited.grainAngle = grain->isChecked() ? std::optional<double>(angle->value()) : std::nullopt;
        edited.notches = std::move(values);
        edited.bevelAllowanceCorners = corners->currentIndex() == 1;
        edited.fabricShrinkageOverride = usePieceCompensation->isChecked()
            ? std::optional(QPointF(weft->value(), warp->value())) : std::nullopt;
        if (edited.bevelAllowanceCorners != original.bevelAllowanceCorners) {
            auto candidate = doc_;
            candidate.pieces()[piece] = edited;
            QStringList names{QString{}};
            for (const auto& size : candidate.production().sizes)
                names.append(size.name);
            for (const auto& name : names) {
                const auto graded = opc::gradedPattern(candidate, name);
                const auto geometry = opc::analyzePiece(graded, piece);
                if (geometry.error != ContourError::None || geometry.allowanceFailed) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                        tr("Check the contour and allowance in %1.").arg(name.isEmpty() ? tr("Base pattern") : name));
                    return;
                }
            }
        }
        dialog.accept();
    });

    const int result = dialog.exec();
    if (result == QDialog::Rejected)
        return;
    auto updated = doc_;
    if (result == 2) {
        if (doc_.hasContourHistory() || !doc_.sizeCutHistory().isEmpty()) {
            QMessageBox::information(this, tr("Delete Piece"),
                tr("Remove contour steps from the construction tree before deleting pieces."));
            return;
        }
        updated.pieces().removeAt(piece);
        updated.production().removePiece(piece);
        for (qsizetype index = 0; index < updated.entities().size(); ++index) {
            auto entity = updated.entities()[index];
            if (entity.piece < piece)
                continue;
            entity.piece = entity.piece == piece ? -1 : entity.piece - 1;
            updated.replace(index, entity);
        }
        applyPatternEdit(std::move(updated), tr("Delete Piece"));
    } else if (edited != original) {
        updated.pieces()[piece] = std::move(edited);
        applyPatternEdit(std::move(updated), tr("Edit Piece"));
    }
}

void MainWindow::moveEntity(qsizetype index, const QPointF& offset) {
    const auto& entity = doc_.entities()[index];
    if (entity.shape == opc::Shape::Point && !entity.draftName.isEmpty()) {
        auto updated = doc_;
        QString error;
        if (!updated.moveDraftPoint(entity.draftName, offset, &error)) {
            setModelingError(error);
            statusBar()->showMessage(error, 8000);
            return;
        }
        setModelingError({});
        applyPatternEdit(std::move(updated), tr("Move Draft Point"), true);
        return;
    }
    if (!canModelEdge(doc_.entities()[index]))
        return;
    auto moved = doc_.entities()[index];
    moved.a += offset;
    moved.b += offset;
    if (moved.shape == opc::Shape::Curve)
        moved.control += offset;
    if (!doc_.modelingHistory().isEmpty() || !doc_.seamLinks().isEmpty()) {
        auto updated = doc_;
        updated.replace(index, moved);
        applyPatternEdit(std::move(updated), tr("Move Object"), true);
    } else {
        undo_.push(new ReplaceEntityCommand(doc_, *view_, index, moved, tr("Move Object")));
    }
}

void MainWindow::addEntity(const opc::Entity& entity) {
    undo_.push(new EntityCommand(doc_, *view_, doc_.entities().size(), entity, true, tr("Add Object")));
}

void MainWindow::deleteSelection() {
    auto selected = selectedObjects();
    if (selected.isEmpty())
        return;
    if (std::ranges::any_of(selected, [this](qsizetype index) {
            return !doc_.entities()[index].draftName.isEmpty();
        })) {
        statusBar()->showMessage(tr("To remove a formula-driven object, edit Pattern → Parametric Draft."), 5000);
        return;
    }
    std::sort(selected.begin(), selected.end(), std::greater<>());
    undo_.beginMacro(tr("Delete Objects"));
    for (const auto index : selected)
        undo_.push(new EntityCommand(doc_, *view_, index, doc_.entities().at(index), false, tr("Delete Object")));
    undo_.endMacro();
}

bool MainWindow::canModelEdge(const opc::Entity& edge) {
    if (!edge.draftName.isEmpty()) {
        statusBar()->showMessage(tr("Edit formula-driven edges in Pattern → Parametric Draft."), 5000);
        return false;
    }
    if (edge.piece >= 0 && std::ranges::any_of(doc_.production().rules, [&edge](const auto& rule) {
            return rule.piece == edge.piece;
        })) {
        statusBar()->showMessage(tr("This operation is unavailable on pieces with point grading rules."), 5000);
        return false;
    }
    return true;
}

void MainWindow::splitSelectedEdge() {
    const auto selected = selectedObjects();
    if (selected.size() != 1)
        return;
    const auto index = selected.front();
    const auto edge = doc_.entities()[index];
    if (!canModelEdge(edge))
        return;

    const auto title = tr("Split Line or Curve");
    const auto label = edge.shape == opc::Shape::Curve
        ? tr("Bézier parameter from start (%)")
        : tr("Distance from start (%)");
    bool accepted = false;
    const double percent = QInputDialog::getDouble(this, title, label, 50, 0.01, 99.99, 2, &accepted);
    if (!accepted)
        return;
    const auto parts = opc::splitEdge(edge, percent / 100);
    if (!parts) {
        statusBar()->showMessage(tr("A split must produce two valid edges and avoid existing marks at the join."), 5000);
        return;
    }

    if (edge.piece >= 0 && edge.layer == opc::Layer::Main) {
        auto trial = doc_;
        trial.replace(index, (*parts)[0]);
        trial.insert(index + 1, (*parts)[1]);
        const auto before = pieces_[edge.piece];
        const auto after = opc::analyzePiece(trial, edge.piece);
        if (before.error == opc::ContourError::None
            && (after.error != opc::ContourError::None || (!before.allowanceFailed && after.allowanceFailed))) {
            statusBar()->showMessage(tr("This split would invalidate the contour or its seam allowance."), 5000);
            return;
        }
    }

    const bool visible = view_->entityVisible(index);
    undo_.beginMacro(title);
    undo_.push(new ReplaceEntityCommand(doc_, *view_, index, (*parts)[0], title));
    undo_.push(new EntityCommand(doc_, *view_, index + 1, (*parts)[1], true, title));
    view_->setEntityVisible(index + 1, visible);
    undo_.endMacro();
}

void MainWindow::joinSelectedEdges() {
    const auto selected = selectedObjects();
    if (selected.size() != 2)
        return;
    const auto first = selected[0];
    const auto second = selected[1];
    const auto edge = doc_.entities()[first];
    if (!canModelEdge(edge) || !canModelEdge(doc_.entities()[second]))
        return;
    const auto joined = opc::joinEdges(doc_, first, second, 5);
    if (!joined) {
        statusBar()->showMessage(tr("Select two edges on the same piece and layer with a unique nearest pair of free ends within 5 mm."), 5000);
        return;
    }

    if (edge.piece >= 0 && edge.layer == opc::Layer::Main) {
        auto trial = doc_;
        trial.replace(first, (*joined)[0]);
        trial.replace(second, (*joined)[1]);
        const auto result = opc::analyzePiece(trial, edge.piece);
        if ((result.error != opc::ContourError::None && result.error != opc::ContourError::Open)
            || result.allowanceFailed) {
            statusBar()->showMessage(tr("This join would produce an invalid contour or seam allowance."), 5000);
            return;
        }
    }

    const auto title = tr("Join Free Endpoints");
    undo_.beginMacro(title);
    undo_.push(new ReplaceEntityCommand(doc_, *view_, first, (*joined)[0], title));
    undo_.push(new ReplaceEntityCommand(doc_, *view_, second, (*joined)[1], title));
    undo_.endMacro();
}

void MainWindow::alignSelectedCurve() {
    const auto selection = selectedObjects();
    if (selection.size() != 2)
        return;
    const auto title = QStringLiteral("Сопрячь кривую по касательной");
    QStringList choices;
    QVector<qsizetype> indices;
    for (const auto index : selection) {
        const auto& edge = doc_.entities()[index];
        if (edge.shape == opc::Shape::Curve && edge.draftName.isEmpty()) {
            choices.append(tr("Object %1").arg(index + 1));
            indices.append(index);
        }
    }
    if (indices.isEmpty())
        return;
    auto editable = indices.first();
    if (indices.size() == 2) {
        bool accepted = false;
        const auto name = QInputDialog::getItem(this, title, QStringLiteral("Изменяемая кривая"),
                                              choices, 0, false, &accepted);
        if (!accepted)
            return;
        editable = indices[choices.indexOf(name)];
    }
    const auto other = selection[0] == editable ? selection[1] : selection[0];
    const auto& edge = doc_.entities()[editable];
    const auto curve = opc::tangentCurve(doc_.entities()[other], edge);
    if (!curve) {
        QMessageBox::information(this, title, QStringLiteral("Нужен один общий конец и ненулевые касательные."));
        return;
    }
    auto updated = opc::editGeometry(doc_, editable, curve->a, curve->b, curve->control, false);
    if (updated && edge.piece >= 0 && edge.layer == opc::Layer::Main) {
        const auto geometry = opc::analyzePiece(*updated, edge.piece);
        if ((geometry.error != opc::ContourError::None && geometry.error != opc::ContourError::Open)
            || geometry.allowanceFailed)
            updated.reset();
    }
    if (!updated) {
        QMessageBox::warning(this, title, QStringLiteral("Сопряжение нарушает контур, припуск или правила градации."));
        return;
    }
    applyPatternEdit(std::move(*updated), title, true);
}

void MainWindow::shapeSilhouette() {
    const auto title = tr("Shape Silhouette");
    const auto piece = choosePiece(title);
    if (piece < 0)
        return;
    const auto geometry = opc::analyzePiece(doc_, piece);
    if (geometry.error != opc::ContourError::None && !doc_.isSizeCutPiece(piece)) {
        QMessageBox::warning(this, title, tr("The piece needs one closed, simple contour."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* axis = new QComboBox(&dialog);
    axis->addItems({tr("Center of piece"), tr("Left edge of bounds"), tr("Right edge of bounds")});
    form->addRow(tr("Vertical axis"), axis);
    const auto width = [&](const QString& label, double value) {
        auto* input = new QDoubleSpinBox(&dialog);
        input->setRange(20, 500);
        input->setSuffix(tr(" %"));
        input->setValue(value);
        form->addRow(label, input);
        return input;
    };
    const auto* top = width(tr("Top width"), 100);
    const auto* bottom = width(tr("Bottom width"), 125);
    const auto* length = width(tr("Length from top of piece"), 100);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (doc_.isSizeCutPiece(piece)) {
        const double fractions[]{0.5, 0, 1};
        auto updated = doc_;
        QString error;
        if (!updated.appendPanelSilhouette(piece, {fractions[axis->currentIndex()], top->value() / 100,
                                                   bottom->value() / 100, length->value() / 100}, &error)) {
            QMessageBox::warning(this, title, error);
            return;
        }
        applyPatternEdit(std::move(updated), title, true);
        return;
    }
    const bool formulaPiece = std::ranges::any_of(doc_.entities(), [piece](const auto& entity) {
        return entity.piece == piece && !entity.draftName.isEmpty();
    });
    if (formulaPiece) {
        const double fractions[]{0.5, 0, 1};
        auto updated = doc_;
        QString error;
        if (!updated.appendSilhouette(piece, fractions[axis->currentIndex()], top->value() / 100,
                                      bottom->value() / 100, &error, length->value() / 100)) {
            setModelingError(error);
            QMessageBox::warning(this, title, error);
            return;
        }
        view_->cancelDrawing();
        undo_.push(new PatternCommand(doc_, std::move(updated), title, view_));
        return;
    }
    const double axes[]{geometry.bounds.center().x(), geometry.bounds.left(), geometry.bounds.right()};
    auto result = opc::shapeSilhouette(doc_, piece, axes[axis->currentIndex()], top->value() / 100,
                                       bottom->value() / 100, length->value() / 100);
    if (!result.edit) {
        QMessageBox::warning(this, title, result.error);
        return;
    }
    undo_.push(new PatternCommand(doc_, std::move(*result.edit), title, *view_));
}

void MainWindow::showModelingHistory() {
    if (!modelingDock_) {
        modelingDock_ = new QDockWidget(tr("Construction Tree"), this);
        modelingDock_->setObjectName(QStringLiteral("constructionTree"));
        auto* panel = new QWidget(modelingDock_);
        auto* layout = new QVBoxLayout(panel);
        modelingTree_ = new QTreeWidget(panel);
        modelingTree_->setHeaderLabels({tr("Construction"), tr("Result")});
        modelingError_ = new QLabel(panel);
        modelingError_->setWordWrap(true);
        modelingError_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        modelingError_->setTextFormat(Qt::PlainText);
        modelingError_->hide();
        removeModelingStep_ = new QPushButton(tr("Remove Last Step"), panel);
        layout->addWidget(modelingTree_);
        layout->addWidget(modelingError_);
        layout->addWidget(removeModelingStep_);
        modelingDock_->setWidget(panel);
        if (!restoreDockWidget(modelingDock_))
            addDockWidget(Qt::RightDockWidgetArea, modelingDock_);
        connect(modelingTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
            const auto pair = item->data(0, Qt::UserRole).toList();
            if (pair.size() == 2)
                matchSeams({static_cast<qsizetype>(pair[0].toLongLong()),
                            static_cast<qsizetype>(pair[1].toLongLong())}, true);
            else if (const auto step = item->data(0, Qt::UserRole + 1); step.isValid())
                editModelingStep(step.toLongLong());
            else if (const auto relation = item->data(0, Qt::UserRole + 2); relation.isValid())
                editSeamLink(relation.toLongLong());
            else if (const auto cut = item->data(0, Qt::UserRole + 3); cut.isValid()) {
                const auto index = static_cast<qsizetype>(cut.toLongLong());
                bool accepted = false;
                const auto formula = QInputDialog::getText(this, tr("Edit Size Cut"), tr("Active guide count"),
                    QLineEdit::Normal, doc_.sizeCutHistory()[index].countFormula, &accepted);
                if (accepted) {
                    auto updated = doc_;
                    QString error;
                    if (updated.editSizeCut(index, formula.trimmed(), &error))
                        applyPatternEdit(std::move(updated), tr("Edit Size Cut"), true);
                    else
                        QMessageBox::warning(this, tr("Edit Size Cut"), error);
                }
            }
        });
        connect(removeModelingStep_, &QPushButton::clicked, this, [this] {
            auto updated = doc_;
            QString error;
            if (!updated.removeLastModelingStep(&error)) {
                setModelingError(error);
                return;
            }
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Remove Last Modeling Step"), view_));
        });
    }
    modelingDock_->show();
    refreshModelingHistory();
    modelingDock_->raise();
}

void MainWindow::editSeamLink(qsizetype index) {
    const auto original = doc_.seamLinks()[index];
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Seam Link %1").arg(index + 1));
    QFormLayout form(&dialog);
    QDoubleSpinBox target, tolerance, width;
    for (auto* field : {&target, &tolerance, &width}) {
        field->setRange(0, 100000);
        field->setDecimals(3);
        field->setSuffix(tr(" mm"));
    }
    target.setMinimum(-100000);
    target.setValue(original.targetEaseMm);
    tolerance.setValue(original.toleranceMm);
    width.setValue(original.allowanceMm.value_or(10));
    width.setEnabled(original.allowanceMm.has_value());
    QCheckBox share(tr("Share allowance on linked edges"));
    share.setChecked(original.allowanceMm.has_value());
    connect(&share, &QCheckBox::toggled, &width, &QWidget::setEnabled);
    QSpinBox count;
    count.setRange(0, 20);
    count.setValue(original.notchPairs);
    form.addRow(tr("Target ease (B − A)"), &target);
    form.addRow(tr("Tolerance (±)"), &tolerance);
    form.addRow(tr("Notch pairs"), &count);
    form.addRow(&share, &width);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* remove = buttons.addButton(tr("Remove Seam Link"), QDialogButtonBox::DestructiveRole);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        auto updated = doc_;
        if (updated.removeSeamLink(index))
            applyPatternEdit(std::move(updated), tr("Remove Seam Link"));
        dialog.accept();
    });
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto revised = original;
        revised.targetEaseMm = target.value();
        revised.toleranceMm = tolerance.value();
        revised.notchPairs = count.value();
        revised.allowanceMm = share.isChecked() ? std::optional<double>(width.value()) : std::nullopt;
        if (revised != original) {
            auto updated = doc_;
            QString error;
            if (!updated.editSeamLink(index, std::move(revised), &error)) {
                QMessageBox::warning(&dialog, dialog.windowTitle(), error);
                return;
            }
            applyPatternEdit(std::move(updated), tr("Edit Seam Link"));
        }
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::rotateConstruction() {
    const auto title = tr("Rotate Construction");
    const auto selection = selectedObjects();
    const auto& entities = doc_.entities();
    if (selection.isEmpty() || std::ranges::any_of(selection, [&](auto index) {
            const auto& entity = entities[index];
            return entity.draftName.isEmpty() || entity.layer != opc::Layer::Construction
                || (entity.shape != opc::Shape::Point && entity.shape != opc::Shape::Line
                    && entity.shape != opc::Shape::Curve);
        })) {
        QMessageBox::information(this, title,
            tr("Select named formula points, lines or curves on the Construction layer."));
        return;
    }
    const auto piece = entities[selection.first()].piece;
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    QFormLayout form(&dialog);
    QComboBox pivots(&dialog);
    pivots.setObjectName(QStringLiteral("rotationPivot"));
    for (qsizetype i = 0; i < entities.size(); ++i) {
        const auto& entity = entities[i];
        if (entity.shape != opc::Shape::Point || entity.draftName.isEmpty() || selection.contains(i))
            continue;
        if (entity.piece >= 0 && (entity.piece != piece
            || std::ranges::any_of(selection, [&](auto index) { return entities[index].piece != piece; })))
            continue;
        pivots.addItem(entity.draftName, QVariant::fromValue(i));
    }
    if (pivots.count() == 0) {
        QMessageBox::information(this, title, tr("Keep one named formula point outside the selection to use as the pivot."));
        return;
    }
    QDoubleSpinBox angle(&dialog);
    angle.setDecimals(6);
    angle.setRange(-360, 360);
    angle.setSuffix(tr("°"));
    angle.setValue(15);
    form.addRow(tr("Pivot point"), &pivots);
    form.addRow(tr("Angle (positive clockwise)"), &angle);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto updated = doc_;
        QString error;
        if (!updated.appendRotation(selection, pivots.currentData().toLongLong(), angle.value(), &error)) {
            QMessageBox::warning(&dialog, title, error);
            return;
        }
        view_->cancelDrawing();
        undo_.push(new PatternCommand(doc_, std::move(updated), title, view_));
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted)
        showModelingHistory();
}

void MainWindow::editModelingStep(qsizetype index) {
    auto settings = doc_.modelingHistory()[index];
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit %1").arg(modelingStepName(settings)));
    QFormLayout form(&dialog);
    const auto number = [&](const QString& label, double value, double minimum, double maximum,
                            const QString& suffix) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setDecimals(6);
        field->setRange(minimum, maximum);
        field->setValue(value);
        field->setSuffix(suffix);
        form.addRow(label, field);
        return field;
    };
    QDoubleSpinBox* axis = nullptr;
    QDoubleSpinBox* top = nullptr;
    QDoubleSpinBox* bottom = nullptr;
    QDoubleSpinBox* length = nullptr;
    QLineEdit* dartWeights = nullptr;
    QLineEdit* spreadFormula = nullptr;
    QDoubleSpinBox* angle = nullptr;
    if (!settings.rotationPivot.isEmpty()) {
        auto* pivot = new QLabel(settings.rotationPivot, &dialog);
        pivot->setTextFormat(Qt::PlainText);
        form.addRow(tr("Pivot point"), pivot);
        angle = number(tr("Angle (positive clockwise)"), settings.rotationDegrees, -360, 360, tr("°"));
    } else if (settings.contour == opc::ContourOperation::SplitDart) {
        auto weights = settings.outletWeights;
        if (weights.isEmpty()) {
            weights.fill((100 - settings.spreadMm) / (settings.guides.size() - 3), settings.guides.size() - 2);
            weights.front() = settings.spreadMm;
        }
        dartWeights = new QLineEdit(dartWeightsText(weights), &dialog);
        form.addRow(QStringLiteral("Веса выводов через пробел"), dartWeights);
    } else if (settings.contour) {
        spreadFormula = new QLineEdit(settings.spreadFormula.isEmpty()
            ? QString::number(settings.spreadMm, 'g', 17) : settings.spreadFormula, &dialog);
        form.addRow(tr("Added width (mm or formula)"), spreadFormula);
    } else {
        axis = number(tr("Axis from left edge"), settings.axisFraction * 100, 0, 100, tr(" %"));
        top = number(tr("Top width"), settings.topScale * 100, 20, 500, tr(" %"));
        bottom = number(tr("Bottom width"), settings.bottomScale * 100, 20, 500, tr(" %"));
        length = number(tr("Length from top"), settings.lengthScale * 100, 20, 500, tr(" %"));
    }
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto read = [](const QDoubleSpinBox* field, double original, double factor) {
            return std::abs(field->value() - original * factor) <= 0.0000005
                ? original : field->value() / factor;
        };
        auto revised = settings;
        if (angle) {
            revised.rotationDegrees = read(angle, settings.rotationDegrees, 1);
        } else if (spreadFormula) {
            const auto expression = spreadFormula->text().trimmed();
            if (expression.isEmpty()) {
                QMessageBox::warning(&dialog, dialog.windowTitle(), tr("Enter a width or formula."));
                return;
            }
            bool constant;
            revised.spreadMm = expression.toDouble(&constant);
            revised.spreadFormula = constant ? QString{} : expression;
        } else if (dartWeights) {
            revised.outletWeights = readDartWeights(dartWeights->text());
            if (revised.outletWeights.size() != settings.guides.size() - 2) {
                QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("Введите по одному весу на каждый вывод."));
                return;
            }
        } else {
            revised.axisFraction = read(axis, settings.axisFraction, 100);
            revised.topScale = read(top, settings.topScale, 100);
            revised.bottomScale = read(bottom, settings.bottomScale, 100);
            revised.lengthScale = read(length, settings.lengthScale, 100);
        }
        auto updated = doc_;
        QString error;
        if (!updated.editModelingStep(index, revised, &error)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), error);
            return;
        }
        if (revised != settings) {
            view_->cancelDrawing();
            undo_.push(new PatternCommand(doc_, std::move(updated), tr("Edit Modeling Step"), view_));
        }
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::setModelingError(const QString& error) {
    if (!modelingDock_ && !error.isEmpty() && !doc_.modelingHistory().isEmpty())
        showModelingHistory();
    if (modelingError_) {
        modelingError_->setText(error);
        modelingError_->setVisible(!error.isEmpty());
    }
}

void MainWindow::refreshModelingHistory() {
    if (!modelingTree_ || modelingDock_->isHidden())
        return;
    modelingTree_->clear();
    auto* root = new QTreeWidgetItem(modelingTree_, {tr("Parametric draft")});
    const auto& draft = doc_.parametricDraft();
    root->setText(1, tr("%1 measurements, %2 formula edges").arg(draft.measurements.size()).arg(draft.edges.size()));
    for (qsizetype i = 0; i < doc_.sizeCutHistory().size(); ++i) {
        const auto& cut = doc_.sizeCutHistory()[i];
        auto* item = new QTreeWidgetItem(root, {tr("Cut by Size Formula"), cut.countFormula});
        item->setData(0, Qt::UserRole + 3, QVariant::fromValue(i));
        for (auto panel = cut.panels.cbegin(); panel != cut.panels.cend(); ++panel) {
            auto* row = new QTreeWidgetItem(item, {doc_.pieces()[panel.value()].name, panel.key()});
            for (const auto& shape : cut.silhouettes.value(panel.value())) {
                new QTreeWidgetItem(row, {tr("Shape Silhouette"), tr("Width %L1% / %L2% · Length %L3%")
                    .arg(shape.topScale * 100).arg(shape.bottomScale * 100).arg(shape.lengthScale * 100)});
            }
        }
    }
    const auto& history = doc_.modelingHistory();
    QVector<QVector<qsizetype>> gradeSources(doc_.production().sizes.size());
    QVector<opc::PatternDocument> grades;
    const auto seamRow = [&](QTreeWidgetItem* step, qsizetype a, qsizetype b) {
        if (grades.isEmpty()) {
            for (qsizetype i = 0; i < doc_.production().sizes.size(); ++i)
                grades.append(opc::gradedPattern(doc_, doc_.production().sizes[i].name, &gradeSources[i]));
        }
        auto* pair = new QTreeWidgetItem(step, {tr("Paired cut edges")});
        pair->setData(0, Qt::UserRole, QVariantList{QVariant::fromValue(a), QVariant::fromValue(b)});
        pair->setToolTip(0, tr("Open the seam matching and notch placement dialog."));
        const auto add = [&](const QString& size, const opc::Entity& first, const opc::Entity& second) {
            const auto left = opc::seamLength(first);
            const auto right = opc::seamLength(second);
            if (left && right) {
                auto* row = new QTreeWidgetItem(pair, {size, tr("%L1 / %L2 mm · B − A %L3 mm")
                    .arg(*left, 0, 'f', 2).arg(*right, 0, 'f', 2).arg(*right - *left, 0, 'f', 2)});
                // Compare distances along the seam after reversing the second sewing direction.
                auto firstNotches = first.seamNotches;
                auto secondNotches = second.seamNotches;
                if (firstNotches.size() != secondNotches.size()) {
                    new QTreeWidgetItem(row, {tr("Notch counts differ"),
                        tr("%1 / %2").arg(firstNotches.size()).arg(secondNotches.size())});
                } else {
                    std::ranges::sort(firstNotches);
                    std::ranges::sort(secondNotches, std::greater<>());
                    double maximumOffset = 0;
                    for (qsizetype i = 0; i < firstNotches.size(); ++i) {
                        const double offset = firstNotches[i] * *left - (1 - secondNotches[i]) * *right;
                        maximumOffset = std::max(maximumOffset, std::abs(offset));
                    }
                    if (maximumOffset >= 0.005)
                        new QTreeWidgetItem(row, {tr("Notch positions differ"),
                            tr("Up to %L1 mm from the sewing start").arg(maximumOffset, 0, 'f', 2)});
                }
            }
        };
        add(tr("Base pattern"), doc_.entities()[a], doc_.entities()[b]);
        for (qsizetype i = 0; i < grades.size(); ++i) {
            const auto left = gradeSources[i].indexOf(a);
            const auto right = gradeSources[i].indexOf(b);
            const auto size = doc_.production().sizes[i].name;
            if (left >= 0 && right >= 0)
                add(size, grades[i].entities()[left], grades[i].entities()[right]);
            else
                new QTreeWidgetItem(pair, {size, tr("Cannot recalculate")});
        }
    };
    for (qsizetype i = 0; i < history.size(); ++i) {
        const auto& step = history[i];
        auto* item = new QTreeWidgetItem(root, {tr("%1. %2").arg(i + 1).arg(modelingStepName(step))});
        if (!step.contour || opc::usesSpreadWidth(*step.contour)
            || *step.contour == opc::ContourOperation::SplitDart) {
            item->setData(0, Qt::UserRole + 1, QVariant::fromValue(i));
            item->setToolTip(0, tr("Double-click to edit step parameters."));
        }
        if (!step.rotationPivot.isEmpty())
            item->setText(1, tr("%L1° around %2").arg(step.rotationDegrees).arg(step.rotationPivot));
        else if (!step.contour)
            item->setText(1, tr("Width %L1% / %L2% · Length %L3%")
                .arg(step.topScale * 100).arg(step.bottomScale * 100).arg(step.lengthScale * 100));
        else if (step.secondPiece >= 0)
            item->setText(1, doc_.pieces()[step.secondPiece].name);
        else if (step.contour == opc::ContourOperation::SplitDart)
            item->setText(1, step.outletWeights.isEmpty()
                ? QStringLiteral("Выводов %1 · первый %L2 %").arg(step.guides.size() - 2).arg(step.spreadMm)
                : QStringLiteral("Веса выводов %1").arg(dartWeightsText(step.outletWeights)));
        else if (!step.spreadFormula.isEmpty())
            item->setText(1, step.spreadFormula);
        else if (step.spreadMm > 0)
            item->setText(1, tr("%L1 mm").arg(step.spreadMm));
        auto* inputs = new QTreeWidgetItem(item, {tr("Source objects"), QString::number(step.objects.size())});
        for (const auto& name : step.objects)
            new QTreeWidgetItem(inputs, {name});
        if (!step.guides.isEmpty()) {
            auto* guides = new QTreeWidgetItem(item, {tr("Guides")});
            for (const auto& name : step.guides)
                new QTreeWidgetItem(guides, {name});
        }
        if (step.secondPiece >= 0) {
            const auto prefix = QStringLiteral("@%1:seam-").arg(i + 1);
            const auto& entities = doc_.entities();
            const auto a = std::ranges::find(entities, prefix + u'A', &opc::Entity::draftName);
            const auto b = std::ranges::find(entities, prefix + u'B', &opc::Entity::draftName);
            if (a != entities.end() && b != entities.end())
                seamRow(item, a - entities.begin(), b - entities.begin());
            else
                new QTreeWidgetItem(item, {tr("Paired cut edges"), tr("Changed by a later contour step")});
        }
    }
    if (!doc_.seamLinks().isEmpty()) {
        if (grades.isEmpty()) {
            for (const auto& size : doc_.production().sizes)
                grades.append(opc::gradedPattern(doc_, size.name));
        }
        auto* links = new QTreeWidgetItem(modelingTree_, {tr("Saved seam links")});
        const auto base = doc_.inspectSeamLinks();
        QVector<opc::SeamLinkReport> reports;
        for (const auto& grade : grades)
            reports.append(grade.inspectSeamLinks());
        for (qsizetype i = 0; i < doc_.seamLinks().size(); ++i) {
            const auto& link = doc_.seamLinks()[i];
            auto* row = new QTreeWidgetItem(links, {tr("Seam link %1").arg(i + 1),
                describeSeamLink(base.checks[i], link.targetEaseMm)});
            row->setData(0, Qt::UserRole + 2, QVariant::fromValue(i));
            row->setToolTip(0, tr("Double-click to edit or remove this seam link."));
            new QTreeWidgetItem(row, {tr("Section A"), QStringList(link.first.begin(), link.first.end()).join(", ")});
            new QTreeWidgetItem(row, {tr("Section B"), QStringList(link.second.begin(), link.second.end()).join(", ")});
            new QTreeWidgetItem(row, {tr("Notch pairs"), QString::number(link.notchPairs)});
            if (link.allowanceMm)
                new QTreeWidgetItem(row, {tr("Shared allowance"), tr("%L1 mm").arg(*link.allowanceMm)});
            for (qsizetype j = 0; j < reports.size(); ++j) {
                const auto& checks = reports[j].checks;
                new QTreeWidgetItem(row, {doc_.production().sizes[j].name,
                    i < checks.size() ? describeSeamLink(checks[i], link.targetEaseMm) : tr("Cannot recalculate")});
            }
        }
        links->setExpanded(true);
    }
    root->setExpanded(true);
    modelingTree_->resizeColumnToContents(0);
    removeModelingStep_->setEnabled(!history.isEmpty());
}

void MainWindow::drapePiece() {
    const auto title = QStringLiteral("Раздвижка по нескольким направляющим");
    QStringList selected;
    for (const auto index : selectedObjects()) {
        const auto& guide = doc_.entities()[index];
        if (guide.layer != opc::Layer::Construction || guide.draftName.isEmpty()
            || (guide.shape != opc::Shape::Line && guide.shape != opc::Shape::Curve)) {
            selected.clear();
            break;
        }
        selected.append(guide.draftName);
    }
    if (selected.size() < 2) {
        QMessageBox::information(this, title, QStringLiteral("Выберите две или более именных направляющих на слое построения."));
        return;
    }
    const auto piece = choosePiece(title);
    if (piece < 0)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    QFormLayout form(&dialog);
    QLabel hint(QStringLiteral("Задайте порядок, раствор и разметку каждого разреза. "
                               "Направляющие должны идти от края до края без пересечений."));
    hint.setWordWrap(true);
    form.addRow(&hint);
    QVector<QComboBox*> guides;
    QVector<QLineEdit*> widths;
    QVector<QComboBox*> markings;
    using enum opc::ContourOperation;
    for (qsizetype i = 0; i < selected.size(); ++i) {
        auto* guide = new QComboBox;
        guide->addItems(selected);
        guide->setCurrentIndex(i);
        auto* width = new QLineEdit(QStringLiteral("20"));
        width->setPlaceholderText(QStringLiteral("Например: hip / 40"));
        form.addRow(QStringLiteral("Направляющая %1").arg(i + 1), guide);
        form.addRow(QStringLiteral("Раствор (мм или формула)"), width);
        guides.append(guide);
        widths.append(width);
        auto* marking = new QComboBox;
        marking->addItem(QStringLiteral("Без разметки"), int(Spread));
        marking->addItem(QStringLiteral("Встречная складка"), int(Pleat));
        marking->addItem(QStringLiteral("Складка с выступами"), int(PleatWithTabs));
        marking->addItem(QStringLiteral("Сборка"), int(Gather));
        form.addRow(QStringLiteral("Разметка"), marking);
        markings.append(marking);
    }
    QGraphicsScene previewScene;
    QGraphicsView drawing(&previewScene);
    drawing.setMinimumSize(320, 180);
    drawing.setRenderHint(QPainter::Antialiasing);
    form.addRow(&drawing);
    QLabel result;
    result.setWordWrap(true);
    form.addRow(&result);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* preview = buttons.addButton(QStringLiteral("Проверить контур"), QDialogButtonBox::ActionRole);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::optional<opc::PatternDocument> updated;
    const auto evaluate = [&] {
        QVector<QString> names;
        QVector<QString> expressions;
        QVector<opc::ContourOperation> operations;
        for (qsizetype i = 0; i < guides.size(); ++i) {
            names.append(guides[i]->currentText());
            expressions.append(widths[i]->text());
            operations.append(opc::ContourOperation(markings[i]->currentData().toInt()));
        }
        previewScene.clear();
        auto candidate = doc_;
        QString error;
        if (!candidate.appendDrapeFormulas(piece, names, expressions, operations, &error)) {
            updated.reset();
            result.setText(error);
            return;
        }
        const auto geometry = opc::analyzePiece(candidate, piece);
        previewScene.addPolygon(geometry.contour, QPen(Qt::darkBlue, 0));
        for (const auto& entity : candidate.entities()) {
            if (entity.piece == piece && entity.layer == opc::Layer::Construction) {
                QPainterPath path(entity.a);
                if (entity.shape == opc::Shape::Curve)
                    path.quadTo(entity.control, entity.b);
                else if (entity.shape == opc::Shape::Line)
                    path.lineTo(entity.b);
                previewScene.addPath(path, QPen(Qt::darkGreen, 0, Qt::DashLine));
            }
        }
        previewScene.setSceneRect(previewScene.itemsBoundingRect().adjusted(-5, -5, 5, 5));
        drawing.fitInView(previewScene.sceneRect(), Qt::KeepAspectRatio);
        const auto bounds = geometry.bounds;
        result.setText(QStringLiteral("Габариты контура %1 × %2 мм").arg(bounds.width(), 0, 'f', 2)
                       .arg(bounds.height(), 0, 'f', 2));
        updated = std::move(candidate);
    };
    connect(preview, &QPushButton::clicked, &dialog, evaluate);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        evaluate();
        if (updated)
            dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted)
        applyPatternEdit(std::move(*updated), title, true);
}

void MainWindow::modelPiece(opc::ContourOperation operation, const QString& title) {
    using opc::ContourOperation;
    auto guides = selectedObjects();
    const bool redistribute = opc::redistributesDarts(operation);
    const bool split = operation == ContourOperation::SplitDart;
    const bool openDart = operation == ContourOperation::MoveOpenDart;
    const bool dart = openDart || operation == ContourOperation::TransferDartToOutlet;
    if (split ? !opc::validDartOutletCount(guides.size()) : redistribute && guides.size() != 5) {
        QMessageBox::information(this, title, split
            ? QStringLiteral("Выберите две ножки и от 2 до 10 выводов на слое построения. Все линии должны исходить из общей вершины.")
            : QStringLiteral("Выберите четыре ножки и один вывод на слое построения. Все линии должны исходить из общей вершины."));
        return;
    }
    const bool spread = opc::usesSpreadWidth(operation);
    if (!redistribute && (spread ? guides.isEmpty() : guides.size() != (dart ? 3 : 1))) {
        QMessageBox::information(this, title, openDart
            ? tr("Select two main-contour dart legs and one construction outlet.")
            : dart ? tr("Select two dart legs and an outlet on the Construction layer.")
            : (opc::dividesPiece(operation) || opc::usesSpreadWidth(operation))
                   ? tr("Select one construction line or quadratic curve with both ends on the piece contour.")
                   : tr("Select one construction line with both ends on the piece contour."));
        return;
    }
    const auto piece = choosePiece(title);
    if (piece < 0)
        return;
    if (openDart) {
        std::stable_partition(guides.begin(), guides.end(), [this](qsizetype index) {
            return doc_.entities()[index].layer == opc::Layer::Main;
        });
    } else if (dart || redistribute) {
        QStringList lines;
        for (const auto index : guides) {
            const auto& line = doc_.entities()[index];
            lines.append(tr("Object %1   (%2, %3) to (%4, %5)").arg(index + 1)
                         .arg(line.a.x()).arg(line.a.y()).arg(line.b.x()).arg(line.b.y()));
        }
        QStringList roles;
        if (split) {
            for (qsizetype i = 1; i <= guides.size() - 2; ++i)
                roles.append(QStringLiteral("Линия вывода %1").arg(i));
        } else if (redistribute) {
            roles = {QStringLiteral("Вывод объединённой вытачки"),
                     QStringLiteral("Первая ножка первой вытачки"),
                     QStringLiteral("Вторая ножка первой вытачки")};
        } else {
            roles = {tr("Choose the new outlet among the selected lines")};
        }
        QVector<qsizetype> chosen;
        for (const auto& role : roles) {
            bool accepted = false;
            const auto item = QInputDialog::getItem(this, title, role, lines, lines.size() - 1, false, &accepted);
            if (!accepted)
                return;
            const auto index = lines.indexOf(item);
            chosen.append(guides.takeAt(index));
            lines.removeAt(index);
        }
        if (redistribute && !split)
            guides = QVector<qsizetype>{chosen[1], chosen[2], guides[0], guides[1], chosen[0]};
        else
            guides += chosen;
    }
    if (spread && guides.size() > 1) {
        QStringList remaining;
        for (auto index : guides) {
            const auto& entity = doc_.entities()[index];
            remaining.append(entity.draftName.isEmpty() ? tr("Object %1").arg(index + 1) : entity.draftName);
        }
        auto indices = guides;
        guides.clear();
        while (indices.size() > 1) {
            bool accepted = false;
            const auto name = QInputDialog::getItem(this, title, tr("Guide segment %1").arg(guides.size() + 1),
                                                   remaining, 0, false, &accepted);
            if (!accepted)
                return;
            const auto index = remaining.indexOf(name);
            guides.append(indices.takeAt(index));
            remaining.removeAt(index);
        }
        guides.append(indices.front());
    }
    const auto named = [](const opc::Entity& entity) { return !entity.draftName.isEmpty(); };
    const bool formula = std::ranges::any_of(doc_.entities(), [piece, named](const auto& entity) {
        return entity.piece == piece && named(entity);
    }) || std::ranges::any_of(guides, [&](qsizetype index) { return named(doc_.entities()[index]); });
    double width = 0;
    QString widthFormula;
    QVector<double> outletWeights;
    if (split) {
        bool accepted = false;
        const QVector<double> equalWeights(guides.size() - 2, 1);
        const auto text = QInputDialog::getText(this, title,
            QStringLiteral("Введите веса выводов в порядке выбора через пробел. Например, 2 1 даёт доли 2/3 и 1/3."),
            QLineEdit::Normal, dartWeightsText(equalWeights), &accepted);
        if (!accepted)
            return;
        outletWeights = readDartWeights(text);
        if (outletWeights.size() != equalWeights.size()) {
            QMessageBox::warning(this, title, QStringLiteral("Введите по одному весу на каждый вывод."));
            return;
        }
    }
    if (spread) {
        bool accepted = false;
        if (formula) {
            const auto expression = QInputDialog::getText(this, title,
                tr("Total added width perpendicular to the cut (mm or formula)"), QLineEdit::Normal,
                QStringLiteral("20"), &accepted).trimmed();
            if (!accepted || expression.isEmpty())
                return;
            bool constant;
            width = expression.toDouble(&constant);
            if (!constant)
                widthFormula = expression;
        } else {
            width = QInputDialog::getDouble(this, title, tr("Total added width perpendicular to the cut (mm)"),
                                            20, 0.01, 100000, 2, &accepted);
            if (!accepted)
                return;
        }
    }
    if (formula) {
        auto updated = doc_;
        QString error;
        if (!updated.appendContour(piece, operation, guides, width, &error, widthFormula, {}, outletWeights)) {
            setModelingError(error);
            QMessageBox::warning(this, title, error);
            return;
        }
        view_->cancelDrawing();
        undo_.push(new PatternCommand(doc_, std::move(updated), title, view_));
        showModelingHistory();
        return;
    }
    auto result = opc::modelContour(doc_, piece, operation, guides, width, outletWeights);
    if (!result.edit) {
        QMessageBox::warning(this, title, result.error);
        return;
    }
    undo_.push(new PatternCommand(doc_, std::move(*result.edit), title, *view_));
    statusBar()->showMessage(tr("%1 completed. The new contour passed validation.").arg(title), 5000);
}

void MainWindow::exactInput() {
    geometryDialog(-1);
}

void MainWindow::editSelectedGeometry() {
    const auto selected = selectedObjects();
    if (selected.size() != 1) {
        statusBar()->showMessage(tr("Select one point, line or quadratic curve to edit."), 5000);
        return;
    }
    const auto index = selected.front();
    const auto& entity = doc_.entities()[index];
    if (!canModelEdge(entity))
        return;
    if (entity.shape == opc::Shape::Rectangle || entity.shape == opc::Shape::Circle) {
        statusBar()->showMessage(tr("Select a point, line or quadratic curve to edit."), 5000);
        return;
    }
    geometryDialog(index);
}

void MainWindow::geometryDialog(qsizetype index) {
    const bool editing = index >= 0;
    const auto original = editing ? doc_.entities()[index] : opc::Entity{};
    QDialog dialog(this);
    dialog.setWindowTitle(editing ? tr("Edit Geometry") : tr("Exact Input"));
    QFormLayout form(&dialog);
    auto* shape = new QComboBox(&dialog);
    shape->addItems({tr("Point"), tr("Line"), tr("Rectangle"), tr("Circle"), tr("Curve")});
    switch (view_->tool()) {
    case PatternView::Tool::Point: shape->setCurrentIndex(0); break;
    case PatternView::Tool::Rectangle: shape->setCurrentIndex(2); break;
    case PatternView::Tool::Circle: shape->setCurrentIndex(3); break;
    case PatternView::Tool::Curve: shape->setCurrentIndex(4); break;
    default: shape->setCurrentIndex(1); break;
    }
    if (editing) {
        shape->setCurrentIndex(static_cast<int>(original.shape));
        shape->setEnabled(false);
    }
    form.addRow(tr("Object"), shape);

    const auto field = [&dialog, &form](const QString& label, double minimum = -1e7, double maximum = 1e7) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setDecimals(6);
        spin->setRange(minimum, maximum);
        spin->setGroupSeparatorShown(false);
        form.addRow(label, spin);
        return spin;
    };
    auto* x1 = field(tr("X₁ (mm)"));
    auto* y1 = field(tr("Y₁ (mm)"));
    auto* mode = new QComboBox(&dialog);
    mode->addItems({tr("End coordinates"), tr("Length and angle")});
    form.addRow(tr("Line input"), mode);
    auto* x2 = field(tr("X₂ (mm)"));
    auto* y2 = field(tr("Y₂ (mm)"));
    auto* length = field(tr("Length (mm)"), 0.000001);
    length->setValue(10);
    auto* radius = field(tr("Radius (mm)"), 0.000001);
    radius->setValue(10);
    auto* controlX = field(tr("Control X (mm)"));
    auto* controlY = field(tr("Control Y (mm)"));
    auto* angle = field(tr("Angle (° clockwise from +X)"), -360, 360);
    QCheckBox connected(tr("Move connected endpoints in the same piece and layer"), &dialog);
    if (editing) {
        for (const auto& [spin, value] : {
                 std::pair{x1, original.a.x()}, {y1, original.a.y()},
                 {x2, original.b.x()}, {y2, original.b.y()},
                 {controlX, original.control.x()}, {controlY, original.control.y()}}) {
            spin->setRange(std::min(spin->minimum(), value), std::max(spin->maximum(), value));
            spin->setValue(value);
        }
        const QLineF line(original.a, original.b);
        length->setMaximum(std::max(length->maximum(), line.length()));
        length->setValue(line.length());
        angle->setValue(-line.angle());
        connected.setEnabled(original.piece >= 0);
        connected.setChecked(connected.isEnabled());
        form.addRow(&connected);
    }
    const QPointF shownA(x1->value(), y1->value());
    const QPointF shownB(x2->value(), y2->value());
    const QPointF shownControl(controlX->value(), controlY->value());
    const auto shownLength = length->value();
    const auto shownAngle = angle->value();

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    const auto updateFields = [&] {
        const bool line = shape->currentIndex() == 1;
        const bool polar = line && mode->currentIndex() == 1;
        const bool circle = shape->currentIndex() == 3;
        const bool curve = shape->currentIndex() == 4;
        const bool endpoint = shape->currentIndex() != 0 && !polar && !circle;
        form.setRowVisible(radius, circle);
        form.setRowVisible(controlX, curve);
        form.setRowVisible(controlY, curve);
        form.setRowVisible(mode, line);
        form.setRowVisible(x2, endpoint);
        form.setRowVisible(y2, endpoint);
        form.setRowVisible(length, polar);
        form.setRowVisible(angle, polar);
    };
    connect(shape, &QComboBox::currentIndexChanged, &dialog, updateFields);
    connect(mode, &QComboBox::currentIndexChanged, &dialog, updateFields);
    updateFields();
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto coordinate = [editing](QPointF entered, QPointF displayed, QPointF old) {
            if (!editing)
                return entered;
            return QPointF(entered.x() == displayed.x() ? old.x() : entered.x(),
                           entered.y() == displayed.y() ? old.y() : entered.y());
        };
        const auto a = coordinate({x1->value(), y1->value()}, shownA, original.a);
        auto b = coordinate({x2->value(), y2->value()}, shownB, original.b);
        const auto control = coordinate({controlX->value(), controlY->value()}, shownControl,
                                        original.control);
        if (shape->currentIndex() == 1 && mode->currentIndex() == 1) {
            const auto originalVector = original.b - original.a;
            if (editing && length->value() == shownLength && angle->value() == shownAngle) {
                b = a == original.a ? original.b : a + originalVector;
            } else {
                const double radians = angle->value() * std::numbers::pi / 180;
                b = a + QPointF(length->value() * std::cos(radians), length->value() * std::sin(radians));
            }
        }
        if (editing) {
            if (a != original.a || (original.shape != opc::Shape::Point && b != original.b)
                || (original.shape == opc::Shape::Curve && control != original.control)) {
                auto updated = opc::editGeometry(doc_, index, a, b, control, connected.isChecked());
                if (!updated) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                        tr("The edit would collapse an edge or move a connected object that cannot be edited here."));
                    return;
                }
                applyPatternEdit(std::move(*updated), dialog.windowTitle(), true);
            }
            dialog.accept();
            return;
        }
        switch (shape->currentIndex()) {
        case 0:
            addEntity({opc::Shape::Point, a, a});
            break;
        case 1:
            if (a.x() == b.x() && a.y() == b.y()) {
                QMessageBox::warning(&dialog, tr("Invalid Line"), tr("Choose two different endpoints."));
                return;
            }
            addEntity({opc::Shape::Line, a, b});
            break;
        case 2:
            if (a.x() == b.x() || a.y() == b.y()) {
                QMessageBox::warning(&dialog, tr("Invalid Rectangle"), tr("Width and height must be nonzero."));
                return;
            }
            addEntity({opc::Shape::Rectangle, a, b});
            break;
        case 3:
            addEntity({opc::Shape::Circle, a, a + QPointF(radius->value(), 0)});
            break;
        case 4:
            if (a == b) {
                QMessageBox::warning(&dialog, tr("Invalid Curve"), tr("Choose two different endpoints."));
                return;
            }
            addEntity({opc::Shape::Curve, a, b, QPointF(controlX->value(), controlY->value())});
            break;
        }
        dialog.accept();
    });
    dialog.exec();
}

MainWindow::~MainWindow() {
    // QUndoStack clears its history during destruction and would otherwise refresh the window.
    undo_.blockSignals(true);
}

bool MainWindow::confirmDiscard() {
    if (!recovered_ && undo_.isClean())
        return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved Drawing"),
        tr("Save the changes to the current drawing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveDocument());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const bool closing = confirmDiscard();
    event->setAccepted(closing);
    if (closing) {
        clearRecovery();
        QSettings settings;
        settings.setValue(QStringLiteral("workspace/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("workspace/layout"), saveState(1));
        settings.setValue(QStringLiteral("workspace/showConstructionTree"),
                          modelingDock_ && !modelingDock_->isHidden());
    }
}

void MainWindow::replaceDocument(opc::PatternDocument document, const QString& path) {
    autoSyncLibrary_->setChecked(false);
    view_->cancelDrawing();
    clearRecovery();
    recovered_ = false;
    autosavePending_ = false;
    overlaySizes_.clear();
    undo_.clear();
    setWindowModified(false);
    doc_ = std::move(document);
    currentPath_ = path;
    setWindowFilePath(path);
    view_->setEntities(doc_.entities());
    refreshObjects();
    view_->fitDrawing();
}

void MainWindow::newDocument() {
    if (confirmDiscard())
        replaceDocument({}, {});
}

void MainWindow::openDocument() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Open pattern"), currentPath_,
                                                  tr("Patterns (*.sofit *.opcad);;All files (*)"));
    if (path.isEmpty() || !confirmDiscard())
        return;
    QString error;
    if (!openProject(path, &error))
        QMessageBox::warning(this, tr("Open failed"), error);
}

bool MainWindow::openProject(const QString& path, QString* error) {
    opc::PatternDocument document;
    if (!document.load(path, error))
        return false;
    replaceDocument(std::move(document), QFileInfo(path).absoluteFilePath());
    return true;
}

void MainWindow::clearRecovery() {
    if (recovery_ && !recovery_->clear())
        statusBar()->showMessage(tr("Could not remove the previous recovery snapshot."), 8000);
    recovery_.reset();
}

void MainWindow::autosaveProject() {
    if (!autosavePending_)
        return;
    if (!recovery_)
        recovery_ = std::make_unique<opc::ProjectRecovery>();
    QString error;
    if (!recovery_->capture(doc_, recoveryDirectory(), currentPath_, &error)) {
        statusBar()->showMessage(tr("Autosave failed: %1").arg(error), 8000);
        return;
    }
    autosavePending_ = false;
}

void MainWindow::offerProjectRecovery() {
    if (!opc::ProjectRecovery::available(recoveryDirectory()).isEmpty())
        recoverProject();
}

void MainWindow::recoverProject() {
    const auto paths = opc::ProjectRecovery::available(recoveryDirectory());
    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("Project Recovery"), tr("No recovery snapshots are available."));
        return;
    }
    QStringList labels;
    for (const auto& path : paths) {
        const QFileInfo info(path);
        labels.append(tr("%1 — %2 [%3]").arg(info.completeBaseName().mid(39),
            QLocale().toString(info.lastModified(), QLocale::ShortFormat), info.fileName().left(8)));
    }
    bool accepted = false;
    const auto choice = QInputDialog::getItem(this, tr("Project Recovery"),
        tr("Open a recovery snapshot as an unsaved project. Use Save As to choose where to keep it."),
        labels, 0, false, &accepted);
    if (!accepted)
        return;

    auto recovery = std::make_unique<opc::ProjectRecovery>();
    opc::PatternDocument document;
    QString error;
    if (!recovery->claim(paths.at(labels.indexOf(choice)), document, &error)) {
        QMessageBox::warning(this, tr("Recovery failed"), error);
        return;
    }
    if (!confirmDiscard())
        return;
    replaceDocument(std::move(document), {});
    recovery_ = std::move(recovery);
    recovered_ = true;
    setWindowModified(true);
}

bool MainWindow::saveDocument() {
    if (currentPath_.endsWith(".sofit", Qt::CaseInsensitive))
        return saveProject(currentPath_);
    return saveDocumentAs();
}

bool MainWindow::saveDocumentAs() {
    auto path = QFileDialog::getSaveFileName(this, tr("Save pattern"), currentPath_, tr("SOFIT (*.sofit)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".sofit", Qt::CaseInsensitive)) {
        path += ".sofit";
        if (QFileInfo::exists(path)
            && QMessageBox::question(this, tr("Replace pattern"),
                tr("Replace the existing file %1?").arg(QFileInfo(path).fileName())) != QMessageBox::Yes)
            return false;
    }
    return saveProject(path);
}

bool MainWindow::saveProject(const QString& path) {
    QString error;
    if (!doc_.save(path, &error)) {
        QMessageBox::warning(this, tr("Save failed"), error);
        return false;
    }
    const bool locationChanged = currentPath_ != path;
    currentPath_ = path;
    setWindowFilePath(path);
    recovered_ = false;
    autosavePending_ = false;
    undo_.setClean();
    setWindowModified(false);
    clearRecovery();
    if (locationChanged && autoSyncLibrary_->isChecked())
        librarySyncTimer_.start();
    return true;
}

void MainWindow::syncLibraryDependencies() {
    if (autoSyncLibrary_->isChecked() && QApplication::activeModalWidget()) {
        librarySyncTimer_.start();
        return;
    }
    librarySyncTimer_.stop();
    QSet<QString> watched;
    auto candidate = doc_;
    QString error;
    const bool enabled = autoSyncLibrary_->isChecked();
    const bool refreshed = enabled && candidate.refreshLibraryDependencies(currentPath_, &error, &watched);
    QSet<QString> paths;
    for (const auto& source : watched) {
        QFileInfo info(source);
        if (!info.isFile()) {
            info = QFileInfo(info.absolutePath());
            while (!info.isDir() && info.absolutePath() != info.absoluteFilePath())
                info = QFileInfo(info.absolutePath());
        }
        if (info.exists())
            paths.insert(info.absoluteFilePath());
    }
    const auto registered = libraryWatcher_.files() + libraryWatcher_.directories();
    const QSet<QString> previous(registered.begin(), registered.end());
    const auto removed = (previous - paths).values();
    if (!removed.isEmpty())
        libraryWatcher_.removePaths(removed);
    const auto added = (paths - previous).values();
    const auto failedPaths = added.isEmpty() ? QStringList{} : libraryWatcher_.addPaths(added);
    if (refreshed && candidate.libraryInstances() != doc_.libraryInstances()) {
        applyPatternEdit(std::move(candidate), autoSyncLibrary_->text(), true);
        statusBar()->showMessage(QStringLiteral("Связанные конструкции обновлены."), 5000);
    }
    if (enabled && !refreshed)
        statusBar()->showMessage(error, 10000);
    else if (!failedPaths.isEmpty())
        statusBar()->showMessage(QStringLiteral("Не удалось отслеживать: %1").arg(failedPaths.join(QStringLiteral(", "))), 10000);
}

opc::PatternDocument MainWindow::exportPattern(const QString& size, QVector<bool>& visibility,
                                                QVector<opc::PieceGeometry>& geometry) const {
    QVector<qsizetype> sourceIndices;
    auto pattern = opc::gradedPattern(doc_, size, &sourceIndices);
    for (const auto index : sourceIndices)
        visibility.append(index < 0 || view_->entityVisible(index));
    if (size.isEmpty() && doc_.sizeCutHistory().isEmpty())
        geometry = pieces_;
    else {
        for (qsizetype i = 0; i < pattern.pieces().size(); ++i)
            geometry.append(opc::analyzePiece(pattern, i));
    }
    return pattern;
}

void MainWindow::importDxf() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Import DXF"), {}, tr("DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    QString error;
    const auto drawing = opc::readDxf(path, error);
    if (!drawing) {
        QMessageBox::warning(this, tr("Import DXF"), error);
        return;
    }
    DxfImportDialog dialog(*drawing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto updated = drawing->appendTo(doc_, dialog.selectedLayers(), dialog.millimetersPerUnit());
    if (updated.entities().size() == doc_.entities().size())
        return;
    view_->cancelDrawing();
    undo_.push(new PatternCommand(doc_, std::move(updated), tr("Import DXF"), view_));
    view_->fitDrawing();
}

void MainWindow::exportDxf() {
    if (QMessageBox::question(this, tr("Export DXF"),
            tr("DXF contains exchange geometry for one size. Keep a SOFIT project to preserve "
               "formulas, grading rules, modeling history, seam links and fabric layout."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok)
        return;
    QString targetSize;
    if (!doc_.production().sizes.isEmpty()) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Export DXF"));
        QFormLayout form(&dialog);
        QComboBox sizes;
        fillExportSizes(sizes, doc_);
        form.addRow(tr("Size"), &sizes);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form.addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        targetSize = sizes.currentData().toString();
    }
    auto path = QFileDialog::getSaveFileName(this, tr("Export DXF"), {}, tr("DXF (*.dxf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".dxf", Qt::CaseInsensitive)) {
        path += ".dxf";
        if (QFileInfo::exists(path)
            && QMessageBox::question(this, tr("Replace DXF"),
                tr("Replace the existing file %1?").arg(QFileInfo(path).fileName())) != QMessageBox::Yes)
            return;
    }
    QVector<bool> visibility;
    QVector<opc::PieceGeometry> geometry;
    const auto pattern = exportPattern(targetSize, visibility, geometry);
    QString error;
    if (!opc::exportPatternDxf(path, pattern, visibility, geometry, error))
        QMessageBox::warning(this, tr("Export failed"), error);
    else
        statusBar()->showMessage(tr("DXF exported"), 3000);
}

void MainWindow::exportPdf() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export PDF for Printing"));
    QFormLayout form(&dialog);
    QComboBox sizes;
    fillExportSizes(sizes, doc_);
    if (sizes.count() > 1)
        form.addRow(tr("Size"), &sizes);
    QComboBox paper;
    paper.addItem(QStringLiteral("A4"), QPageSize::A4);
    paper.addItem(QStringLiteral("A3"), QPageSize::A3);
    form.addRow(tr("Paper"), &paper);
    QComboBox orientation;
    orientation.addItem(tr("Portrait"), QPageLayout::Portrait);
    orientation.addItem(tr("Landscape"), QPageLayout::Landscape);
    form.addRow(tr("Orientation"), &orientation);
    QLabel instructions(tr("Print at actual size (100%). Large drawings span multiple sheets with 10 mm overlap."));
    instructions.setWordWrap(true);
    form.addRow(&instructions);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto path = QFileDialog::getSaveFileName(this, tr("Export PDF for Printing"), {}, tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";

    QVector<bool> visibility;
    QVector<opc::PieceGeometry> geometry;
    const auto pattern = exportPattern(sizes.currentData().toString(), visibility, geometry);
    QVector<QPolygonF> allowances;
    for (const auto& piece : geometry)
        allowances.append(piece.allowance);

    const opc::PatternPdfOptions options{
        static_cast<QPageSize::PageSizeId>(paper.currentData().toInt()),
        static_cast<QPageLayout::Orientation>(orientation.currentData().toInt())
    };
    QString error;
    if (!opc::exportPatternPdf(path, pattern, visibility, allowances, options, error))
        QMessageBox::warning(this, tr("Export failed"), error);
    else
        statusBar()->showMessage(tr("PDF exported"), 3000);
}

void MainWindow::exportSvg() {
    auto path = QFileDialog::getSaveFileName(this, tr("Export SVG"), {}, tr("SVG (*.svg)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".svg", Qt::CaseInsensitive))
        path += ".svg";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return;
    }

    const auto& entities = doc_.entities();
    const auto isExported = [this](const opc::Entity& entity) {
        return doc_.layerVisible(entity.layer);
    };
    QRectF bounds;
    for (const auto& entity : entities) {
        if (!isExported(entity))
            continue;
        QRectF extent = QRectF(entity.a, entity.b).normalized();
        if (entity.shape == opc::Shape::Circle) {
            const auto radius = QLineF(entity.a, entity.b).length();
            extent = QRectF(entity.a - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
        }
        if (entity.shape == opc::Shape::Curve) {
            QPainterPath curve(entity.a);
            curve.quadTo(entity.control, entity.b);
            extent = curve.boundingRect();
        }
        bounds = bounds.united(extent.adjusted(-5, -5, 5, 5));
    }
    if (doc_.layerVisible(opc::Layer::Allowance)) {
        for (const auto& geometry : pieces_) {
            if (!geometry.allowance.isEmpty())
                bounds = bounds.united(geometry.allowance.boundingRect().adjusted(-5, -5, 5, 5));
        }
    }
    if (bounds.isNull())
        bounds = QRectF(-500, -350, 1000, 700);
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("svg");
    xml.writeDefaultNamespace("http://www.w3.org/2000/svg");
    const auto number = [](qreal value) { return QString::number(value, 'g', 16); };
    const auto attribute = [&xml, &number](const QString& name, qreal value) {
        xml.writeAttribute(name, number(value));
    };
    xml.writeAttribute("width", number(bounds.width()) + "mm");
    xml.writeAttribute("height", number(bounds.height()) + "mm");
    xml.writeAttribute("viewBox", QStringLiteral("%1 %2 %3 %4").arg(
        number(bounds.x()), number(bounds.y()), number(bounds.width()), number(bounds.height())));
    for (const auto& entity : entities) {
        if (!isExported(entity))
            continue;
        switch (entity.shape) {
        case opc::Shape::Point:
            xml.writeStartElement("circle");
            attribute("cx", entity.a.x());
            attribute("cy", entity.a.y());
            attribute("r", 3);
            xml.writeAttribute("fill", "#1e64be");
            break;
        case opc::Shape::Line:
            xml.writeStartElement("line");
            attribute("x1", entity.a.x());
            attribute("y1", entity.a.y());
            attribute("x2", entity.b.x());
            attribute("y2", entity.b.y());
            break;
        case opc::Shape::Circle:
            xml.writeStartElement("circle");
            attribute("cx", entity.a.x());
            attribute("cy", entity.a.y());
            attribute("r", QLineF(entity.a, entity.b).length());
            xml.writeAttribute("fill", "none");
            break;
        case opc::Shape::Curve:
            xml.writeStartElement("path");
            xml.writeAttribute("d", QStringLiteral("M %1 %2 Q %3 %4 %5 %6")
                .arg(number(entity.a.x()), number(entity.a.y()),
                     number(entity.control.x()), number(entity.control.y()),
                     number(entity.b.x()), number(entity.b.y())));
            xml.writeAttribute("fill", "none");
            break;
        case opc::Shape::Rectangle: {
            const auto rect = QRectF(entity.a, entity.b).normalized();
            xml.writeStartElement("rect");
            attribute("x", rect.x());
            attribute("y", rect.y());
            attribute("width", rect.width());
            attribute("height", rect.height());
            xml.writeAttribute("fill", "none");
            break;
        }
        }
        if (entity.shape != opc::Shape::Point) {
            xml.writeAttribute("stroke", entity.layer == opc::Layer::Main ? "#1e64be" : "#58657d");
            if (entity.layer == opc::Layer::Construction)
                xml.writeAttribute("stroke-dasharray", "3 2");
            xml.writeAttribute("stroke-width", "0.5");
        }
        xml.writeEndElement();
    }
    if (doc_.layerVisible(opc::Layer::Allowance)) {
        for (const auto& geometry : pieces_) {
            if (geometry.allowance.isEmpty())
                continue;
            QStringList points;
            for (const auto& point : geometry.allowance)
                points.append(number(point.x()) + "," + number(point.y()));
            xml.writeStartElement("polygon");
            xml.writeAttribute("points", points.join(' '));
            xml.writeAttribute("fill", "none");
            xml.writeAttribute("stroke", "#ac5f13");
            xml.writeAttribute("stroke-width", "0.5");
            xml.writeAttribute("stroke-dasharray", "3 2");
            xml.writeEndElement();
        }
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    if (xml.hasError() || !file.commit())
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
}
