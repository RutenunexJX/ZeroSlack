#pragma once
#include "../../ui/contextcontentprovider.h"
#include "zeroslackexport.h"

class ZEROSLACK_API SimDockContextProvider final : public IContextContentProvider
{
public:
    QString providerId() const override;
    QString displayName() const override;
    QString iconKey() const override;
    ContextResource activationResource(const QString &workspace) const override;
    QWidget *createView(const ContextResource &, QWidget *parent) override;
    bool activateView(QWidget *view, const ContextResource &resource) override;
    bool canCloseView(QWidget *view, QString *error) const override;
    void deactivateView(QWidget *view) override;
    ContextViewCapabilities capabilities(const ContextResource &) const override;
    QVariantMap saveViewState(QWidget *view) const override;
    void restoreViewState(QWidget *view, const QVariantMap &state) override;
};
