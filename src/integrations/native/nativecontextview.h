#pragma once

#include "../../ui/contextresource.h"
#include "zeroslackexport.h"
#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

// A host-owned surface survives a missing/broken component. Ready describes
// the native child, not the existence of this recovery surface.
class ZEROSLACK_API NativeContextView final : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Xips, SimDock };
    explicit NativeContextView(Kind kind, QWidget *parent = nullptr);
    void setHostBridge(QObject *bridge);
    bool activate(const ContextResource &resource);
    void setWorkspace(const QString &workspace);
    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);
    bool canClose(QString *error = nullptr) const;
    void retire();
    QWidget *component() const { return panel; }
    bool isReady() const { return property("nativeComponentReady").toBool(); }

public slots:
    void retry();
    void resetSavedState();

private:
    Kind kind;
    ContextResource desired;
    ContextResource lastActivation;
    bool activated = false;
    QString appliedWorkspace;
    QString explicitLibrary;
    bool contextApplied = false;
    bool statePending = false;
    bool invoking = false;
    QPointer<QWidget> panel;
    QPointer<QObject> bridge;
    QLabel *status = nullptr;
    QPushButton *retryButton = nullptr;
    QPushButton *locateButton = nullptr;
    QPushButton *resetStateButton = nullptr;
    QVBoxLayout *layout = nullptr;

    QString name() const;
    QStringList candidates() const;
    bool createComponent(QString *error);
    bool applyContextAndState(QString *error);
    void synchronizeTheme();
    void showError(const QString &error);
};
