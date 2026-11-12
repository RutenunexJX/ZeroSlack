#pragma once

#include "zeroslackexport.h"
#include "webeditorcontainer.h"
#include <zeroslack/documents/documentfileread.h>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QPointer>
#include <QObject>
#include <functional>
#include <memory>

class QTimer;
class WebDocumentPage;

// Native file/recovery authority. The corresponding shared worker owns the
// structured content and sole logical history; pages own only presentation.
class ZEROSLACK_API WebDocumentSession final : public QObject {
    Q_OBJECT
public:
    enum class Kind { Markdown, Blocks };
    struct Options { QString runtimeDirectory; QString profileDirectory; QString documentIdentity; };
    explicit WebDocumentSession(Kind kind, const Options& options = {});
    static bool supportsFile(const QString& path);
    static Kind kindForFile(const QString& path);
    static bool validateBlockPackage(const QJsonObject& package, QString* error);
    bool openFile(const QString& path, QString* error = nullptr);
    bool setInitialContent(const QString& text, QString* error = nullptr);
    bool importMarkdown(const QString& sourcePath, QString* error = nullptr);
    bool exportMarkdown();
    QString fileName() const { return path; }
    QString documentId() const { return id; }
    QString title() const;
    Kind documentKind() const { return kind; }
    bool dirty() const { return revision != savedRevision || untitledDirty; }
    bool hasExternalConflict() const { return externalConflict; }
    bool isProtected() const { return protectedContent; }
    bool ready() const { return initialized; }
    bool busy() const { return operation; }
    quint64 currentRevision() const { return revision; }
    QByteArray savedBaseline() const { return baseline; }
    QList<WebDocumentPage*> views() const;
    void setPresentationView(WebDocumentPage* view);
    bool save(bool saveAs = false, const QString& explicitPath = {});
    bool createFile(const QString& target, QString* error = nullptr);
    bool confirmClose();
    bool prepareViewClose(WebDocumentPage* view);
    bool captureRecovery(QString* text, quint64* capturedRevision);
    bool restoreRecovery(const QString& recovery, QString* error = nullptr);
    void checkExternalFile();
    void setTargetValidator(std::function<bool(const QString&, QString*)> validator);
    std::shared_ptr<void> lockForPathMutation(quint64 expectedRevision, QString* error, bool requireCurrentFile = false);
    bool retargetAfterRename(const QString& target, QString* error);

signals:
    void statusChanged();
    void noticeChanged(const QString& text);
    void fileSaved(const QString& path);
    void fileSaveFailed(const QString& path, const QString& reason);
    void identityChanged(const QString& previousPath, const QString& path);
    void recoveryCheckpointRequested();
    void snapshotArrived(const QString& request, const QJsonObject& message);

private:
    friend class WebDocumentPage;
    Kind kind;
    Options options;
    QString id, session, path, physicalPath, initialContent, failure, importSource, noticeText;
    QByteArray baseline;
    DocumentFileFormat format;
    quint64 revision = 0, savedRevision = 0;
    bool initialized = false, operation = false, untitledDirty = false;
    bool externalConflict = false, protectedContent = false;
    QList<QPointer<WebDocumentPage>> pages;
    QPointer<WebDocumentPage> presentation;
    QTimer* recoveryTimer;
    QFileSystemWatcher watcher;
    std::function<bool(const QString&, QString*)> targetValidator;
    QHash<QString, WebEditorResource> stagedImages;
    void attach(WebDocumentPage* page);
    void detach(WebDocumentPage* page);
    WebDocumentPage* presentationView() const;
    QWidget* dialogParent() const;
    void initializeEditor(WebDocumentPage* view);
    void receive(WebDocumentPage* view, const QJsonObject& message);
    bool snapshot(bool freeze, QJsonObject* result, QString* error, const QString& mode = {});
    void releaseSnapshot();
    void showNotice(const QString& text);
    void watchFile();
    void reloadFile();
    void keepLocal();
    bool sameFileGeneration(const QString& target, const QString& identity,
                            const QByteArray& digest, bool existed, QString* error) const;
    WebEditorResource asset(const QUrl& url) const;
    bool saveImages(const QString& target, const QJsonArray& images, QString* error);
};
