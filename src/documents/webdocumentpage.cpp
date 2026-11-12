#include "webdocumentpage.h"
#include "insightvisualstyle.h"
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

WebDocumentPage::WebDocumentPage(Kind kind, QWidget* parent, const Options& options, const QString& restoredViewId)
    : WebDocumentPage(std::make_shared<WebDocumentSession>(kind, options), parent, restoredViewId) {}

WebDocumentPage::WebDocumentPage(std::shared_ptr<WebDocumentSession> session, QWidget* parent, const QString& restoredViewId)
    : QWidget(parent), document(std::move(session)), viewId(restoredViewId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : restoredViewId) {
    setObjectName(QStringLiteral("webDocumentPage"));
    setProperty("documentViewId", viewId); setProperty("editorViewId", viewId);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* strip = new QWidget(this); auto* row = new QHBoxLayout(strip); row->setContentsMargins(10, 5, 10, 5);
    notice = new QLabel(QStringLiteral("Loading document editor…"), strip); notice->setWordWrap(true);
    reloadButton = new QPushButton(QStringLiteral("Reload from Disk"), strip);
    keepButton = new QPushButton(QStringLiteral("Keep Local Changes"), strip);
    row->addWidget(notice, 1); row->addWidget(reloadButton); row->addWidget(keepButton);
    reloadButton->hide(); keepButton->hide(); layout->addWidget(strip);
    const auto& options = document->options;
    const QString runtime = options.runtimeDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/resources/document-editors") : options.runtimeDirectory;
    const QString profileRoot = qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    const QString profile = options.profileDirectory.isEmpty()
        ? (profileRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : profileRoot) + QStringLiteral("/document-webview") : options.profileDirectory;
    web = new WebEditorContainer(runtime, profile, documentKind() == Kind::Markdown ? QStringLiteral("markdown") : QStringLiteral("blocks"), this);
    web->setAssetResolver([weak = std::weak_ptr<WebDocumentSession>(document)](const QUrl& url) {
        const auto session = weak.lock(); return session ? session->asset(url) : WebEditorResource{};
    });
    layout->addWidget(web, 1); setFocusProxy(web); document->attach(this);
    startupTimer = new QTimer(this); startupTimer->setSingleShot(true); startupTimer->setInterval(30000);
    connect(startupTimer, &QTimer::timeout, this, [this] {
        if (!initialized) showNotice(QStringLiteral("The document editor did not finish loading. Restore the editor resources and reopen this tab."));
    }); startupTimer->start();
    connect(web, &WebEditorContainer::messageReceived, this, [this](const QJsonObject& value) { document->receive(this, value); });
    connect(web, &WebEditorContainer::activated, this, [this] { document->setPresentationView(this); checkExternalFile(); emit activated(); });
    connect(web, &WebEditorContainer::externalLinkRequested, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
    connect(web, &WebEditorContainer::commandRequested, this, [this](const QString& command) {
        if (busy()) return;
        if (command == QStringLiteral("save") || command == QStringLiteral("saveAs")) emit saveRequested(command == QStringLiteral("saveAs"));
        else if (command == QStringLiteral("undo") || command == QStringLiteral("redo")) executeCommand(command);
        else emit fileCommandRequested(command);
    });
    connect(web, &WebEditorContainer::initializationFailed, this, [this](const QString& text) {
        document->failure = text; initialized = false; showNotice(text); startupTimer->stop(); emit statusChanged();
    });
    connect(reloadButton, &QPushButton::clicked, this, [this] { document->setPresentationView(this); document->reloadFile(); });
    connect(keepButton, &QPushButton::clicked, this, [this] { document->setPresentationView(this); document->keepLocal(); });
    connect(document.get(), &WebDocumentSession::statusChanged, this, &WebDocumentPage::statusChanged);
    connect(document.get(), &WebDocumentSession::noticeChanged, this, &WebDocumentPage::showNotice);
    connect(document.get(), &WebDocumentSession::fileSaved, this, &WebDocumentPage::fileSaved);
    connect(document.get(), &WebDocumentSession::fileSaveFailed, this, &WebDocumentPage::fileSaveFailed);
    connect(document.get(), &WebDocumentSession::identityChanged, this, &WebDocumentPage::identityChanged);
    connect(document.get(), &WebDocumentSession::recoveryCheckpointRequested, this, &WebDocumentPage::recoveryCheckpointRequested);
    connect(document.get(), &WebDocumentSession::snapshotArrived, this, &WebDocumentPage::snapshotArrived);
}
WebDocumentPage::~WebDocumentPage() { releaseView(); }
void WebDocumentPage::releaseView() {
    if (!attached) return;
    if (hello) web->postMessage({{"type", "dispose"}, {"session", document->session}, {"document", documentId()}});
    attached = false; startupTimer->stop(); document->detach(this); web->shutdown();
}
bool WebDocumentPage::supportsFile(const QString& path) { return WebDocumentSession::supportsFile(path); }
WebDocumentPage::Kind WebDocumentPage::kindForFile(const QString& path) { return WebDocumentSession::kindForFile(path); }
bool WebDocumentPage::validateBlockPackage(const QJsonObject& package, QString* error) { return WebDocumentSession::validateBlockPackage(package, error); }
bool WebDocumentPage::openFile(const QString& path, QString* error) { return document->openFile(path, error); }
bool WebDocumentPage::setInitialContent(const QString& text, QString* error) { return document->setInitialContent(text, error); }
bool WebDocumentPage::importMarkdown(const QString& path, QString* error) { return document->importMarkdown(path, error); }
QString WebDocumentPage::title() const { return document->title(); }
bool WebDocumentPage::save(bool saveAs, const QString& explicitPath) { document->setPresentationView(this); return document->save(saveAs, explicitPath); }
bool WebDocumentPage::exportMarkdown() { document->setPresentationView(this); return document->exportMarkdown(); }
bool WebDocumentPage::confirmClose() { return document->prepareViewClose(this); }
bool WebDocumentPage::captureRecovery(QString* text, quint64* revision) { return document->captureRecovery(text, revision); }
bool WebDocumentPage::restoreRecovery(const QString& text, QString* error) { return document->restoreRecovery(text, error); }
void WebDocumentPage::checkExternalFile() { document->checkExternalFile(); }
void WebDocumentPage::restoreViewState(const QJsonObject& state) {
    presentationState = state;
    if (initialized) web->postMessage({{"type", "restore-view"}, {"session", document->session}, {"document", documentId()}, {"state", state}});
}
void WebDocumentPage::setTargetValidator(std::function<bool(const QString&, QString*)> validator) { document->setTargetValidator(std::move(validator)); }
std::shared_ptr<void> WebDocumentPage::lockForPathMutation(quint64 expected, QString* error) { return document->lockForPathMutation(expected, error); }
void WebDocumentPage::executeCommand(const QString& command) {
    if (!initialized || busy()) return;
    document->setPresentationView(this);
    if (command == QStringLiteral("focus") || command == QStringLiteral("selectAll")
        || command == QStringLiteral("copy") || command == QStringLiteral("cut")
        || command == QStringLiteral("paste")) web->focusContent();
    QJsonObject message{{"type", "command"}, {"session", document->session}, {"document", documentId()}, {"command", command}};
    if (command == QStringLiteral("paste")) {
        const auto* mime = QApplication::clipboard()->mimeData(); if (!mime) return;
        QJsonObject clipboard{{"text", mime->text()}, {"html", mime->html()}};
        if (mime->hasImage()) {
            QImage image = qvariant_cast<QImage>(mime->imageData()); QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            if (!image.isNull() && image.save(&buffer, "PNG")) clipboard.insert("image", QString::fromLatin1(bytes.toBase64()));
        }
        message.insert("clipboard", clipboard);
    }
    web->postMessage(message);
}
void WebDocumentPage::showNotice(const QString& text) {
    notice->setText(text); notice->parentWidget()->setVisible(!text.isEmpty() || document->externalConflict);
    reloadButton->setVisible(document->externalConflict); keepButton->setVisible(document->externalConflict);
}
void WebDocumentPage::setTheme(bool dark) { if (initialized) web->postMessage({{"type", "theme"}, {"session", document->session}, {"document", documentId()}, {"dark", dark}, {"uiTheme", InsightVisualStyle::webThemeTokens(dark)}}); }
void WebDocumentPage::setZoom(double factor) { web->setZoomFactor(factor); }
void WebDocumentPage::setAppearance(const QString& family, int sizePt, double lineHeight) {
    appearance = {{"family", family}, {"size", qBound(8, sizePt, 48) * 4.0 / 3.0}, {"lineHeight", qBound(1.0, lineHeight, 3.0)}};
    if (initialized) web->postMessage({{"type", "appearance"}, {"session", document->session}, {"document", documentId()}, {"appearance", appearance}});
}
void WebDocumentPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) setTheme(palette().color(QPalette::Window).lightness() < 128);
}
