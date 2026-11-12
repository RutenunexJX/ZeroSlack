#include "editorblockcontent.h"
#include "editorblockpreview.h"
#include "pinloompreviewimage.h"
#include "blocknotestyle.h"
#include "uicontrols.h"
#include "uitypography.h"
#include <QTextEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenu>
#include <QColorDialog>

void EditorBlockContent::addAppearanceMenu(QMenu* menu, DocumentBlockModel* value, const QString& id,
    QWidget* host, std::function<bool()> readOnly) {
    const QPointer<DocumentBlockModel> model(value); const QPointer<QWidget> guard(host);
    auto* appearance=menu->addMenu("Appearance");
    const auto change=[model,id,guard,readOnly](const QString& key, const QVariant& value) {
        if (!guard || !model || readOnly()) return;
        if (const auto* block=model->find(id)) {
            auto next=*block; if (key.isEmpty()) next.appearance.clear(); else next.appearance.insert(key,value);
            model->endTextSession(); model->update(next);
        }
    };
    for (const auto* style:{"Plain","Outlined","Soft"})
        connect(appearance->addAction(QString::fromLatin1(style)),&QAction::triggered,host,
            [change,style] { change("style",QString::fromLatin1(style)); });
    for (const auto& entry:QList<QStringList>{{"background","Background…"},{"textColor","Text color…"},{"borderColor","Border color…"}})
        connect(appearance->addAction(entry[1]),&QAction::triggered,host,[model,id,guard,readOnly,change,key=entry[0],title=entry[1]] {
            if (!guard || !model || readOnly() || !model->find(id)) return;
            const auto revision=model->revision(); const auto style=BlockNoteStyle::instance().appearance(model->find(id)->appearance);
            const auto initial=key=="background" ? style.background : key=="textColor" ? style.text : style.border;
            const auto color=QColorDialog::getColor(initial,guard,title);
            if (guard && model && color.isValid() && model->revision()==revision) change(key,color.name());
        });
    connect(appearance->addAction("Use global appearance"),&QAction::triggered,host,[change] { change({},{}); });
}

EditorBlockContent::EditorBlockContent(DocumentBlockModel* value, PinloomHostClient* client,
    const EditorAssociatedBlock& block, QWidget* parent) : QWidget(parent),model(value),id(block.id) {
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
    if (block.type=="text") {
        text=new QTextEdit(this); text->setObjectName("editorBlockText"); text->setFrameShape(QFrame::NoFrame);
        text->setAcceptRichText(false); text->setUndoRedoEnabled(false); text->setPlaceholderText("Write a note…");
        text->setMouseTracking(true); layout->addWidget(text,1); setFocusProxy(text);
        connect(text,&QTextEdit::textChanged,this,[this] { if(!updating && model && !text->isReadOnly()) model->editText(id,text->toPlainText()); });
    } else {
        preview=new EditorBlockPreview(model,client,this);
        imageHost=new QWidget(this); auto* imageRow=new QHBoxLayout(imageHost); imageRow->setContentsMargins(0,0,0,0);
        image=new PinloomPreviewImageLabel(imageHost); image->activated=[this] { if(openOriginal) openOriginal(); };
        imageRow->addStretch(); imageRow->addWidget(image,1); imageRow->addStretch(); layout->addWidget(imageHost,1);
        description=UiControls::label(this); description->setWordWrap(true); description->setTextFormat(Qt::PlainText);
        description->setTextInteractionFlags(Qt::TextSelectableByMouse); description->setAlignment(Qt::AlignLeft|Qt::AlignTop); layout->addWidget(description);
        UiTypography::apply(description,UiTypography::Role::Metadata);
        referenceText=new QTextEdit(this); referenceText->setObjectName("editorBlockReferenceText");
        referenceText->setReadOnly(true); referenceText->setAcceptRichText(false); referenceText->setFrameShape(QFrame::NoFrame);
        referenceText->setMinimumSize(0,0); layout->addWidget(referenceText,1); referenceText->hide();
    }
}
void EditorBlockContent::present(const EditorAssociatedBlock& block, bool readOnly, bool compact) {
    if (!model || id!=block.id) return;
    setVisible(!compact); updating=true;
    if (text) {
        text->setReadOnly(readOnly);
        if (text->toPlainText()!=block.text) text->setPlainText(block.text);
        if (revision!=block.revision || styleRevision!=BlockNoteStyle::instance().revision()) {
            const auto appearance=BlockNoteStyle::instance().appearance(block.appearance);
            text->setStyleSheet(QString("QTextEdit {background:transparent;color:%1;border:0;}").arg(appearance.text.name()));
            BlockNoteStyle::format(text->document(),appearance);
        }
    } else {
        // Auto-height links scroll with the code view. A transient inner bar
        // would narrow the document and keep it in an over-wrapped layout.
        referenceText->setVerticalScrollBarPolicy(block.autoHeight && !EditorBlockPreview::linkOnlyText(block).isEmpty()
            ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        if (revision!=block.revision || styleRevision!=BlockNoteStyle::instance().revision()) {
            const auto appearance=BlockNoteStyle::instance().appearance(block.appearance);
            referenceText->setStyleSheet(QString("QTextEdit {background:transparent;color:%1;border:0;}").arg(appearance.text.name()));
            BlockNoteStyle::format(referenceText->document(),appearance);
        }
        image->setMaximumWidth(block.width);
        const auto key=model->documentId()+'\n'+block.asset+'\n'+block.uri+'\n'+block.caption;
        if(compact) { preview->cancel(); resourceKey.clear(); image->clearSourcePixmap(); imageHost->hide(); }
        else if(resourceKey!=key) {
            resourceKey=key; const QPointer<EditorBlockContent> self(this);
            preview->request(id,[self,key](const EditorBlockPreviewResult& result) {
                if(!self || self->resourceKey!=key || !self->model) return;
                const auto* current=self->model->find(self->id); if(!current) return;
                QStringList content;
                if(!current->caption.isEmpty() && EditorBlockPreview::linkOnlyText(*current).isEmpty()) content<<current->caption;
                if(!result.status.isEmpty()) content<<result.status;
                self->description->setText(content.join('\n')); self->description->setVisible(!content.isEmpty());
                self->referenceText->setPlainText(result.text); self->referenceText->setVisible(!result.text.isEmpty());
                BlockNoteStyle::format(self->referenceText->document(),BlockNoteStyle::instance().appearance(current->appearance));
                if(result.image.isNull()) self->image->clearSourcePixmap();
                else self->image->setSourcePixmap(QPixmap::fromImage(result.image),current->caption);
                self->imageHost->setVisible(!result.image.isNull());
            });
        }
    }
    revision=block.revision; styleRevision=BlockNoteStyle::instance().revision(); updating=false;
}
