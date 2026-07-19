#ifndef ACTIVITYLOGPANELCOORDINATOR_H
#define ACTIVITYLOGPANELCOORDINATOR_H

#include <QDockWidget>

class ActivityLogService;
class QPlainTextEdit;
class QPushButton;

class ActivityLogPanelCoordinator
{
public:
    explicit ActivityLogPanelCoordinator(QWidget* parent);

    QDockWidget* dock() const { return activityDock; }

private:
    QDockWidget* activityDock = nullptr;
    QPlainTextEdit* outputText = nullptr;
    QPushButton* clearButton = nullptr;
    ActivityLogService* service = nullptr;

    void appendExistingEvents();
};

#endif // ACTIVITYLOGPANELCOORDINATOR_H
