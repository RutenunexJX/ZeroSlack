#ifndef CONTEXTCONTENTPROVIDER_H
#define CONTEXTCONTENTPROVIDER_H

#include "contextresource.h"

#include <QString>
#include <QVariantMap>

#include <functional>

class QObject;
class QWidget;

class IContextContentProvider
{
public:
    using ResourceUpdateHandler =
        std::function<void(const ContextResource&)>;

    virtual ~IContextContentProvider() = default;

    virtual QString providerId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString iconKey() const
    {
        return {};
    }

    virtual bool canOpen(const ContextResource& resource) const
    {
        return resource.isValid()
            && resource.providerId == providerId();
    }

    virtual QWidget* createView(
        const ContextResource& resource,
        QWidget* parent) = 0;

    virtual bool activateView(
        QWidget* view,
        const ContextResource& resource)
    {
        if (!view)
            return false;
        restoreViewState(view, resource.state);
        return true;
    }

    virtual ContextViewCapabilities capabilities(
        const ContextResource&) const
    {
        return {};
    }

    virtual void observeViewResourceChanges(
        QWidget*,
        QObject*,
        ResourceUpdateHandler)
    {
    }

    virtual QVariantMap saveViewState(QWidget*) const
    {
        return {};
    }

    virtual void restoreViewState(
        QWidget*,
        const QVariantMap&)
    {
    }

    virtual ContextResource resourceForPersistence(
        const ContextResource& resource,
        QWidget* view,
        const QString&) const
    {
        ContextResource persisted = resource;
        const QVariantMap viewState = saveViewState(view);
        if (!viewState.isEmpty())
            persisted.state = viewState;
        return persisted;
    }

    virtual ContextResource resourceFromPersistence(
        const ContextResource& resource,
        const QString&) const
    {
        return resource;
    }
};

#endif // CONTEXTCONTENTPROVIDER_H
