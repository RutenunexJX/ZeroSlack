#pragma once
#include "../../ui/contextcontentprovider.h"
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <functional>

class TabManager;
class WorkspaceManager;
class SimulationCommandService;

class ZEROSLACK_API XipsHostBridge final : public QObject
{
    Q_OBJECT
  public:
    XipsHostBridge(TabManager *tabs, WorkspaceManager *workspaces, QObject *parent);
    Q_INVOKABLE QString destinationError(const QString &path);
    Q_INVOKABLE QString exportCompleted(const QVariantMap &receipt);
    Q_INVOKABLE QStringList collectionSources();
    static QVariantList workspaceReferences(const QString &workspace, QString *error = nullptr);
    static QVariantList sourceBindings(const QString &workspace, const QStringList &sourceFiles);
    static QString recordPublishedVersion(const QString &workspace, const QVariantMap &expected,
                                          const QVariantMap &published, const QString &requestId,
                                          QVariantMap *saveFailure = nullptr);

  private:
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
};

class ZEROSLACK_API XipsContextProvider final : public IContextContentProvider
{
  public:
    XipsContextProvider(TabManager *tabs, WorkspaceManager *workspaces,
                        SimulationCommandService *commands = nullptr);
    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(const QString &workspaceId) const override;
    QWidget *createView(const ContextResource &resource, QWidget *parent) override;
    bool activateView(QWidget *view, const ContextResource &resource) override;
    bool canCloseView(QWidget *view, QString *error) const override;
    void deactivateView(QWidget *view) override;
    ContextViewCapabilities capabilities(const ContextResource &) const override;
    QVariantMap saveViewState(QWidget *view) const override;
    void restoreViewState(QWidget *view, const QVariantMap &state) override;

  private:
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
    QPointer<SimulationCommandService> commands;
};
