#ifndef CONTEXTPEEKHOST_H
#define CONTEXTPEEKHOST_H

#include "contextresource.h"
#include "zeroslackexport.h"

#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

class QEvent;
class QHideEvent;
class QLabel;
class QResizeEvent;
class QToolButton;
class QVBoxLayout;

class ZEROSLACK_API ContextPeekHost final : public QWidget
{
    Q_OBJECT

public:
    explicit ContextPeekHost(QWidget* editorRegion);
    ~ContextPeekHost() override;

    bool hasResource() const;
    ContextResource resource() const;
    QWidget* view() const;
    QSize preferredSize() const;
    int preferredWidth() const;
    int preferredHeight() const;

    void setPreferredSize(const QSize& size);
    void setPreferredWidth(int width);
    void setPreferredHeight(int height);
    void setActionsAvailable(bool pinAvailable,
                             bool fullViewAvailable);
    void setView(const ContextResource& resource,
                 QWidget* view);
    bool updateResource(const ContextResource& resource);
    QWidget* takeView();
    void clearView();

signals:
    void pinRequested();
    void closeRequested();
    void fullViewRequested();
    void preferredSizeChanged(const QSize& size);
    void preferredSizeResetRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class ResizeMode {
        None,
        Width,
        Height,
        WidthAndHeight
    };

    QVBoxLayout* rootLayout = nullptr;
    QWidget* header = nullptr;
    QWidget* contentHost = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    QLabel* titleLabel = nullptr;
    QToolButton* pinButton = nullptr;
    QToolButton* fullViewButton = nullptr;
    QToolButton* closeButton = nullptr;
    QWidget* leftResizeHandle = nullptr;
    QWidget* bottomResizeHandle = nullptr;
    QWidget* cornerResizeHandle = nullptr;
    QPointer<QWidget> currentView;
    ContextResource currentResource;
    QSize preferredSizeValue = QSize(520, 440);
    QPoint resizeStartGlobal;
    QSize resizeStartSize;
    QSize resizeStartPreference;
    ResizeMode resizeMode = ResizeMode::None;
    bool resizeChanged = false;

    void buildUi();
    void buildResizeHandles();
    void synchronizeGeometry();
    void updateResizeHandleGeometry();
    QSize boundedStoredSize(const QSize& requested) const;
    QSize boundedVisibleSize(const QSize& requested) const;
    ResizeMode modeForHandle(const QObject* object) const;
    bool handleResizeEvent(QWidget* handle, QEvent* event);
    void applyResizePosition(const QPoint& globalPosition);
    void finishResize(bool commit);
};

#endif // CONTEXTPEEKHOST_H
