#include "../src/integrations/xips/xipsbrowserapi.h"
#include <QWidget>
#include <QVariantMap>
#ifdef NATIVE_FIXTURE_PRIVATE_DEPENDENCY
extern "C" Q_DECL_IMPORT int native_fixture_private_marker();
#endif

class FakeXips final : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    Q_INVOKABLE void setContext(const QString &library, const QString &workspace)
    { setProperty("library", library); setProperty("workspace", workspace); }
    Q_INVOKABLE void collectPaths(const QStringList &) {}
    Q_INVOKABLE void revealAsset(const QString &) {}
    Q_INVOKABLE void refresh() { setProperty("refreshed", true); }
    Q_INVOKABLE void setDarkTheme(bool dark) { setProperty("darkTheme", dark); }
    Q_INVOKABLE bool isCatalogBusy() const { return property("busy").toBool(); }
    Q_INVOKABLE QVariantMap saveState() const { return property("state").toMap(); }
    Q_INVOKABLE void restoreState(const QVariantMap &state) { setProperty("state", state); }
};

extern "C" Q_DECL_EXPORT const char *xips_browser_abi_v1()
{
#ifdef NATIVE_FIXTURE_BAD_ABI
    return "xips-browser/v1;qt=incompatible";
#else
    static const auto abi = xipsExpectedBrowserAbi();
    return abi.constData();
#endif
}
#ifndef NATIVE_FIXTURE_MISSING_FACTORY
extern "C" Q_DECL_EXPORT QWidget *xips_create_browser_v1(QWidget *parent, QObject *)
{
#ifdef NATIVE_FIXTURE_PRIVATE_DEPENDENCY
    if (native_fixture_private_marker() != 2703) return nullptr;
#endif
    if (qEnvironmentVariableIsSet("NATIVE_FIXTURE_NULL")) return nullptr;
    if (qEnvironmentVariableIsSet("NATIVE_FIXTURE_BAD_CONTRACT")) return new QWidget(parent);
    return new FakeXips(parent);
}

#endif
#include "native_component_fixture.moc"
