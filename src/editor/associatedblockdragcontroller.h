#pragma once
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QTextCursor>

class TabManager;
class SharedDocument;
class MyCodeEditor;
class QMimeData;
class QWidget;
class QLabel;
class QFrame;
class QTabWidget;
class QTimer;
class EditorDropPreviewOverlay;

class ZEROSLACK_API AssociatedBlockDragController final : public QObject {
    Q_OBJECT
public:
    explicit AssociatedBlockDragController(TabManager* manager);
    ~AssociatedBlockDragController() override;
    void attachEditor(MyCodeEditor* editor);
    QMimeData* createMimeData(SharedDocument* document, const QString& blockId);
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
    QPointer<MyCodeEditor> targetEditor;
    QPointer<QLabel> marker;
    QPointer<QFrame> markerLine;
    QPointer<EditorDropPreviewOverlay> splitPreview;
    QTimer* scrollTimer;
    QString id, identity, beforeId;
    quint64 serial=0, sourceRevision=0, associationRevision=0;
    QPoint pressedAt, lastGlobal;
    QTextCursor targetCursor;
    bool dragging=false, tabDragging=false, cancelled=false;
    void arm(QMimeData*, SharedDocument*, const QString&);
    bool valid(const QMimeData*) const;
    void start(QWidget* handle);
    bool gutterTarget(MyCodeEditor* editor, const QPoint& global);
    bool routeTarget(QWidget* widget, const QPoint& global, bool drop);
    void clearPreview();
    QTabWidget* groupAt(QWidget* widget, const QPoint& global) const;
};
