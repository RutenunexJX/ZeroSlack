#pragma once
#include "documentblockmodel.h"
#include <QHash>
#include <QPointer>
#include <QRectF>

class MyCodeEditor;
class QFrame;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QKeyEvent;
class PinloomHostClient;
class PinloomPreviewImageLabel;
class EditorBlockPreview;

// Native children of the editor viewport. The projection alone owns their
// display coordinates; offscreen controls are released, the model is retained.
class EditorBlockHost final : public QObject {
public:
    explicit EditorBlockHost(MyCodeEditor* editor);
    ~EditorBlockHost() override;
    void rebind();
    void updateVisible();
    void insert(const QVariantMap& preset, const QTextCursor& cursor);
    bool navigateFromCode(QKeyEvent* event);
    void finishEditing();
    int viewCount() const { return views.size(); }
    QString selected() const { return selectedId; }
    void select(const QString& id, bool edit = false);
    void addAssociationActions(class QMenu* menu);
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    struct View {
        QFrame* frame = nullptr;
        QLabel *title = nullptr, *preview = nullptr;
        QPlainTextEdit* text = nullptr;
        QPushButton* collapse = nullptr;
        PinloomPreviewImageLabel* image = nullptr;
        EditorBlockPreview* resource = nullptr;
        QString resourceKey;
        quint64 revision = 0;
    };
    MyCodeEditor* editor;
    QPointer<DocumentBlockModel> model;
    PinloomHostClient* client;
    QList<QMetaObject::Connection> connections;
    QHash<QString,View> views;
    QString selectedId;
    bool scheduled = false, updating = false;
    void schedule();
    View createView(const EditorAssociatedBlock& block);
    void returnToCode(const EditorAssociatedBlock& block);
    void leaveBlock(const QString& id, bool forward);
    void openOriginal(const QString& id);
};
