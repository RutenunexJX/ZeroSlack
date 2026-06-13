#ifndef RTLINSIGHTSPANELCOORDINATOR_H
#define RTLINSIGHTSPANELCOORDINATOR_H

#include <QDockWidget>
#include <QString>
#include <QTreeWidget>

#include <functional>

class RtlInsightsPanelCoordinator
{
public:
    explicit RtlInsightsPanelCoordinator(QWidget* parent);

    void setNavigationHandler(std::function<void(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);

    void updateModuleContext(const QString& fileName,
                             const QString& moduleName,
                             const QString& signalName = QString());
    void showModuleInsights(const QString& fileName,
                            const QString& moduleName,
                            const QString& signalName = QString());
    void refresh();

    QDockWidget* dock() const { return insightsDock; }
    QTreeWidget* tree() const { return insightsTree; }

private:
    QDockWidget* insightsDock = nullptr;
    QTreeWidget* insightsTree = nullptr;
    QString currentFileName;
    QString currentModuleName;
    QString currentSignalName;

    std::function<void(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // RTLINSIGHTSPANELCOORDINATOR_H
