#pragma once

#include "zeroslackexport.h"
#include "webdocumentsession.h"
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;

// A movable view of a native/Web worker document session.
class ZEROSLACK_API WebDocumentPage final : public QWidget {
    Q_OBJECT
public:
    using Kind = WebDocumentSession::Kind;
    using Options = WebDocumentSession::Options;
    explicit WebDocumentPage(Kind kind, QWidget* parent = nullptr, const Options& options = {}, const QString& restoredViewId = {});
    explicit WebDocumentPage(std::shared_ptr<WebDocumentSession> session, QWidget* parent = nullptr, const QString& restoredViewId = {});
    ~WebDocumentPage() override;
    std::shared_ptr<WebDocumentSession> documentSession() const { return document; }
    void releaseView();
    static bool supportsFile(const QString& path);
    static Kind kindForFile(const QString& path);
    bool openFile(const QString& path, QString* error = nullptr);
    bool setInitialContent(const QString& text, QString* error = nullptr);
    QString fileName() const { return document->fileName(); }
    QString documentId() const { return document->documentId(); }
    QString viewIdentity() const { return viewId; }
    QString title() const;
    QJsonObject viewState() const { return presentationState; }
    void restoreViewState(const QJsonObject& state);
    Kind documentKind() const { return document->documentKind(); }
    bool dirty() const { return document->dirty(); }
    bool ready() const { return initialized; }
    bool busy() const { return document->busy(); }
    quint64 currentRevision() const { return document->currentRevision(); }
    QByteArray savedBaseline() const { return document->savedBaseline(); }
    bool save(bool saveAs = false, const QString& explicitPath = QString());
    bool confirmClose();
    bool captureRecovery(QString* text, quint64* capturedRevision);
    bool restoreRecovery(const QString& recovery, QString* error = nullptr);
    void executeCommand(const QString& command);
    void checkExternalFile();
    void setTheme(bool dark);
    void setZoom(double factor);
    void setAppearance(const QString& family, int sizePt, double lineHeight);
    void setTargetValidator(std::function<bool(const QString&, QString*)> validator);
    std::shared_ptr<void> lockForPathMutation(quint64 expectedRevision, QString* error);

signals:
    void statusChanged();
    void viewStateChanged();
    void activated();
    void saveRequested(bool saveAs);
    void fileCommandRequested(const QString& command);
    void fontZoomRequested(int steps);
    void fileSaved(const QString& path);
    void fileSaveFailed(const QString& path, const QString& reason);
    void identityChanged(const QString& previousPath, const QString& path);
    void recoveryCheckpointRequested();
    void snapshotArrived(const QString& request, const QJsonObject& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    friend class WebDocumentSession;
    std::shared_ptr<WebDocumentSession> document;
    QString viewId;
    bool initialized = false, hello = false, attached = true;
    WebEditorContainer* web;
    QLabel* notice;
    QPushButton* reloadButton;
    QPushButton* keepButton;
    QTimer* startupTimer;
    QJsonObject appearance;
    QJsonObject presentationState;
    void showNotice(const QString& text);
};
