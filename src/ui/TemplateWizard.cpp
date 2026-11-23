#include "TemplateWizard.h"
#include "PatternTemplates.h"
#include "BodiceDraft.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QWizard>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

std::optional<opc::PatternDocument> createPatternFromTemplate(QWidget* parent) {
    QWizard wizard(parent);
    const auto tr = [](const char* text) { return QCoreApplication::translate("TemplateWizard", text); };
    wizard.setWindowTitle(tr("New Pattern from Template"));

    auto* templatePage = new QWizardPage;
    templatePage->setTitle(tr("Choose a template"));
    auto* selection = new QFormLayout(templatePage);
    enum Template { Gathered, ALine, Bodice, Trousers, Dress, Apron };
    auto* style = new QComboBox;
    style->addItem(tr("Gathered skirt"));
    style->addItem(tr("A-line skirt"));
    style->addItem(tr("Bodice and set-in sleeve (fitting block)"));
    style->addItem(tr("Trousers (fitting block)"));
    style->addItem(tr("Sleeveless dress with waist seam (fitting block)"));
    style->addItem(tr("Bib apron with waist and neck ties"));
    selection->addRow(tr("Template"), style);
    auto* note = new QLabel(tr("All templates open as editable parametric drafts. "
                               "Fitting blocks use sewing lines and are intended for a fitting sample."));
    note->setWordWrap(true);
    selection->addRow(note);
    wizard.addPage(templatePage);

    auto* measurementsPage = new QWizardPage;
    measurementsPage->setTitle(tr("Enter measurements"));
    auto* pageLayout = new QVBoxLayout(measurementsPage);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(260);
    auto* fields = new QWidget;
    auto* form = new QFormLayout(fields);
    scroll->setWidget(fields);
    pageLayout->addWidget(scroll);
    const auto measurement = [&](const QString& label, double initial, double minimum, double maximum) {
        auto* input = new QDoubleSpinBox;
        input->setRange(minimum, maximum);
        input->setDecimals(1);
        input->setSuffix(tr(" mm"));
        input->setValue(initial);
        form->addRow(label, input);
        return input;
    };
    const auto* hip = measurement(tr("Hip circumference"), 1000, 300, 2500);
    const auto* length = measurement(tr("Finished length"), 600, 200, 1500);
    auto* ease = measurement(tr("Extra opening circumference"), 350, 40, 1500);
    auto* flare = measurement(tr("Extra hem circumference"), 400, 40, 2000);
    const auto* casing = measurement(tr("Casing depth"), 35, 15, 100);
    auto* instructions = new QLabel(tr("Cut two panels. Join the side seams, turn down the top at the "
                                       "casing fold, and fit the elastic at the waist. "
                                       "The cutting contour includes a 10 mm allowance."));
    instructions->setWordWrap(true);
    form->addRow(instructions);

    const int fittingStart = form->rowCount();
    auto* fittingNote = new QLabel(tr("Make a fitting sample before adding allowances. "
                                      "Check joining seams again after editing measurements or grading."));
    fittingNote->setWordWrap(true);
    form->addRow(fittingNote);
    const int bodiceStart = form->rowCount();
    const auto* bust = measurement(tr("Bust circumference"), 920, 400, 2000);
    auto* waist = measurement(tr("Waist circumference"), 740, 350, 2000);
    const auto* bodiceLength = measurement(tr("Neck-side level to waist"), 420, 200, 800);
    const auto* armhole = measurement(tr("Neck-side level to underarm"), 210, 100, 450);
    const auto* neckWidth = measurement(tr("Center to neck-side point, horizontal"), 68, 25, 180);
    const auto* frontNeck = measurement(tr("Front neckline depth"), 75, 20, 250);
    const auto* backNeck = measurement(tr("Back neckline depth"), 22, 5, 150);
    const auto* shoulderRun = measurement(tr("Shoulder run, horizontal"), 116, 45, 200);
    const auto* shoulderDrop = measurement(tr("Shoulder drop, vertical"), 38, 5, 100);
    const auto* bustEase = measurement(tr("Bust wearing ease"), 60, 0, 300);
    auto* waistEase = measurement(tr("Waist wearing ease"), 40, 0, 300);
    const auto* dart = measurement(tr("Waist dart length"), 115, 20, 250);
    const int sleeveStart = form->rowCount();
    const auto* upperArm = measurement(tr("Upper arm circumference"), 300, 120, 800);
    const auto* upperArmEase = measurement(tr("Upper arm wearing ease"), 50, 0, 200);
    const auto* cuff = measurement(tr("Finished sleeve opening circumference"), 220, 100, 700);
    const auto* sleeveLength = measurement(tr("Sleeve crown to hem"), 570, 180, 900);
    const auto* capHeight = measurement(tr("Sleeve cap height"), 87, 30, 350);
    const auto* capEase = measurement(tr("Target ease per cap section"), 4, 0, 30);
    const auto* tolerance = measurement(tr("Tolerance per cap section"), 3, 0, 30);

    auto* sleeveStyle = new QComboBox;
    sleeveStyle->addItems({tr("Fitted"), tr("Wide hem"), tr("Puff with gathered cap and hem band")});
    form->addRow(tr("Sleeve style"), sleeveStyle);
    auto* extraWidth = measurement(tr("Added hem width per sleeve"), 100, 10, 500);
    auto* crownRise = measurement(tr("Added crown height"), 30, 5, 150);
    auto* capGather = measurement(tr("Target gathering per cap section"), 65, 0, 200);

    const int finishStart = form->rowCount();
    auto* neckFinish = new QComboBox;
    neckFinish->addItems({tr("None"), tr("Flat collar"), tr("Straight stand"), tr("Neck facing")});
    form->addRow(tr("Neckline finish"), neckFinish);
    auto* finishDepth = measurement(tr("Collar, stand or facing depth"), 35, 10, 120);

    const int hipStart = form->rowCount();
    const auto* fittedHip = measurement(tr("Hip circumference"), 1000, 350, 2500);
    const auto* hipEase = measurement(tr("Hip wearing ease"), 60, 0, 300);
    const auto* hipDepth = measurement(tr("Waist to hip level"), 195, 50, 400);
    const auto* lowerDart = measurement(tr("Lower waist dart length"), 100, 20, 250);

    const int trousersStart = form->rowCount();
    const auto* rise = measurement(tr("Waist to seat level"), 270, 100, 600);
    const auto* kneeDepth = measurement(tr("Waist to knee level"), 600, 200, 1000);
    const auto* trouserLength = measurement(tr("Waist to trouser hem"), 1030, 300, 1600);
    const auto* knee = measurement(tr("Finished knee circumference per leg"), 460, 100, 1200);
    const auto* hem = measurement(tr("Finished hem circumference per leg"), 380, 100, 1200);

    const int dressStart = form->rowCount();
    const auto* skirtLength = measurement(tr("Waist to dress hem"), 620, 200, 1500);
    const auto* hemFlare = measurement(tr("Extra dress hem circumference beyond eased hip"), 160, 0, 1500);

    const int apronStart = form->rowCount();
    const auto* apronWidth = measurement(tr("Apron width at hem"), 700, 200, 1600);
    const auto* apronLength = measurement(tr("Bib top to hem"), 780, 200, 1600);
    const auto* bibWidth = measurement(tr("Bib width at top"), 260, 60, 800);
    const auto* bibHeight = measurement(tr("Bib top to waist"), 250, 60, 600);
    const auto* waistTie = measurement(tr("Length of one waist tie"), 600, 50, 1500);
    const auto* neckTie = measurement(tr("Length of one neck tie"), 420, 50, 1200);
    const auto* tieWidth = measurement(tr("Finished tie width"), 25, 10, 100);
    auto* apronInstructions = new QLabel(tr("Cut one body, two waist ties and two neck ties. "
        "Fold each tie lengthwise, sew and turn it. Attach ties at the top bib corners and waist. "
        "The template adds a uniform 10 mm allowance. Use Pattern → Diagnose Pieces by Size "
        "to check the cut outlines and quantities before arranging the fabric."));
    apronInstructions->setWordWrap(true);
    form->addRow(apronInstructions);

    const auto showFields = [=](int index) {
        for (int row = 0; row < form->rowCount(); ++row) {
            const bool visible = row >= apronStart ? index == Apron
                : index == Apron ? false
                : row < fittingStart ? index < Bodice
                : row < bodiceStart ? index >= Bodice
                : row < sleeveStart ? index == Bodice || index == Dress
                : row < finishStart ? index == Bodice
                : row < hipStart ? index == Bodice || index == Dress
                : row < trousersStart ? index == Trousers || index == Dress
                : row < dressStart ? index == Trousers : index == Dress;
            form->setRowVisible(row, visible);
        }
        form->setRowVisible(waist, index >= Bodice && index != Apron);
        form->setRowVisible(waistEase, index >= Bodice && index != Apron);
        form->setRowVisible(flare, index == ALine);
        form->setRowVisible(extraWidth, index == Bodice && sleeveStyle->currentIndex() > 0);
        form->setRowVisible(crownRise, index == Bodice && sleeveStyle->currentIndex() == 2);
        form->setRowVisible(capGather, index == Bodice && sleeveStyle->currentIndex() == 2);
        form->setRowVisible(finishDepth, (index == Bodice || index == Dress) && neckFinish->currentIndex() > 0);
        if (index < Bodice)
            ease->setValue(index == ALine ? 80 : 350);
    };
    QObject::connect(style, &QComboBox::currentIndexChanged, &wizard, showFields);
    QObject::connect(sleeveStyle, &QComboBox::currentIndexChanged, &wizard, [=] { showFields(style->currentIndex()); });
    QObject::connect(neckFinish, &QComboBox::currentIndexChanged, &wizard, [=] { showFields(style->currentIndex()); });
    showFields(0);
    wizard.addPage(measurementsPage);

    while (wizard.exec() == QDialog::Accepted) {
        QString error;
        const int index = style->currentIndex();
        if (index < Bodice) {
            auto document = opc::draftPullOnSkirt(
                index == ALine ? opc::SkirtStyle::ALine : opc::SkirtStyle::Gathered,
                {hip->value(), length->value(), ease->value(), flare->value(), casing->value()}, &error);
            if (document)
                return document;
        } else if (index == Apron) {
            if (auto document = opc::draftBibApron({
                    apronWidth->value(), apronLength->value(), bibWidth->value(), bibHeight->value(),
                    waistTie->value(), neckTie->value(), tieWidth->value()
                }, &error))
                return document;
        } else if (index == Trousers) {
            if (auto document = opc::draftTrousers({
                    waist->value(), fittedHip->value(), waistEase->value(), hipEase->value(),
                    hipDepth->value(), rise->value(), kneeDepth->value(), trouserLength->value(),
                    knee->value(), hem->value(), lowerDart->value()
                }, &error))
                return document;
        } else {
            const opc::BodiceMeasurements bodice{
                bust->value(), waist->value(), bodiceLength->value(), armhole->value(),
                neckWidth->value(), frontNeck->value(), backNeck->value(), shoulderRun->value(),
                shoulderDrop->value(), bustEase->value(), waistEase->value(), dart->value(),
                upperArm->value(), upperArmEase->value(), cuff->value(), sleeveLength->value(),
                capHeight->value()
            };
            const opc::BodiceOptions options{
                static_cast<opc::NeckFinish>(neckFinish->currentIndex()), finishDepth->value(),
                static_cast<opc::SleeveStyle>(sleeveStyle->currentIndex()), extraWidth->value(),
                crownRise->value(), capGather->value()
            };
            if (index == Dress) {
                if (auto document = opc::draftDress(bodice, {
                        fittedHip->value(), hipEase->value(), hipDepth->value(), skirtLength->value(),
                        hemFlare->value(), lowerDart->value()
                    }, options, &error))
                    return document;
                QMessageBox::warning(parent, wizard.windowTitle(), error);
                continue;
            }
            auto document = opc::draftBodiceAndSleeve(bodice, options, &error);
            if (document) {
                const auto lengths = opc::measureBodiceSeams(*document);
                if (lengths) {
                    const double front = lengths->frontCap - lengths->frontArmhole;
                    const double back = lengths->backCap - lengths->backArmhole;
                    const double target = capEase->value() + (options.sleeveStyle == opc::SleeveStyle::Puff
                        ? options.gatherPerCapSection : 0);
                    const bool within = std::abs(front - target) <= tolerance->value()
                        && std::abs(back - target) <= tolerance->value();
                    const auto mm = [](double value) { return QString::number(value, 'f', 2); };
                    const auto summary = tr("Front: armhole %1 mm, cap %2 mm, difference %3 mm.\n"
                                            "Back: armhole %4 mm, cap %5 mm, difference %6 mm.\n\n"
                                            "Target per section: %7 ± %8 mm. %9\n\n"
                                            "Open this fitting block?")
                        .arg(mm(lengths->frontArmhole), mm(lengths->frontCap), mm(front),
                             mm(lengths->backArmhole), mm(lengths->backCap), mm(back),
                             mm(target), mm(tolerance->value()),
                             within ? tr("Both sections are within tolerance.")
                                    : tr("At least one section is outside tolerance."));
                    if (QMessageBox::question(parent, wizard.windowTitle(), summary,
                                              QMessageBox::Yes | QMessageBox::No,
                                              within ? QMessageBox::Yes : QMessageBox::No) == QMessageBox::Yes)
                        return document;
                    continue;
                }
                error = tr("Could not measure both sleeve cap sections and armholes.");
            }
        }
        QMessageBox::warning(parent, wizard.windowTitle(), error);
    }
    return std::nullopt;
}
