#pragma once

#include "pinloomhostclient.h"
#include "pinloomcodelinkstore.h"
#include <QPointer>
#include <QRect>
#include <QTimer>

class MyCodeEditor;
class EditorHoverPopup;
class PinloomPreviewImageLoader;
class PinloomPreviewImageLabel;
class QLabel;
class QPushButton;
class QWidget;

// One hover session per editor coordinator; Pinloom remains the preview authority.
class ZEROSLACK_API PinloomCodeLinkHover final : public QObject
{
public:
    explicit PinloomCodeLinkHover(PinloomHostClient* client, QObject* parent = nullptr);
    ~PinloomCodeLinkHover() override;
    void setStore(PinloomCodeLinkStore* store);
    void hover(MyCodeEditor* editor, const QString& anchorId, const QRect& globalRect);
    void dismiss();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showCard();
    void resolveTarget(int index);
    void clearSession(bool closePopup);
    bool contains(const QPoint& globalPosition) const;
    void showFailure(const QString& error);

    QPointer<PinloomHostClient> client;
    PinloomCodeLinkStore* store = nullptr;
    PinloomPreviewImageLoader* loader = nullptr;
    QPointer<MyCodeEditor> editor;
    QPointer<EditorHoverPopup> popup;
    QPointer<QWidget> body;
    QPointer<PinloomPreviewImageLabel> image;
    QPointer<QLabel> titleLabel;
    QPointer<QLabel> detailsLabel;
    QPointer<QLabel> statusLabel;
    QPointer<QPushButton> openButton;
    QTimer dwellTimer;
    QTimer leaveTimer;
    QString sourceAnchorId;
    QRect anchorRect;
    QList<PinloomCodeLinkRecord> targets;
    PinloomHostIdentity identity;
    int targetIndex = -1;
    quint64 generation = 0;
    bool opening = false;
    QList<QMetaObject::Connection> sessionConnections;
};
