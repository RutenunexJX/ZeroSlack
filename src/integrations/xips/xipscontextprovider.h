#pragma once
#include "../../ui/contextcontentprovider.h"
#include "zeroslackexport.h"
#include <QLibrary>
#include <QObject>
#include <QPointer>

class TabManager;
class WorkspaceManager;

class ZEROSLACK_API XipsHostBridge final : public QObject
{
    Q_OBJECT
  public:
    XipsHostBridge(TabManager *tabs, WorkspaceManager *workspaces, QObject *parent);
    Q_INVOKABLE QString destinationError(const QString &path);
    Q_INVOKABLE QString exportCompleted(const QVariantMap &receipt);
    Q_INVOKABLE QStringList collectionSources();

  private:
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
};

class ZEROSLACK_API XipsContextProvider final : public IContextContentProvider
{
  public:
    XipsContextProvider(TabManager *tabs, WorkspaceManager *workspaces);
    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(const QString &workspaceId) const override;
    QWidget *createView(const ContextResource &resource, QWidget *parent) override;
    bool activateView(QWidget *view, const ContextResource &resource) override;
    ContextViewCapabilities capabilities(const ContextResource &) const override;
    QVariantMap saveViewState(QWidget *view) const override;
    void restoreViewState(QWidget *view, const QVariantMap &state) override;

  private:
    bool loadLibrary(QString *error);
    QLibrary library;
    bool compatible = false;
    QString loadError;
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
};
