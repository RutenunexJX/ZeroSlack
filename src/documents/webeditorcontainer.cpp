#include "webeditorcontainer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlwapi.h>
#include <WebView2.h>

namespace {
template<class Interface, class... Args>
class WebCallback final : public Interface {
public:
    WebCallback(const wchar_t* iid, std::function<HRESULT(Args...)> function)
        : function_(std::move(function)) { CLSIDFromString(iid, &iid_); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, iid_)) return E_NOINTERFACE;
        *result = static_cast<Interface*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return function_(args...); }
private:
    std::atomic<ULONG> references_{1};
    GUID iid_{};
    std::function<HRESULT(Args...)> function_;
};

QString takeString(LPWSTR value) {
    const QString result = value ? QString::fromWCharArray(value) : QString();
    CoTaskMemFree(value);
    return result;
}
constexpr auto runtimeHost = "editor.zeroslack.invalid";
constexpr auto assetsHost = "assets.zeroslack.invalid";
}
#endif

class WebEditorContainer::Private {
public:
    explicit Private(WebEditorContainer* owner) : q(owner) {}
    WebEditorContainer* q;
    QString runtimeRoot;
    QString profile;
    QUrl entry;
    AssetResolver assets;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool initialized = false;
    double zoom = 1;
#ifdef Q_OS_WIN
    HRESULT apartment = E_FAIL;
    ICoreWebView2Environment* environment = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* web = nullptr;
    bool boundsUpdatePending = false;
    EventRegistrationToken messageToken{}, resourceToken{}, navigationToken{}, frameToken{},
        popupToken{}, permissionToken{}, loadedToken{}, focusToken{}, acceleratorToken{};

    void fail(const QString& reason) { if (*alive) emit q->initializationFailed(reason); }
    void resize() {
        if (!controller) return;
        RECT bounds{};
        GetClientRect(reinterpret_cast<HWND>(q->winId()), &bounds);
        controller->put_Bounds(bounds);
    }
    void scheduleBoundsUpdate() {
        if (boundsUpdatePending) return;
        boundsUpdatePending = true;
        // Qt can deliver resize/show before a hidden native child gets its new geometry.
        QTimer::singleShot(0, q, [this] {
            boundsUpdatePending = false;
            if (*alive) resize();
        });
    }
    WebEditorResource resource(const QUrl& url) {
        if (url.scheme() != QStringLiteral("https") || !url.userInfo().isEmpty()
            || (url.port() != -1 && url.port() != 443)) return {};
        if (url.host() == QLatin1String(assetsHost)) return assets ? assets(url) : WebEditorResource{};
        if (url.host() != QLatin1String(runtimeHost)) return {};
        const QString relative = url.path(QUrl::FullyDecoded).mid(1);
        if (relative.isEmpty() || relative.contains(QLatin1Char('\\')) || relative.contains(QLatin1Char(':')))
            return {};
        const QFileInfo info(QDir(runtimeRoot).filePath(relative));
        const QString canonical = info.canonicalFilePath();
        if (!canonical.startsWith(runtimeRoot + QLatin1Char('/'), Qt::CaseInsensitive)
            || !info.isFile() || info.size() > 32 * 1024 * 1024) return {};
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly)) return {};
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) return {};
        QByteArray type;
        const QString suffix = info.suffix().toLower();
        if (suffix == QStringLiteral("js") || suffix == QStringLiteral("mjs")) type = "text/javascript";
        else if (suffix == QStringLiteral("css")) type = "text/css";
        else if (suffix == QStringLiteral("html")) type = "text/html";
        else if (suffix == QStringLiteral("wasm")) type = "application/wasm";
        else type = QMimeDatabase().mimeTypeForFile(info).name().toLatin1();
        return {bytes, type, 200};
    }
    void bind() {
        const auto live = alive;
        QPointer<WebEditorContainer> guard(q);
        auto valid = [live, guard] { return *live && guard; };
        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(web->get_Settings(&settings))) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreHostObjectsAllowed(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            GUID settings3Id{}; CLSIDFromString(L"{fdb5ab74-af33-4854-84f0-0a631deb5eba}", &settings3Id);
            ICoreWebView2Settings3* settings3 = nullptr;
            if (SUCCEEDED(settings->QueryInterface(settings3Id, reinterpret_cast<void**>(&settings3)))) {
                settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE); settings3->Release();
            }
            settings->Release();
        }
        auto messages = new WebCallback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}", [this, valid](ICoreWebView2*, auto* args) {
                if (!valid()) return S_OK;
                LPWSTR source = nullptr; args->get_Source(&source);
                if (QUrl(takeString(source)) != entry) return S_OK;
                LPWSTR raw = nullptr;
                if (FAILED(args->get_WebMessageAsJson(&raw))) return S_OK;
                const QString text = takeString(raw);
                if (text.size() > 96 * 1024 * 1024) return S_OK;
                const auto json = QJsonDocument::fromJson(text.toUtf8());
                if (json.isObject()) emit q->messageReceived(json.object());
                return S_OK;
            });
        web->add_WebMessageReceived(messages, &messageToken); messages->Release();
        auto resources = new WebCallback<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
            L"{ab00b74c-15f1-4646-80e8-e76341d25d71}", [this, valid](ICoreWebView2*, auto* args) {
                if (!valid()) return S_OK;
                ICoreWebView2WebResourceRequest* request = nullptr;
                if (FAILED(args->get_Request(&request)) || !request) return S_OK;
                LPWSTR raw = nullptr; request->get_Uri(&raw); const QUrl url(takeString(raw));
                request->get_Method(&raw); const QString method = takeString(raw); request->Release();
                const WebEditorResource result = method == QStringLiteral("GET") || method == QStringLiteral("HEAD") ? resource(url) : WebEditorResource{};
                const QByteArray body = method == QStringLiteral("HEAD") ? QByteArray() : result.bytes;
                IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(body.constData()), UINT(body.size()));
                QString headers = QStringLiteral("Content-Type: %1\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nAccess-Control-Allow-Origin: https://editor.zeroslack.invalid\r\n").arg(QString::fromLatin1(result.contentType.isEmpty() ? QByteArray("text/plain") : result.contentType));
                if (result.contentType == "text/html") headers += QStringLiteral("Content-Security-Policy: default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' https://assets.zeroslack.invalid data: blob:; font-src 'self' data:; connect-src 'self' https://assets.zeroslack.invalid data: blob:; worker-src 'self' blob:; media-src 'self' blob:; base-uri https://assets.zeroslack.invalid; frame-src 'none'; object-src 'none'\r\n");
                ICoreWebView2WebResourceResponse* response = nullptr;
                environment->CreateWebResourceResponse(stream, result.status, result.status == 200 ? L"OK" : L"Not Found", reinterpret_cast<LPCWSTR>(headers.utf16()), &response);
                if (stream) stream->Release();
                if (response) { args->put_Response(response); response->Release(); }
                return S_OK;
            });
        web->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        web->add_WebResourceRequested(resources, &resourceToken); resources->Release();
        auto navigating = new WebCallback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}", [this, valid](ICoreWebView2*, auto* args) {
                if (!valid()) { args->put_Cancel(TRUE); return S_OK; }
                LPWSTR raw = nullptr; args->get_Uri(&raw); const QUrl url(takeString(raw));
                QUrl noFragment = url; noFragment.setFragment(QString());
                if (noFragment != entry) {
                    args->put_Cancel(TRUE);
                    BOOL user = FALSE; args->get_IsUserInitiated(&user);
                    if (user && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))
                        && url.host() != QLatin1String(assetsHost)) emit q->externalLinkRequested(url);
                }
                return S_OK;
            });
        web->add_NavigationStarting(navigating, &navigationToken); navigating->Release();
        auto frames = new WebCallback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}", [](ICoreWebView2*, auto* args) { args->put_Cancel(TRUE); return S_OK; });
        web->add_FrameNavigationStarting(frames, &frameToken); frames->Release();
        auto popup = new WebCallback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}", [this, valid](ICoreWebView2*, auto* args) {
                args->put_Handled(TRUE);
                if (!valid()) return S_OK;
                LPWSTR raw = nullptr; args->get_Uri(&raw); const QUrl url(takeString(raw));
                BOOL user = FALSE; args->get_IsUserInitiated(&user);
                if (user && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))
                    && url.host() != QLatin1String(assetsHost) && url.host() != QLatin1String(runtimeHost)) emit q->externalLinkRequested(url);
                return S_OK;
            });
        web->add_NewWindowRequested(popup, &popupToken); popup->Release();
        auto permissions = new WebCallback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
            L"{15e1c6a3-c72a-4df3-91d7-d097fbec6bfd}", [this, valid](ICoreWebView2*, auto* args) {
                COREWEBVIEW2_PERMISSION_KIND kind{}; args->get_PermissionKind(&kind);
                LPWSTR raw = nullptr; args->get_Uri(&raw); const QUrl url(takeString(raw));
                args->put_State(valid() && url == entry && kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ
                    ? COREWEBVIEW2_PERMISSION_STATE_ALLOW : COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            });
        web->add_PermissionRequested(permissions, &permissionToken); permissions->Release();
        auto loaded = new WebCallback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            L"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}", [this, valid](ICoreWebView2*, auto* args) {
                if (!valid()) return S_OK;
                BOOL success = FALSE; args->get_IsSuccess(&success);
                if (success) emit q->navigationReady();
                else fail(QStringLiteral("The document editor could not load its local resources. Restore the resources/document-editors folder and reopen the document."));
                return S_OK;
            });
        web->add_NavigationCompleted(loaded, &loadedToken); loaded->Release();
        auto focus = new WebCallback<ICoreWebView2FocusChangedEventHandler, ICoreWebView2Controller*, IUnknown*>(
            L"{05ea24bd-6452-4926-9014-4b82b498135d}", [this, valid](auto*, auto*) { if (valid()) emit q->activated(); return S_OK; });
        controller->add_GotFocus(focus, &focusToken); focus->Release();
        auto accelerator = new WebCallback<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>(
            L"{b29c7e28-fa79-41a8-8e44-65811c76dcb2}", [this, valid](auto*, auto* args) {
                if (!valid()) return S_OK;
                COREWEBVIEW2_KEY_EVENT_KIND kind{}; args->get_KeyEventKind(&kind);
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) return S_OK;
                UINT key = 0; args->get_VirtualKey(&key);
                if ((GetKeyState(VK_CONTROL) & 0x8000) && (key == 'S' || key == 'O' || key == 'N' || key == 'Z' || key == 'Y')) {
                    args->put_Handled(TRUE);
                    const QString command = key == 'Z' ? (GetKeyState(VK_SHIFT) & 0x8000 ? QStringLiteral("redo") : QStringLiteral("undo"))
                        : key == 'Y' ? QStringLiteral("redo") : key == 'N' ? QStringLiteral("new") : key == 'O' ? QStringLiteral("open")
                        : GetKeyState(VK_SHIFT) & 0x8000 ? QStringLiteral("saveAs") : QStringLiteral("save");
                    QTimer::singleShot(0, q, [this, valid, command] { if (valid()) emit q->commandRequested(command); });
                } else if (((GetKeyState(VK_CONTROL) & 0x8000) && (key == 'P' || key == 'R' || key == 'J' || key == 'U')) || key == VK_F5 || key == VK_F12) args->put_Handled(TRUE);
                return S_OK;
            });
        controller->add_AcceleratorKeyPressed(accelerator, &acceleratorToken); accelerator->Release();
    }
    void initialize() {
        apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(apartment)) { fail(QStringLiteral("The document editor requires the Windows UI COM apartment.")); return; }
        const QString loaderFile = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QStringLiteral("/WebView2Loader.dll"));
        // The loader stays loaded until process shutdown; pending COM callbacks may outlive a widget.
        static HMODULE loader = nullptr;
        if (!loader) loader = LoadLibraryExW(reinterpret_cast<LPCWSTR>(loaderFile.utf16()), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Create = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
        const auto create = loader ? reinterpret_cast<Create>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
        if (!create) { fail(QStringLiteral("WebView2Loader.dll is missing or incompatible. Restore the application package and reopen the document.")); return; }
        const auto live = alive;
        QPointer<WebEditorContainer> guard(q);
        auto valid = [live, guard] { return *live && guard; };
        auto callback = new WebCallback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}", [this, valid](HRESULT status, auto* env) {
                if (!valid()) return S_OK;
                if (FAILED(status) || !env) { fail(QStringLiteral("Microsoft Edge WebView2 Runtime is unavailable. Install the Evergreen WebView2 Runtime for Windows, then reopen this document.")); return S_OK; }
                environment = env; environment->AddRef();
                auto completed = new WebCallback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                    L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}", [this, valid](HRESULT result, auto* value) {
                        if (!valid()) { if (value) value->Close(); return S_OK; }
                        if (FAILED(result) || !value) { fail(QStringLiteral("The document WebView could not be created. Close this tab and retry.")); return S_OK; }
                        controller = value; controller->AddRef(); controller->get_CoreWebView2(&web);
                        if (!web) { fail(QStringLiteral("The document WebView is unavailable.")); return S_OK; }
                        initialized = true; bind(); resize(); scheduleBoundsUpdate();
                        controller->put_ZoomFactor(zoom); controller->put_IsVisible(q->isVisible());
                        const QString uri = entry.toString(QUrl::FullyEncoded);
                        if (FAILED(web->Navigate(reinterpret_cast<LPCWSTR>(uri.utf16())))) fail(QStringLiteral("The local document editor could not be opened."));
                        return S_OK;
                    });
                const auto result = environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(q->winId()), completed);
                completed->Release();
                if (FAILED(result)) fail(QStringLiteral("The document WebView could not be initialized."));
                return S_OK;
            });
        const auto result = create(nullptr, reinterpret_cast<LPCWSTR>(profile.utf16()), nullptr, callback);
        callback->Release();
        if (FAILED(result)) fail(QStringLiteral("The WebView2 environment could not start. Check the installed Runtime and the document cache directory."));
    }
#endif
};

WebEditorContainer::WebEditorContainer(const QString& runtimeDirectory, const QString& profileDirectory,
                                     const QString& editorKind, QWidget* parent)
    : QWidget(parent), d(std::make_unique<Private>(this)) {
    setFocusPolicy(Qt::StrongFocus); setAttribute(Qt::WA_NativeWindow);
    d->runtimeRoot = QFileInfo(runtimeDirectory).canonicalFilePath();
    d->profile = QDir::toNativeSeparators(QDir(profileDirectory).absolutePath());
    d->entry = QUrl(QStringLiteral("https://editor.zeroslack.invalid/%1/index.html").arg(editorKind));
    QTimer::singleShot(0, this, [this, editorKind] {
        if (!*d->alive) return;
        if ((editorKind != QStringLiteral("markdown") && editorKind != QStringLiteral("blocks") && editorKind != QStringLiteral("linked"))
            || d->runtimeRoot.isEmpty() || !QFileInfo::exists(QDir(d->runtimeRoot).filePath(editorKind + QStringLiteral("/index.html")))) {
            emit initializationFailed(QStringLiteral("The document editor resources are missing. Restore resources/document-editors from the application package.")); return;
        }
#ifdef Q_OS_WIN
        d->initialize();
#else
        emit initializationFailed(QStringLiteral("This document editor requires Windows WebView2."));
#endif
    });
}
WebEditorContainer::~WebEditorContainer() { shutdown(); }
void WebEditorContainer::shutdown() {
    if (!*d->alive) return;
    *d->alive = false; d->initialized = false; d->assets = {};
#ifdef Q_OS_WIN
    if (d->controller) d->controller->Close();
    if (d->web) { d->web->Release(); d->web = nullptr; }
    if (d->controller) { d->controller->Release(); d->controller = nullptr; }
    if (d->environment) { d->environment->Release(); d->environment = nullptr; }
    if (SUCCEEDED(d->apartment)) { CoUninitialize(); d->apartment = E_FAIL; }
#endif
}
bool WebEditorContainer::postMessage(const QJsonObject& message) {
#ifdef Q_OS_WIN
    if (*d->alive && d->web) {
        const QString text = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
        return SUCCEEDED(d->web->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(text.utf16())));
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}
void WebEditorContainer::setAssetResolver(AssetResolver resolver) { d->assets = std::move(resolver); }
void WebEditorContainer::setZoomFactor(double value) {
    d->zoom = qBound(.5, value, 3.0);
#ifdef Q_OS_WIN
    if (d->controller) d->controller->put_ZoomFactor(d->zoom);
#endif
}
bool WebEditorContainer::isInitialized() const { return d->initialized; }
QString WebEditorContainer::entryUrl() const { return d->entry.toString(); }
void WebEditorContainer::resizeEvent(QResizeEvent* e) { QWidget::resizeEvent(e);
#ifdef Q_OS_WIN
    d->resize(); d->scheduleBoundsUpdate();
#endif
}
void WebEditorContainer::showEvent(QShowEvent* e) { QWidget::showEvent(e);
#ifdef Q_OS_WIN
    if (d->controller) { d->resize(); d->scheduleBoundsUpdate(); d->controller->put_IsVisible(TRUE); }
#endif
}
void WebEditorContainer::hideEvent(QHideEvent* e) {
#ifdef Q_OS_WIN
    if (d->controller) d->controller->put_IsVisible(FALSE);
#endif
    QWidget::hideEvent(e);
}
void WebEditorContainer::focusInEvent(QFocusEvent* e) { QWidget::focusInEvent(e);
#ifdef Q_OS_WIN
    if (d->controller) d->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
#endif
}
bool WebEditorContainer::event(QEvent* e) {
    const bool result = QWidget::event(e);
#ifdef Q_OS_WIN
    if (d && d->controller) {
        if (e->type() == QEvent::WinIdChange)
            d->controller->put_ParentWindow(reinterpret_cast<HWND>(winId()));
        if (e->type() == QEvent::WinIdChange || e->type() == QEvent::ParentChange
            || e->type() == QEvent::DevicePixelRatioChange) d->scheduleBoundsUpdate();
    }
#endif
    return result;
}
