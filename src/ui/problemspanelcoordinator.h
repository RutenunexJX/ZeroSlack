#ifndef PROBLEMSPANELCOORDINATOR_H
#define PROBLEMSPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QStringList>
#include <QTreeWidget>
#include "diagnosticservice.h"

#include <functional>
#include <memory>

struct DiagnosticPanelReport;
class QAbstractButton;
class QButtonGroup;
class QStackedWidget;

class ProblemsPanelCoordinator : public QObject
{
public:
    explicit ProblemsPanelCoordinator(QWidget* parent);

    void setCurrentFileProvider(std::function<QString()> provider);
    void setWorkspaceFilesProvider(std::function<QStringList()> provider);
    void setWorkspaceRootProvider(std::function<QString()> provider);
    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setAnalysisState(const QString& state);

    void update();

    QDockWidget* dock() const { return problemsDock; }
    QTreeWidget* tree() const { return problemsTree; }
    QComboBox* scopeCombo() const { return problemsScopeCombo; }
    DiagnosticSeverityFilter severityFilter() const;
    QAbstractButton* severityButton(DiagnosticSeverityFilter filter) const;
    QLabel* emptyLabel() const { return emptyStateLabel; }
    QLabel* stateLabel() const { return diagnosticStateLabel; }
    bool showsCurrentFileScope() const;
    bool isVisibleToUser() const;
    int updateInvocationCount() const { return updateInvocations; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QDockWidget* problemsDock = nullptr;
    QTreeWidget* problemsTree = nullptr;
    QComboBox* problemsScopeCombo = nullptr;
    QButtonGroup* severityButtons = nullptr;
    QStackedWidget* contentStack = nullptr;
    QLabel* emptyStateLabel = nullptr;
    QLabel* diagnosticStateLabel = nullptr;
    QString lastDiagnosticActivityMessage;
    QString externalAnalysisState;
    int updateInvocations = 0;
    std::shared_ptr<const DiagnosticPanelReport> displayedReport;

    std::function<QString()> currentFileProvider;
    std::function<QStringList()> workspaceFilesProvider;
    std::function<QString()> workspaceRootProvider;
    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;
    void navigateItem(QTreeWidgetItem* item);
    void updateStatus(const DiagnosticPanelReport& report);
};

#endif // PROBLEMSPANELCOORDINATOR_H
