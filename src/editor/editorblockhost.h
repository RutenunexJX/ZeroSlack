#pragma once
#include "documentblockmodel.h"
#include <QHash>
#include <QPointer>
#include <QRectF>

class MyCodeEditor;
class QFrame;
class QTextEdit;
class QLabel;
class QPushButton;
class QToolButton;
class QKeyEvent;
class PinloomHostClient;
class PinloomPreviewImageLabel;
class EditorBlockPreview;
class EditorBlockContent;

// Native children of the editor viewport. The projection alone owns their
// display coordinates; offscreen controls are released, the model is retained.
class EditorBlockHost final : public QObject {
public:
    explicit EditorBlockHost(MyCodeEditor* editor);
    ~EditorBlockHost() override;
    void rebind();
    void updateVisible();
    void schedule();
    void insert(const QVariantMap& preset, const QTextCursor& cursor);
    bool navigateFromCode(QKeyEvent* event);
    bool finishEditing();
    EditorBlockContent* takeContent(const QString& id);
    void adoptContent(const QString& id, EditorBlockContent* content);
    int viewCount() const { return views.size(); }
    QString selected() const { return selectedId; }
    void select(const QString& id, bool edit = false);
    void addAssociationActions(class QMenu* menu);
    void populateBlockMenu(class QMenu* menu, const QString& id);
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    struct View {
        QFrame* frame = nullptr;
        QPointer<EditorBlockContent> content;
        QLabel* title = nullptr;
        QTextEdit* text = nullptr;
        QToolButton* collapse = nullptr;
        QToolButton *edit = nullptr, *grip = nullptr;
        QWidget* tools = nullptr;
        quint64 revision = 0;
        quint64 styleRevision = 0;
    };
    MyCodeEditor* editor;
    QPointer<DocumentBlockModel> model;
    PinloomHostClient* client;
    QList<QMetaObject::Connection> connections;
    QHash<QString,View> views;
    QString selectedId;
    bool scheduled = false, updating = false;
    View createView(const EditorAssociatedBlock& block, EditorBlockContent* retained = nullptr);
    void returnToCode(const EditorAssociatedBlock& block);
    void leaveBlock(const QString& id, bool forward);
    void openOriginal(const QString& id);
    void refreshControls();
};
