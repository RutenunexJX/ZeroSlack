#ifndef PROBLEMSPANELCOORDINATOR_H
#define PROBLEMSPANELCOORDINATOR_H

#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QStringList>
#include <QTreeWidget>

#include <functional>

class ProblemsPanelCoordinator
{
public:
    explicit ProblemsPanelCoordinator(QWidget* parent);

    void setCurrentFileProvider(std::function<QString()> provider);
    void setWorkspaceFilesProvider(std::function<QStringList()> provider);
    void setNavigationHandler(std::function<bool(const QString&, int, int)> handler);
    void setStatusMessageHandler(std::function<void(const QString&, int)> handler);
    void setAnalysisState(const QString& state);

    void update();

    QDockWidget* dock() const { return problemsDock; }
    QTreeWidget* tree() const { return problemsTree; }
    QComboBox* scopeCombo() const { return problemsScopeCombo; }
    QComboBox* severityCombo() const { return problemsSeverityCombo; }
    QComboBox* bandCombo() const { return problemsBandCombo; }
    QLabel* summaryLabel() const { return diagnosticSummaryLabel; }
    QLabel* stateLabel() const { return diagnosticStateLabel; }
    bool showsCurrentFileScope() const;
    bool isVisibleToUser() const;
    int updateInvocationCount() const { return updateInvocations; }

private:
    QDockWidget* problemsDock = nullptr;
    QTreeWidget* problemsTree = nullptr;
    QComboBox* problemsScopeCombo = nullptr;
    QComboBox* problemsSeverityCombo = nullptr;
    QComboBox* problemsBandCombo = nullptr;
    QLabel* diagnosticSummaryLabel = nullptr;
    QLabel* diagnosticStateLabel = nullptr;
    QString lastDiagnosticActivityMessage;
    QString externalAnalysisState;
    int updateInvocations = 0;

    std::function<QString()> currentFileProvider;
    std::function<QStringList()> workspaceFilesProvider;
    std::function<bool(const QString&, int, int)> navigationHandler;
    std::function<void(const QString&, int)> statusMessageHandler;
};

#endif // PROBLEMSPANELCOORDINATOR_H
