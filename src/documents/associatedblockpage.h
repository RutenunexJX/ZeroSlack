#pragma once
#include "zeroslackexport.h"
#include <QWidget>
#include <QPointer>

class SharedDocument;
class DocumentBlockModel;
struct EditorAssociatedBlock;
class QTextEdit;
class QLabel;
class QToolButton;
class EditorBlockPreview;
class EditorBlockContent;
class PinloomHostClient;
class PinloomPreviewImageLabel;

class ZEROSLACK_API AssociatedBlockPage final : public QWidget {
    Q_OBJECT
public:
    AssociatedBlockPage(SharedDocument* document, const QString& blockId, QWidget* parent = nullptr,
                        const QString& viewId = {}, const EditorAssociatedBlock* prepared = nullptr,
                        EditorBlockContent* retained = nullptr);
    ~AssociatedBlockPage() override;
    SharedDocument* ownerDocument() const;
    DocumentBlockModel* model() const;
    QString blockId() const { return id; }
    QString viewIdentity() const { return view; }
    QString title() const;
    bool finishEditing();
    EditorBlockContent* takeContent();
    void adoptContent(EditorBlockContent* value);
    void replay(bool redo);
signals:
    void statusChanged();
    void sourceRequested();
    void saveRequested(bool saveAs);
    void clipboardRequested(bool cut);
protected:
    bool eventFilter(QObject*, QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    QPointer<SharedDocument> document;
    QString id, view;
    QTextEdit* text = nullptr;
    EditorBlockContent* content = nullptr;
    QLabel *origin = nullptr, *description = nullptr;
    PinloomHostClient* client = nullptr;
    QToolButton *grip = nullptr, *remove = nullptr, *edit = nullptr;
    QWidget* tools = nullptr;
    bool updating = false;
    void refresh();
    void refreshControls();
    void openOriginal();
};
