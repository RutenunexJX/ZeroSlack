#pragma once
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QTextCursor>
#include <QPixmap>
#include "documentblocktransfer.h"

class TabManager;
class SharedDocument;
class MyCodeEditor;
class QMimeData;
class QWidget;
class QLabel;
class QFrame;
class QTabWidget;
class QTimer;
class QDrag;
class EditorDropPreviewOverlay;

class ZEROSLACK_API AssociatedBlockDragController final : public QObject {
    Q_OBJECT
public:
    explicit AssociatedBlockDragController(TabManager* manager);
    ~AssociatedBlockDragController() override;
    void attachEditor(MyCodeEditor* editor);
    QMimeData* createMimeData(SharedDocument* document, const QString& blockId);
    bool copyBlock(SharedDocument* document, const QString& blockId, bool cut);
    bool pasteBlock(MyCodeEditor* editor, const QTextCursor& destination, const QString& beforeId = {});
    bool dragInProgress() const { return dragging || tabDragging; }
signals:
    void gestureFinished();
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    TabManager* manager;
    QPointer<SharedDocument> owner;
    QPointer<QMimeData> activeMime;
    QPointer<QWidget> pressed;
    QPointer<QWidget> pressedTabPage;
    QPointer<QWidget> hiddenPresentation;
    QPointer<QDrag> nativeDrag;
    QPixmap dragImage;
    QPointer<MyCodeEditor> targetEditor;
    QPointer<QLabel> marker;
    QPointer<QFrame> markerLine;
    QPointer<EditorDropPreviewOverlay> splitPreview;
    QTimer* scrollTimer;
    QTimer* presentationTimer;
    QString id, beforeId;
    BlockTransferSource sourceSnapshot;
    BlockTransferSource cutSource;
    QByteArray cutPayload;
    Qt::DropAction proposedAction=Qt::MoveAction;
    QPoint pressedAt, lastGlobal;
    QTextCursor targetCursor;
    bool dragging=false, tabDragging=false, cancelled=false,externalPreview=false;
    void arm(QMimeData*, SharedDocument*, const QString&);
    bool valid(const QMimeData*) const;
    void start(QWidget* handle, Qt::KeyboardModifiers modifiers);
    bool gutterTarget(MyCodeEditor* editor, const QPoint& global, bool external = false);
    bool routeTarget(QWidget* widget, const QPoint& global, bool drop, Qt::DropAction action = Qt::MoveAction);
    void clearPreview();
    void concealSource();
    void restoreSource();
    QTabWidget* groupAt(QWidget* widget, const QPoint& global) const;
};
