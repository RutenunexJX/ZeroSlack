#ifndef ACTIVITYLOGPANELCOORDINATOR_H
#define ACTIVITYLOGPANELCOORDINATOR_H

#include <QDockWidget>
#include <QStringList>

class ActivityLogService;
class QPlainTextEdit;
class QAction;

class ActivityLogPanelCoordinator : public QObject
{
public:
    explicit ActivityLogPanelCoordinator(QWidget* parent);

    QDockWidget* dock() const { return activityDock; }
    QAction* clearAction() const { return clearLogAction; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QDockWidget* activityDock = nullptr;
    QPlainTextEdit* outputText = nullptr;
    QAction* clearLogAction = nullptr;
    ActivityLogService* service = nullptr;
    QStringList pendingLines;
    bool flushQueued = false;
    bool rebuildFromService = false;
    quint64 pendingSequence = 0;

    void appendExistingEvents();
    void schedulePendingFlush();
    void flushPendingEvents();
    bool isVisibleToUser() const;
};

#endif // ACTIVITYLOGPANELCOORDINATOR_H
