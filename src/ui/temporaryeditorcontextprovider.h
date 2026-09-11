#ifndef TEMPORARYEDITORCONTEXTPROVIDER_H
#define TEMPORARYEDITORCONTEXTPROVIDER_H

#include "contextcontentprovider.h"
#include "editorlocation.h"
#include "editorsearchcandidate.h"

#include <QPointer>

#include <functional>

class TabManager;
class TemporaryEditorContextView;

class ZEROSLACK_API TemporaryEditorContextProvider final
    : public IContextContentProvider
{
public:
    using SearchProvider =
        std::function<EditorSearchCandidates(const QString&)>;

    explicit TemporaryEditorContextProvider(TabManager* tabManager);

    static QString staticProviderId();
    static ContextResource resourceForLocation(
        const EditorLocation& location,
        const QString& workspaceId = {});
    static ContextResource resourceForCurrentEditor(
        TabManager* tabManager,
        const QString& workspaceId = {});
    static EditorLocation locationFromResource(
        const ContextResource& resource);

    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(
        const QString& workspaceId) const override;
    bool canOpen(const ContextResource& resource) const override;
    QWidget* createView(const ContextResource& resource,
                        QWidget* parent) override;
    bool activateView(QWidget* view,
                      const ContextResource& resource) override;
    ContextViewCapabilities capabilities(
        const ContextResource& resource) const override;
    void observeViewResourceChanges(
        QWidget* view,
        QObject* context,
        ResourceUpdateHandler handler) override;
    QVariantMap saveViewState(QWidget* view) const override;
    void restoreViewState(QWidget* view,
                          const QVariantMap& state) override;
    ContextResource resourceForPersistence(
        const ContextResource& resource,
        QWidget* view,
        const QString& workspaceRoot) const override;
    ContextResource resourceFromPersistence(
        const ContextResource& resource,
        const QString& workspaceRoot) const override;

    void setSearchProvider(SearchProvider provider);

private:
    QPointer<TabManager> tabManagerValue;
    SearchProvider searchProvider;
};

#endif // TEMPORARYEDITORCONTEXTPROVIDER_H
