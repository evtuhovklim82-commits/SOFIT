#pragma once

#include <QList>
#include <array>

class QAction;
class QMainWindow;
class QMenu;

struct WorkflowShortcut {
    const char* id;
    QAction* action;
};

void installWorkflow(QMainWindow& window, const std::array<QMenu*, 5>& stages, QMenu& viewMenu,
                     const QList<WorkflowShortcut>& shortcuts);
