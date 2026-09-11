#ifndef EDITORDROPPREVIEWOVERLAY_H
#define EDITORDROPPREVIEWOVERLAY_H

#include "zeroslackexport.h"

#include <QPointer>
#include <QMetaObject>
#include <QRect>
#include <QWidget>

class QPaintEvent;
class QPixmap;
class QPoint;
class QTabBar;

enum class EditorSplitDirection;

class ZEROSLACK_API EditorDropPreviewOverlay final : public QWidget
{
public:
    explicit EditorDropPreviewOverlay(QWidget* parent = nullptr);

    static EditorSplitDirection directionAt(
        const QRect& targetRect,
        const QPoint& targetPosition);
    static QRect previewRect(
        const QRect& targetRect,
        EditorSplitDirection direction);
    static QPixmap tabDragPixmap(
        const QTabBar* tabBar,
        int tabIndex);

    void showPreview(
        QWidget* target,
        EditorSplitDirection direction);
    void showPreview(
        QWidget* target,
        const QPoint& targetPosition);
    void clearPreview();

    QWidget* target() const;
    EditorSplitDirection direction() const;
    QRect highlightedRect() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPointer<QWidget> previewTarget;
    QMetaObject::Connection targetDestroyedConnection;
    EditorSplitDirection previewDirection;

    QRect targetContentRect() const;
    void syncToTarget();
};

#endif // EDITORDROPPREVIEWOVERLAY_H
