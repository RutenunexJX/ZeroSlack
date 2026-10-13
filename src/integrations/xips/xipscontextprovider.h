#pragma once
#include "../../ui/contextcontentprovider.h"
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <functional>

class TabManager;
class WorkspaceManager;
class XipsHdlCommandService;

class ZEROSLACK_API XipsHostBridge final : public QObject
{
    Q_OBJECT
  public:
    XipsHostBridge(TabManager *tabs, WorkspaceManager *workspaces, QObject *parent);
    Q_INVOKABLE QString destinationError(const QString &path);
    Q_INVOKABLE QString exportCompleted(const QVariantMap &receipt);
    Q_INVOKABLE QStringList collectionSources();
    std::function<QString(const QVariantMap&)> adoptHdlReceipt;

  private:
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
};

class ZEROSLACK_API XipsContextProvider final : public IContextContentProvider
{
  public:
    XipsContextProvider(TabManager *tabs, WorkspaceManager *workspaces, XipsHdlCommandService* commands = nullptr);
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
    QPointer<XipsHdlCommandService> commands;
};
