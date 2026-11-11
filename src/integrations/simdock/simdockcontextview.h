#pragma once
#include "../../ui/contextresource.h"
#include "zeroslackexport.h"
#include <QWidget>
class QLabel;
class QPushButton;
namespace simdock { class Workbench; class WorkspaceSimulationController; }
class DesignProjectService;

class ZEROSLACK_API SimDockContextView final : public QWidget
{
    Q_OBJECT
public:
    explicit SimDockContextView(QWidget* parent = nullptr, simdock::WorkspaceSimulationController* controller = nullptr,
        DesignProjectService* design = nullptr);
    bool activate(const ContextResource& resource);
    bool canClose(QString* error = nullptr) const;
    void retire();
    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);
    simdock::Workbench* workbench() const { return panel; }
    QWidget* component() const;
    bool isReady() const { return property("nativeComponentReady").toBool(); }
    void setCompactControls();
signals:
    void issueRaised(const QString& message);
public slots:
    void retry();
    void resetSavedState();
private:
    void synchronizeTheme();
    void persistPreferences();
    void updateStatus();
    ContextResource desired, lastActivation;
    simdock::Workbench* panel = nullptr;
    QLabel* status = nullptr;
    QPushButton *retryButton = nullptr, *resetButton = nullptr;
    QString appliedWorkspace;
    QString reportedIssue;
    QVariantMap legacyLayout;
    bool activated = false, contextApplied = false, statePending = false, invoking = false, retired = false;
};
