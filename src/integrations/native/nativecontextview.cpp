#include "nativecontextview.h"
#include "../xips/xipsbrowserapi.h"
#include "../simdock/simdockworkbenchapi.h"
#include "uicontrols.h"
#include "applicationthememanager.h"

#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QLibrary>
#include <QMetaMethod>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QThread>
#include <QVBoxLayout>
#include <iterator>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
class NativeLibrary
{
public:
    explicit NativeLibrary(const QString &path) : path(path)
#ifndef Q_OS_WIN
        , library(path)
#endif
    {}
    bool load()
    {
#ifdef Q_OS_WIN
        // GUI-thread only, like the widget contract. Keep one loader reference
        // per file for process lifetime: Qt callbacks/types may outlive a view.
        static QHash<QString, HMODULE> modules;
        const QString key = QFileInfo(path).canonicalFilePath().toCaseFolded();
        handle = modules.value(key);
        if (handle) return true;
        const QString nativePath = QDir::toNativeSeparators(path);
        handle = LoadLibraryExW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (handle) {
            modules.insert(key, handle);
            return true;
        }
        const DWORD code = GetLastError();
        wchar_t *message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
        error = QStringLiteral("Windows loader error %1: %2").arg(code)
                    .arg(message ? QString::fromWCharArray(message).trimmed() : QStringLiteral("component or dependency unavailable"));
        if (message) LocalFree(message);
        return false;
#else
        library.setLoadHints(QLibrary::PreventUnloadHint | QLibrary::ResolveAllSymbolsHint);
        if (library.load()) return true;
        error = library.errorString();
        return false;
#endif
    }
    QFunctionPointer resolve(const char *symbol)
    {
#ifdef Q_OS_WIN
        return handle ? reinterpret_cast<QFunctionPointer>(GetProcAddress(handle, symbol)) : nullptr;
#else
        return library.resolve(symbol);
#endif
    }
    QString errorString() const { return error; }
private:
    QString path;
    QString error;
#ifdef Q_OS_WIN
    HMODULE handle = nullptr;
#else
    QLibrary library;
#endif
};

QString preloadSimDockUi(const QString &entryPath)
{
#ifdef Q_OS_WIN
    const QString expected = QFileInfo(QFileInfo(entryPath).dir().filePath(QStringLiteral("SimDockEla.dll"))).canonicalFilePath();
    if (expected.isEmpty()) return QStringLiteral("Missing adjacent SimDockEla.dll.");
    if (const HMODULE loaded = GetModuleHandleW(L"SimDockEla.dll")) {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(loaded, path, DWORD(std::size(path)));
        const QString actual = length && length < std::size(path)
            ? QFileInfo(QString::fromWCharArray(path, int(length))).canonicalFilePath() : QString();
        if (actual.compare(expected, Qt::CaseInsensitive) != 0)
            return QStringLiteral("A different private SimDockEla.dll is already loaded. Restart ZeroSlack before switching builds.");
    }
    NativeLibrary dependency(expected);
    if (!dependency.load()) return QStringLiteral("SimDockEla.dll: %1").arg(dependency.errorString());
#else
    Q_UNUSED(entryPath)
#endif
    return {};
}

bool hasMethod(const QWidget *widget, const char *signature, int resultType)
{
    const int index = widget->metaObject()->indexOfMethod(signature);
    return index >= 0 && widget->metaObject()->method(index).returnMetaType().id() == resultType;
}
QString checkContract(QWidget *widget, NativeContextView::Kind kind)
{
    const bool simdock = kind == NativeContextView::Kind::SimDock;
    if (!hasMethod(widget, "saveState()", QMetaType::QVariantMap)
        || !hasMethod(widget, "restoreState(QVariantMap)", simdock ? QMetaType::QString : QMetaType::Void)
        || !(simdock ? hasMethod(widget, "setContext(QString)", QMetaType::QString)
                     : hasMethod(widget, "setContext(QString,QString)", QMetaType::Void)))
        return QStringLiteral("The component does not implement the native context/state contract.");
    if (simdock && (!hasMethod(widget, "canClose()", QMetaType::Bool)
                    || !hasMethod(widget, "openProject(QString)", QMetaType::QString)))
        return QStringLiteral("The SimDock component does not implement the project/close contract.");
    if (!simdock && (!hasMethod(widget, "collectPaths(QStringList)", QMetaType::Void)
                     || !hasMethod(widget, "revealAsset(QString)", QMetaType::Void)
                     || !hasMethod(widget, "refresh()", QMetaType::Void)
                     || !hasMethod(widget, "isCatalogBusy()", QMetaType::Bool)))
        return QStringLiteral("The xIPs component does not implement the browser contract.");
    return {};
}
} // namespace

NativeContextView::NativeContextView(Kind kind, QWidget *parent) : QWidget(parent), kind(kind)
{
    setObjectName(kind == Kind::Xips ? QStringLiteral("xipsHost") : QStringLiteral("simdockHost"));
    setProperty("nativeComponentReady", false);
    layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    status = UiControls::label(this);
    status->setObjectName(QStringLiteral("nativeComponentStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    retryButton = UiControls::pushButton(tr("Reload %1").arg(name()), this);
    retryButton->setObjectName(QStringLiteral("nativeComponentRetry"));
    locateButton = UiControls::pushButton(tr("Locate %1 component…").arg(name()), this);
    locateButton->setObjectName(QStringLiteral("nativeComponentLocate"));
    resetStateButton = UiControls::pushButton(tr("Reset saved panel state"), this);
    resetStateButton->setObjectName(QStringLiteral("nativeComponentResetState"));
    resetStateButton->hide();
    layout->addWidget(status);
    layout->addWidget(retryButton);
    layout->addWidget(locateButton);
    layout->addWidget(resetStateButton);
    layout->addStretch();
    connect(retryButton, &QPushButton::clicked, this, &NativeContextView::retry);
    connect(resetStateButton, &QPushButton::clicked, this, &NativeContextView::resetSavedState);
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
            this, [this] { synchronizeTheme(); });
    connect(locateButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Locate %1 component").arg(name()),
                                                        {}, tr("Native component (*.dll)"));
        if (path.isEmpty()) return;
        if (panel) {
            showError(tr("A component is already loaded. Restart ZeroSlack to replace its DLL."));
            return;
        }
        explicitLibrary = path;
        retry();
    });
}

QString NativeContextView::name() const
{ return kind == Kind::Xips ? QStringLiteral("xIPs") : QStringLiteral("SimDock"); }

void NativeContextView::setHostBridge(QObject *hostBridge)
{
    Q_ASSERT(!panel);
    bridge = hostBridge;
    if (bridge) bridge->setParent(this);
}

QStringList NativeContextView::candidates() const
{
    if (!explicitLibrary.isEmpty()) return {explicitLibrary};
    const QString environment = qEnvironmentVariable(kind == Kind::Xips ? "XIPS_BROWSER_LIBRARY" : "SIMDOCK_WORKBENCH_LIBRARY");
    // An explicit override is authoritative, including a missing file. Do not
    // silently load an unrelated installed build when a developer selects one.
    if (!environment.isEmpty()) return {environment};
    const QString id = kind == Kind::Xips ? QStringLiteral("xips") : QStringLiteral("simdock");
    const QString file = kind == Kind::Xips ? QStringLiteral("xips-browser.dll") : QStringLiteral("simdock-workbench.dll");
    const QDir app(QCoreApplication::applicationDirPath());
    const QString configured = QSettings().value(QStringLiteral("integrations/native/%1Library").arg(id)).toString();
    QStringList paths{configured, app.filePath(QStringLiteral("components/%1/%2").arg(id, file)), app.filePath(file),
                      app.filePath(QStringLiteral("../%1/%2").arg(name(), file)),
                      app.filePath(QStringLiteral("../%1/bin/%2").arg(name(), file))};
    paths.removeAll(QString());
    paths.removeDuplicates();
    return paths;
}

bool NativeContextView::createComponent(QString *error)
{
    if (!qApp || QThread::currentThread() != qApp->thread() || QByteArray(qVersion()) != QT_VERSION_STR) {
        *error = tr("The native component requires the matching Qt runtime on the application thread.");
        return false;
    }
    using Abi = const char *(*)();
    using Factory = QWidget *(*)(QWidget *, QObject *);
    const bool xips = kind == Kind::Xips;
    const QByteArray expected = xips ? xipsExpectedBrowserAbi() : simdockExpectedWorkbenchAbi();
    QStringList failures;
    for (const QString &candidate : candidates()) {
        const QFileInfo file(candidate);
        if (!file.isAbsolute() || !file.isFile()) {
            failures.append(tr("Component file not found: %1").arg(candidate));
            continue;
        }
        // Qt metaobjects, queued callbacks and worker code can outlive a view.
        // Native modules stay mapped for the process lifetime, including a
        // rejected module whose static initializers have already run. Retrying
        // a different file is supported; replacing a loaded DLL needs restart.
        if (!xips) {
            const QString dependencyError = preloadSimDockUi(file.absoluteFilePath());
            if (!dependencyError.isEmpty()) {
                failures.append(tr("%1: %2").arg(candidate, dependencyError));
                continue;
            }
        }
        NativeLibrary library(file.absoluteFilePath());
        if (!library.load()) {
            failures.append(tr("%1: %2").arg(candidate, library.errorString()));
            continue;
        }
        const auto abi = reinterpret_cast<Abi>(library.resolve(xips ? "xips_browser_abi_v1" : "simdock_workbench_abi_v1"));
        const auto factory = reinterpret_cast<Factory>(library.resolve(xips ? "xips_create_browser_v1" : "simdock_create_workbench_v1"));
        const char *reported = abi ? abi() : nullptr;
        if (!reported || QByteArray(reported) != expected || !factory) {
            failures.append(tr("%1: incompatible component.\nExpected: %2\nReported: %3\n"
                               "Use a matching build; restart ZeroSlack after replacing a loaded DLL.")
                                .arg(candidate, QString::fromLatin1(expected),
                                     !reported ? tr("missing ABI export") : !factory ? tr("missing widget factory export")
                                                                                   : QString::fromLatin1(reported)));
            continue;
        }
        if (xips) {
            // xIPs loads its implementation with ALTERED_SEARCH_PATH. A Qt
            // module not already imported by this host is then searched next
            // to the component, not next to ZeroSlack.exe. Preload the host's
            // explicitly deployed QtSql without changing process search paths.
            const QFileInfo sql(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Qt6Sql.dll")));
            if (sql.isFile()) {
                NativeLibrary dependency(sql.absoluteFilePath());
                if (!dependency.load()) {
                    failures.append(tr("Host QtSql: %1").arg(dependency.errorString()));
                    continue;
                }
            }
        }
        QWidget *created = factory(this, bridge);
        if (!created) {
            const auto lastError = reinterpret_cast<Abi>(library.resolve(xips ? "xips_browser_last_error_v1" : "simdock_workbench_last_error_v1"));
            const char *detail = lastError ? lastError() : nullptr;
            failures.append(tr("%1 returned no native widget.%2").arg(candidate,
                            detail && *detail ? QStringLiteral("\n") + QString::fromUtf8(detail) : QString()));
            continue;
        }
        const QString contractError = checkContract(created, kind);
        if (!contractError.isEmpty() || created->parentWidget() != this || created->isWindow()) {
            delete created;
            failures.append(tr("%1: %2").arg(candidate, contractError.isEmpty()
                                        ? tr("The factory did not return a child widget.") : contractError));
            continue;
        }
        panel = created;
        // Component styles can select their root by object name. The host owns
        // its wrapper identity and must preserve the factory's widget identity.
        if (panel->objectName().isEmpty())
            panel->setObjectName(xips ? QStringLiteral("xipsBrowser") : QStringLiteral("simdockWorkbench"));
        layout->insertWidget(0, panel, 1);
        setProperty("nativeComponentLibrary", file.canonicalFilePath());
        setProperty("nativeComponentAbi", expected);
        if (!explicitLibrary.isEmpty())
            QSettings().setValue(QStringLiteral("integrations/native/%1Library")
                                    .arg(xips ? QStringLiteral("xips") : QStringLiteral("simdock")), file.absoluteFilePath());
        const auto capabilities = reinterpret_cast<Abi>(library.resolve(xips ? "xips_browser_capabilities_v1" : "simdock_workbench_capabilities_v1"));
        if (capabilities) {
            const char *json = capabilities();
            const auto document = json ? QJsonDocument::fromJson(QByteArray(json)) : QJsonDocument();
            if (document.isObject()) setProperty("nativeComponentCapabilities", document.toVariant());
        }
        synchronizeTheme();
        return true;
    }
    *error = failures.join(QStringLiteral("\n\n"))
        + tr("\nDeploy the matching native component and its runtime dependencies, then reload. "
             "Starting the standalone application is not required.");
    return false;
}

bool NativeContextView::activate(const ContextResource &resource)
{
    if (property("nativeComponentRetired").toBool()) return false;
    if (resource.providerId != (kind == Kind::Xips ? QStringLiteral("xips") : QStringLiteral("simdock")))
        return false;
    // createView and the docking controller both activate a new resource.
    // Reopening an existing rail entry must not restart scans or replay old UI
    // state. Explicit Reload remains available after a failed activation.
    if (activated && resource.workspaceId == lastActivation.workspaceId
        && resource.stableKey() == lastActivation.stableKey() && resource.state == lastActivation.state)
        return true;
    activated = true;
    lastActivation = resource;
    desired = resource;
    statePending = !resource.state.isEmpty();
    retry();
    // A recoverable host is a valid resource even when the native child is
    // unavailable. Callers/tests must inspect nativeComponentReady separately.
    return true;
}

void NativeContextView::setWorkspace(const QString &workspace)
{
    if (desired.workspaceId == workspace) return;
    desired.workspaceId = workspace;
    desired.state.clear();
    lastActivation.workspaceId = workspace;
    lastActivation.state.clear();
    statePending = false;
    retry();
}

bool NativeContextView::applyContextAndState(QString *error)
{
    if (!panel) return false;
    if (!contextApplied || appliedWorkspace != desired.workspaceId) {
        if (contextApplied && !canClose(error)) return false;
        QString componentError;
        const bool called = kind == Kind::Xips
            ? QMetaObject::invokeMethod(panel, "setContext", Qt::DirectConnection,
                                       Q_ARG(QString, QString()), Q_ARG(QString, desired.workspaceId))
            : QMetaObject::invokeMethod(panel, "setContext", Qt::DirectConnection,
                                       Q_RETURN_ARG(QString, componentError), Q_ARG(QString, desired.workspaceId));
        if (!called || !componentError.isEmpty()) {
            *error = called ? componentError : tr("The component could not receive the workspace context.");
            return false;
        }
        contextApplied = true;
        appliedWorkspace = desired.workspaceId;
    }
    if (statePending) {
        // SimDock's standalone state also contains a workspace. The host owns
        // that identity: an old/moved session must never reopen another root.
        if (kind == Kind::SimDock)
            desired.state.insert(QStringLiteral("workspace"), desired.workspaceId);
        QString componentError;
        const bool called = kind == Kind::Xips
            ? QMetaObject::invokeMethod(panel, "restoreState", Qt::DirectConnection, Q_ARG(QVariantMap, desired.state))
            : QMetaObject::invokeMethod(panel, "restoreState", Qt::DirectConnection,
                                       Q_RETURN_ARG(QString, componentError), Q_ARG(QVariantMap, desired.state));
        if (!called || !componentError.isEmpty()) {
            *error = called ? componentError : tr("The component could not restore its state.");
            return false;
        }
        statePending = false;
    }
    return true;
}

void NativeContextView::showError(const QString &error)
{
    setProperty("nativeComponentReady", false);
    setProperty("nativeComponentError", error);
    status->setText(tr("%1 is not ready.\n%2").arg(name(), error));
    status->show();
    retryButton->show();
    locateButton->setVisible(!panel);
    resetStateButton->setVisible(panel && statePending);
    // Retain an existing busy panel so its Cancel/Stop controls remain usable.
}

void NativeContextView::retry()
{
    if (invoking || property("nativeComponentRetired").toBool()) return;
    const QScopedValueRollback<bool> guard(invoking, true);
    QString error;
    if ((!panel && !createComponent(&error)) || !applyContextAndState(&error)) {
        showError(error);
        return;
    }
    status->hide();
    retryButton->hide();
    locateButton->hide();
    resetStateButton->hide();
    setProperty("nativeComponentError", QString());
    setProperty("nativeComponentReady", true);
    panel->show();
    synchronizeTheme();
}

void NativeContextView::synchronizeTheme()
{
    if (!panel || property("nativeComponentRetired").toBool()) return;
    if (!hasMethod(panel, "setDarkTheme(bool)", QMetaType::Void)) return;
    const bool dark = isDarkTheme(ApplicationThemeManager::instance().mode());
    if (QMetaObject::invokeMethod(panel, "setDarkTheme", Qt::DirectConnection, Q_ARG(bool, dark)))
        setProperty("nativeComponentDarkTheme", dark);
}

void NativeContextView::resetSavedState()
{
    desired.state.clear();
    lastActivation.state.clear();
    statePending = false;
    retry();
}

QVariantMap NativeContextView::saveState() const
{
    if (!panel || statePending || !isReady()) return desired.state;
    QVariantMap state;
    if (!QMetaObject::invokeMethod(panel, "saveState", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, state)))
        return desired.state;
    return state;
}

void NativeContextView::restoreState(const QVariantMap &state)
{
    desired.state = state;
    lastActivation.state = state;
    statePending = !state.isEmpty();
    retry();
}

bool NativeContextView::canClose(QString *error) const
{
    const QWidget *modal = QApplication::activeModalWidget();
    if (modal && (modal == this || isAncestorOf(modal))) {
        if (error) *error = tr("Close the %1 dialog before changing workspace or closing the panel.").arg(name());
        return false;
    }
    if (!panel) return true;
    bool allowed = false;
    bool called;
    if (kind == Kind::Xips) {
        bool busy = true;
        called = QMetaObject::invokeMethod(panel, "isCatalogBusy", Qt::DirectConnection, Q_RETURN_ARG(bool, busy));
        allowed = !busy;
    } else {
        called = QMetaObject::invokeMethod(panel, "canClose", Qt::DirectConnection, Q_RETURN_ARG(bool, allowed));
    }
    if ((!called || !allowed) && error)
        *error = tr("%1 is busy. Finish or stop its operation before closing it or changing workspace.").arg(name());
    return called && allowed;
}

void NativeContextView::retire()
{
    setProperty("nativeComponentRetired", true);
    setEnabled(false);
}
