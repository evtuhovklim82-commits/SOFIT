#include "TemplateWizard.h"
#include "PatternTemplates.h"
#include "BodiceDraft.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QWizard>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

#include <cmath>

std::optional<opc::PatternDocument> createPatternFromTemplate(QWidget* parent) {
    QWizard wizard(parent);
    const auto tr = [](const char* text) { return QCoreApplication::translate("TemplateWizard", text); };
    wizard.setWindowTitle(tr("New Pattern from Template"));
    wizard.setOption(QWizard::HaveHelpButton);
    wizard.setButtonText(QWizard::HelpButton, QStringLiteral("Справка"));
    const auto showHelp = [&wizard] {
        QMessageBox::information(&wizard, QStringLiteral("Базовые конструкции"),
            wizard.currentId() == 0
                ? QStringLiteral("Выберите основу изделия. Для прилегающей юбки мастер строит перед и спинку "
                                 "со сгибом, вытачками и сопряжением боковых срезов.")
                : QStringLiteral("Введите мерки и прибавки в миллиметрах. Базовые конструкции строятся "
                                 "по линиям шва. Проверьте сопряжение срезов и посадку на макете "
                                 "перед назначением припусков и раскроем."));
    };
    QObject::connect(&wizard, &QWizard::helpRequested, &wizard, showHelp);
    auto* helpShortcut = new QShortcut(QKeySequence(Qt::Key_F1), &wizard);
    QObject::connect(helpShortcut, &QShortcut::activated, &wizard, showHelp);


    auto* templatePage = new QWizardPage;
    templatePage->setTitle(tr("Choose a template"));
    auto* selection = new QFormLayout(templatePage);
    enum Template { Gathered, ALine, Bodice, Trousers, Dress, Apron, Pocket, FittedSkirt, ShirtCuff };
    auto* style = new QComboBox;
    style->addItem(tr("Gathered skirt"));
    style->addItem(tr("A-line skirt"));
    style->addItem(tr("Bodice and set-in sleeve (fitting block)"));
    style->addItem(tr("Trousers (fitting block)"));
    style->addItem(tr("Sleeveless dress with waist seam (fitting block)"));
    style->addItem(tr("Bib apron with waist and neck ties"));
    style->addItem(tr("Rounded patch pocket"));
    style->addItem(QStringLiteral("Прилегающая юбка с вытачками"));
    style->addItem(QStringLiteral("Цельнокроеная манжета рубашки"));
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
    const auto* dartShare = measurement(tr("Waist suppression taken by the dart (%)"), 50, 10, 90);
    const auto* dartPosition = measurement(tr("Dart position from center (%)"), 50, 30, 70);
    const int sleeveStart = form->rowCount();
    const auto* upperArm = measurement(tr("Upper arm circumference"), 300, 120, 800);
    const auto* upperArmEase = measurement(tr("Upper arm wearing ease"), 50, 0, 200);
    const auto* cuff = measurement(tr("Finished sleeve opening circumference"), 220, 100, 700);
    const auto* sleeveLength = measurement(tr("Sleeve crown to hem"), 570, 180, 900);
    const auto* capHeight = measurement(tr("Sleeve cap height"), 87, 30, 350);
    const auto* capEase = measurement(tr("Target ease per cap section"), 4, 0, 30);
    const auto* tolerance = measurement(tr("Tolerance per cap section"), 3, 0, 30);

    auto* sleeveStyle = new QComboBox;
    sleeveStyle->addItems({tr("Fitted"), tr("Wide hem"), tr("Puff with gathered cap and hem band"), tr("Two-piece with shaped elbow seam")});
    form->addRow(tr("Sleeve style"), sleeveStyle);
    auto* extraWidth = measurement(tr("Added hem width per sleeve"), 100, 10, 500);
    auto* crownRise = measurement(tr("Added crown height"), 30, 5, 150);
    auto* capGather = measurement(tr("Target gathering per cap section"), 65, 0, 200);

    auto* elbowShape = measurement(tr("Elbow seam shaping"), 25, 5, 60);
    auto* elbowLevel = measurement(tr("Elbow level from underarm to hem (%)"), 50, 25, 75);
    const int finishStart = form->rowCount();
    auto* neckFinish = new QComboBox;
    neckFinish->addItems({tr("None"), tr("Flat collar"), tr("Straight stand"), tr("Neck facing"), tr("Pointed collar")});
    form->addRow(tr("Neckline finish"), neckFinish);
    auto* finishDepth = measurement(tr("Collar, stand or facing depth"), 35, 10, 120);

    auto* pointExtension = measurement(tr("Collar point extension"), 30, 5, 100);
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

    auto* linkedTrousers = new QCheckBox(tr("Track side seams and inseams with paired notches"));
    linkedTrousers->setToolTip(tr("Add a seam link for the side and one for each section of the inseam. Set cutting allowances after the fitting."));
    form->addRow(linkedTrousers);

    const int dressStart = form->rowCount();
    const auto* skirtLength = measurement(tr("Waist to skirt hem"), 620, 200, 1500);
    const auto* hemFlare = measurement(tr("Extra skirt hem circumference beyond eased hip"), 160, 0, 1500);

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

    const int pocketStart = form->rowCount();
    const auto* pocketWidth = measurement(tr("Finished pocket width"), 150, 25, 600);
    const auto* pocketHeight = measurement(tr("Finished pocket height"), 170, 25, 600);
    const auto* pocketRadius = measurement(tr("Corner radius setting"), 22, 1, 200);
    const auto* pocketTurnback = measurement(tr("Top turnback"), 25, 5, 100);
    auto* pocketInstructions = new QLabel(tr("Fold the turnback at the construction line. "
        "The cutting contour adds 8 mm around the pocket. Check the fit on the garment before cutting."));
    pocketInstructions->setWordWrap(true);
    form->addRow(pocketInstructions);

    const int cuffStart = form->rowCount();
    const auto* cuffWrist = measurement(QStringLiteral("Обхват запястья"), 170, 80, 350);
    const auto* cuffEase = measurement(QStringLiteral("Прибавка к обхвату"), 20, 1, 80);
    const auto* cuffOverlap = measurement(QStringLiteral("Ширина застёжки"), 25, 5, 80);
    const auto* cuffDepth = measurement(QStringLiteral("Готовая ширина манжеты"), 55, 15, 150);
    auto* cuffInstructions = new QLabel(QStringLiteral(
        "Выкроите две манжеты. Линии построения отмечают сгиб и нахлёст. "
        "Ориентиры пуговицы и петли расположены на одной половине детали. Припуск 10 мм."));
    cuffInstructions->setWordWrap(true);
    form->addRow(cuffInstructions);

    const auto showFields = [=](int index) {
        if (index == Pocket || index == ShirtCuff) {
            for (int row = 0; row < form->rowCount(); ++row)
                form->setRowVisible(row, index == Pocket ? row >= pocketStart && row < cuffStart
                                                         : row >= cuffStart);
            return;
        }
        for (int row = 0; row < form->rowCount(); ++row) {
            const bool visible = row >= pocketStart ? false
                : row >= apronStart ? index == Apron
                : index == Apron ? false
                : row < fittingStart ? index < Bodice
                : row < bodiceStart ? index >= Bodice
                : row < sleeveStart ? index == Bodice || index == Dress
                : row < finishStart ? index == Bodice
                : row < hipStart ? index == Bodice || index == Dress
                : row < trousersStart ? index == Trousers || index == Dress || index == FittedSkirt
                : row < dressStart ? index == Trousers : index == Dress || index == FittedSkirt;
            form->setRowVisible(row, visible);
        }
        form->setRowVisible(waist, index >= Bodice && index != Apron);
        form->setRowVisible(waistEase, index >= Bodice && index != Apron);
        form->setRowVisible(flare, index == ALine);
        const auto selectedSleeve = static_cast<opc::SleeveStyle>(sleeveStyle->currentIndex());
        form->setRowVisible(extraWidth, index == Bodice
            && (selectedSleeve == opc::SleeveStyle::Wide || selectedSleeve == opc::SleeveStyle::Puff));
        form->setRowVisible(elbowShape, index == Bodice && selectedSleeve == opc::SleeveStyle::TwoPiece);
        form->setRowVisible(elbowLevel, index == Bodice && selectedSleeve == opc::SleeveStyle::TwoPiece);
        form->setRowVisible(pointExtension, (index == Bodice || index == Dress)
            && neckFinish->currentIndex() == int(opc::NeckFinish::PointedCollar));
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
        } else if (index == Pocket) {
            if (auto document = opc::draftPatchPocket(
                    {pocketWidth->value(), pocketHeight->value(), pocketRadius->value(), pocketTurnback->value()},
                    &error))
                return document;
        } else if (index == ShirtCuff) {
            if (auto document = opc::draftShirtCuff(
                    {cuffWrist->value(), cuffEase->value(), cuffOverlap->value(), cuffDepth->value()}, &error))
                return document;
        } else if (index == FittedSkirt) {
            if (auto document = opc::draftFittedSkirt(waist->value(), waistEase->value(), {
                    fittedHip->value(), hipEase->value(), hipDepth->value(), skirtLength->value(),
                    hemFlare->value(), lowerDart->value()
                }, &error))
                return document;
        } else if (index == Trousers) {
            const opc::TrouserMeasurements trousers{
                waist->value(), fittedHip->value(), waistEase->value(), hipEase->value(),
                hipDepth->value(), rise->value(), kneeDepth->value(), trouserLength->value(),
                knee->value(), hem->value(), lowerDart->value()
            };
            auto document = linkedTrousers->isChecked()
                ? opc::draftTrousersWithSeamLinks(trousers, &error) : opc::draftTrousers(trousers, &error);
            if (document)
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
                crownRise->value(), capGather->value(), elbowShape->value(), pointExtension->value(),
                dartShare->value(), dartPosition->value(), elbowLevel->value(), capEase->value()
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
