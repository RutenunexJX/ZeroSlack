#pragma once

#include "zeroslackexport.h"
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <memory>

struct WebEditorResource {
    QByteArray bytes;
    QByteArray contentType;
    int status = 404;
};

class ZEROSLACK_API WebEditorContainer final : public QWidget {
    Q_OBJECT
public:
    using AssetResolver = std::function<WebEditorResource(const QUrl&)>;
    explicit WebEditorContainer(const QString& runtimeDirectory,
                                const QString& profileDirectory,
                                const QString& editorKind,
                                QWidget* parent = nullptr);
    ~WebEditorContainer() override;
    bool postMessage(const QJsonObject& message);
    void setAssetResolver(AssetResolver resolver);
    void setZoomFactor(double factor);
    void shutdown();
    bool isInitialized() const;
    QString entryUrl() const;

signals:
    void messageReceived(const QJsonObject& message);
    void initializationFailed(const QString& reason);
    void navigationReady();
    void activated();
    void commandRequested(const QString& command);
    void externalLinkRequested(const QUrl& url);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    class Private;
    std::unique_ptr<Private> d;
};
