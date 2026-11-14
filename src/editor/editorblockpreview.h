#pragma once
#include "documentblockmodel.h"
#include "pinloomhostclient.h"
#include <QImage>
#include <QPointer>
class QAction;
class QMenu;

struct EditorBlockOriginalTarget {
    enum class Kind { Unavailable, Pinloom, Url };
    Kind kind = Kind::Unavailable;
    PinloomHostIdentity identity;
    QUrl url;
    QString reason;
    bool verifyLocation = false;
    bool available() const { return kind != Kind::Unavailable; }
};

struct EditorBlockPreviewResult {
    QString text, status;
    QImage image;
    bool loading = false;
    EditorBlockOriginalTarget original;
    QString title, location;
    bool linkOnly = false;
};

// A visible consumer of existing providers. Late replies cannot change a
// deleted/rebound block or a newer resource request.
class ZEROSLACK_API EditorBlockPreview final : public QObject {
public:
    using Handler = std::function<void(const EditorBlockPreviewResult&)>;
    EditorBlockPreview(DocumentBlockModel* model, PinloomHostClient* client, QObject* parent);
    static QString linkOnlyText(const EditorAssociatedBlock& block);
    using Current = std::function<bool()>;
    static EditorBlockOriginalTarget originalTargetForDocument(const PinloomHostIdentity& identity,
        const PinloomHostDocument& document);
    static void inspectTarget(const QString& uri, PinloomHostClient* client, QObject* consumer,
        Current current, std::function<void(const EditorBlockOriginalTarget&)> handler);
    static void openTarget(const QString& uri, PinloomHostClient* client, QObject* consumer,
        Current current, PinloomHostClient::CompletionHandler handler);
    static void openInspectedTarget(const EditorBlockOriginalTarget& original, PinloomHostClient* client,
        QObject* consumer, Current current, PinloomHostClient::CompletionHandler handler);
    static void requestTarget(const QString& uri, PinloomHostClient* client, QObject* consumer,
        Current current, Handler handler);
    static void inspectOriginal(DocumentBlockModel* model, PinloomHostClient* client,
        const QString& id, QObject* consumer, std::function<void(const EditorBlockOriginalTarget&)> handler);
    static void bindOriginalAction(QAction* action, QMenu* menu, DocumentBlockModel* model,
        PinloomHostClient* client, const QString& id);
    static void openReference(DocumentBlockModel* model, PinloomHostClient* client,
        const QString& id, QObject* consumer, PinloomHostClient::CompletionHandler handler);
    ~EditorBlockPreview() override;
    void request(const QString& id, Handler handler);
    void cancel();
    void rebind(DocumentBlockModel* value) {cancel();model=value;}
private:
    QPointer<DocumentBlockModel> model;
    QPointer<PinloomHostClient> client;
    QPointer<QObject> consumer;
    quint64 generation = 0;
};
