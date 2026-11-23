#include "TemplateWizard.h"
#include "PatternTemplates.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QWizard>

std::optional<opc::PatternDocument> createPatternFromTemplate(QWidget* parent) {
    QWizard wizard(parent);
    const auto tr = [](const char* text) { return QCoreApplication::translate("TemplateWizard", text); };
    wizard.setWindowTitle(tr("New Pattern from Template"));

    auto* templatePage = new QWizardPage;
    templatePage->setTitle(tr("Choose a skirt"));
    auto* selection = new QFormLayout(templatePage);
    auto* style = new QComboBox;
    style->addItem(tr("Gathered skirt"));
    style->addItem(tr("A-line skirt"));
    selection->addRow(tr("Template"), style);
    auto* note = new QLabel(tr("Both templates use two identical panels and a fold-over elastic casing. "
                               "The new pattern will open as an editable parametric draft."));
    note->setWordWrap(true);
    selection->addRow(note);
    wizard.addPage(templatePage);

    auto* measurementsPage = new QWizardPage;
    measurementsPage->setTitle(tr("Enter measurements"));
    auto* form = new QFormLayout(measurementsPage);
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
    form->setRowVisible(flare, false);
    QObject::connect(style, &QComboBox::currentIndexChanged, &wizard, [=](int index) {
        form->setRowVisible(flare, index == 1);
        ease->setValue(index == 1 ? 80 : 350);
    });
    auto* instructions = new QLabel(tr("Cut two panels. Join the side seams, turn down the top at the "
                                       "casing fold, and fit the elastic at the waist. "
                                       "The cutting contour includes a 10 mm allowance."));
    instructions->setWordWrap(true);
    form->addRow(instructions);
    wizard.addPage(measurementsPage);

    if (wizard.exec() != QDialog::Accepted)
        return std::nullopt;
    QString error;
    auto document = opc::draftPullOnSkirt(style->currentIndex() == 1 ? opc::SkirtStyle::ALine : opc::SkirtStyle::Gathered,
                                        {hip->value(), length->value(), ease->value(), flare->value(), casing->value()},
                                        &error);
    if (!document)
        QMessageBox::warning(parent, wizard.windowTitle(), error);
    return document;
}
