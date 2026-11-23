#include "Workflow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString tr(const char* text) {
    return QCoreApplication::translate("Workflow", text);
}

QString shortcutKey(const WorkflowShortcut& command) {
    return QStringLiteral("workflow/shortcuts/") + QString::fromLatin1(command.id);
}

struct ShortcutBinding {
    QAction* action;
    QKeySequence sequence;
};

QString collision(const QList<ShortcutBinding>& used, const QKeySequence& sequence) {
    for (const auto& binding : used) {
        if (sequence.matches(binding.sequence) != QKeySequence::NoMatch
            || binding.sequence.matches(sequence) != QKeySequence::NoMatch)
            return binding.action->text();
    }
    return {};
}

bool supported(const QKeySequence& sequence) {
    if (sequence.isEmpty())
        return true;
    const auto key = sequence[0];
    return sequence.count() == 1 && key.key() != Qt::Key_unknown
        && key.keyboardModifiers() != Qt::AltModifier;
}

void showGuide(QMainWindow& window) {
    QDialog dialog(&window);
    dialog.setWindowTitle(tr("GRAFIS ⇄ SOFIT"));
    dialog.resize(850, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* guide = new QTextBrowser(&dialog);
    guide->setObjectName(QStringLiteral("grafisWorkflowGuide"));
    guide->setOpenExternalLinks(true);
    guide->document()->setDefaultStyleSheet(QStringLiteral("a { color: %1; }")
                                                .arg(guide->palette().color(QPalette::Text).name()));
    const auto header = tr("<p>Use this guide in either direction to find the same task in GRAFIS and SOFIT. "
                           "The tools and their options can differ.</p>"
                           "<table cellpadding='7' cellspacing='0' border='1'><thead>"
                           "<tr><th>Task</th><th>GRAFIS 26 documentation</th><th>SOFIT command</th></tr>"
                           "</thead><tbody>");
    const auto content = tr(
        "<tr><td>Start from a basic block</td><td><a href='https://help.grafis.de/en-gb/Content/Modellorganisation/Grundkonstrutionen_Holen.htm'>Library → Call basic blocks</a></td>"
        "<td>Construction → New Pattern from Template…</td></tr>"
        "<tr><td>Spread a pattern piece</td><td><a href='https://help.grafis.de/en-gb/Content/Tools/Sperren.htm'>Spread with lines</a></td>"
        "<td>Modeling → Slash and Spread… uses one straight construction guide across a closed straight-edged contour.</td></tr>"
        "<tr><td>Rotate construction objects by a fixed angle</td><td><a href='https://help.grafis.de/en-gb/Content/Tools/Drehen.htm'>Rotate with angle</a></td>"
        "<td>Modeling → Rotate Construction… Select named construction objects and choose a formula pivot. "
        "SOFIT recalculates the pivot for each size and uses the same angle.</td></tr>"
        "<tr><td>Move an open dart to a new position</td><td><a href='https://help.grafis.de/en-gb/Content/Tools/Abnaeherr.htm'>Relocate dart</a></td>"
        "<td>Modeling → Move Open Contour Dart… Select the two straight contour dart legs and one straight construction outlet.</td></tr>"
        "<tr><td>Grade using size measurements</td><td><a href='https://help.grafis.de/en-gb/Content/Gradieren/Masstabellen.htm'>Grading with measurement charts</a></td>"
        "<td>Grading → Sizes, Measurements and Grade Rules… → Measurements; enable Use size measurements in draft formulas. "
        "Use Grading → Size Overlays to compare sizes.</td></tr>"
        "<tr><td>Prepare an outline and seam allowance</td><td><a href='https://help.grafis.de/en-gb/Content/Verarbeitung/Der_Teile-Assistent.htm'>Part assistant</a></td>"
        "<td>Pieces → Create Piece from Selection…, then Piece, Contour and Allowance…</td></tr>"
        "<tr><td>Place pieces on fabric</td><td><a href='https://www.grafis.com/marker'>Marker making</a></td>"
        "<td>Layout → Fabric Layout…; set fabric width, add copies by piece and size, and place them manually.</td></tr>"
        "<tr><td>Exchange a single size</td><td><a href='https://help.grafis.de/en-gb/Content/ExportIImport/Import_von_Sprungwertschnitten.htm'>Import as AutoCAD DXF</a></td>"
        "<td>File → Export DXF…; choose the size, then check a known dimension and contours in GRAFIS. "
        "Keep the SOFIT project for editing. SOFIT currently exports DXF and PDF.</td></tr>");
    const auto footer = tr("</tbody></table><p>Exact GRAFIS keystrokes and click-by-click sequences "
                           "require verification with an experienced GRAFIS 26 operator.</p>");
    guide->setHtml(header + content + footer);
    layout->addWidget(guide);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
}

void installWorkflow(QMainWindow& window, const std::array<QMenu*, 5>& stages, QMenu& viewMenu,
                     const QList<WorkflowShortcut>& shortcuts) {
    QSettings settings;
    auto* toolbar = new QToolBar(tr("Workflow"), &window);
    toolbar->setObjectName(QStringLiteral("workflowToolbar"));
    toolbar->setMovable(false);
    window.addToolBarBreak(Qt::TopToolBarArea);
    window.addToolBar(Qt::TopToolBarArea, toolbar);

    for (auto* menu : stages) {
        auto* button = new QToolButton(toolbar);
        button->setText(QString(menu->title()).remove('&'));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::TabFocus);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        toolbar->addWidget(button);
    }
    viewMenu.addAction(toolbar->toggleViewAction());
    toolbar->setVisible(settings.value(QStringLiteral("workflow/showToolbar"), true).toBool());
    QObject::connect(toolbar->toggleViewAction(), &QAction::triggered, &window, [](bool visible) {
        QSettings().setValue(QStringLiteral("workflow/showToolbar"), visible);
    });

    QList<QKeySequence> defaults;
    QList<ShortcutBinding> fixed;
    auto windowActions = window.findChildren<QAction*>();
    windowActions.append(window.actions());
    for (const auto& command : shortcuts) {
        defaults.append(command.action->shortcut());
        windowActions.removeAll(command.action);
    }
    for (auto* action : windowActions) {
        for (const auto& sequence : action->shortcuts()) {
            if (!sequence.isEmpty())
                fixed.append({action, sequence});
        }
    }

    // Restore the set together so exchanging two shortcuts is valid.
    auto proposed = fixed;
    QList<QKeySequence> restored;
    bool valid = true;
    for (qsizetype i = 0; i < shortcuts.size(); ++i) {
        const auto& command = shortcuts[i];
        const auto value = settings.value(shortcutKey(command), defaults[i].toString(QKeySequence::PortableText));
        const auto sequence = QKeySequence::fromString(value.toString(), QKeySequence::PortableText);
        if ((!value.toString().isEmpty() && sequence.isEmpty()) || !supported(sequence)
            || (!sequence.isEmpty() && !collision(proposed, sequence).isEmpty()))
            valid = false;
        if (!sequence.isEmpty())
            proposed.append({command.action, sequence});
        restored.append(sequence);
    }
    if (valid) {
        for (qsizetype i = 0; i < shortcuts.size(); ++i)
            shortcuts[i].action->setShortcut(restored[i]);
    }

    auto* help = window.menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("GRAFIS ⇄ SOFIT"), &window, [&window] { showGuide(window); });
    viewMenu.addAction(tr("Keyboard Shortcuts…"), &window, [&window, shortcuts, defaults, fixed] {
        QDialog dialog(&window);
        dialog.setWindowTitle(tr("SOFIT Keyboard Shortcuts"));
        auto* form = new QFormLayout(&dialog);
        auto* description = new QLabel(tr("Set a personal shortcut for each command. "
                                          "Use one key combination per command. Leave a field empty to disable its shortcut."), &dialog);
        description->setWordWrap(true);
        form->addRow(description);
        QList<QKeySequenceEdit*> editors;
        for (const auto& command : shortcuts) {
            auto* editor = new QKeySequenceEdit(command.action->shortcut(), &dialog);
            editor->setObjectName(QString::fromLatin1(command.id));
            editor->setClearButtonEnabled(true);
            form->addRow(command.action->text(), editor);
            editors.append(editor);
        }
        auto* error = new QLabel(&dialog);
        error->setWordWrap(true);
        error->setAccessibleName(tr("Shortcut conflict"));
        form->addRow(error);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                            | QDialogButtonBox::RestoreDefaults, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
                         &dialog, [&] {
            for (qsizetype i = 0; i < editors.size(); ++i)
                editors[i]->setKeySequence(defaults[i]);
            error->clear();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            auto used = fixed;
            for (qsizetype i = 0; i < editors.size(); ++i) {
                const auto sequence = editors[i]->keySequence();
                if (sequence.isEmpty())
                    continue;
                if (!supported(sequence)) {
                    error->setText(tr("Use one key combination. Alt alone with a key is reserved for menu access."));
                    editors[i]->setFocus();
                    return;
                }
                const auto existing = collision(used, sequence);
                if (!existing.isEmpty()) {
                    error->setText(tr("%1 is already assigned to %2.")
                                       .arg(sequence.toString(QKeySequence::NativeText), existing));
                    editors[i]->setFocus();
                    return;
                }
                used.append({shortcuts[i].action, sequence});
            }
            QSettings settings;
            for (qsizetype i = 0; i < editors.size(); ++i) {
                settings.setValue(shortcutKey(shortcuts[i]),
                                  editors[i]->keySequence().toString(QKeySequence::PortableText));
            }
            settings.sync();
            if (settings.status() != QSettings::NoError) {
                error->setText(tr("Could not save the shortcuts in user settings."));
                return;
            }
            for (qsizetype i = 0; i < editors.size(); ++i)
                shortcuts[i].action->setShortcut(editors[i]->keySequence());
            dialog.accept();
        });
        dialog.exec();
    });
}
