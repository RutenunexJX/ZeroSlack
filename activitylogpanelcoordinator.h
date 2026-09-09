#ifndef ACTIVITYLOGPANELCOORDINATOR_H
#define ACTIVITYLOGPANELCOORDINATOR_H

#include <QDockWidget>
#include <QStringList>

class ActivityLogService;
class QPlainTextEdit;
class QPushButton;

class ActivityLogPanelCoordinator : public QObject
{
public:
    explicit ActivityLogPanelCoordinator(QWidget* parent);

    QDockWidget* dock() const { return activityDock; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QDockWidget* activityDock = nullptr;
    QPlainTextEdit* outputText = nullptr;
    QPushButton* clearButton = nullptr;
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
