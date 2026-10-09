#pragma once
#include "../../ui/contextresource.h"
#include "zeroslackexport.h"
#include <QWidget>
class QLabel;
class QPushButton;
namespace simdock { class Workbench; }

class ZEROSLACK_API SimDockContextView final : public QWidget
{
    Q_OBJECT
public:
    explicit SimDockContextView(QWidget* parent = nullptr);
    bool activate(const ContextResource& resource);
    bool canClose(QString* error = nullptr) const;
    void retire();
    QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);
    simdock::Workbench* workbench() const { return panel; }
    QWidget* component() const;
    bool isReady() const { return property("nativeComponentReady").toBool(); }
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
    QVariantMap legacyLayout;
    bool activated = false, contextApplied = false, statePending = false, invoking = false, retired = false;
};
