#pragma once

#include "zeroslackexport.h"
#include "webeditorcontainer.h"
#include <zeroslack/documents/documentfileread.h>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;

// The native side owns identity and persistence. The web editor owns content and undo.
class ZEROSLACK_API WebDocumentPage final : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Markdown, Blocks };
    struct Options { QString runtimeDirectory; QString profileDirectory; };
    explicit WebDocumentPage(Kind kind, QWidget* parent = nullptr, const Options& options = {});
    ~WebDocumentPage() override;
    static bool supportsFile(const QString& path);
    static Kind kindForFile(const QString& path);
    bool openFile(const QString& path, QString* error = nullptr);
    bool setInitialContent(const QString& text, QString* error = nullptr);
    bool importMarkdown(const QString& sourcePath, QString* error = nullptr);
    bool exportMarkdown();
    QString fileName() const { return path; }
    QString documentId() const { return id; }
    QString title() const;
    Kind documentKind() const { return kind; }
    bool dirty() const { return revision != savedRevision || untitledDirty; }
    bool ready() const { return initialized; }
    bool busy() const { return operation; }
    quint64 currentRevision() const { return revision; }
    QByteArray savedBaseline() const { return baseline; }
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
    static bool validateBlockPackage(const QJsonObject& package, QString* error);

signals:
    void statusChanged();
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
    Kind kind;
    QString id, session, path, physicalPath, initialContent, failure, importSource;
    QByteArray baseline;
    DocumentFileFormat format;
    quint64 revision = 0, savedRevision = 0;
    bool initialized = false, hello = false, operation = false, untitledDirty = false;
    bool externalConflict = false, protectedContent = false;
    WebEditorContainer* web;
    QLabel* notice;
    QPushButton* reloadButton;
    QPushButton* keepButton;
    QTimer* startupTimer;
    QTimer* recoveryTimer;
    QFileSystemWatcher watcher;
    std::function<bool(const QString&, QString*)> targetValidator;
    QHash<QString, WebEditorResource> stagedImages;
    QJsonObject appearance;
    void initializeEditor();
    void receive(const QJsonObject& message);
    bool snapshot(bool freeze, QJsonObject* result, QString* error, const QString& mode = QString());
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
