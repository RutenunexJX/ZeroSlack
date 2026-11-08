#pragma once
#include "documentblockmodel.h"
#include "pinloomhostclient.h"
#include <QImage>
#include <QPointer>

struct EditorBlockPreviewResult {
    QString text, status;
    QImage image;
    bool loading = false;
};

// A visible consumer of existing providers. Late replies cannot change a
// deleted/rebound block or a newer resource request.
class ZEROSLACK_API EditorBlockPreview final : public QObject {
public:
    using Handler = std::function<void(const EditorBlockPreviewResult&)>;
    EditorBlockPreview(DocumentBlockModel* model, PinloomHostClient* client, QObject* parent);
    ~EditorBlockPreview() override;
    void request(const QString& id, Handler handler);
    void cancel();
private:
    QPointer<DocumentBlockModel> model;
    QPointer<PinloomHostClient> client;
    QPointer<QObject> consumer;
    quint64 generation = 0;
};
