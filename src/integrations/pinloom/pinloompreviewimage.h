#pragma once

#include "pinloomhostclient.h"

#include <QImage>
#include <QLabel>
#include <functional>
#include <memory>

class QDialog;

class ZEROSLACK_API PinloomPreviewImageLabel final : public QLabel
{
public:
    explicit PinloomPreviewImageLabel(QWidget* parent = nullptr);
    void setSourcePixmap(const QPixmap& pixmap, const QString& accessibleText);
    void clearSourcePixmap();
    QPixmap sourcePixmap() const;
    std::function<void()> activated;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateScaledPixmap();
    QPixmap source;
    bool pressed = false;
};

struct PinloomPreviewImageResult {
    QImage image;
    QString error;
};

// One running decode and one replaceable pending request per consumer.
class ZEROSLACK_API PinloomPreviewImageLoader final : public QObject
{
public:
    using Handler = std::function<void(const PinloomPreviewImageResult&)>;
    explicit PinloomPreviewImageLoader(QObject* parent = nullptr);
    ~PinloomPreviewImageLoader() override;
    void load(const PinloomHostPreview& preview, Handler handler);
    bool cancel();

private:
    struct State;
    std::unique_ptr<State> state;
    void startPending();
};

ZEROSLACK_API QString pinloomDocumentFileName(const PinloomHostDocument& document);
ZEROSLACK_API QDialog* openPinloomImagePreview(
    QWidget* owner, const QPixmap& pixmap, const QString& title,
    const QString& accessibleText);
